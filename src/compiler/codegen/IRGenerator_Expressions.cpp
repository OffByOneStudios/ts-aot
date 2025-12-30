#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    SPDLOG_INFO("visitIdentifier: {}", node->name);
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

    if (currentAsyncFrame) {
        auto it = currentAsyncFrameMap.find(node->name);
        if (it != currentAsyncFrameMap.end()) {
            SPDLOG_INFO("visitIdentifier: found {} in async frame at index {}", node->name, it->second);
            llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
            lastValue = builder->CreateLoad(currentAsyncFrameType->getStructElementType(it->second), ptr);
            SPDLOG_INFO("visitIdentifier: loaded {} from async frame, lastValue={}", node->name, (void*)lastValue);
            return;
        }
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
            if (concreteTypes.count(val)) {
                concreteTypes[lastValue] = concreteTypes[val];
            }
            if (boxedAllocas.count(val)) {
                boxedValues.insert(lastValue);
            }
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

    SPDLOG_ERROR("Error: Undefined variable {}", node->name);
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
    llvm::FunctionType* createFt = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = createCall(createFt, createFn.getCallee(), { strConst });
}

void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (id->name == "fetch") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            llvm::FunctionType* fetchFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fetchFn = module->getOrInsertFunction("ts_fetch", fetchFt);
            
            lastValue = createCall(fetchFt, fetchFn.getCallee(), { arg });
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
            createCall(ctor->getFunctionType(), ctor, args);
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
                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    createCall(pushFt, pushFn.getCallee(), { arrObj, boxedVal });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "pop") {
                    llvm::FunctionType* popFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee popFn = module->getOrInsertFunction("ts_array_pop", popFt);
                    llvm::Value* ret = createCall(popFt, popFn.getCallee(), { arrObj });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "shift") {
                    llvm::FunctionType* shiftFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee shiftFn = module->getOrInsertFunction("ts_array_shift", shiftFt);
                    llvm::Value* ret = createCall(shiftFt, shiftFn.getCallee(), { arrObj });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "unshift") {
                    llvm::FunctionType* unshiftFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee unshiftFn = module->getOrInsertFunction("ts_array_unshift", unshiftFt);
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    createCall(unshiftFt, unshiftFn.getCallee(), { arrObj, boxedVal });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "sort") {
                    if (node->arguments.empty()) {
                        llvm::FunctionType* sortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                        llvm::FunctionCallee sortFn = module->getOrInsertFunction("ts_array_sort", sortFt);
                        createCall(sortFt, sortFn.getCallee(), { arrObj });
                    } else {
                        visit(node->arguments[0].get());
                        llvm::Value* comparator = boxValue(lastValue, node->arguments[0]->inferredType);
                        
                        llvm::FunctionType* sortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                        llvm::FunctionCallee sortFn = module->getOrInsertFunction("ts_array_sort_with_comparator", sortFt);
                        createCall(sortFt, sortFn.getCallee(), { arrObj, comparator });
                    }
                    lastValue = arrObj;  // sort() returns the array for chaining
                    return;
                } else if (methodName == "indexOf") {
                    llvm::FunctionType* idxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee idxFn = module->getOrInsertFunction("ts_array_indexOf", idxFt);
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    lastValue = createCall(idxFt, idxFn.getCallee(), { arrObj, boxedVal });
                    return;
                } else if (methodName == "includes") {
                    llvm::FunctionType* incFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee incFn = module->getOrInsertFunction("ts_array_includes", incFt);
                    visit(node->arguments[0].get());
                    llvm::Value* val = lastValue;
                    std::shared_ptr<Type> argType = node->arguments[0]->inferredType;
                    llvm::Value* boxedVal = boxValue(val, argType);
                    lastValue = createCall(incFt, incFn.getCallee(), { arrObj, boxedVal });
                    return;
                } else if (methodName == "at") {
                    llvm::FunctionType* atFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee atFn = module->getOrInsertFunction("ts_array_at", atFt);
                    visit(node->arguments[0].get());
                    llvm::Value* idx = lastValue;
                    llvm::Value* ret = createCall(atFt, atFn.getCallee(), { arrObj, idx });
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                    lastValue = unboxValue(ret, elemType);
                    return;
                } else if (methodName == "join") {
                    llvm::FunctionType* joinFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee joinFn = module->getOrInsertFunction("ts_array_join", joinFt);
                    llvm::Value* sep;
                    if (node->arguments.empty()) {
                        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);
                        sep = createCall(createFt, createFn.getCallee(), { builder->CreateGlobalStringPtr(",") });
                    } else {
                        visit(node->arguments[0].get());
                        sep = lastValue;
                    }
                    lastValue = createCall(joinFt, joinFn.getCallee(), { arrObj, sep });
                    return;
                } else if (methodName == "slice") {
                    llvm::FunctionType* sliceFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee sliceFn = module->getOrInsertFunction("ts_array_slice", sliceFt);
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
                    lastValue = createCall(sliceFt, sliceFn.getCallee(), { arrObj, start, end });
                    return;
                } else if (methodName == "forEach" || methodName == "map" || methodName == "filter" || methodName == "some" || methodName == "every" || methodName == "find" || methodName == "findIndex") {
                    std::string fnName = "ts_array_" + methodName;
                    llvm::Type* retTy = builder->getPtrTy();
                    if (methodName == "forEach") retTy = llvm::Type::getVoidTy(*context);
                    else if (methodName == "some" || methodName == "every") retTy = llvm::Type::getInt1Ty(*context);
                    else if (methodName == "findIndex") retTy = llvm::Type::getInt64Ty(*context);

                    llvm::FunctionType* funcFt = llvm::FunctionType::get(retTy, { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee funcFn = module->getOrInsertFunction(fnName, funcFt);
                    
                    visit(node->arguments[0].get());
                    llvm::Value* callback = lastValue;
                    llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
                    
                    llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    
                    lastValue = createCall(funcFt, funcFn.getCallee(), { arrObj, boxedCallback, thisArg });
                    
                    if (methodName == "find") {
                        std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                        if (objType->kind == TypeKind::Array) elemType = std::static_pointer_cast<ArrayType>(objType)->elementType;
                        lastValue = unboxValue(lastValue, elemType);
                    }
                    return;
                } else if (methodName == "reduce") {
                    llvm::FunctionType* reduceFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee reduceFn = module->getOrInsertFunction("ts_array_reduce", reduceFt);
                    
                    visit(node->arguments[0].get());
                    llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
                    
                    llvm::Value* initialValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        initialValue = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    
                    lastValue = createCall(reduceFt, reduceFn.getCallee(), { arrObj, callback, initialValue });
                    return;
                }
            }

            if (objType->kind == TypeKind::String) {
                visit(prop->expression.get());
                llvm::Value* strObj = lastValue;
                std::string methodName = prop->name;

                if (methodName == "charCodeAt") {
                    llvm::FunctionType* charAtFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee charAtFn = module->getOrInsertFunction("ts_string_charCodeAt", charAtFt);
                    visit(node->arguments[0].get());
                    llvm::Value* idx = lastValue;
                    lastValue = createCall(charAtFt, charAtFn.getCallee(), { strObj, idx });
                    return;
                } else if (methodName == "split") {
                    llvm::FunctionType* splitFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee splitFn = module->getOrInsertFunction("ts_string_split", splitFt);
                    visit(node->arguments[0].get());
                    llvm::Value* sep = lastValue;
                    lastValue = createCall(splitFt, splitFn.getCallee(), { strObj, sep });
                    return;
                } else if (methodName == "trim") {
                    llvm::FunctionType* trimFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee trimFn = module->getOrInsertFunction("ts_string_trim", trimFt);
                    lastValue = createCall(trimFt, trimFn.getCallee(), { strObj });
                    return;
                } else if (methodName == "substring" || methodName == "slice") {
                    llvm::FunctionType* subFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee subFn = module->getOrInsertFunction("ts_string_slice", subFt);
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
                    lastValue = createCall(subFt, subFn.getCallee(), { strObj, start, end });
                    return;
                } else if (methodName == "toLowerCase") {
                    llvm::FunctionType* lowerFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee lowerFn = module->getOrInsertFunction("ts_string_toLowerCase", lowerFt);
                    lastValue = createCall(lowerFt, lowerFn.getCallee(), { strObj });
                    return;
                } else if (methodName == "toUpperCase") {
                    llvm::FunctionType* upperFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee upperFn = module->getOrInsertFunction("ts_string_toUpperCase", upperFt);
                    lastValue = createCall(upperFt, upperFn.getCallee(), { strObj });
                    return;
                } else if (methodName == "replace" || methodName == "replaceAll") {
                    std::string fnName = (methodName == "replace") ? "ts_string_replace" : "ts_string_replaceAll";
                    llvm::FunctionType* replaceFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee replaceFn = module->getOrInsertFunction(fnName, replaceFt);
                    visit(node->arguments[0].get());
                    llvm::Value* pattern = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* replacement = lastValue;
                    lastValue = createCall(replaceFt, replaceFn.getCallee(), { strObj, pattern, replacement });
                    return;
                } else if (methodName == "repeat") {
                    llvm::FunctionType* repeatFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                    llvm::FunctionCallee repeatFn = module->getOrInsertFunction("ts_string_repeat", repeatFt);
                    visit(node->arguments[0].get());
                    llvm::Value* count = lastValue;
                    lastValue = createCall(repeatFt, repeatFn.getCallee(), { strObj, count });
                    return;
                } else if (methodName == "padStart" || methodName == "padEnd") {
                    std::string fnName = (methodName == "padStart") ? "ts_string_padStart" : "ts_string_padEnd";
                    llvm::FunctionType* padFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
                    llvm::FunctionCallee padFn = module->getOrInsertFunction(fnName, padFt);
                    visit(node->arguments[0].get());
                    llvm::Value* targetLen = lastValue;
                    llvm::Value* padStr;
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        padStr = lastValue;
                    } else {
                        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);
                        padStr = createCall(createFt, createFn.getCallee(), { builder->CreateGlobalStringPtr(" ") });
                    }
                    lastValue = createCall(padFt, padFn.getCallee(), { strObj, targetLen, padStr });
                    return;
                }
            }

            if (objType->kind == TypeKind::Map) {
                visit(prop->expression.get());
                llvm::Value* mapObj = lastValue;
                
                std::string methodName = prop->name;

                if (methodName == "set") {
                    llvm::FunctionType* setFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set", setFt);
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    visit(node->arguments[1].get());
                    llvm::Value* val = boxValue(lastValue, node->arguments[1]->inferredType);
                    createCall(setFt, setFn.getCallee(), { mapObj, key, val });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "get") {
                    llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::Value* ret = createCall(getFt, getFn.getCallee(), { mapObj, key });
                    lastValue = unboxValue(ret, std::make_shared<Type>(TypeKind::Any));
                    return;
                } else if (methodName == "has") {
                    llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee hasFn = module->getOrInsertFunction("ts_map_has", hasFt);
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    lastValue = createCall(hasFt, hasFn.getCallee(), { mapObj, key });
                    return;
                } else if (methodName == "delete") {
                    llvm::FunctionType* delFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee delFn = module->getOrInsertFunction("ts_map_delete", delFt);
                    visit(node->arguments[0].get());
                    llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                    lastValue = createCall(delFt, delFn.getCallee(), { mapObj, key });
                    return;
                } else if (methodName == "clear") {
                    llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee clearFn = module->getOrInsertFunction("ts_map_clear", clearFt);
                    createCall(clearFt, clearFn.getCallee(), { mapObj });
                    lastValue = nullptr;
                    return;
                } else if (methodName == "values" || methodName == "entries") {
                    std::string fnName = "ts_map_" + methodName;
                    llvm::FunctionType* funcFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee funcFn = module->getOrInsertFunction(fnName, funcFt);
                    lastValue = createCall(funcFt, funcFn.getCallee(), { mapObj });
                    return;
                } else if (methodName == "forEach") {
                    llvm::FunctionType* forEachFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee forEachFn = module->getOrInsertFunction("ts_map_forEach", forEachFt);
                    visit(node->arguments[0].get());
                    llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
                    llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    if (node->arguments.size() >= 2) {
                        visit(node->arguments[1].get());
                        thisArg = boxValue(lastValue, node->arguments[1]->inferredType);
                    }
                    createCall(forEachFt, forEachFn.getCallee(), { mapObj, callback, thisArg });
                    lastValue = nullptr;
                    return;
                }
            }
        }
    }
}

