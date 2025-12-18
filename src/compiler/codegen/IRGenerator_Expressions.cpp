#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

void IRGenerator::visitBinaryExpression(ast::BinaryExpression* node) {
    if (node->op == "instanceof") {
        visit(node->left.get());
        llvm::Value* left = lastValue;
        
        if (auto id = dynamic_cast<ast::Identifier*>(node->right.get())) {
            std::string className = id->name;
            std::string vtableGlobalName = className + "_VTable_Global";
            llvm::GlobalVariable* vtableGlobal = module->getGlobalVariable(vtableGlobalName);
            
            if (!vtableGlobal) {
                lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                return;
            }
            
            llvm::FunctionCallee instanceofFn = module->getOrInsertFunction("ts_instanceof",
                llvm::Type::getInt1Ty(*context),
                builder->getPtrTy(), builder->getPtrTy());
            
            lastValue = builder->CreateCall(instanceofFn, { left, vtableGlobal });
            return;
        }
        lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
        return;
    }

    visit(node->left.get());
    llvm::Value* left = lastValue;

    visit(node->right.get());
    llvm::Value* right = lastValue;

    if (!left || !right) return;

    bool leftIsDouble = left->getType()->isDoubleTy();
    bool rightIsDouble = right->getType()->isDoubleTy();
    bool isDouble = leftIsDouble || rightIsDouble;
    
    bool leftIsString = node->left->inferredType && node->left->inferredType->kind == TypeKind::String;
    bool rightIsString = node->right->inferredType && node->right->inferredType->kind == TypeKind::String;
    bool isString = leftIsString || rightIsString;

    if (isDouble && !isString) {
        if (!leftIsDouble) left = builder->CreateSIToFP(left, llvm::Type::getDoubleTy(*context), "casttmp");
        if (!rightIsDouble) right = builder->CreateSIToFP(right, llvm::Type::getDoubleTy(*context), "casttmp");
    }

    if (node->op == "+") {
        if (isString) {
             // Convert non-string operands to string
             if (!leftIsString) {
                 if (left->getType()->isIntegerTy(1)) {
                     llvm::FunctionCallee fromBool = module->getOrInsertFunction("ts_string_from_bool",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false));
                     left = builder->CreateCall(fromBool, { left });
                 } else if (left->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false));
                     if (left->getType()->isIntegerTy(32)) {
                         left = builder->CreateSExt(left, llvm::Type::getInt64Ty(*context));
                     }
                     left = builder->CreateCall(fromInt, { left });
                 } else if (left->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false));
                     left = builder->CreateCall(fromDouble, { left });
                 } else if (left->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                         builder->getPtrTy(), builder->getPtrTy());
                     left = builder->CreateCall(fromValue, { left });
                 }
             }
             if (!rightIsString) {
                 if (right->getType()->isIntegerTy(1)) {
                     llvm::FunctionCallee fromBool = module->getOrInsertFunction("ts_string_from_bool",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false));
                     right = builder->CreateCall(fromBool, { right });
                 } else if (right->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false));
                     if (right->getType()->isIntegerTy(32)) {
                         right = builder->CreateSExt(right, llvm::Type::getInt64Ty(*context));
                     }
                     right = builder->CreateCall(fromInt, { right });
                 } else if (right->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false));
                     right = builder->CreateCall(fromDouble, { right });
                 } else if (right->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                         builder->getPtrTy(), builder->getPtrTy());
                     right = builder->CreateCall(fromValue, { right });
                 }
             }

             llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat",
                 builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy());
             lastValue = builder->CreateCall(concatFn, { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFAdd(left, right, "addtmp");
        } else {
            lastValue = builder->CreateAdd(left, right, "addtmp");
        }
    } else if (node->op == "-") {
        if (isDouble) lastValue = builder->CreateFSub(left, right, "subtmp");
        else lastValue = builder->CreateSub(left, right, "subtmp");
    } else if (node->op == "*") {
        if (isDouble) lastValue = builder->CreateFMul(left, right, "multmp");
        else lastValue = builder->CreateMul(left, right, "multmp");
    } else if (node->op == "<") {
        if (isDouble) lastValue = builder->CreateFCmpOLT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLT(left, right, "cmptmp");
    } else if (node->op == "<=") {
        if (isDouble) lastValue = builder->CreateFCmpOLE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLE(left, right, "cmptmp");
    } else if (node->op == ">") {
        if (isDouble) lastValue = builder->CreateFCmpOGT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGT(left, right, "cmptmp");
    } else if (node->op == ">=") {
        if (isDouble) lastValue = builder->CreateFCmpOGE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGE(left, right, "cmptmp");
    } else if (node->op == "==" || node->op == "===") {
        if (isString) {
             llvm::FunctionCallee eqFn = module->getOrInsertFunction("ts_string_eq",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(eqFn, { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFCmpOEQ(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpEQ(left, right, "cmptmp");
        }
    } else if (node->op == "!=" || node->op == "!==") {
        if (isString) {
             llvm::FunctionCallee eqFn = module->getOrInsertFunction("ts_string_eq",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             llvm::Value* eq = builder->CreateCall(eqFn, { left, right });
             lastValue = builder->CreateNot(eq);
        } else if (isDouble) {
            lastValue = builder->CreateFCmpONE(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpNE(left, right, "cmptmp");
        }
    } else if (node->op == "&&") {
        lastValue = builder->CreateAnd(left, right, "andtmp");
    } else if (node->op == "||") {
        lastValue = builder->CreateOr(left, right, "ortmp");
    }
}

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (namedValues.count(node->name)) {
        llvm::Value* val = namedValues[node->name];
        llvm::Type* type = nullptr;
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            type = alloca->getAllocatedType();
        } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
            type = gep->getResultElementType();
        }
        
        if (type) {
            lastValue = builder->CreateLoad(type, val, node->name.c_str());
        } else {
            lastValue = val;
        }
        return;
    }

    // Check for global variable
    llvm::GlobalVariable* gv = module->getGlobalVariable(node->name);
    if (gv) {
        lastValue = builder->CreateLoad(gv->getValueType(), gv, node->name.c_str());
        return;
    }

    llvm::errs() << "Error: Undefined variable " << node->name << "\n";
    lastValue = nullptr;
}

void IRGenerator::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, (int64_t)node->value));
    } else {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    }
}

