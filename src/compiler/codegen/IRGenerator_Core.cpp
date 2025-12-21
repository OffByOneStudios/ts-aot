#include "IRGenerator.h"
#include <fmt/core.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Transforms/Scalar/SROA.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/IPO/Inliner.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Analysis/InlineCost.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Linker/Linker.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

namespace ts {
using namespace ast;

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(ast::Program* program, const std::vector<Specialization>& specializations, const Analyzer& analyzer) {
    this->specializations = specializations;
    // Initialize target for DataLayout
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
    if (target) {
        auto targetMachine = target->createTargetMachine(targetTriple, "generic", "", {}, {});
        module->setDataLayout(targetMachine->createDataLayout());
    }

    generateGlobals(analyzer);
    
    asyncContextType = llvm::StructType::create(*context, "AsyncContext");
    asyncContextType->setBody({
        builder->getPtrTy(), // vtable
        llvm::Type::getInt32Ty(*context), // state
        llvm::Type::getInt1Ty(*context), // error
        builder->getPtrTy(), // promise
        builder->getPtrTy(), // resumeFn
        builder->getPtrTy()  // data
    });

    generateClasses(analyzer, specializations);
    generatePrototypes(specializations);
    generateBodies(specializations);
    generateEntryPoint();
}

void IRGenerator::generateGlobals(const Analyzer& analyzer) {
    const auto& globals = analyzer.getSymbolTable().getGlobalSymbols();
    for (auto& [name, symbol] : globals) {
        if (module->getGlobalVariable(name)) continue;
        
        // Skip functions, they are handled by generatePrototypes
        if (symbol->type->kind == TypeKind::Function) continue;
        
        // Skip classes and interfaces
        if (symbol->type->kind == TypeKind::Class || symbol->type->kind == TypeKind::Interface) continue;

        llvm::Type* type = getLLVMType(symbol->type);
        new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(type), name);
    }
}