void IRGenerator::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    visit(node->expression.get());
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    visit(node->operand.get());
    llvm::Value* operand = lastValue;

    if (node->op == "++" || node->op == "--") {
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }
            if (variable) {
                llvm::Value* one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
                operand = node->op == "++" ? builder->CreateAdd(operand, one) : builder->CreateSub(operand, one);
                builder->CreateStore(operand, variable);
                lastValue = operand;
                return;
            }
        }
    }

    if (node->op == "-") {
        if (operand->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operand, "negtmp");
        } else {
            lastValue = builder->CreateNeg(operand, "negtmp");
        }
    } else if (node->op == "!") {
        lastValue = builder->CreateNot(operand, "nottmp");
    } else if (node->op == "~") {
        operand = toInt32(operand);
        lastValue = builder->CreateNot(operand, "complementtmp");
        lastValue = builder->CreateIntCast(lastValue, llvm::Type::getInt64Ty(*context), true);
        lastValue = builder->CreateSIToFP(lastValue, llvm::Type::getDoubleTy(*context));
    }
}

void IRGenerator::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    visit(node->operand.get());
    llvm::Value* operand = lastValue;

    if (node->op == "++" || node->op == "--") {
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }
            if (variable) {
                llvm::Value* one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
                llvm::Value* newVal = node->op == "++" ? builder->CreateAdd(operand, one) : builder->CreateSub(operand, one);
                builder->CreateStore(newVal, variable);
            }
        }
    }

    lastValue = operand;
}