void IRGenerator::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastValue = llvm::ConstantInt::get(*context, llvm::APInt(1, node->value ? 1 : 0));
}

void IRGenerator::visitStringLiteral(ast::StringLiteral* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = builder->CreateCall(createFn, { strConst });
}

void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (id->name == "fetch") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            llvm::FunctionCallee fetchFn = module->getOrInsertFunction("ts_fetch",
                llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
            
            lastValue = builder->CreateCall(fetchFn, { arg });
            return;
        }
    }

    if (auto superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get())) {
        // super() call in constructor
        if (!currentClass || currentClass->kind != TypeKind::Class) return;
        auto classType = std::static_pointer_cast<ClassType>(currentClass);
        if (!classType->baseClass) return;
        
        std::string baseClassName = classType->baseClass->name;
        std::string ctorName = baseClassName + "_constructor";
        llvm::Function* ctor = module->getFunction(ctorName);
        if (ctor) {
            std::vector<llvm::Value*> args;
            // 'this' is the first argument of the current function
            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            args.push_back(currentFunc->getArg(0));
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                args.push_back(lastValue);
            }
            builder->CreateCall(ctor, args);
        }
        return;
    }

    if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get())) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Namespace) {
            // Namespace call: math.add(1, 2)
            std::vector<std::shared_ptr<Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType);
            }
            std::string specializedName = Monomorphizer::generateMangledName(prop->name, argTypes, node->resolvedTypeArguments);
            
            // Find the specialization to get the return type
            std::shared_ptr<Type> returnType = std::make_shared<Type>(TypeKind::Any);
            for (const auto& spec : specializations) {
                if (spec.specializedName == specializedName) {
                    returnType = spec.returnType;
                    break;
                }
            }

            std::vector<llvm::Type*> paramTypes;
            for (const auto& argType : argTypes) {
                paramTypes.push_back(getLLVMType(argType));
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(returnType), paramTypes, false);
            llvm::FunctionCallee func = module->getOrInsertFunction(specializedName, ft);
            
            std::vector<llvm::Value*> args;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                args.push_back(lastValue);
            }
            
            lastValue = builder->CreateCall(ft, func.getCallee(), args);
            return;
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
                
                // Static method call
                auto current = cls;
                while (current) {
                    if (current->staticMethods.count(prop->name)) {
                        if (current->name == "Promise" && prop->name == "resolve") {
                            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                                builder->getPtrTy(), builder->getPtrTy());
                            
                            llvm::Value* val;
                            std::shared_ptr<Type> valType;
                            if (node->arguments.empty()) {
                                val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                                valType = std::make_shared<Type>(TypeKind::Void);
                            } else {
                                visit(node->arguments[0].get());
                                val = lastValue;
                                valType = node->arguments[0]->inferredType;
                            }
                            
                            llvm::Value* boxedVal = boxValue(val, valType);
                            lastValue = builder->CreateCall(resolveFn, { boxedVal });
                            return;
                        }

                        std::string implName = current->name + "_static_" + prop->name;
                        auto methodType = current->staticMethods[prop->name];
                        
                        std::vector<llvm::Type*> paramTypes;
                        for (const auto& param : methodType->paramTypes) {
                            paramTypes.push_back(getLLVMType(param));
                        }
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            getLLVMType(methodType->returnType), paramTypes, false);
                        
                        llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                        
                        std::vector<llvm::Value*> args;
                        int argIdx = 0;
                        for (auto& arg : node->arguments) {
                            visit(arg.get());
                            llvm::Value* val = lastValue;
                            
                            // Cast if necessary
                            if (argIdx < (int)ft->getNumParams()) {
                                val = castValue(val, ft->getParamType(argIdx));
                            }
                            
                            args.push_back(val);
                            argIdx++;
                        }
                        lastValue = builder->CreateCall(ft, func.getCallee(), args);
                        return;
                    }
                    current = current->baseClass;
                }
            }
        }

        if (dynamic_cast<ast::SuperExpression*>(prop->expression.get())) {
            // super.method()
            if (!currentClass || currentClass->kind != TypeKind::Class) return;
            auto classType = std::static_pointer_cast<ClassType>(currentClass);
            if (!classType->baseClass) return;
            
            std::string baseClassName = classType->baseClass->name;
            std::string methodName = prop->name;
            
            // Static dispatch to base class method
            std::string funcName = baseClassName + "_" + methodName;
            llvm::Function* func = module->getFunction(funcName);
            if (func) {
                std::vector<llvm::Value*> args;
                llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
                args.push_back(currentFunc->getArg(0)); // this
                
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    args.push_back(lastValue);
                }
                lastValue = builder->CreateCall(func, args);
            }
            return;
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            std::string className = classType->name;
            std::string methodName = prop->name;
            
            if (className == "Date") {
                visit(prop->expression.get());
                llvm::Value* dateObj = lastValue;
                
                std::string funcName = "Date_" + methodName;
                llvm::Function* func = module->getFunction(funcName);
                if (!func) {
                    std::vector<llvm::Type*> argTypes = { llvm::PointerType::getUnqual(*context) };
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), argTypes, false);
                    func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, funcName, module.get());
                }
                
                std::vector<llvm::Value*> args = { dateObj };
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    args.push_back(lastValue);
                }
                lastValue = builder->CreateCall(func, args);
                return;
            } else if (className == "RegExp") {
                // ... existing RegExp code ...
                return;
            } else if (className == "Promise") {
                visit(prop->expression.get());
                llvm::Value* promiseObj = lastValue;
                
                if (methodName == "then") {
                    llvm::FunctionCallee thenFn = module->getOrInsertFunction("ts_promise_then",
                        builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy());
                    
                    visit(node->arguments[0].get());
                    llvm::Value* callback = lastValue;
                    
                    // Box the callback if it's not already a TsValue*
                    llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
                    
                    lastValue = builder->CreateCall(thenFn, { promiseObj, boxedCallback });
                    return;
                } else if (methodName == "resolve") {
                    llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                        builder->getPtrTy(), builder->getPtrTy());
                    
                    llvm::Value* val;
                    std::shared_ptr<Type> valType;
                    if (node->arguments.empty()) {
                        val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        valType = std::make_shared<Type>(TypeKind::Void);
                    } else {
                        visit(node->arguments[0].get());
                        val = lastValue;
                        valType = node->arguments[0]->inferredType;
                    }
                    
                    llvm::Value* boxedVal = boxValue(val, valType);
                    lastValue = builder->CreateCall(resolveFn, { boxedVal });
                    return;
                }
            }

            // 1. Evaluate Object
            visit(prop->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
            if (!classStruct) {
                return;
            }
            
            if (!classLayouts.count(className)) {
                return;
            }
            const auto& layout = classLayouts[className];
            if (!layout.methodIndices.count(methodName)) {
                return;
            }
            
            int methodIndex = layout.methodIndices.at(methodName);

            // 2. Get VTable Pointer
            llvm::Value* vptrPtr = builder->CreateStructGEP(classStruct, objPtr, 0);
            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(vtableStruct), vptrPtr);
            
            // 4. Load Function Pointer (index + 1 because of parentVTable at index 0)
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIndex + 1);
            
            auto methodType = layout.allMethods[methodIndex].second;
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
            for (const auto& param : methodType->paramTypes) {
                paramTypes.push_back(getLLVMType(param));
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
            
            llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);
            
            // 5. Call
            std::vector<llvm::Value*> args;
            args.push_back(objPtr); // this
            
            int argIdx = 0;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                
                // Cast if necessary
                if (argIdx + 1 < (int)ft->getNumParams()) {
                    val = castValue(val, ft->getParamType(argIdx + 1));
                }
                
                args.push_back(val);
                argIdx++;
            }
            
            lastValue = builder->CreateCall(ft, funcPtr, args);
            return;
        }

        if (prop->name == "log") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "console") {
                    if (node->arguments.empty()) return;
                    
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) {
                        llvm::errs() << "Error: console.log argument evaluated to null\n";
                        return;
                    }

                    llvm::Type* argType = arg->getType();

                    if (argType->isVoidTy()) {
                        llvm::errs() << "Error: Argument to console.log is void\n";
                        return;
                    }

                    std::string funcName = "ts_console_log";
                    llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

                    if (argType->isIntegerTy(64)) {
                        funcName = "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (argType->isDoubleTy()) {
                        funcName = "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (argType->isIntegerTy(1)) {
                        funcName = "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    builder->CreateCall(logFn, { arg });
                    lastValue = nullptr;
                    return;
                }
            }
        } else if (prop->name == "readFileSync") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "fs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
                    }
                    
                    llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false));
                    
                    lastValue = builder->CreateCall(readFn, { arg });
                    return;
                }
            }
        } else if (prop->name == "readFile") {
            if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
                if (auto obj = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                    if (obj->name == "fs" && innerProp->name == "promises") {
                        if (node->arguments.empty()) return;
                        visit(node->arguments[0].get());
                        llvm::Value* arg = lastValue;
                        
                        llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFile_async",
                            llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                        
                        lastValue = builder->CreateCall(readFn, { arg });
                        return;
                    }
                }
            }
        } else if (prop->name == "writeFileSync") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "fs") {
                    if (node->arguments.size() < 2) return;
                    visit(node->arguments[0].get());
                    llvm::Value* path = lastValue;
                    if (path->getType()->isIntegerTy(64)) {
                        path = builder->CreateIntToPtr(path, llvm::PointerType::getUnqual(*context));
                    }
                    visit(node->arguments[1].get());
                    llvm::Value* content = lastValue;
                    if (content->getType()->isIntegerTy(64)) {
                        content = builder->CreateIntToPtr(content, llvm::PointerType::getUnqual(*context));
                    }
                    
                    llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_writeFileSync",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
                    
                    builder->CreateCall(writeFn, { path, content });
                    return;
                }
            }
        } else if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max") {
                    if (node->arguments.size() < 2) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg1 = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* arg2 = lastValue;
                    
                    std::string funcName = (prop->name == "min") ? "ts_math_min" : "ts_math_max";
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName,
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg1, arg2 });
                    return;
                } else if (prop->name == "abs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_abs",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "floor") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    if (arg->getType()->isIntegerTy(64)) {
                        // floor(int) is just int
                        lastValue = arg;
                        return;
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_floor",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "ceil") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        lastValue = arg;
                        return;
                    }
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_ceil",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "round") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        lastValue = arg;
                        return;
                    }
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_round",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "sqrt") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        arg = builder->CreateSIToFP(arg, llvm::Type::getDoubleTy(*context));
                    }
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_sqrt",
                        llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "pow") {
                    if (node->arguments.size() < 2) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg1 = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* arg2 = lastValue;
                    if (arg1->getType()->isIntegerTy(64)) arg1 = builder->CreateSIToFP(arg1, llvm::Type::getDoubleTy(*context));
                    if (arg2->getType()->isIntegerTy(64)) arg2 = builder->CreateSIToFP(arg2, llvm::Type::getDoubleTy(*context));
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_pow",
                        llvm::FunctionType::get(llvm::Type::getDoubleTy(*context),
                            { llvm::Type::getDoubleTy(*context), llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg1, arg2 });
                    return;
                } else if (prop->name == "random") {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_random",
                        llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false));
                    lastValue = builder->CreateCall(fn);
                    return;
                }
            } else if (obj->name == "Date") {
                if (prop->name == "now") {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("Date_static_now",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false));
                    lastValue = builder->CreateCall(fn);
                    return;
                }
            } else if (obj->name == "Promise") {
                if (prop->name == "resolve") {
                    llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                        builder->getPtrTy(), builder->getPtrTy());
                    
                    llvm::Value* val;
                    std::shared_ptr<Type> valType;
                    if (node->arguments.empty()) {
                        val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        valType = std::make_shared<Type>(TypeKind::Void);
                    } else {
                        visit(node->arguments[0].get());
                        val = lastValue;
                        valType = node->arguments[0]->inferredType;
                    }
                    
                    llvm::Value* boxedVal = boxValue(val, valType);
                    lastValue = builder->CreateCall(resolveFn, { boxedVal });
                    return;
                }
            }
        } else if (prop->name == "charCodeAt") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* index = lastValue;
             
             if (index->getType()->isDoubleTy()) {
                 index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_charCodeAt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             
             lastValue = builder->CreateCall(fn, { obj, index });
             return;
        } else if (prop->name == "split") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* sep = lastValue;
             
             if (sep->getType()->isIntegerTy(64)) {
                 sep = builder->CreateIntToPtr(sep, llvm::PointerType::getUnqual(*context));
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_split",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, sep });
             return;
        } else if (prop->name == "trim") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_trim",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj });
             return;
        } else if (prop->name == "startsWith") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* prefix = lastValue;
             
             if (prefix->getType()->isIntegerTy(64)) {
                 prefix = builder->CreateIntToPtr(prefix, llvm::PointerType::getUnqual(*context));
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_startsWith",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, prefix });
             return;
        } else if (prop->name == "includes") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* search = lastValue;
             
             if (search->getType()->isIntegerTy(64)) {
                 search = builder->CreateIntToPtr(search, llvm::PointerType::getUnqual(*context));
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_includes",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, search });
             return;
        } else if (prop->name == "indexOf") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* search = lastValue;
             
             if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Array) {
                 if (search->getType()->isPointerTy()) {
                     // If it's a pointer (string), we need to cast it to i64 for the generic array storage
                     search = builder->CreatePtrToInt(search, llvm::Type::getInt64Ty(*context));
                 } else if (search->getType()->isDoubleTy()) {
                     search = builder->CreateFPToSI(search, llvm::Type::getInt64Ty(*context));
                 }
                 llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_indexOf",
                     llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                         { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
                 lastValue = builder->CreateCall(fn, { obj, search });
             } else {
                 if (search->getType()->isIntegerTy(64)) {
                     search = builder->CreateIntToPtr(search, llvm::PointerType::getUnqual(*context));
                 }
                 llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_indexOf",
                     llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                         { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
                 lastValue = builder->CreateCall(fn, { obj, search });
             }
             return;
        } else if (prop->name == "slice") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
             llvm::Value* end = nullptr;
             
             if (node->arguments.size() >= 1) {
                 visit(node->arguments[0].get());
                 start = lastValue;
                 if (start->getType()->isDoubleTy()) {
                     start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
                 }
             }
             if (node->arguments.size() >= 2) {
                 visit(node->arguments[1].get());
                 end = lastValue;
                 if (end->getType()->isDoubleTy()) {
                     end = builder->CreateFPToSI(end, llvm::Type::getInt64Ty(*context));
                 }
             } else {
                 // Default end to length
                 llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
                     llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                         { llvm::PointerType::getUnqual(*context) }, false));
                 end = builder->CreateCall(lenFn, { obj });
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_slice",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, start, end });
             return;
        } else if (prop->name == "join") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::Value* sep = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
             if (!node->arguments.empty()) {
                 visit(node->arguments[0].get());
                 sep = lastValue;
                 if (sep->getType()->isIntegerTy(64)) {
                     sep = builder->CreateIntToPtr(sep, llvm::PointerType::getUnqual(*context));
                 }
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_join",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, sep });
             return;
        } else if (prop->name == "toLowerCase") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toLowerCase",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj });
             return;
        } else if (prop->name == "toUpperCase") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_toUpperCase",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj });
             return;
        } else if (prop->name == "substring") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             
             visit(node->arguments[0].get());
             llvm::Value* start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
             }

             llvm::Value* end = nullptr;
             if (node->arguments.size() > 1) {
                 visit(node->arguments[1].get());
                 end = lastValue;
                 if (end->getType()->isDoubleTy()) {
                     end = builder->CreateFPToSI(end, llvm::Type::getInt64Ty(*context));
                 }
             } else {
                 // Default end to length
                 llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                     llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                         { llvm::PointerType::getUnqual(*context) }, false));
                 end = builder->CreateCall(lenFn, { obj });
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_substring",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, start, end });
             return;
        } else if (prop->name == "sort") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             if (obj->getType()->isIntegerTy(64)) {
                 obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_sort",
                 llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             builder->CreateCall(fn, { obj });
             lastValue = nullptr;
             return;
        } else if (prop->name == "set") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             if (map->getType()->isIntegerTy(64)) {
                 map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.size() < 2) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             if (key->getType()->isIntegerTy(64)) {
                 key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
             }
             visit(node->arguments[1].get());
             llvm::Value* value = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_set",
                 llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             builder->CreateCall(fn, { map, key, value });
             lastValue = nullptr;
             return;
        } else if (prop->name == "get") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             if (map->getType()->isIntegerTy(64)) {
                 map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             if (key->getType()->isIntegerTy(64)) {
                 key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_get",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { map, key });
             return;
        } else if (prop->name == "has") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             if (map->getType()->isIntegerTy(64)) {
                 map = builder->CreateIntToPtr(map, llvm::PointerType::getUnqual(*context));
             }
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             if (key->getType()->isIntegerTy(64)) {
                 key = builder->CreateIntToPtr(key, llvm::PointerType::getUnqual(*context));
             }
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_has",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             llvm::Value* res = builder->CreateCall(fn, { map, key });
             lastValue = builder->CreateICmpNE(res, llvm::ConstantInt::get(res->getType(), 0), "tobool");
             return;
        } else if (prop->name == "md5") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "crypto") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (arg->getType()->isIntegerTy(64)) {
                        arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_crypto_md5",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::PointerType::getUnqual(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                }
            }
        } else if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "Date") {
                if (prop->name == "now") {
                    llvm::FunctionCallee fn = module->getOrInsertFunction("Date_static_now",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false));
                    lastValue = builder->CreateCall(fn);
                    return;
                }
            } else if (obj->name == "Promise") {
                if (prop->name == "resolve") {
                    llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                        builder->getPtrTy(), builder->getPtrTy());
                    
                    llvm::Value* val;
                    std::shared_ptr<Type> valType;
                    if (node->arguments.empty()) {
                        val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        valType = std::make_shared<Type>(TypeKind::Void);
                    } else {
                        visit(node->arguments[0].get());
                        val = lastValue;
                        valType = node->arguments[0]->inferredType;
                    }
                    
                    llvm::Value* boxedVal = boxValue(val, valType);
                    lastValue = builder->CreateCall(resolveFn, { boxedVal });
                    return;
                }
            }
        }
    }

    // User function call
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (namedValues.count(id->name)) {
            llvm::Value* val = namedValues[id->name];
            llvm::Type* varType = nullptr;
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
                varType = gep->getResultElementType();
            }

            if (varType && varType->isPointerTy()) {
                llvm::Value* funcPtr = builder->CreateLoad(varType, val, id->name.c_str());
                
                std::vector<llvm::Value*> args;
                std::vector<llvm::Type*> argTypes;
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* v = lastValue;
                    
                    // Arrow functions always take TsValue* (ptr)
                    v = boxValue(v, arg->inferredType);
                    
                    args.push_back(v);
                    argTypes.push_back(v->getType());
                }
                
                llvm::Type* retType = builder->getPtrTy(); // Arrow functions always return TsValue* (ptr)
                llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
                lastValue = builder->CreateCall(ft, funcPtr, args);
                return;
            }
        }

        if (id->name == "setTimeout" || id->name == "setInterval") {
            if (node->arguments.size() < 2) return;
            
            visit(node->arguments[0].get());
            llvm::Value* callback = lastValue;
            
            // Ensure callback is a pointer to the function
            if (callback->getType()->isIntegerTy()) {
                callback = builder->CreateIntToPtr(callback, builder->getPtrTy());
            }
            
            llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
            
            visit(node->arguments[1].get());
            llvm::Value* delay = lastValue;
            if (delay->getType()->isDoubleTy()) {
                delay = builder->CreateFPToSI(delay, llvm::Type::getInt64Ty(*context));
            }
            
            std::string funcName = (id->name == "setTimeout") ? "ts_set_timeout" : "ts_set_interval";
            llvm::FunctionCallee timerFn = module->getOrInsertFunction(funcName,
                builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context));
            
            llvm::Value* boxedRes = builder->CreateCall(timerFn, { boxedCallback, delay });
            lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
            return;
        }

        if (id->name == "clearTimeout" || id->name == "clearInterval") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* timerId = lastValue;
            llvm::Value* boxedId = boxValue(timerId, node->arguments[0]->inferredType);
            
            llvm::FunctionCallee clearFn = module->getOrInsertFunction("ts_clear_timer",
                llvm::Type::getVoidTy(*context), builder->getPtrTy());
            
            builder->CreateCall(clearFn, { boxedId });
            lastValue = nullptr;
            return;
        }

        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;
             if (arg->getType()->isIntegerTy(64)) {
                 arg = builder->CreateIntToPtr(arg, llvm::PointerType::getUnqual(*context));
             }

             llvm::FunctionCallee parseFn = module->getOrInsertFunction("ts_parseInt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             
             lastValue = builder->CreateCall(parseFn, { arg });
             return;
        }

        if (id->name == "ts_console_log") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            std::string funcName = "ts_console_log";
            llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

            if (arg->getType()->isIntegerTy(64)) {
                funcName = "ts_console_log_int";
                paramType = llvm::Type::getInt64Ty(*context);
            } else if (arg->getType()->isDoubleTy()) {
                funcName = "ts_console_log_double";
                paramType = llvm::Type::getDoubleTy(*context);
            } else if (arg->getType()->isIntegerTy(1)) {
                funcName = "ts_console_log_bool";
                paramType = llvm::Type::getInt1Ty(*context);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context), { paramType }, false);
            llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

            builder->CreateCall(logFn, { arg });
            lastValue = nullptr;
            return;
        }

        std::string funcName = id->name;
        
        std::vector<llvm::Value*> args;
        std::vector<std::shared_ptr<Type>> argTypes;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            if (lastValue) {
                args.push_back(lastValue);
                if (arg->inferredType) {
                    argTypes.push_back(arg->inferredType);
                } else {
                    // Fallback if type inference failed
                    argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
            }
        }

        std::string mangledName = Monomorphizer::generateMangledName(funcName, argTypes, node->resolvedTypeArguments);

        llvm::Function* func = module->getFunction(mangledName);
        if (func) {
            lastValue = builder->CreateCall(func, args);
        } else {
            llvm::errs() << "Function not found: " << mangledName << "\n";
            // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

void IRGenerator::visitAssignmentExpression(ast::AssignmentExpression* node) {
    // 1. Evaluate the RHS
    visit(node->right.get());
    llvm::Value* val = lastValue;
    if (!val) return;

    // 2. Check LHS type
    if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
        // 3. Look up the variable
        llvm::Value* variable = nullptr;
        llvm::Type* varType = nullptr;

        if (namedValues.count(id->name)) {
            variable = namedValues[id->name];
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(variable)) {
                varType = gep->getResultElementType();
            }
        } else {
            // Check for global variable
            llvm::GlobalVariable* gv = module->getGlobalVariable(id->name);
            if (gv) {
                variable = gv;
                varType = gv->getValueType();
            }
        }

        if (!variable) {
            llvm::errs() << "Error: Unknown variable name " << id->name << "\n";
            return;
        }

        // 4. Store the value
        val = castValue(val, varType);
        builder->CreateStore(val, variable);
    } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
        visit(elem->expression.get());
        llvm::Value* arr = lastValue;
        
        visit(elem->argumentExpression.get());
        llvm::Value* index = lastValue;
        
        if (index->getType()->isDoubleTy()) {
            index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::Value* storeVal = val;
        if (storeVal->getType()->isDoubleTy()) {
            storeVal = builder->CreateBitCast(storeVal, llvm::Type::getInt64Ty(*context));
        } else if (storeVal->getType()->isPointerTy()) {
            storeVal = builder->CreatePtrToInt(storeVal, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_array_set",
            llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                
        builder->CreateCall(setFn, { arr, index, storeVal });
    } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get())) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);

                // Check if it's a static setter
                auto current = cls;
                while (current) {
                    if (current->setters.count(prop->name)) {
                        std::string implName = current->name + "_static_set_" + prop->name;
                        auto methodType = current->setters[prop->name];
                        
                        std::vector<llvm::Type*> paramTypes;
                        paramTypes.push_back(getLLVMType(methodType->paramTypes[0]));
                        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                        
                        llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                        builder->CreateCall(ft, func.getCallee(), { val });
                        lastValue = val;
                        return;
                    }
                    current = current->baseClass;
                }

                std::string mangledName = cls->name + "_static_" + prop->name;
                auto* gVar = module->getGlobalVariable(mangledName);
                if (gVar) {
                    // Cast if necessary
                    val = castValue(val, gVar->getValueType());
                    builder->CreateStore(val, gVar);
                    lastValue = val;
                    return;
                }
            }
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            std::string className = classType->name;
            std::string fieldName = prop->name;
            
            // Check if it's a setter
            std::string vname = "set_" + fieldName;
            if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
                visit(prop->expression.get());
                llvm::Value* objPtr = lastValue;
                
                int methodIdx = classLayouts[className].methodIndices[vname];
                
                // Load VTable pointer
                llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), objPtr);
                
                llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
                if (!vtableStruct) return;
                
                // Load function pointer from VTable
                llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
                llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), funcPtrPtr);
                
                // Get the setter type
                std::shared_ptr<FunctionType> setterType;
                auto current = classType;
                while (current) {
                    if (current->setters.count(fieldName)) {
                        setterType = current->setters[fieldName];
                        break;
                    }
                    current = current->baseClass;
                }
                
                if (setterType) {
                    std::vector<llvm::Type*> paramTypes;
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
                    paramTypes.push_back(getLLVMType(setterType->paramTypes[0])); // value
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                    
                    // Cast value if necessary
                    llvm::Value* castVal = val;
                    llvm::Type* expectedType = ft->getParamType(1);
                    if (castVal->getType() != expectedType) {
                        if (expectedType->isDoubleTy() && castVal->getType()->isIntegerTy()) {
                            castVal = builder->CreateSIToFP(castVal, expectedType);
                        } else if (expectedType->isIntegerTy() && castVal->getType()->isDoubleTy()) {
                            castVal = builder->CreateFPToSI(castVal, expectedType);
                        }
                    }

                    builder->CreateCall(ft, funcPtr, { objPtr, castVal });
                    lastValue = val;
                    return;
                }
            }

            // Check if it's a field access
            if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
                visit(prop->expression.get());
                llvm::Value* objPtr = lastValue;
                
                llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
                if (!classStruct) return;
                
                llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));
                
                int fieldIndex = classLayouts[className].fieldIndices[fieldName];
                llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
                
                // We need the type of the field to load it correctly
                // The field could be in a base class, so we look it up in the layout's allFields
                std::shared_ptr<Type> fieldType;
                for (const auto& f : classLayouts[className].allFields) {
                    if (f.first == fieldName) {
                        fieldType = f.second;
                        break;
                    }
                }
                
                if (fieldType) {
                    val = castValue(val, getLLVMType(fieldType));
                    builder->CreateStore(val, fieldPtr);
                    lastValue = val;
                } else {
                    lastValue = nullptr;
                }
            } else {
                llvm::errs() << "Error: Field " << fieldName << " not found in class " << className << "\n";
            }
        }
    } else {
        llvm::errs() << "Error: LHS of assignment must be an identifier or element access\n";
        return;
    }
    
    // Assignment evaluates to the value
    lastValue = val;
}

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", 
        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
    
    llvm::Value* arr = builder->CreateCall(createFn);

    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));

    for (auto& el : node->elements) {
        if (auto spread = dynamic_cast<ast::SpreadElement*>(el.get())) {
            visit(spread->expression.get());
            llvm::Value* otherArr = lastValue;
            
            llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_array_concat",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            builder->CreateCall(concatFn, { arr, otherArr });
            continue;
        }

        visit(el.get());
        llvm::Value* val = lastValue;
        if (!val) continue;

        if (val->getType()->isDoubleTy()) {
             val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
        } else if (val->getType()->isPointerTy()) {
             val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
        }
        
        builder->CreateCall(pushFn, { arr, val });
    }
    
    lastValue = arr;
}

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (index->getType()->isDoubleTy()) {
        index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
    }
    
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get",
        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
            
    llvm::Value* val = builder->CreateCall(getFn, { arr, index });

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->expression->inferredType);
        if (arrayType->elementType->kind == TypeKind::String || arrayType->elementType->kind == TypeKind::Object) {
             lastValue = builder->CreateIntToPtr(val, llvm::PointerType::getUnqual(*context));
             return;
        }
    }
    
    lastValue = val;
}