void IRGenerator::generateEntryPoint() {
    llvm::Function* userMain = module->getFunction("user_main");
    if (!userMain) return;

    // Declare ts_main: int ts_main(int argc, char** argv, TsValue* (*user_main)(void*))
    std::vector<llvm::Type*> args = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), args, false);
    llvm::Function* tsMain = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_main", module.get());

    // Define main: int main(int argc, char** argv)
    std::vector<llvm::Type*> mainArgs = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* mainFt = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), mainArgs, false);
    llvm::Function* mainFn = llvm::Function::Create(mainFt, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", mainFn);
    builder->SetInsertPoint(bb);

    llvm::Value* argc = mainFn->getArg(0);
    llvm::Value* argv = mainFn->getArg(1);
    
    createCall(ft, tsMain, { argc, argv, userMain });
    
    builder->CreateRet(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
}

llvm::Type* IRGenerator::getLLVMType(const std::shared_ptr<Type>& type) {
    if (!type) return builder->getPtrTy();
    switch (type->kind) {
        case TypeKind::Void: return builder->getPtrTy();
        case TypeKind::Int: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Enum: return llvm::Type::getInt64Ty(*context);
        case TypeKind::Double: return llvm::Type::getDoubleTy(*context);
        case TypeKind::Boolean: return llvm::Type::getInt1Ty(*context);
        case TypeKind::Object: {
            auto objType = std::static_pointer_cast<ObjectType>(type);
            std::vector<llvm::Type*> fieldTypes;
            for (auto& [name, fieldType] : objType->fields) {
                fieldTypes.push_back(getLLVMType(fieldType));
            }
            llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::Class: {
            auto classType = std::static_pointer_cast<ClassType>(type);
            llvm::StructType* structType = llvm::StructType::getTypeByName(*context, classType->name);
            if (!structType) {
                // Should have been created in generateClasses, but fallback just in case
                structType = llvm::StructType::create(*context, classType->name);
            }
            return llvm::PointerType::getUnqual(structType);
        }
        case TypeKind::String:
        case TypeKind::Any:
        case TypeKind::Array:
        case TypeKind::Tuple:
        case TypeKind::Map:
        case TypeKind::Interface:
        case TypeKind::Union:
        case TypeKind::Intersection:
        case TypeKind::TypeParameter:
        case TypeKind::Function:
            return llvm::PointerType::getUnqual(*context);
        default: return llvm::Type::getInt8Ty(*context);
    }
}

void IRGenerator::generateDestructuring(llvm::Value* value, std::shared_ptr<Type> type, ast::Node* pattern) {
    if (auto id = dynamic_cast<ast::Identifier*>(pattern)) {
        if (auto gv = module->getGlobalVariable(id->name)) {
            builder->CreateStore(value, gv);
            return;
        }

        if (currentAsyncFrame) {
            auto it = currentAsyncFrameMap.find(id->name);
            if (it != currentAsyncFrameMap.end()) {
                llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
                builder->CreateStore(value, ptr);
                namedValues[id->name] = ptr;
                return;
            }
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), id->name, value->getType());
        builder->CreateStore(value, alloca);
        namedValues[id->name] = alloca;
    } else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(pattern)) {
        if (!type || type->kind != TypeKind::Class) {
            for (auto& elementNode : obp->elements) {
                if (auto element = dynamic_cast<ast::BindingElement*>(elementNode.get())) {
                    generateDestructuring(llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context)), 
                                          std::make_shared<Type>(TypeKind::Any), element->name.get());
                }
            }
            return;
        }

        auto classType = std::static_pointer_cast<ClassType>(type);
        std::string className = classType->name;
        llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
        if (!classStruct) return;

        llvm::Value* typedObjPtr = builder->CreateBitCast(value, llvm::PointerType::getUnqual(classStruct));

        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<ast::BindingElement*>(elementNode.get());
            if (!element) continue;

            std::string fieldName;
            if (!element->propertyName.empty()) {
                fieldName = element->propertyName;
            } else if (auto nameId = dynamic_cast<ast::Identifier*>(element->name.get())) {
                fieldName = nameId->name;
            }

            if (fieldName.empty()) continue;

            if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
                int fieldIndex = classLayouts[className].fieldIndices[fieldName];
                llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
                
                std::shared_ptr<Type> fieldType;
                for (const auto& f : classLayouts[className].allFields) {
                    if (f.first == fieldName) {
                        fieldType = f.second;
                        break;
                    }
                }
                
                if (fieldType) {
                    llvm::Value* fieldValue = builder->CreateLoad(getLLVMType(fieldType), fieldPtr);
                    
                    if (element->initializer) {
                        if (fieldValue->getType()->isPointerTy()) {
                            llvm::Value* isNull = builder->CreateICmpEQ(fieldValue, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(fieldValue->getType())));
                            
                            llvm::Function* function = builder->GetInsertBlock()->getParent();
                            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "destruct.default", function);
                            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "destruct.value", function);
                            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "destruct.merge", function);
                            
                            builder->CreateCondBr(isNull, thenBB, elseBB);
                            
                            builder->SetInsertPoint(thenBB);
                            visit(element->initializer.get());
                            llvm::Value* defaultVal = lastValue;
                            defaultVal = castValue(defaultVal, getLLVMType(fieldType));
                            builder->CreateBr(mergeBB);
                            thenBB = builder->GetInsertBlock();
                            
                            builder->SetInsertPoint(elseBB);
                            builder->CreateBr(mergeBB);
                            elseBB = builder->GetInsertBlock();
                            
                            builder->SetInsertPoint(mergeBB);
                            llvm::PHINode* phi = builder->CreatePHI(fieldValue->getType(), 2);
                            phi->addIncoming(defaultVal, thenBB);
                            phi->addIncoming(fieldValue, elseBB);
                            fieldValue = phi;
                        }
                    }

                    generateDestructuring(fieldValue, fieldType, element->name.get());
                }
            }
        }
    } else if (auto abp = dynamic_cast<ast::ArrayBindingPattern*>(pattern)) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (type && type->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(type)->elementType;
        }

        llvm::Function* getFn = module->getFunction("ts_array_get");
        if (!getFn) {
             std::vector<llvm::Type*> args = { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), args, false);
             getFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_get", module.get());
        }

        for (size_t i = 0; i < abp->elements.size(); ++i) {
            auto elementNode = abp->elements[i].get();
            if (auto oe = dynamic_cast<ast::OmittedExpression*>(elementNode)) continue;
            
            auto element = dynamic_cast<ast::BindingElement*>(elementNode);
            if (!element) continue;

            llvm::Value* idxVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i);

            if (element->isSpread) {
                llvm::FunctionType* sliceFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                    { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee sliceFn = module->getOrInsertFunction("ts_array_slice", sliceFt);
                llvm::Value* restArr = createCall(sliceFt, sliceFn.getCallee(), { value, idxVal });
                generateDestructuring(restArr, type, element->name.get());
                break; 
            }

            llvm::Value* elementVal = createCall(getFn->getFunctionType(), getFn, { value, idxVal });
            llvm::Value* castedVal = castValue(elementVal, getLLVMType(elementType));
            
            if (element->initializer) {
                llvm::Value* isNull = builder->CreateICmpEQ(elementVal, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0));
                
                llvm::Function* function = builder->GetInsertBlock()->getParent();
                llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "destruct.arr.default", function);
                llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "destruct.arr.value", function);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "destruct.arr.merge", function);
                
                builder->CreateCondBr(isNull, thenBB, elseBB);
                
                builder->SetInsertPoint(thenBB);
                visit(element->initializer.get());
                llvm::Value* defaultVal = lastValue;
                defaultVal = castValue(defaultVal, getLLVMType(elementType));
                builder->CreateBr(mergeBB);
                thenBB = builder->GetInsertBlock();
                
                builder->SetInsertPoint(elseBB);
                builder->CreateBr(mergeBB);
                elseBB = builder->GetInsertBlock();
                
                builder->SetInsertPoint(mergeBB);
                llvm::PHINode* phi = builder->CreatePHI(castedVal->getType(), 2);
                phi->addIncoming(defaultVal, thenBB);
                phi->addIncoming(castedVal, elseBB);
                castedVal = phi;
            }

            generateDestructuring(castedVal, elementType, element->name.get());
        }
    }
}