void IRGenerator::visitStringTemplateExpression(ast::StringTemplateExpression* node) {
    llvm::FunctionType* concatFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat", concatFt);

    llvm::Value* result = llvm::ConstantPointerNull::get(builder->getPtrTy());
    for (size_t i = 0; i < node->elements.size(); ++i) {
        visit(node->elements[i].get());
        llvm::Value* elem = lastValue;

        if (i == 0) {
            result = elem;
        } else {
            result = createCall(concatFt, concatFn.getCallee(), { result, elem });
        }
    }
    lastValue = result;
}

void IRGenerator::visitTemplateSubstitutionExpression(ast::TemplateSubstitutionExpression* node) {
    visit(node->expression.get());
    llvm::Value* exprValue = lastValue;

    llvm::FunctionType* toStringFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee toStringFn = module->getOrInsertFunction("ts_string_create", toStringFt);
    llvm::Value* strValue = createCall(toStringFt, toStringFn.getCallee(), { exprValue });

    if (node->tail.size() > 0) {
        visit(node->tail[0].get());
        llvm::Value* tailValue = lastValue;

        llvm::FunctionType* concatFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat", concatFt);
        lastValue = createCall(concatFt, concatFn.getCallee(), { strValue, tailValue });
    } else {
        lastValue = strValue;
    }
}