void IRGenerator::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Namespace) {
        // Namespace property access: math.x
        // This refers to a global variable in another module
        // For now, we assume global variables are not mangled by module
        auto* gVar = module->getGlobalVariable(node->name);
        if (gVar) {
            lastValue = builder->CreateLoad(gVar->getValueType(), gVar);
        } else {
            // If not found, it might be a function reference (not a call)
            // TODO: Handle function references
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Math" && node->name == "PI") {
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_PI",
                llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false));
            lastValue = builder->CreateCall(fn);
            return;
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(node->expression->inferredType);
        if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            
            // Check if it's a static getter
            auto current = cls;
            while (current) {
                if (current->getters.count(node->name)) {
                    std::string implName = current->name + "_static_get_" + node->name;
                    auto methodType = current->getters[node->name];
                    
                    std::vector<llvm::Type*> paramTypes;
                    llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
                    
                    llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                    lastValue = builder->CreateCall(ft, func.getCallee(), {});
                    return;
                }
                current = current->baseClass;
            }

            // Check if it's a static field
            std::string mangledName = cls->name + "_static_" + node->name;
            auto* gVar = module->getGlobalVariable(mangledName);
            if (gVar) {
                lastValue = builder->CreateLoad(gVar->getValueType(), gVar);
                return;
            }

            // Check if it's a static method
            current = cls;
            while (current) {
                if (current->staticMethods.count(node->name)) {
                    std::string implName = current->name + "_static_" + node->name;
                    auto methodType = current->staticMethods[node->name];
                    
                    std::vector<llvm::Type*> paramTypes;
                    for (const auto& param : methodType->paramTypes) {
                        paramTypes.push_back(getLLVMType(param));
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
                    
                    lastValue = module->getOrInsertFunction(implName, ft).getCallee();
                    return;
                }
                current = current->baseClass;
            }
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        std::string className = classType->name;
        std::string fieldName = node->name;
        
        // Check if it's a getter
        std::string vname = "get_" + fieldName;
        if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            int methodIdx = classLayouts[className].methodIndices[vname];
            
            // Load VTable pointer (first field of the struct)
            llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), objPtr);
            
            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            if (!vtableStruct) return;
            
            // Load function pointer from VTable
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
            llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), funcPtrPtr);
            
            // Get the getter type
            std::shared_ptr<FunctionType> getterType;
            auto currentClass = classType;
            while (currentClass) {
                if (currentClass->getters.count(fieldName)) {
                    getterType = currentClass->getters[fieldName];
                    break;
                }
                currentClass = currentClass->baseClass;
            }
            
            if (getterType) {
                std::vector<llvm::Type*> paramTypes;
                paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
                llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(getterType->returnType), paramTypes, false);
                
                lastValue = builder->CreateCall(ft, funcPtr, { objPtr });
                return;
            }
        }

        // Check if it's a field access
        if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
            if (!classStruct) return;
            
            llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));
            
            int fieldIndex = classLayouts[className].fieldIndices[fieldName];
            llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
            
            // We need the type of the field to load it correctly
            // The field could be in a base class, so we look it up in the layout's allFields
            std::shared_ptr<Type> fieldType;
            for (const auto& f : classLayouts[className].allFields) {
                if (f.first == fieldName) {
                    fieldType = f.second;
                    break;
                }
            }
            
            if (fieldType) {
                lastValue = builder->CreateLoad(getLLVMType(fieldType), fieldPtr);
            } else {
                lastValue = nullptr;
            }
            return;
        }
    }

    if (node->name == "length") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
        }
        
        if (!node->expression->inferredType) {
             llvm::errs() << "Warning: No type inferred for length access, assuming string\n";
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
             return;
        }
        
        if (node->expression->inferredType->kind == TypeKind::String) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Array) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else {
             llvm::errs() << "Error: length property not supported on type " << node->expression->inferredType->toString() << "\n";
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else if (node->name == "size") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
        }
        
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
             llvm::FunctionCallee sizeFn = module->getOrInsertFunction("ts_map_size",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(sizeFn, { obj });
        } else {
             llvm::errs() << "Error: size property only supported on Map\n";
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else {
        if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            std::map<std::string, std::shared_ptr<Type>> allFields;
            if (node->expression->inferredType->kind == TypeKind::Object) {
                allFields = std::static_pointer_cast<ObjectType>(node->expression->inferredType)->fields;
            } else {
                auto inter = std::static_pointer_cast<IntersectionType>(node->expression->inferredType);
                for (auto& t : inter->types) {
                    if (t->kind == TypeKind::Object) {
                        for (auto& [name, type] : std::static_pointer_cast<ObjectType>(t)->fields) {
                            allFields[name] = type;
                        }
                    }
                }
            }
            
            std::vector<llvm::Type*> fieldTypes;
            int fieldIdx = -1;
            int idx = 0;
            for (auto& [name, type] : allFields) {
                fieldTypes.push_back(getLLVMType(type));
                if (name == node->name) {
                    fieldIdx = idx;
                }
                idx++;
            }
            
            if (fieldIdx != -1) {
                llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);
                llvm::Value* fieldPtr = builder->CreateStructGEP(structType, objPtr, fieldIdx);
                lastValue = builder->CreateLoad(fieldTypes[fieldIdx], fieldPtr, node->name);
                return;
            }
        }

        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
            llvm::Value* propNameStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* propName = builder->CreateCall(createStrFn, { propNameStr });
            
            llvm::FunctionCallee getPropFn = module->getOrInsertFunction("ts_object_get_property",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            
            lastValue = builder->CreateCall(getPropFn, { objPtr, propName });
            return;
        }
        
        llvm::errs() << "Error: Unknown property " << node->name << "\n";
        lastValue = nullptr;
    }
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    if (node->op == "typeof") {
        visit(node->operand.get());
        llvm::Value* val = lastValue;
        
        if (val->getType()->isDoubleTy() || val->getType()->isIntegerTy(64)) {
            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
                builder->getPtrTy(), builder->getPtrTy());
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("number");
            lastValue = builder->CreateCall(createFn, {strPtr});
            return;
        }
        
        if (val->getType()->isIntegerTy(1)) {
            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
                builder->getPtrTy(), builder->getPtrTy());
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("boolean");
            lastValue = builder->CreateCall(createFn, {strPtr});
            return;
        }

        if (val->getType()->isPointerTy()) {
            llvm::FunctionCallee typeofFn = module->getOrInsertFunction("ts_typeof",
                builder->getPtrTy(), builder->getPtrTy());
            lastValue = builder->CreateCall(typeofFn, {val});
            return;
        }

        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
            builder->getPtrTy(), builder->getPtrTy());
        llvm::Value* strPtr = builder->CreateGlobalStringPtr("unknown");
        lastValue = builder->CreateCall(createFn, {strPtr});
        return;
    }

    visit(node->operand.get());
    llvm::Value* operandV = lastValue;

    if (node->op == "-") {
        if (operandV->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operandV, "negtmp");
        } else if (operandV->getType()->isIntegerTy()) {
            lastValue = builder->CreateNeg(operandV, "negtmp");
        }
    } else if (node->op == "!") {
        if (operandV->getType()->isDoubleTy()) {
            operandV = builder->CreateFCmpONE(operandV, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "tobool");
        } else if (operandV->getType()->isIntegerTy() && operandV->getType()->getIntegerBitWidth() > 1) {
             operandV = builder->CreateICmpNE(operandV, llvm::ConstantInt::get(operandV->getType(), 0), "tobool");
        }
        lastValue = builder->CreateNot(operandV, "nottmp");
    }
}