llvm::AllocaInst* IRGenerator::createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type) {
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, varName);
}

llvm::Function* IRGenerator::getRuntimeFunction(const std::string& name) {
    if (auto* f = module->getFunction(name)) return f;

    if (name == "ts_push_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_pop_exception_handler") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_throw") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context), {llvm::PointerType::getUnqual(*context)}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "ts_get_exception") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context), {}, false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    } else if (name == "_setjmp") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context), 
            {llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context)}, 
            false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }

    return nullptr;
}

void IRGenerator::visit(ast::Node* node) {
    if (node) {
        node->accept(this);
    }
}

void IRGenerator::collectVariables(ast::Node* node, std::vector<VariableInfo>& vars) {
    if (!node) return;

    if (auto decl = dynamic_cast<ast::VariableDeclaration*>(node)) {
        if (auto id = dynamic_cast<ast::Identifier*>(decl->name.get())) {
            vars.push_back({id->name, decl->resolvedType, getLLVMType(decl->resolvedType)});
        }
    } else if (auto block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& stmt : block->statements) collectVariables(stmt.get(), vars);
    } else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        collectVariables(ifStmt->thenStatement.get(), vars);
        collectVariables(ifStmt->elseStatement.get(), vars);
    } else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        collectVariables(whileStmt->body.get(), vars);
    } else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        collectVariables(forStmt->initializer.get(), vars);
        collectVariables(forStmt->body.get(), vars);
    } else if (auto forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        // Add implicit variables for the loop
        std::string baseName = "forof_" + std::to_string(anonVarCounter++);
        
        // 1. Index
        vars.push_back({ baseName + "_index", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });
        
        // 2. Iterable
        vars.push_back({ baseName + "_iterable", forOf->expression->inferredType, getLLVMType(forOf->expression->inferredType) });
        
        // 3. Length
        vars.push_back({ baseName + "_length", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });

        collectVariables(forOf->initializer.get(), vars);
        collectVariables(forOf->body.get(), vars);
    } else if (auto forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        // Add implicit variables for the loop
        std::string baseName = "forin_" + std::to_string(anonVarCounter++);
        
        // 1. Index
        vars.push_back({ baseName + "_index", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });
        
        // 2. Keys (Array of strings)
        vars.push_back({ baseName + "_keys", std::make_shared<Type>(TypeKind::Array), builder->getPtrTy() });
        
        // 3. Length
        vars.push_back({ baseName + "_length", std::make_shared<Type>(TypeKind::Int), llvm::Type::getInt64Ty(*context) });

        collectVariables(forIn->initializer.get(), vars);
        collectVariables(forIn->body.get(), vars);
    }
}