void IRGenerator::visitYieldExpression(ast::YieldExpression* node) {
    if (node->argument) {
        visit(node->argument.get());
        llvm::Value* argValue = lastValue;

        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_generator_create", createFt);
        lastValue = createCall(createFt, createFn.getCallee(), { argValue, currentAsyncFrame });
    } else {
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
    }
}

void IRGenerator::visitClassExpression(ast::ClassExpression* node) {
    // For class expressions, we directly generate the class type and use it
    // as the value of the expression. This is in contrast to class declarations,
    // where the class type is stored in a global variable.
    auto classType = std::make_shared<ClassType>();
    classType->name = node->id->name;
    classType->kind = TypeKind::Class;
    classType->baseClass = node->superClass ? std::make_shared<ClassType>() : nullptr;

    if (node->superClass) {
        // If there is a superclass, initialize the baseClass field of the classType
        if (auto superClassId = dynamic_cast<ast::Identifier*>(node->superClass.get())) {
            classType->baseClass->name = superClassId->name;
        }
    }

    // Register the class type in the current module
    module->addClassType(classType);

    // The value of the class expression is the class type itself
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitFunctionExpression(ast::FunctionExpression* node) {
    // For function expressions, we generate the function type and use it as the
    // value of the expression. This is similar to how we handle function declarations.
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        builder->getPtrTy(), // Return type
        false // Not vararg
    );

    llvm::FunctionCallee funcCallee = module->getOrInsertFunction(node->id->name, funcType);

    // The value of the function expression is the function itself
    lastValue = funcCallee.getCallee();
}

