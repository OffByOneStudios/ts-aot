#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateMemberCall(ast::CallExpression* node) {
    SPDLOG_DEBUG("tryGenerateMemberCall");
    auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (!prop) return false;

    if (tryGenerateBuiltinCall(node, prop)) {
        return true;
    }

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
        
        lastValue = createCall(ft, func.getCallee(), args);
        return true;
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
        if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            
            // Static method call
            auto current = cls;
            while (current) {
                if (current->staticMethods.count(prop->name)) {
                    std::string implName = current->name + "_static_" + prop->name;
                    if (current->name == "Promise" && prop->name == "reject") {
                        implName = "ts_promise_reject";
                    } else if (current->name == "Promise" && prop->name == "resolve") {
                        implName = "ts_promise_resolve";
                    } else if (current->name == "Buffer" && prop->name == "from") {
                        implName = "ts_buffer_from_string";
                    } else if (current->name == "Buffer" && prop->name == "alloc") {
                        implName = "ts_buffer_alloc";
                    }
                    auto methodType = current->staticMethods[prop->name];
                    
                    // Try specialized version first
                    std::vector<std::shared_ptr<Type>> argTypes;
                    for (auto& arg : node->arguments) {
                        argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<Type>(TypeKind::Any));
                    }
                    std::string mangledName = Monomorphizer::generateMangledName(implName, argTypes, {});
                    
                    llvm::Function* specializedFunc = module->getFunction(mangledName);
                    llvm::FunctionType* ft = nullptr;
                    llvm::FunctionCallee func;

                    if (specializedFunc) {
                        func = specializedFunc;
                        ft = specializedFunc->getFunctionType();
                    } else {
                        std::vector<llvm::Type*> paramTypes;
                        // Add context
                        paramTypes.push_back(builder->getPtrTy());

                        // Add vtable param for Buffer static methods
                        if (current->name == "Buffer") {
                            paramTypes.push_back(builder->getPtrTy());
                        }
                        for (const auto& param : methodType->paramTypes) {
                            paramTypes.push_back(getLLVMType(param));
                        }
                        ft = llvm::FunctionType::get(
                            getLLVMType(methodType->returnType), paramTypes, false);
                        
                        func = module->getOrInsertFunction(implName, ft);
                    }
                    
                    std::vector<llvm::Value*> args;
                    // Add context
                    if (currentAsyncContext) {
                        args.push_back(currentAsyncContext);
                    } else {
                        args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
                    }

                    if (current->name == "Buffer") {
                        llvm::Value* vtable = module->getGlobalVariable("Buffer_VTable_Global");
                        if (!vtable) vtable = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        args.push_back(vtable);
                    }
                    int argIdx = 0;
                    int paramOffset = (current->name == "Buffer") ? 2 : 1;
                    for (auto& arg : node->arguments) {
                        visit(arg.get());
                        llvm::Value* val = lastValue;
                        
                        // Cast if necessary (account for context and potential vtable)
                        if (argIdx + paramOffset < (int)ft->getNumParams()) {
                            val = castValue(val, ft->getParamType(argIdx + paramOffset));
                        }
                        
                        args.push_back(val);
                        argIdx++;
                    }
                    lastValue = createCall(ft, func.getCallee(), args);
                    return true;
                }
                current = current->baseClass;
            }
        }
    }

    if (dynamic_cast<ast::SuperExpression*>(prop->expression.get())) {
        // super.method()
        if (!currentClass || currentClass->kind != TypeKind::Class) return false;
        auto classType = std::static_pointer_cast<ClassType>(currentClass);
        if (!classType->baseClass) return false;
        
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
            lastValue = createCall(func->getFunctionType(), func, args);
        }
        return true;
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        std::string className = classType->name;
        std::string methodName = prop->name;
        
        SPDLOG_DEBUG("Method call on class: {} method: {}", className, methodName);
        
        if (className == "RegExp") {
            // ... existing RegExp code ...
            return true;
        } else if (className == "Promise") {
            visit(prop->expression.get());
            llvm::Value* promiseObj = lastValue;
            emitNullCheckForExpression(prop->expression.get(), promiseObj);
            
            if (methodName == "then") {
                llvm::FunctionType* thenFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee thenFn = module->getOrInsertFunction("ts_promise_then", thenFt);
                
                visit(node->arguments[0].get());
                llvm::Value* onFulfilled = boxValue(lastValue, node->arguments[0]->inferredType);
                
                llvm::Value* onRejected;
                if (node->arguments.size() > 1) {
                    visit(node->arguments[1].get());
                    onRejected = boxValue(lastValue, node->arguments[1]->inferredType);
                } else {
                    onRejected = llvm::ConstantPointerNull::get(builder->getPtrTy());
                }
                
                lastValue = createCall(thenFt, thenFn.getCallee(), { promiseObj, onFulfilled, onRejected });
                return true;
            } else if (methodName == "catch") {
                llvm::FunctionType* catchFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee catchFn = module->getOrInsertFunction("ts_promise_catch", catchFt);
                
                visit(node->arguments[0].get());
                llvm::Value* onRejected = boxValue(lastValue, node->arguments[0]->inferredType);
                
                lastValue = createCall(catchFt, catchFn.getCallee(), { promiseObj, onRejected });
                return true;
            } else if (methodName == "finally") {
                llvm::FunctionType* finallyFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee finallyFn = module->getOrInsertFunction("ts_promise_finally", finallyFt);
                
                visit(node->arguments[0].get());
                llvm::Value* onFinally = boxValue(lastValue, node->arguments[0]->inferredType);
                
                lastValue = createCall(finallyFt, finallyFn.getCallee(), { promiseObj, onFinally });
                return true;
            } else if (methodName == "resolve") {
                llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve", resolveFt);
                
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
                lastValue = createCall(resolveFt, resolveFn.getCallee(), { boxedVal });
                return true;
            } else if (methodName == "reject") {
                llvm::FunctionType* rejectFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee rejectFn = module->getOrInsertFunction("ts_promise_reject", rejectFt);
                
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
                lastValue = createCall(rejectFt, rejectFn.getCallee(), { boxedVal });
                return true;
            }
        } else if (className == "Generator") {
            visit(prop->expression.get());
            llvm::Value* genObj = lastValue;
            emitNullCheckForExpression(prop->expression.get(), genObj);

            if (methodName == "next") {
                llvm::FunctionType* nextFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee nextFn = module->getOrInsertFunction("Generator_next", nextFt);

                llvm::Value* boxedGen = boxValue(genObj, prop->expression->inferredType);

                llvm::Value* val;
                if (node->arguments.empty()) {
                    val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                } else {
                    visit(node->arguments[0].get());
                    val = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                lastValue = createCall(nextFt, nextFn.getCallee(), { boxedGen, val });
                return true;
            }
        } else if (className == "AsyncGenerator") {
            visit(prop->expression.get());
            llvm::Value* genObj = lastValue;
            emitNullCheckForExpression(prop->expression.get(), genObj);

            if (methodName == "next") {
                llvm::FunctionType* nextFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee nextFn = module->getOrInsertFunction("AsyncGenerator_next", nextFt);

                llvm::Value* boxedGen = boxValue(genObj, prop->expression->inferredType);

                llvm::Value* val;
                if (node->arguments.empty()) {
                    val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                } else {
                    visit(node->arguments[0].get());
                    val = boxValue(lastValue, node->arguments[0]->inferredType);
                }

                lastValue = createCall(nextFt, nextFn.getCallee(), { boxedGen, val });
                return true;
            }
        } else if (className == "Map") {
            SPDLOG_DEBUG("Generating Map method: {} for object of class: {}", methodName, className);
            visit(prop->expression.get());
            llvm::Value* mapObj = lastValue;
            
            emitNullCheckForExpression(prop->expression.get(), mapObj);

            if (methodName == "set") {
                // void ts_map_set(void* map, TsValue* key, TsValue* value)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_set", ft);

                visit(node->arguments[0].get());
                llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);
                
                visit(node->arguments[1].get());
                llvm::Value* val = boxValue(lastValue, node->arguments[1]->inferredType);

                createCall(ft, fn.getCallee(), { mapObj, key, val });
                lastValue = mapObj; // Map.set returns the map itself
                return true;
            } else if (methodName == "get") {
                // TsValue* ts_map_get(void* map, TsValue* key)
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_get", ft);

                visit(node->arguments[0].get());
                llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);

                llvm::Value* boxedVal = createCall(ft, fn.getCallee(), { mapObj, key });
                
                if (node->inferredType) {
                    lastValue = unboxValue(boxedVal, node->inferredType);
                } else {
                    lastValue = boxedVal;
                }
                return true;
            } else if (methodName == "has") {
                // bool ts_map_has(void* map, TsValue* key)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_has", ft);

                visit(node->arguments[0].get());
                llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);

                lastValue = createCall(ft, fn.getCallee(), { mapObj, key });
                return true;
            } else if (methodName == "delete") {
                // bool ts_map_delete(void* map, TsValue* key)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_delete", ft);

                visit(node->arguments[0].get());
                llvm::Value* key = boxValue(lastValue, node->arguments[0]->inferredType);

                lastValue = createCall(ft, fn.getCallee(), { mapObj, key });
                return true;
            } else if (methodName == "clear") {
                // void ts_map_clear(void* map)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_clear", ft);

                createCall(ft, fn.getCallee(), { mapObj });
                lastValue = nullptr;
                return true;
            }
        } else if (className == "Set") {
            SPDLOG_DEBUG("Generating Set method: {} for object of class: {}", methodName, className);
            visit(prop->expression.get());
            llvm::Value* setObj = lastValue;
            
            emitNullCheckForExpression(prop->expression.get(), setObj);

            if (methodName == "add") {
                // void ts_set_add(void* set, TsValue* value)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_add", ft);

                visit(node->arguments[0].get());
                llvm::Value* val = boxValue(lastValue, node->arguments[0]->inferredType);

                createCall(ft, fn.getCallee(), { setObj, val });
                lastValue = setObj; // Set.add returns the set itself
                return true;
            } else if (methodName == "has") {
                // bool ts_set_has(void* set, TsValue* value)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_has", ft);

                visit(node->arguments[0].get());
                llvm::Value* val = boxValue(lastValue, node->arguments[0]->inferredType);

                lastValue = createCall(ft, fn.getCallee(), { setObj, val });
                return true;
            } else if (methodName == "delete") {
                // bool ts_set_delete(void* set, TsValue* value)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_delete", ft);

                visit(node->arguments[0].get());
                llvm::Value* val = boxValue(lastValue, node->arguments[0]->inferredType);

                lastValue = createCall(ft, fn.getCallee(), { setObj, val });
                return true;
            } else if (methodName == "clear") {
                // void ts_set_clear(void* set)
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_clear", ft);

                createCall(ft, fn.getCallee(), { setObj });
                lastValue = nullptr;
                return true;
            }
        }

        // 1. Evaluate Object
        visit(prop->expression.get());
        llvm::Value* objPtr = lastValue;
        emitNullCheckForExpression(prop->expression.get(), objPtr);
        
        llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
        if (!classStruct) {
            return false;
        }
        
        if (!classLayouts.count(className)) {
            return false;
        }
        const auto& layout = classLayouts[className];
        if (!layout.methodIndices.count(methodName)) {
            return false;
        }
        
        int methodIndex = layout.methodIndices.at(methodName);

        auto methodType = layout.allMethods[methodIndex].second;
        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(builder->getPtrTy()); // context
        paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
        for (const auto& param : methodType->paramTypes) {
            paramTypes.push_back(getLLVMType(param));
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);

        // DEVIRTUALIZATION:
        // If the receiver is a specific class (not an interface), we can try to call the method directly.
        // This is safe if the method is not overridden in any subclass, or if we assume the type is exact.
        ClassType* concreteClass = nullptr;
        if (concreteTypes.count(objPtr)) {
            concreteClass = concreteTypes[objPtr];
        }

        if (concreteClass) {
        }

        std::shared_ptr<ClassType> definer = concreteClass ? 
            std::static_pointer_cast<ClassType>(concreteClass->shared_from_this()) : classType;
        
        std::string baseMangledName;
        bool isAbstract = false;
        while (definer) {
            if (definer->abstractMethods.count(methodName)) {
                isAbstract = true;
                break;
            }
            if (definer->methods.count(methodName)) {
                baseMangledName = definer->name + "_" + methodName;
                break;
            }
            definer = definer->baseClass;
        }

        llvm::Value* funcPtr = nullptr;
        if (concreteClass && !baseMangledName.empty() && !isAbstract) {
            // Try specialized version
            std::vector<std::shared_ptr<Type>> argTypes;
            argTypes.push_back(std::static_pointer_cast<ClassType>(concreteClass->shared_from_this())); // this
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<Type>(TypeKind::Any));
            }
            std::string specializedName = Monomorphizer::generateMangledName(baseMangledName, argTypes, {});
            
            llvm::Function* specializedFunc = module->getFunction(specializedName);
            if (specializedFunc) {
                funcPtr = specializedFunc;
                ft = specializedFunc->getFunctionType();
            } else {
                funcPtr = module->getFunction(baseMangledName);
                if (!funcPtr) {
                    funcPtr = module->getOrInsertFunction(baseMangledName, ft).getCallee();
                }
            }
            if (funcPtr) {
            }
        }

        if (!funcPtr) {
            // Fallback to virtual call
            // 2. Get VTable Pointer
            llvm::Value* vptrPtr = builder->CreateStructGEP(classStruct, objPtr, 0);
            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(vtableStruct), vptrPtr);
            
            // CFI Check
            emitCFICheck(vptr, className);

            // 4. Load Function Pointer (index + 1 because of parentVTable at index 0)
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIndex + 1);
            funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);
        }
        
        // 5. Call
        std::vector<llvm::Value*> args;
        if (currentAsyncContext) {
            args.push_back(currentAsyncContext);
        } else {
            args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
        }
        
        llvm::Value* thisArg = objPtr;
        if (classType->isStruct && !thisArg->getType()->isPointerTy()) {
            // If it's a struct value, we need to pass a pointer to it.
            // Store it to a temporary alloca.
            llvm::Value* temp = builder->CreateAlloca(thisArg->getType());
            builder->CreateStore(thisArg, temp);
            thisArg = temp;
        }
        args.push_back(thisArg); // this
        
        int argIdx = 0;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            llvm::Value* val = lastValue;
            
            if (argIdx < (int)methodType->paramTypes.size()) {
                auto expectedType = methodType->paramTypes[argIdx];
                if (arg->inferredType && arg->inferredType->kind == TypeKind::Any && expectedType->kind != TypeKind::Any) {
                    val = unboxValue(val, expectedType);
                } else {
                    val = castValue(val, getLLVMType(expectedType));
                }
            }
            
            args.push_back(val);
            argIdx++;
        }
        
        lastValue = createCall(ft, funcPtr, args);
        return true;
    }

    return false;
}

} // namespace ts
