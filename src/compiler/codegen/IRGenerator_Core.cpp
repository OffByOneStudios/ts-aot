#include "IRGenerator.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>

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
    generateClasses(analyzer, specializations);
    generatePrototypes(specializations);
    generateBodies(specializations);
    generateEntryPoint();
}

void IRGenerator::generateGlobals(const Analyzer& analyzer) {
    for (auto& [path, modulePtr] : analyzer.modules) {
        if (!modulePtr->ast) continue;
        for (auto& stmt : modulePtr->ast->body) {
            if (auto var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
                    if (module->getGlobalVariable(id->name)) continue;

                    llvm::Type* type = getLLVMType(var->resolvedType);
                    new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::ExternalLinkage,
                        llvm::Constant::getNullValue(type), id->name);
                }
            }
        }
    }
}

void IRGenerator::generateEntryPoint() {
    llvm::Function* userMain = module->getFunction("user_main");
    if (!userMain) return;

    // Declare ts_main
    std::vector<llvm::Type*> args = {
        llvm::Type::getInt32Ty(*context),
        llvm::PointerType::getUnqual(*context),
        llvm::PointerType::getUnqual(*context)
    };
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context), args, false);
    llvm::Function* tsMain = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_main", module.get());

    // Define main
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
    
    builder->CreateCall(tsMain, { argc, argv, userMain });
    
    builder->CreateRet(llvm::ConstantInt::get(*context, llvm::APInt(32, 0)));
}

llvm::Type* IRGenerator::getLLVMType(const std::shared_ptr<Type>& type) {
    switch (type->kind) {
        case TypeKind::Void: return llvm::Type::getInt8Ty(*context);
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
                llvm::FunctionCallee sliceFn = module->getOrInsertFunction("ts_array_slice",
                    llvm::PointerType::getUnqual(*context),
                    llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context));
                llvm::Value* restArr = builder->CreateCall(sliceFn, { value, idxVal });
                generateDestructuring(restArr, type, element->name.get());
                break; 
            }

            llvm::Value* elementVal = builder->CreateCall(getFn, { value, idxVal });
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
        collectVariables(forOf->initializer.get(), vars);
        collectVariables(forOf->body.get(), vars);
    }
}

llvm::Value* IRGenerator::boxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!val) return nullptr;
    
    llvm::Type* valType = val->getType();
    std::string funcName;
    
    if (valType->isIntegerTy(64)) funcName = "ts_value_make_int";
    else if (valType->isDoubleTy()) funcName = "ts_value_make_double";
    else if (valType->isIntegerTy(1)) funcName = "ts_value_make_bool";
    else if (type && type->kind == TypeKind::Function) {
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_value_make_function",
            builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy());
        return builder->CreateCall(fn, { builder->CreateBitCast(val, builder->getPtrTy()), llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    } else if (valType->isPointerTy()) {
        if (type && type->kind == TypeKind::String) funcName = "ts_value_make_string";
        else funcName = "ts_value_make_object";
    } else if (type && type->kind == TypeKind::Any) {
        llvm::Function* fn = getRuntimeFunction("ts_value_make_object");
        return builder->CreateCall(fn, { val });
    }
    
    if (funcName.empty()) return val;
    
    llvm::FunctionCallee boxFn = module->getOrInsertFunction(funcName,
        builder->getPtrTy(), valType);
    
    return builder->CreateCall(boxFn, { val });
}

llvm::Value* IRGenerator::unboxValue(llvm::Value* val, std::shared_ptr<Type> type) {
    if (!type) return val;
    
    if (type->kind == TypeKind::Int) {
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int",
            llvm::Type::getInt64Ty(*context), builder->getPtrTy());
        return builder->CreateCall(unboxFn, { val });
    } else if (type->kind == TypeKind::Double) {
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double",
            llvm::Type::getDoubleTy(*context), builder->getPtrTy());
        return builder->CreateCall(unboxFn, { val });
    } else if (type->kind == TypeKind::Boolean) {
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool",
            llvm::Type::getInt1Ty(*context), builder->getPtrTy());
        return builder->CreateCall(unboxFn, { val });
    } else if (type->kind == TypeKind::String) {
        llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_string",
            builder->getPtrTy(), builder->getPtrTy());
        return builder->CreateCall(unboxFn, { val });
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
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

    module->setDataLayout(targetMachine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if (ec) {
        llvm::errs() << "Could not open file: " << ec.message();
        return;
    }

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

llvm::Value* IRGenerator::castValue(llvm::Value* val, llvm::Type* expectedType) {
    if (val->getType() == expectedType) return val;
    
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