void IRGenerator::visitArrowFunctionExpression(ast::ArrowFunctionExpression* node) {
    // Arrow functions are treated similarly to regular functions for code generation
    visitFunctionExpression(node);
}

void IRGenerator::visitNewExpression(ast::NewExpression* node) {
    visit(node->callee.get());
    llvm::Value* callee = lastValue;

    std::vector<llvm::Value*> args;
    for (auto& arg : node->arguments) {
        visit(arg.get());
        args.push_back(lastValue);
    }

    llvm::FunctionType* newFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee newFn = module->getOrInsertFunction("ts_new", newFt);
    lastValue = createCall(newFt, newFn.getCallee(), { callee, builder->CreateArray(args) });

    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(node->inferredType);
        SPDLOG_DEBUG("NewExpression inferred type: {}", cls->name);
        concreteTypes[lastValue] = cls;
    }
}

void IRGenerator::visitSuperExpression(ast::SuperExpression* node) {
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
        
        createCall(ctor->getFunctionType(), ctor, args);
    }
}

void IRGenerator::visitThisExpression(ast::ThisExpression* node) {
    if (currentAsyncFrame) {
        auto it = currentAsyncFrameMap.find("this");
        if (it != currentAsyncFrameMap.end()) {
            llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
            lastValue = builder->CreateLoad(currentAsyncFrameType->getStructElementType(it->second), ptr);
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
}

void IRGenerator::visitNullLiteral(ast::NullLiteral* node) {
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitTypeAssertion(ast::TypeAssertion* node) {
    visit(node->expression.get());
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
}

void IRGenerator::visitNonNullExpression(ast::NonNullExpression* node) {
    visit(node->expression.get());
}

void IRGenerator::visitOptionalChain(ast::OptionalChain* node) {
    visit(node->expression.get());
}

void IRGenerator::visitSpreadElement(ast::SpreadElement* node) {
    visit(node->expression.get());
}

void IRGenerator::visitArrayExpression(ast::ArrayExpression* node) {
    llvm::FunctionType* arrayCreateFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee arrayCreateFn = module->getOrInsertFunction("ts_array_create", arrayCreateFt);

    llvm::Value* arrayObj = createCall(arrayCreateFt, arrayCreateFn.getCallee(), {});

    for (size_t i = 0; i < node->elements.size(); ++i) {
        visit(node->elements[i].get());
        llvm::Value* elem = boxValue(lastValue, node->elements[i]->inferredType);

        llvm::FunctionType* arrayPushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee arrayPushFn = module->getOrInsertFunction("ts_array_push", arrayPushFt);
        createCall(arrayPushFt, arrayPushFn.getCallee(), { arrayObj, elem });
    }

    lastValue = arrayObj;
}

void IRGenerator::visitObjectExpression(ast::ObjectExpression* node) {
    SPDLOG_DEBUG("visitObjectExpression");
    llvm::FunctionType* objectCreateFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee objectCreateFn = module->getOrInsertFunction("ts_map_create", objectCreateFt);

    llvm::Value* obj = createCall(objectCreateFt, objectCreateFn.getCallee(), {});

    for (auto& prop : node->properties) {
        if (auto kvPair = dynamic_cast<ast::KeyValuePair*>(prop.get())) {
            visit(kvPair->value.get());
            llvm::Value* value = boxValue(lastValue, kvPair->value->inferredType);

            llvm::FunctionType* objectSetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee objectSetFn = module->getOrInsertFunction("ts_map_set", objectSetFt);
            
            llvm::Value* key = nullptr;
            if (auto id = dynamic_cast<ast::Identifier*>(kvPair->key.get())) {
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                key = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(id->name) });
            } else {
                visit(kvPair->key.get());
                key = lastValue;
            }

            createCall(objectSetFt, objectSetFn.getCallee(), { obj, key, value });
        }
    }

    lastValue = obj;
}

void IRGenerator::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(builder->getPtrTy()); // context
    for (size_t i = 0; i < node->params.size(); ++i) {
        argTypes.push_back(builder->getPtrTy());
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        builder->getPtrTy(), // Return type
        argTypes,
        false // Not vararg
    );

    llvm::FunctionCallee funcCallee = module->getOrInsertFunction(node->id->name, funcType);
    llvm::Function* function = llvm::dyn_cast<llvm::Function>(funcCallee.getCallee());

    // The function body is generated in the context of the function itself
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry);

    auto oldNamedValues = namedValues;
    auto oldBoxedVariables = boxedVariables;
    namedValues.clear();
    boxedVariables.clear();

    auto argIt = function->arg_begin();
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        currentContext = &*argIt;
        ++argIt;
    }

    // Allocate space for the arguments and store them
    for (size_t i = 0; i < node->params.size(); ++i) {
        auto param = node->params[i];
        if (argIt != function->arg_end()) {
            llvm::Value* argVal = &*argIt;
            argVal->setName(param->id->name);
            
            llvm::AllocaInst* alloca = builder->CreateAlloca(builder->getPtrTy(), nullptr, param->id->name);
            builder->CreateStore(argVal, alloca);
            namedValues[param->id->name] = alloca;
            ++argIt;
        }
    }

    visit(node->body.get());

    // Create the function
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }

    namedValues = oldNamedValues;
    boxedVariables = oldBoxedVariables;
}

