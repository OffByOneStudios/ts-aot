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

    // Unbox if one is a pointer and the other is not, and it's not a string operation
    bool leftIsPtr = left->getType()->isPointerTy();
    bool rightIsPtr = right->getType()->isPointerTy();
    
    if (leftIsPtr && !rightIsPtr && node->left->inferredType && node->left->inferredType->kind != TypeKind::String) {
        if (right->getType()->isDoubleTy()) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double",
                llvm::Type::getDoubleTy(*context), builder->getPtrTy());
            left = builder->CreateCall(unboxFn, { left });
        } else if (right->getType()->isIntegerTy(64)) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int",
                llvm::Type::getInt64Ty(*context), builder->getPtrTy());
            left = builder->CreateCall(unboxFn, { left });
        } else if (right->getType()->isIntegerTy(1)) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool",
                llvm::Type::getInt1Ty(*context), builder->getPtrTy());
            left = builder->CreateCall(unboxFn, { left });
        }
    }
    if (rightIsPtr && !leftIsPtr && node->right->inferredType && node->right->inferredType->kind != TypeKind::String) {
        if (left->getType()->isDoubleTy()) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_double",
                llvm::Type::getDoubleTy(*context), builder->getPtrTy());
            right = builder->CreateCall(unboxFn, { right });
        } else if (left->getType()->isIntegerTy(64)) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int",
                llvm::Type::getInt64Ty(*context), builder->getPtrTy());
            right = builder->CreateCall(unboxFn, { right });
        } else if (left->getType()->isIntegerTy(1)) {
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_bool",
                llvm::Type::getInt1Ty(*context), builder->getPtrTy());
            right = builder->CreateCall(unboxFn, { right });
        }
    }

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

    if (node->op == "+" || node->op == "+=") {
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
    } else if (node->op == "-" || node->op == "-=") {
        if (isDouble) lastValue = builder->CreateFSub(left, right, "subtmp");
        else lastValue = builder->CreateSub(left, right, "subtmp");
    } else if (node->op == "*" || node->op == "*=") {
        if (isDouble) lastValue = builder->CreateFMul(left, right, "multmp");
        else lastValue = builder->CreateMul(left, right, "multmp");
    } else if (node->op == "/" || node->op == "/=") {
        if (isDouble) lastValue = builder->CreateFDiv(left, right, "divtmp");
        else lastValue = builder->CreateSDiv(left, right, "divtmp");
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

    // Handle compound assignment store-back
    if (node->op == "+=" || node->op == "-=" || node->op == "*=" || node->op == "/=") {
        if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }
            if (variable) {
                builder->CreateStore(lastValue, variable);
            }
        } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
            // Handle array element compound assignment: arr[i] += val
            visit(elem->expression.get());
            llvm::Value* arr = lastValue;
            visit(elem->argumentExpression.get());
            llvm::Value* index = lastValue;
            if (index->getType()->isDoubleTy()) {
                index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
            }
            
            std::shared_ptr<Type> elementType;
            if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Array) {
                elementType = std::static_pointer_cast<ArrayType>(elem->expression->inferredType)->elementType;
            }
            
            llvm::Value* storeVal = boxValue(lastValue, elementType);
            llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_array_set",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false));
            builder->CreateCall(setFn, { arr, index, storeVal });
        }
    }
}

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (node->name == "undefined") {
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return;
    }

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
                        size_t numParams = methodType->paramTypes.size();
                        bool hasRest = methodType->hasRest;

                        for (size_t i = 0; i < (hasRest ? numParams - 1 : numParams); ++i) {
                            if (i < node->arguments.size()) {
                                visit(node->arguments[i].get());
                                llvm::Value* val = lastValue;
                                val = castValue(val, ft->getParamType(i));
                                args.push_back(val);
                            } else {
                                // Missing optional parameter
                                args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
                            }
                        }

                        if (hasRest) {
                            // Create an empty array
                            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create",
                                llvm::FunctionType::get(builder->getPtrTy(), {}, false));
                            llvm::Value* arr = builder->CreateCall(createFn);

                            llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
                                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                                    { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));

                            for (size_t i = numParams - 1; i < node->arguments.size(); ++i) {
                                visit(node->arguments[i].get());
                                llvm::Value* val = lastValue;
                                std::shared_ptr<Type> argType = node->arguments[i]->inferredType;
                                llvm::Value* boxedVal = boxValue(val, argType);
                                builder->CreateCall(pushFn, { arr, boxedVal });
                            }
                            args.push_back(arr);
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

        if (prop->expression->inferredType) {
            auto objType = prop->expression->inferredType;

            // Special case for console.log
            if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (id->name == "console" && prop->name == "log") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    std::string funcName = "ts_console_log";
                    llvm::Type* paramType = builder->getPtrTy();

                    if (arg->getType()->isIntegerTy(64)) {
                        funcName = "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (arg->getType()->isDoubleTy()) {
                        funcName = "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (arg->getType()->isIntegerTy(1)) {
                        funcName = "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    } else if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::String) {
                        funcName = "ts_console_log";
                        paramType = builder->getPtrTy();
                    } else {
                        funcName = "ts_console_log_any";
                        paramType = builder->getPtrTy();
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    builder->CreateCall(logFn, { arg });
                    lastValue = nullptr;
                    return;
                }
            }

            if (objType->kind == TypeKind::Map) {
                visit(prop->expression.get());
                llvm::Value* mapObj = lastValue;
                std::string methodName = prop->name;
                
                if (methodName == "set") {
                    llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
                            { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
                    
                    visit(node->arguments[0].get());
                    llvm::Value* key = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* val = lastValue;
                    
                    if (val->getType()->isDoubleTy()) {
                        val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
                    } else if (val->getType()->isIntegerTy(1)) {
                        val = builder->CreateZExt(val, llvm::Type::getInt64Ty(*context));
                    }

                    lastValue = builder->CreateCall(setFn, { mapObj, key, val });
                    return;
                } else if (methodName == "get") {
                    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), 
                            { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
                    
                    visit(node->arguments[0].get());
                    llvm::Value* key = lastValue;
                    
                    lastValue = builder->CreateCall(getFn, { mapObj, key });
                    return;
                } else if (methodName == "has") {
                    llvm::FunctionCallee hasFn = module->getOrInsertFunction("ts_map_has",
                        llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), 
                            { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
                    
                    visit(node->arguments[0].get());
                    llvm::Value* key = lastValue;
                    
                    lastValue = builder->CreateCall(hasFn, { mapObj, key });
                    return;
                }
            }

            if (objType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(objType);
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
                    visit(prop->expression.get());
                    llvm::Value* regexObj = lastValue;
                    
                    if (methodName == "test") {
                        llvm::FunctionCallee testFn = module->getOrInsertFunction("ts_regexp_test",
                            llvm::Type::getInt1Ty(*context), builder->getPtrTy(), builder->getPtrTy());
                        
                        visit(node->arguments[0].get());
                        llvm::Value* str = lastValue;
                        
                        lastValue = builder->CreateCall(testFn, { regexObj, str });
                        return;
                    } else if (methodName == "exec") {
                        llvm::FunctionCallee execFn = module->getOrInsertFunction("ts_regexp_exec",
                            builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy());
                        
                        visit(node->arguments[0].get());
                        llvm::Value* str = lastValue;
                        
                        lastValue = builder->CreateCall(execFn, { regexObj, str });
                        return;
                    }
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

            if (varType && (varType->isPointerTy() || varType->isIntegerTy(64))) {
                llvm::Value* boxedFunc = builder->CreateLoad(varType, val, id->name.c_str());
                if (varType->isIntegerTy(64)) {
                    boxedFunc = builder->CreateIntToPtr(boxedFunc, builder->getPtrTy());
                }
                
                // Get the raw function pointer from the boxed value
                llvm::FunctionCallee getPtrFn = module->getOrInsertFunction("ts_function_get_ptr",
                    builder->getPtrTy(), builder->getPtrTy());
                llvm::Value* funcPtr = builder->CreateCall(getPtrFn, { boxedFunc });
                
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
        return;
    }

    // General expression call (e.g. (function(){})() or a variable call)
    visit(node->callee.get());
    llvm::Value* calleeVal = lastValue;
    if (calleeVal && node->callee->inferredType && node->callee->inferredType->kind == TypeKind::Function) {
        llvm::Value* boxedFunc = calleeVal;
        
        // Get the raw function pointer from the boxed value
        llvm::FunctionCallee getPtrFn = module->getOrInsertFunction("ts_function_get_ptr",
            builder->getPtrTy(), builder->getPtrTy());
        llvm::Value* funcPtr = builder->CreateCall(getPtrFn, { boxedFunc });
        
        std::vector<llvm::Value*> args;
        std::vector<llvm::Type*> argTypes;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            llvm::Value* v = lastValue;
            v = boxValue(v, arg->inferredType);
            args.push_back(v);
            argTypes.push_back(v->getType());
        }
        
        llvm::Type* retType = builder->getPtrTy();
        llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
        lastValue = builder->CreateCall(ft, funcPtr, args);
        return;
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
        
        std::shared_ptr<Type> elementType;
        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(elem->expression->inferredType)->elementType;
        }

        llvm::Value* storeVal = boxValue(val, elementType);

        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_array_set",
            llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false));
                
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
        } else if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
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
                    lastValue = builder->CreateLoad(getLLVMType(fieldType), fieldPtr);
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
            { builder->getPtrTy(), builder->getPtrTy() }, false));

    auto tupleType = (node->inferredType && node->inferredType->kind == TypeKind::Tuple) ? 
        std::static_pointer_cast<ts::TupleType>(node->inferredType) : nullptr;

    int i = 0;
    for (auto& el : node->elements) {
        if (auto spread = dynamic_cast<ast::SpreadElement*>(el.get())) {
            visit(spread->expression.get());
            llvm::Value* otherArr = lastValue;
            
            llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_array_concat",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy() }, false));
            builder->CreateCall(concatFn, { arr, otherArr });
            continue;
        }

        visit(el.get());
        llvm::Value* val = lastValue;
        if (!val) continue;

        if (tupleType && i < (int)tupleType->elementTypes.size()) {
            val = castValue(val, getLLVMType(tupleType->elementTypes[i]));
        }

        llvm::Value* boxedVal = boxValue(val, el->inferredType);
        builder->CreateCall(pushFn, { arr, boxedVal });
        i++;
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
        llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
            
    llvm::Value* val = builder->CreateCall(getFn, { arr, index });

    std::shared_ptr<ts::Type> elementType;
    if (node->expression->inferredType) {
        if (node->expression->inferredType->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ts::ArrayType>(node->expression->inferredType)->elementType;
        } else if (node->expression->inferredType->kind == TypeKind::Tuple) {
            auto tupleType = std::static_pointer_cast<ts::TupleType>(node->expression->inferredType);
            if (auto lit = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get())) {
                size_t idx = (size_t)lit->value;
                if (idx < tupleType->elementTypes.size()) {
                    elementType = tupleType->elementTypes[idx];
                }
            }
            if (!elementType) {
                // Fallback to union of all types if index is not literal
                if (!tupleType->elementTypes.empty()) {
                    elementType = std::make_shared<ts::UnionType>(tupleType->elementTypes);
                }
            }
        }
    }

    lastValue = unboxValue(val, elementType);
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
    } else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Enum) {
        auto enumType = std::static_pointer_cast<EnumType>(node->expression->inferredType);
        if (enumType->members.count(node->name)) {
            int value = enumType->members[node->name];
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), value);
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
