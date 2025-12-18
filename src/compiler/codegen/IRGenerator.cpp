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

IRGenerator::IRGenerator() {
    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("ts_module", *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
}

void IRGenerator::generate(ast::Program* program, const std::vector<Specialization>& specializations, const Analyzer& analyzer) {
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
        case TypeKind::Void: return llvm::Type::getVoidTy(*context);
        case TypeKind::Int: return llvm::Type::getInt64Ty(*context);
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
        case TypeKind::Union:
        case TypeKind::Intersection:
            return llvm::PointerType::getUnqual(*context);
        default: return llvm::Type::getVoidTy(*context);
    }
}

void IRGenerator::generateDestructuring(llvm::Value* value, std::shared_ptr<Type> type, ast::Node* pattern) {
    if (auto id = dynamic_cast<ast::Identifier*>(pattern)) {
        llvm::errs() << "Defining identifier: " << id->name << "\n";
        llvm::AllocaInst* alloca = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), id->name, value->getType());
        builder->CreateStore(value, alloca);
        namedValues[id->name] = alloca;
    } else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(pattern)) {
        llvm::errs() << "Destructuring object pattern\n";
        if (!type) {
            llvm::errs() << "Type is null\n";
        } else {
            llvm::errs() << "Type kind: " << (int)type->kind << "\n";
        }
        if (!type || type->kind != TypeKind::Class) {
            // Fallback for now: just declare variables as Any/null if we can't destructure
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
                        // If value is null, use initializer
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
             std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), args, false);
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
                break; // Spread must be last
            }

            llvm::Value* elementVal = builder->CreateCall(getFn, { value, idxVal });
            
            // Cast elementVal to the correct LLVM type if needed
            llvm::Value* castedVal = castValue(elementVal, getLLVMType(elementType));
            
            if (element->initializer) {
                // If value is 0 (null/undefined for numbers/pointers), use initializer
                // This is a bit loose but works for now
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
        // Windows x64 _setjmp(jmp_buf, frame_ptr)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context), 
            {llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context)}, 
            false);
        return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }

    return nullptr;
}

void IRGenerator::visit(ast::Node* node) {
    if (!node) return;
    if (auto ret = dynamic_cast<ast::ReturnStatement*>(node)) visitReturnStatement(ret);
    else if (auto bin = dynamic_cast<ast::BinaryExpression*>(node)) visitBinaryExpression(bin);
    else if (auto assign = dynamic_cast<ast::AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto id = dynamic_cast<ast::Identifier*>(node)) visitIdentifier(id);
    else if (auto num = dynamic_cast<ast::NumericLiteral*>(node)) visitNumericLiteral(num);
    else if (auto boolean = dynamic_cast<ast::BooleanLiteral*>(node)) visitBooleanLiteral(boolean);
    else if (auto str = dynamic_cast<ast::StringLiteral*>(node)) visitStringLiteral(str);
    else if (auto call = dynamic_cast<ast::CallExpression*>(node)) visitCallExpression(call);
    else if (auto n = dynamic_cast<ast::NewExpression*>(node)) visitNewExpression(n);
    else if (auto obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) visitObjectLiteralExpression(obj);
    else if (auto arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node)) visitPropertyAccessExpression(prop);
    else if (auto as = dynamic_cast<ast::AsExpression*>(node)) visitAsExpression(as);
    else if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) visitVariableDeclaration(varDecl);
    else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node)) visitExpressionStatement(exprStmt);
    else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto forOfStmt = dynamic_cast<ast::ForOfStatement*>(node)) visitForOfStatement(forOfStmt);
    else if (auto sw = dynamic_cast<ast::SwitchStatement*>(node)) visitSwitchStatement(sw);
    else if (auto tryStmt = dynamic_cast<ast::TryStatement*>(node)) visitTryStatement(tryStmt);
    else if (auto throwStmt = dynamic_cast<ast::ThrowStatement*>(node)) visitThrowStatement(throwStmt);
    else if (auto imp = dynamic_cast<ast::ImportDeclaration*>(node)) visitImportDeclaration(imp);
    else if (auto exp = dynamic_cast<ast::ExportDeclaration*>(node)) visitExportDeclaration(exp);
    else if (auto br = dynamic_cast<ast::BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ast::ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<ast::BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto pre = dynamic_cast<ast::PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto sup = dynamic_cast<ast::SuperExpression*>(node)) visitSuperExpression(sup);
    else if (auto obp = dynamic_cast<ast::ObjectBindingPattern*>(node)) visitObjectBindingPattern(obp);
    else if (auto abp = dynamic_cast<ast::ArrayBindingPattern*>(node)) visitArrayBindingPattern(abp);
    else if (auto be = dynamic_cast<ast::BindingElement*>(node)) visitBindingElement(be);
    else if (auto se = dynamic_cast<ast::SpreadElement*>(node)) visitSpreadElement(se);
    else if (auto oe = dynamic_cast<ast::OmittedExpression*>(node)) visitOmittedExpression(oe);
}

void IRGenerator::dumpIR() {
    module->print(llvm::outs(), nullptr);
}

void IRGenerator::emitObjectCode(const std::string& filename) {
    auto targetTriple = module->getTargetTriple();
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if (!target) {
        llvm::errs() << error;
        return;
    }

    auto cpu = "generic";
    auto features = "";

    llvm::TargetOptions opt;
    auto rm = std::optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, rm);

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

llvm::Value* IRGenerator::castValue(llvm::Value* val, llvm::Type* expectedType) {
    if (!val) return nullptr;
    llvm::Type* valType = val->getType();
    if (valType == expectedType) return val;

    if (expectedType->isDoubleTy() && valType->isIntegerTy()) {
        return builder->CreateSIToFP(val, expectedType);
    }
    if (expectedType->isIntegerTy() && valType->isDoubleTy()) {
        return builder->CreateFPToSI(val, expectedType);
    }

    // Handle 'any' (pointers)
    if (expectedType->isPointerTy()) {
        if (valType->isDoubleTy()) {
            // Bitcast double to i64 then inttoptr
            llvm::Value* i64Val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
            return builder->CreateIntToPtr(i64Val, expectedType);
        }
        if (valType->isIntegerTy()) {
            return builder->CreateIntToPtr(val, expectedType);
        }
    }

    if (valType->isPointerTy()) {
        if (expectedType->isDoubleTy()) {
            // ptrtoint then bitcast to double
            llvm::Value* i64Val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
            return builder->CreateBitCast(i64Val, expectedType);
        }
        if (expectedType->isIntegerTy()) {
            return builder->CreatePtrToInt(val, expectedType);
        }
    }

    return builder->CreateBitCast(val, expectedType);
}

void IRGenerator::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {}
void IRGenerator::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {}
void IRGenerator::visitBindingElement(ast::BindingElement* node) {}
void IRGenerator::visitSpreadElement(ast::SpreadElement* node) {}
void IRGenerator::visitOmittedExpression(ast::OmittedExpression* node) {}

void IRGenerator::visitImportDeclaration(ast::ImportDeclaration* node) {
    // Handled during module resolution
}

void IRGenerator::visitExportDeclaration(ast::ExportDeclaration* node) {
    // Handled during module resolution
}

} // namespace ts