void IRGenerator::visitClassDeclaration(ast::ClassDeclaration* node) {
    auto classType = std::make_shared<ClassType>();
    classType->name = node->id->name;
    classType->kind = TypeKind::Class;
    classType->baseClass = node->superClass ? std::make_shared<ClassType>() : nullptr;

    if (node->superClass) {
        // If there is a superclass, initialize the baseClass field of the classType
        if (auto superClassId = dynamic_cast<ast::Identifier*>(node->superClass.get())) {
            classType->baseClass->name = superClassId->name;
        }
    }

    // Register the class type in the current module
    module->addClassType(classType);

    // The value of the class declaration is the class type itself
    lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitModuleDeclaration(ast::ModuleDeclaration* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void IRGenerator::visitNamespaceDeclaration(ast::NamespaceDeclaration* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

llvm::Value* IRGenerator::generateJsonValue(const nlohmann::json& j) {
    using json = nlohmann::json;
    
    if (j.is_null()) {
        return llvm::ConstantPointerNull::get(builder->getPtrTy());
    }
    
    if (j.is_boolean()) {
        llvm::Value* val = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), j.get<bool>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Boolean));
    }
    
    if (j.is_number_integer()) {
        llvm::Value* val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), j.get<int64_t>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Int));
    }
    
    if (j.is_number_float()) {
        llvm::Value* val = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), j.get<double>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Double));
    }
    
    if (j.is_string()) {
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(j.get<std::string>()) });
        return boxValue(strVal, std::make_shared<Type>(TypeKind::String));
    }
    
    if (j.is_array()) {
        // Create array and populate
        llvm::FunctionType* createArrFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createArrFn = module->getOrInsertFunction("ts_array_create", createArrFt);
        llvm::Value* arr = createCall(createArrFt, createArrFn.getCallee(), {});
        
        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);
        
        for (const auto& elem : j) {
            llvm::Value* elemVal = generateJsonValue(elem);
            createCall(pushFt, pushFn.getCallee(), { arr, elemVal });
        }
        
        return boxValue(arr, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any)));
    }
    
    if (j.is_object()) {
        // Create map and populate
        llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
        llvm::Value* map = createCall(createMapFt, createMapFn.getCallee(), {});
        
        llvm::FunctionType* setMapFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set", setMapFt);
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        
        for (auto& [key, val] : j.items()) {
            llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(key) });
            llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
            llvm::Value* boxedVal = generateJsonValue(val);
            createCall(setMapFt, setFn.getCallee(), { map, boxedKey, boxedVal });
        }
        
        return boxValue(map, std::make_shared<Type>(TypeKind::Object));
    }
    
    return llvm::ConstantPointerNull::get(builder->getPtrTy());
}