void IRGenerator::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    visit(node->operand.get());
    llvm::Value* val = lastValue;
    
    if (node->op == "++" || node->op == "--") {
        llvm::Value* next;
        if (val->getType()->isDoubleTy()) {
            double delta = (node->op == "++") ? 1.0 : -1.0;
            next = builder->CreateFAdd(val, llvm::ConstantFP::get(*context, llvm::APFloat(delta)), "incdec");
        } else {
            int64_t delta = (node->op == "++") ? 1 : -1;
            next = builder->CreateAdd(val, llvm::ConstantInt::get(val->getType(), delta), "incdec");
        }
        
        // Store back
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }

            if (variable) {
                builder->CreateStore(next, variable);
            }
        }
        lastValue = val; // Postfix returns old value
    }
}

void IRGenerator::visitTemplateExpression(ast::TemplateExpression* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }
    
    llvm::Function* concatFn = module->getFunction("ts_string_concat");
    if (!concatFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
        concatFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_concat", module.get());
    }

    llvm::Constant* headStr = builder->CreateGlobalStringPtr(node->head);
    llvm::Value* currentStr = builder->CreateCall(createFn, { headStr });
    
    for ( auto& span : node->spans ) {
        visit(span.expression.get());
        llvm::Value* exprVal = lastValue;
        
        if (exprVal->getType()->isIntegerTy()) {
            llvm::Function* fromIntFn = module->getFunction("ts_string_from_int");
            if (!fromIntFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false);
                fromIntFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_int", module.get());
            }
            exprVal = builder->CreateCall(fromIntFn, { exprVal });
        } else if (exprVal->getType()->isDoubleTy()) {
            llvm::Function* fromDoubleFn = module->getFunction("ts_string_from_double");
            if (!fromDoubleFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false);
                fromDoubleFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_double", module.get());
            }
            exprVal = builder->CreateCall(fromDoubleFn, { exprVal });
        }
        
        currentStr = builder->CreateCall(concatFn, { currentStr, exprVal });
        
        llvm::Constant* litStr = builder->CreateGlobalStringPtr(span.literal);
        llvm::Value* litVal = builder->CreateCall(createFn, { litStr });
        currentStr = builder->CreateCall(concatFn, { currentStr, litVal });
    }
    
    lastValue = currentStr;
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    lastValue = castValue(lastValue, getLLVMType(node->inferredType));
}

void IRGenerator::visitAwaitExpression(ast::AwaitExpression* node) {
    if (!currentAsyncContext) {
        // Fallback for non-async context (should be caught by analyzer)
        visit(node->expression.get());
        return;
    }

    // 1. Evaluate expression
    visit(node->expression.get());
    llvm::Value* promiseVal = lastValue;
    
    // Box it if needed
    promiseVal = boxValue(promiseVal, node->expression->inferredType);

    // 2. Save state
    int nextState = asyncStateBlocks.size();
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "state" + std::to_string(nextState), builder->GetInsertBlock()->getParent());
    asyncStateBlocks.push_back(nextBB);

    // Update ctx->state
    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState), statePtr);

    // 3. Call ts_async_await
    llvm::FunctionCallee awaitFn = module->getOrInsertFunction("ts_async_await", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
    builder->CreateCall(awaitFn, { promiseVal, currentAsyncContext });

    // 4. Return from SM function
    builder->CreateRetVoid();

    // 5. Start next state
    builder->SetInsertPoint(nextBB);
    
    // The resumed value is in currentAsyncResumedValue
    // Unbox it to the expected type
    lastValue = unboxValue(currentAsyncResumedValue, node->inferredType);
}

} // namespace ts