llvm::Value* IRGenerator::boxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (type && type->kind == TypeKind::Void) {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
        return createCall(undefFt, undefFn.getCallee(), {});
    }

    if (!val) return nullptr;
    
    // If it's already a pointer and we expect a primitive or Any, it's likely already a TsValue*
    
    llvm::Type* valType = val->getType();
    std::string funcName;
    
    if (valType->isIntegerTy(64)) funcName = "ts_value_make_int";
    else if (valType->isDoubleTy()) funcName = "ts_value_make_double";
    else if (valType->isIntegerTy(1)) funcName = "ts_value_make_bool";
    else if (valType->isIntegerTy(8)) {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
        return createCall(undefFt, undefFn.getCallee(), {});
    }
    else if (type && type->kind == TypeKind::Function) {
        llvm::FunctionType* fnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_value_make_function", fnFt);
        
        llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
        llvm::Value* currentContext = nullptr;
        if (currentFunc->arg_size() > 0 && currentFunc->getArg(0)->getType()->isPointerTy()) {
            currentContext = currentFunc->getArg(0);
        } else {
            currentContext = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        return createCall(fnFt, fn.getCallee(), { builder->CreateBitCast(val, builder->getPtrTy()), currentContext });
    } else if (valType->isPointerTy()) {
        if (type && type->kind == TypeKind::String) funcName = "ts_value_make_string";
        else if (type && type->kind == TypeKind::Array) funcName = "ts_value_make_array";
        else if (type && type->kind == TypeKind::Class && std::static_pointer_cast<ClassType>(type)->name.find("Promise") == 0) {
            // Promises are already returned as boxed TsValue* from async functions
            return val;
        }
        else funcName = "ts_value_make_object";
    }
    
    if (funcName.empty()) {
        if (type && type->kind == TypeKind::Any && !val->getType()->isPointerTy()) {
             // Fallback for Any that isn't a pointer yet
             if (val->getType()->isIntegerTy(64)) funcName = "ts_value_make_int";
             else if (val->getType()->isDoubleTy()) funcName = "ts_value_make_double";
             else if (val->getType()->isIntegerTy(1)) funcName = "ts_value_make_bool";
        }
    }

    if (funcName.empty()) return val;
    
    llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { valType }, false);
    llvm::FunctionCallee boxFn = module->getOrInsertFunction(funcName, boxFt);
    
    return createCall(boxFt, boxFn.getCallee(), { val });
}

llvm::Value* IRGenerator::unboxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!type) return val;
    
    if (type->kind == TypeKind::Int) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Double) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Boolean) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::String) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_string", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    } else if (type->kind == TypeKind::Object || type->kind == TypeKind::Intersection || 
               type->kind == TypeKind::Class || type->kind == TypeKind::Array) {
        llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
        return createCall(unboxFt, unboxFn.getCallee(), { val });
    }
    
    return val;
}

void IRGenerator::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {}
void IRGenerator::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {}
void IRGenerator::visitBindingElement(ast::BindingElement* node) {}
void IRGenerator::visitSpreadElement(ast::SpreadElement* node) {}
void IRGenerator::visitOmittedExpression(ast::OmittedExpression* node) {}

void IRGenerator::visitImportDeclaration(ast::ImportDeclaration* node) {}
void IRGenerator::visitExportDeclaration(ast::ExportDeclaration* node) {}

void IRGenerator::visitProgram(ast::Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void IRGenerator::visitClassDeclaration(ast::ClassDeclaration* node) {}
void IRGenerator::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {}
void IRGenerator::visitFunctionDeclaration(ast::FunctionDeclaration* node) {}
void IRGenerator::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {}
void IRGenerator::visitEnumDeclaration(ast::EnumDeclaration* node) {}

void IRGenerator::emitObjectCode(const std::string& filename) {
    std::string targetTriple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(targetTriple);

    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        llvm::errs() << error;
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = llvm::Reloc::Model::PIC_;
    auto targetMachine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(targetTriple, cpu, features, opt, rm));

    if (!targetMachine) {
        llvm::errs() << "Could not create target machine\n";
        return;
    }

    module->setDataLayout(targetMachine->createDataLayout());

    if (!runtimeBitcodePath.empty()) {
        fmt::print("Linking runtime bitcode: {}\n", runtimeBitcodePath);
        llvm::SMDiagnostic err;
        auto runtimeModule = llvm::parseIRFile(runtimeBitcodePath, err, *context);
        if (!runtimeModule) {
            err.print("ts-aot", llvm::errs());
        } else {
            if (llvm::Linker::linkModules(*module, std::move(runtimeModule))) {
                fmt::print(stderr, "Error linking runtime bitcode\n");
            } else {
                // Internalize everything except main to allow aggressive DCE
                for (auto& F : *module) {
                    if (!F.isDeclaration() && F.getName() != "main") {
                        F.setLinkage(llvm::GlobalValue::InternalLinkage);
                    }
                }
            }
        }
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        llvm::errs() << "Could not open file: " << ec.message();
        return;
    }

    // New Pass Manager for IR optimizations
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(targetMachine.get());

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::OptimizationLevel level;
    switch (optLevel) {
        case 0: level = llvm::OptimizationLevel::O0; break;
        case 1: level = llvm::OptimizationLevel::O1; break;
        case 2: level = llvm::OptimizationLevel::O2; break;
        case 3: level = llvm::OptimizationLevel::O3; break;
        default: level = llvm::OptimizationLevel::O0; break;
    }

    if (level != llvm::OptimizationLevel::O0) {
        fmt::print("Running IR optimizations (Level O{})\n", optLevel);
        llvm::ModulePassManager MPM;
        
        // Add custom "priority" passes as requested in Epic 73
        MPM.addPass(llvm::ModuleInlinerWrapperPass());
        
        llvm::FunctionPassManager FPM;
        FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
        FPM.addPass(llvm::GVNPass());
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        
        // Add default pipeline
        MPM.addPass(PB.buildPerModuleDefaultPipeline(level));
        
        // Global DCE to remove unused runtime functions after linking
        MPM.addPass(llvm::GlobalDCEPass());
        
        MPM.run(*module, MAM);
    }

    if (filename.ends_with(".bc")) {
        std::error_code ec;
        llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
        if (ec) {
            llvm::errs() << "Could not open file: " << ec.message();
            return;
        }
        llvm::WriteBitcodeToFile(*module, dest);
        return;
    }

    if (filename.ends_with(".ll")) {
        std::error_code ec;
        llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
        if (ec) {
            llvm::errs() << "Could not open file: " << ec.message();
            return;
        }
        module->print(dest, nullptr);
        return;
    }

    // Legacy Pass Manager for CodeGen
    llvm::legacy::PassManager pass;
    auto fileType = llvm::CodeGenFileType::ObjectFile;

    if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
        llvm::errs() << "TargetMachine can't emit a file of this type";
        return;
    }

    pass.run(*module);
    dest.flush();
}