void IRGenerator::visitImportDeclaration(ast::ImportDeclaration* node) {
    // Check if this is a JSON module import
    if (analyzer) {
        ResolvedModule resolved = analyzer->resolveModule(node->moduleSpecifier);
        if (resolved.isValid() && resolved.type == ModuleType::JSON) {
            // Find the module with JSON content
            auto it = analyzer->modules.find(resolved.path);
            if (it != analyzer->modules.end() && it->second->jsonContent.has_value()) {
                // Generate the JSON value
                llvm::Value* jsonVal = generateJsonValue(it->second->jsonContent.value());
                
                // Bind to the default import name
                if (!node->defaultImport.empty()) {
                    // Create alloca and store
                    llvm::Function* func = builder->GetInsertBlock()->getParent();
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(func, node->defaultImport, builder->getPtrTy());
                    builder->CreateStore(jsonVal, alloca);
                    namedValues[node->defaultImport] = alloca;
                    boxedVariables.insert(node->defaultImport);
                    SPDLOG_DEBUG("JSON import: {} -> {}", node->defaultImport, resolved.path);
                }
            }
            return;
        }
    }
    // Regular imports are handled at the module level, no action needed here
}

void IRGenerator::visitExportDeclaration(ast::ExportDeclaration* node) {
    // Exports are handled at the module level, no action needed here
}

void IRGenerator::visitEmptyStatement(ast::EmptyStatement* node) {
    // No action needed for empty statements
}

void IRGenerator::visitExpressionStatement(ast::ExpressionStatement* node) {
    visit(node->expression.get());
}

