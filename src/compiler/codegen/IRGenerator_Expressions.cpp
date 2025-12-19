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
    
    if (leftIsPtr && !rightIsPtr && node->left->inferredType && 
        node->left->inferredType->kind != TypeKind::String &&
        node->left->inferredType->kind != TypeKind::Null &&
        node->left->inferredType->kind != TypeKind::Undefined) {
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
    if (rightIsPtr && !leftIsPtr && node->right->inferredType && 
        node->right->inferredType->kind != TypeKind::String &&
        node->right->inferredType->kind != TypeKind::Null &&
        node->right->inferredType->kind != TypeKind::Undefined) {
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
             if (!left->getType()->isPointerTy()) {
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
                 }
             } else if (!leftIsString) {
                 // It's a pointer but not a string, assume it's a TsValue*
                 llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                     builder->getPtrTy(), builder->getPtrTy());
                 left = builder->CreateCall(fromValue, { left });
             }

             if (!right->getType()->isPointerTy()) {
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
                 }
             } else if (!rightIsString) {
                 // It's a pointer but not a string, assume it's a TsValue*
                 llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                     builder->getPtrTy(), builder->getPtrTy());
                 right = builder->CreateCall(fromValue, { right });
             }

             llvm::FunctionType* concatFt = llvm::FunctionType::get(
                 builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
             llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat", concatFt);
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
    if (node->name == "undefined" || node->name == "null") {
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return;
    }

    if (node->name == "this") {
        if (currentAsyncFrame) {
            auto it = currentAsyncFrameMap.find("this");
            if (it != currentAsyncFrameMap.end()) {
                llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
                lastValue = builder->CreateLoad(currentAsyncFrameType->getElementType(it->second), ptr);
                return;
            }
        }
        llvm::Function* func = builder->GetInsertBlock()->getParent();
        // In our new calling convention, context is arg(0), this is arg(1)
        if (func->arg_size() > 1) {
            lastValue = func->getArg(1);
        } else {
            lastValue = func->getArg(0); // Fallback for global functions if they ever use 'this'
        }
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
        if (prop->expression->inferredType) {
            auto objType = prop->expression->inferredType;
            
            if (objType->kind == TypeKind::Array || objType->kind == TypeKind::Tuple) {
                visit(prop->expression.get());
                llvm::Value* arrObj = lastValue;
                std::string methodName = prop->name;

                if (methodName == "push") {
                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    builder->CreateCall(pushFn, { arrObj, boxedVal });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "pop") {
                    llvm::FunctionCallee popFn = module->getOrInsertFunction("ts_array_pop",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    llvm::Value* ret = builder->CreateCall(popFn, { arrObj });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "shift") {
                    llvm::FunctionCallee shiftFn = module->getOrInsertFunction("ts_array_shift",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    llvm::Value* ret = builder->CreateCall(shiftFn, { arrObj });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "unshift") {
                    llvm::FunctionCallee unshiftFn = module->getOrInsertFunction("ts_array_unshift",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    builder->CreateCall(unshiftFn, { arrObj, boxedVal });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "sort") {
                    llvm::FunctionCallee sortFn = module->getOrInsertFunction("ts_array_sort",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false));
                    builder->CreateCall(sortFn, { arrObj });
                    return;
                } else if (methodName == "indexOf") {
                    llvm::FunctionCallee idxFn = module->getOrInsertFunction("ts_array_indexOf",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    lastValue = builder->CreateCall(idxFn, { arrObj, boxedVal });
                    return;
                } else if (methodName == "includes") {
                    llvm::FunctionCallee incFn = module->getOrInsertFunction("ts_array_includes",
                        llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    lastValue = builder->CreateCall(incFn, { arrObj, boxedVal });
                    return;
                } else if (methodName == "at") {
                    llvm::FunctionCallee atFn = module->getOrInsertFunction("ts_array_at",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* idx = lastValue;
                    llvm::Value* ret = builder->CreateCall(atFn, { arrObj, idx });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "join") {
                    llvm::FunctionCallee joinFn = module->getOrInsertFunction("ts_array_join",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    llvm::Value* sep;
                    if (node->arguments.empty()) {
                        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create",
                            llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                        sep = builder->CreateCall(createFn, { builder->CreateGlobalStringPtr(",") });
                    } else {
                        visit(node->arguments[0].get());
                        sep = lastValue;
                    }
                    lastValue = builder->CreateCall(joinFn, { arrObj, sep });
                    return;
                } else if (methodName == "slice") {
                    llvm::FunctionCallee sliceFn = module->getOrInsertFunction("ts_array_slice",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                    llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
                    llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1); // -1 means end of array
                    if (node->arguments.size() >= 1) {
                        visit(node->arguments[0].get());
                        start = lastValue;
                    }
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        end = lastValue;
                    }
                    lastValue = builder->CreateCall(sliceFn, { arrObj, start, end });
                    return;
                } else if (methodName == "forEach" || methodName == "map" || methodName == "filter" || methodName == "some" || methodName == "every" || methodName == "find" || methodName == "findIndex") {
                    std::string fnName = "ts_array_" + methodName;
                    llvm::Type* retTy = builder->getPtrTy();
                    if (methodName == "forEach") retTy = llvm::Type::getVoidTy(*context);
                    else if (methodName == "some" || methodName == "every") retTy = llvm::Type::getInt1Ty(*context);
                    else if (methodName == "findIndex") retTy = llvm::Type::getInt64Ty(*context);

                    llvm::FunctionCallee funcFn = module->getOrInsertFunction(fnName,
                        llvm::FunctionType::get(retTy, { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    
                    visit(node->arguments[0].get());
                    llvm::Value* callback = lastValue;
                    llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
                    
                    llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    
                    lastValue = builder->CreateCall(funcFn, { arrObj, boxedCallback, thisArg });
                    
                    if (methodName == "find") {
                        std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                        if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                        lastValue = unboxValue(lastValue, elemType);
                    }
                    return;
                } else if (methodName == "reduce") {
                    llvm::FunctionCallee reduceFn = module->getOrInsertFunction("ts_array_reduce",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    
                    visit(node->arguments[0].get());
                    llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
                    
                    llvm::Value* initialValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        initialValue = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    
                    lastValue = builder->CreateCall(reduceFn, { arrObj, callback, initialValue });
                    return;
                }
            }

            if (objType->kind == TypeKind::String) {
                visit(prop->expression.get());
                llvm::Value* strObj = lastValue;
                std::string methodName = prop->name;

                if (methodName == "charCodeAt") {
                    llvm::FunctionCallee charAtFn = module->getOrInsertFunction("ts_string_charCodeAt",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* idx = lastValue;
                    lastValue = builder->CreateCall(charAtFn, { strObj, idx });
                    return;
                } else if (methodName == "split") {
                    llvm::FunctionCallee splitFn = module->getOrInsertFunction("ts_string_split",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* sep = lastValue;
                    lastValue = builder->CreateCall(splitFn, { strObj, sep });
                    return;
                } else if (methodName == "trim") {
                    llvm::FunctionCallee trimFn = module->getOrInsertFunction("ts_string_trim",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(trimFn, { strObj });
                    return;
                } else if (methodName == "substring" || methodName == "slice") {
                    llvm::FunctionCallee subFn = module->getOrInsertFunction("ts_string_slice",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                    llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
                    llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
                    if (node->arguments.size() >= 1) {
                        visit(node->arguments[0].get());
                        start = lastValue;
                    }
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        end = lastValue;
                    }
                    lastValue = builder->CreateCall(subFn, { strObj, start, end });
                    return;
                } else if (methodName == "toLowerCase") {
                    llvm::FunctionCallee lowerFn = module->getOrInsertFunction("ts_string_toLowerCase",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(lowerFn, { strObj });
                    return;
                } else if (methodName == "toUpperCase") {
                    llvm::FunctionCallee upperFn = module->getOrInsertFunction("ts_string_toUpperCase",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(upperFn, { strObj });
                    return;
                } else if (methodName == "replace" || methodName == "replaceAll") {
                    std::string fnName = (methodName == "replace") ? "ts_string_replace" : "ts_string_replaceAll";
                    llvm::FunctionCallee replaceFn = module->getOrInsertFunction(fnName,
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* pattern = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* replacement = lastValue;
                    lastValue = builder->CreateCall(replaceFn, { strObj, pattern, replacement });
                    return;
                } else if (methodName == "repeat") {
                    llvm::FunctionCallee repeatFn = module->getOrInsertFunction("ts_string_repeat",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* count = lastValue;
                    lastValue = builder->CreateCall(repeatFn, { strObj, count });
                    return;
                } else if (methodName == "padStart" || methodName == "padEnd") {
                    std::string fnName = (methodName == "padStart") ? "ts_string_padStart" : "ts_string_padEnd";
                    llvm::FunctionCallee padFn = module->getOrInsertFunction(fnName,
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* targetLen = lastValue;
                    llvm::Value* padStr;
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        padStr = lastValue;
                    } else {
                        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create",
                            llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                        padStr = builder->CreateCall(createFn, { builder->CreateGlobalStringPtr(" ") });
                    }
                    lastValue = builder->CreateCall(padFn, { strObj, targetLen, padStr });
                    return;
                }
            }

            if (objType->kind == TypeKind::Map) {
                visit(prop->expression.get());
                llvm::Value* mapObj = lastValue;
                std::string methodName = prop->name;

                if (methodName == "set") {
                    llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* val = boxValue(lastValue, node->arguments[1]->inferredType);
                    builder->CreateCall(setFn, { mapObj, key, val });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "get") {
                    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get",
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::Value* ret = builder->CreateCall(getFn, { mapObj, key });
                    lastValue = unboxValue(ret, std::make_shared<Type>(TypeKind::Any));
                    return;
                } else if (methodName == "has") {
                    llvm::FunctionCallee hasFn = module->getOrInsertFunction("ts_map_has",
                        llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    lastValue = builder->CreateCall(hasFn, { mapObj, key });
                    return;
                } else if (methodName == "delete") {
                    llvm::FunctionCallee delFn = module->getOrInsertFunction("ts_map_delete",
                        llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    lastValue = builder->CreateCall(delFn, { mapObj, key });
                    return;
                } else if (methodName == "clear") {
                    llvm::FunctionCallee clearFn = module->getOrInsertFunction("ts_map_clear",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false));
                    builder->CreateCall(clearFn, { mapObj });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "values" || methodName == "entries") {
                    std::string fnName = "ts_map_" + methodName;
                    llvm::FunctionCallee funcFn = module->getOrInsertFunction(fnName,
                        llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                    lastValue = builder->CreateCall(funcFn, { mapObj });
                    return;
                } else if (methodName == "forEach") {
                    llvm::FunctionCallee forEachFn = module->getOrInsertFunction("ts_map_forEach",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
                    visit(node->arguments[0].get());
                    llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    builder->CreateCall(forEachFn, { mapObj, callback, thisArg });
                    lastValue = nullptr;
                    return;
                }
            }
        }
    }
}