void IRGenerator::dumpIR() {
    module->print(llvm::errs(), nullptr);
}

llvm::Value* IRGenerator::createCall(llvm::FunctionType* ft, llvm::Value* callee, std::vector<llvm::Value*> args) {
    if (ft->getNumParams() != args.size()) {
        // Some functions might be vararg or we might have optional args
        // For now, just warn if not vararg
        if (!ft->isVarArg() && args.size() < ft->getNumParams()) {
            llvm::errs() << "Warning: Too few arguments in createCall. Expected " << ft->getNumParams() << ", got " << args.size() << "\n";
        }
    }
    
    std::vector<llvm::Value*> castedArgs;
    for (unsigned i = 0; i < args.size(); ++i) {
        if (i < ft->getNumParams()) {
            castedArgs.push_back(castValue(args[i], ft->getParamType(i)));
        } else {
            // For varargs, we don't have an expected type, but we should probably box them if they are primitives
            // to match our runtime's expectations for 'any'
            castedArgs.push_back(args[i]);
        }
    }
    
    return builder->CreateCall(ft, callee, castedArgs);
}

llvm::Value* IRGenerator::castValue(llvm::Value* val, llvm::Type* expectedType) {
    if (val->getType() == expectedType) return val;
    
    // Boxing: primitive -> ptr
    if (!val->getType()->isPointerTy() && expectedType->isPointerTy()) {
        if (val->getType()->isIntegerTy(64)) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_int", boxFt);
            return builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isDoubleTy()) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getDoubleTy() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_double", boxFt);
            return builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isIntegerTy(1)) {
            llvm::FunctionType* boxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt1Ty() }, false);
            llvm::FunctionCallee boxFn = module->getOrInsertFunction("ts_value_make_bool", boxFt);
            return builder->CreateCall(boxFt, boxFn.getCallee(), { val });
        } else if (val->getType()->isIntegerTy(8)) {
            // Void/bool8 -> undefined
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
            return builder->CreateCall(undefFt, undefFn.getCallee());
        }
    }

    // Unboxing: ptr -> primitive
    if (val->getType()->isPointerTy() && !expectedType->isPointerTy()) {
        if (expectedType->isIntegerTy(64)) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
            return builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isDoubleTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getDoubleTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double", unboxFt);
            return builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        } else if (expectedType->isIntegerTy(1)) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool", unboxFt);
            return builder->CreateCall(unboxFt, unboxFn.getCallee(), { val });
        }
    }

    if (val->getType()->isIntegerTy() && expectedType->isIntegerTy()) {
        return builder->CreateIntCast(val, expectedType, true);
    }
    
    if (val->getType()->isIntegerTy() && expectedType->isDoubleTy()) {
        return builder->CreateSIToFP(val, expectedType);
    }
    
    if (val->getType()->isDoubleTy() && expectedType->isIntegerTy()) {
        return builder->CreateFPToSI(val, expectedType);
    }

    if (val->getType()->isPointerTy() && expectedType->isPointerTy()) {
        return builder->CreateBitCast(val, expectedType);
    }
    
    return val;
}

void IRGenerator::visitExportAssignment(ast::ExportAssignment* node) {
    visit(node->expression.get());
}

} // namespace ts