void IRGenerator::visitIfStatement(ast::IfStatement* node) {
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;

    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(*context, "then");
    llvm::BasicBlock* elseBlock = llvm::BasicBlock::Create(*context, "else");
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "ifcont");

    builder->CreateCondBr(condValue, thenBlock, elseBlock);

    // Then block
    builder->SetInsertPoint(thenBlock);
    visit(node->thenStatement.get());
    builder->CreateBr(mergeBlock);

    // Else block
    builder->SetInsertPoint(elseBlock);
    visit(node->elseStatement.get());
    builder->CreateBr(mergeBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void IRGenerator::visitSwitchStatement(ast::SwitchStatement* node) {
    visit(node->discriminant.get());
    llvm::Value* switchValue = lastValue;

    llvm::BasicBlock* defaultBlock = llvm::BasicBlock::Create(*context, "default");
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "switchcont");
    llvm::SwitchInst* switchInst = builder->CreateSwitch(switchValue, defaultBlock);

    for (auto& caseClause : node->cases) {
        llvm::BasicBlock* caseBlock = llvm::BasicBlock::Create(*context, "case");
        visit(caseClause.get());
        builder->CreateBr(mergeBlock);
        switchInst->addCase(llvm::cast<llvm::ConstantInt>(caseClause->test->getValue()), caseBlock);
    }

    // Default block
    builder->SetInsertPoint(defaultBlock);
    visit(node->defaultCase.get());
    builder->CreateBr(mergeBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void IRGenerator::visitWhileStatement(ast::WhileStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "whilecond", func);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "whilebody", func);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "whilecont", func);

    builder->CreateBr(condBlock);

    // Condition block
    builder->SetInsertPoint(condBlock);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;
    builder->CreateCondBr(condValue, bodyBlock, mergeBlock);

    // Body block
    builder->SetInsertPoint(bodyBlock);
    loopStack.push_back({ condBlock, mergeBlock, {} });
    visit(node->body.get());
    loopStack.pop_back();
    builder->CreateBr(condBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void IRGenerator::visitDoWhileStatement(ast::DoWhileStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "dowhilebody", func);
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "dowhilecond", func);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "dowhilecont", func);

    builder->CreateBr(bodyBlock);

    // Body block
    builder->SetInsertPoint(bodyBlock);
    loopStack.push_back({ condBlock, mergeBlock, {} });
    visit(node->body.get());
    loopStack.pop_back();
    builder->CreateBr(condBlock);

    // Condition block
    builder->SetInsertPoint(condBlock);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;
    builder->CreateCondBr(condValue, bodyBlock, mergeBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void IRGenerator::visitForStatement(ast::ForStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "forcond", func);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "forbody", func);
    llvm::BasicBlock* incBlock = llvm::BasicBlock::Create(*context, "forinc", func);
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "forcont", func);

    // Initialize
    if (node->init) {
        visit(node->init.get());
    }
    builder->CreateBr(condBlock);

    // Condition block
    builder->SetInsertPoint(condBlock);
    if (node->condition) {
        visit(node->condition.get());
        llvm::Value* condValue = lastValue;
        builder->CreateCondBr(condValue, bodyBlock, mergeBlock);
    } else {
        builder->CreateBr(bodyBlock);
    }

    // Body block
    builder->SetInsertPoint(bodyBlock);
    loopStack.push_back({ incBlock, mergeBlock, {} });
    
    // BCE: Identify induction variable and array length
    // for (let i = 0; i < arr.length; i++)
    if (auto bin = dynamic_cast<ast::BinaryExpression*>(node->condition.get())) {
        if (bin->op == "<") {
            if (auto id = dynamic_cast<ast::Identifier*>(bin->left.get())) {
                if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(bin->right.get())) {
                    if (prop->name == "length") {
                        if (auto arrId = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                            loopStack.back().safeIndices[id->name] = arrId->name;
                        }
                    }
                }
            }
        }
    }

    visit(node->body.get());
    loopStack.pop_back();
    builder->CreateBr(incBlock);

    // Increment block
    builder->SetInsertPoint(incBlock);
    if (node->update) {
        visit(node->update.get());
    }
    builder->CreateBr(condBlock);

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void IRGenerator::visitBreakStatement(ast::BreakStatement* node) {
    // Break statements are handled by the loop structure, no action needed here
}

void IRGenerator::visitContinueStatement(ast::ContinueStatement* node) {
    // Continue statements are handled by the loop structure, no action needed here
}

} // namespace ts