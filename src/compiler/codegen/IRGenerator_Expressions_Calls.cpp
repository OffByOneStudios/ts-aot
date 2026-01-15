#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#include "../ast/AstNodes.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts {
using namespace ast;

static ast::Expression* unwrapCalleeExpression(ast::Expression* expr) {
    while (expr) {
        if (auto asExpr = dynamic_cast<ast::AsExpression*>(expr)) {
            expr = asExpr->expression.get();
            continue;
        }
        break;
    }
    return expr;
}

static bool paramHasDefaultInitializer(const std::shared_ptr<FunctionType>& funcType, size_t paramIndex) {
    if (!funcType || !funcType->node) return false;

    if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(funcType->node)) {
        if (paramIndex >= fn->parameters.size()) return false;
        const auto& p = fn->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto fnExpr = dynamic_cast<ast::FunctionExpression*>(funcType->node)) {
        if (paramIndex >= fnExpr->parameters.size()) return false;
        const auto& p = fnExpr->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto arrow = dynamic_cast<ast::ArrowFunction*>(funcType->node)) {
        if (paramIndex >= arrow->parameters.size()) return false;
        const auto& p = arrow->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto method = dynamic_cast<ast::MethodDefinition*>(funcType->node)) {
        if (paramIndex >= method->parameters.size()) return false;
        const auto& p = method->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    return false;
}

void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    if (node->isOptional) {
        visit(node->callee.get());
        llvm::Value* callee = lastValue;

        llvm::Value* boxedCallee = boxValue(callee, node->callee->inferredType);
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = getRuntimeFunction("ts_value_is_nullish", isNullishFt);
        llvm::Value* isNullish = createCall(isNullishFt, isNullishFn.getCallee(), { boxedCallee });
        llvm::Value* undef = getUndefinedValue();

        llvm::Function* func = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* callBB = llvm::BasicBlock::Create(*context, "opt_call", func);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "opt_merge", func);

        builder->CreateCondBr(isNullish, mergeBB, callBB);
        llvm::BasicBlock* checkBB = builder->GetInsertBlock();

        builder->SetInsertPoint(callBB);
        valueOverrides[node->callee.get()] = callee;
        generateCall(node);
        valueOverrides.erase(node->callee.get());
        
        llvm::Value* callResult = lastValue;
        llvm::BasicBlock* finalCallBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), 2);
        phi->addIncoming(undef, checkBB);
        phi->addIncoming(boxValue(callResult, node->inferredType), finalCallBB);
        boxedValues.insert(phi);
        lastValue = phi;
        return;
    }
    generateCall(node);
}

void IRGenerator::generateCall(ast::CallExpression* node) {
    emitLocation(node);
    SPDLOG_DEBUG("visitCallExpression: isComptime={}", node->isComptime);

    // Debug: log callee info
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        SPDLOG_INFO("generateCall: callee is Identifier '{}', type={}",
                   id->name,
                   node->callee->inferredType ? (int)node->callee->inferredType->kind : -1);
    }

    // Slow-path JS often has missing or any-typed callees; default to any to keep runtime behavior.
    if (!node->callee->inferredType) {
        node->callee->inferredType = std::make_shared<Type>(TypeKind::Any);
    }

    // Handle IIFE (Immediately Invoked Function Expression): (function() {...})()
    // Callee is a FunctionExpression or ArrowFunction, not an Identifier
    if (dynamic_cast<ast::FunctionExpression*>(node->callee.get()) || 
        dynamic_cast<ast::ArrowFunction*>(node->callee.get())) {
        
        // Visit the function expression to generate it and get the boxed function
        visit(node->callee.get());
        llvm::Value* funcValue = lastValue;
        
        // Call using ts_call_N runtime function
        std::vector<llvm::Value*> args;
        args.push_back(funcValue);  // The boxed function
        
        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(builder->getPtrTy());
        
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
            paramTypes.push_back(builder->getPtrTy());
        }
        
        std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
        llvm::FunctionCallee fn = getRuntimeFunction(fnName, ft);
        
        lastValue = createCall(ft, fn.getCallee(), args);
        boxedValues.insert(lastValue);
        // DO NOT unbox here - let the consumer (property access, etc.) unbox if needed
        // Unboxing here causes double-unboxing when result is stored in variable then accessed
        return;
    }

    if (node->isComptime) {
        SPDLOG_DEBUG("Handling comptime call");
        if (node->arguments.size() > 0) {
            if (auto arrow = dynamic_cast<ast::ArrowFunction*>(node->arguments[0].get())) {
                if (auto block = dynamic_cast<ast::BlockStatement*>(arrow->body.get())) {
                    if (block->statements.size() == 1) {
                        if (auto ret = dynamic_cast<ast::ReturnStatement*>(block->statements[0].get())) {
                            visit(ret->expression.get());
                            return;
                        }
                    }
                } else {
                    // Expression body
                    visit(arrow->body.get());
                    return;
                }
            }
        }
    }

    if (auto pa = dynamic_cast<ast::PropertyAccessExpression*>(unwrapCalleeExpression(node->callee.get()))) {
        if (pa->name == "call") {
            visit(pa->expression.get());
            llvm::Value* target = nullptr;

            // IMPORTANT: the left side of `.call` may be a raw compiled function pointer
            // (e.g. an IIFE lowered to `@func_expr_N`). In that case, we must wrap it in a
            // TsFunction via `ts_value_make_function` rather than treating it as an arbitrary
            // object pointer (which would crash when invoked).
            if (lastValue) {
                llvm::Value* stripped = lastValue->stripPointerCasts();
                if (stripped && llvm::isa<llvm::Function>(stripped)) {
                    llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);

                    // Get context for function closure: use member currentContext if set (e.g., in async state machine),
                    // otherwise fall back to current function's first argument
                    llvm::Value* ctxForClosure = this->currentContext;
                    if (!ctxForClosure) {
                        llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
                        if (currentFunc->arg_size() > 0 && currentFunc->getArg(0)->getType()->isPointerTy()) {
                            ctxForClosure = currentFunc->getArg(0);
                        } else {
                            ctxForClosure = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        }
                    }

                    target = createCall(
                        makeFnFt,
                        makeFnFn.getCallee(),
                        { builder->CreateBitCast(stripped, builder->getPtrTy()), ctxForClosure });
                    boxedValues.insert(target);
                }
            }

            if (!target) {
                target = boxValue(lastValue, pa->expression->inferredType);
            }

            // thisArg is first argument; default undefined/null if missing
            llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
            size_t argStart = 0;
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                // Always box the thisArg - even if type is 'any', the value may be
                // a raw pointer (e.g., function parameter or loaded from alloca).
                // boxValue will detect already-boxed values and return them as-is.
                thisArg = boxValue(lastValue, node->arguments[0]->inferredType);
                argStart = 1;
            }

            size_t restCount = node->arguments.size() > argStart ? node->arguments.size() - argStart : 0;
            llvm::AllocaInst* argvAlloca = nullptr;
            if (restCount == 0) {
                argvAlloca = builder->CreateAlloca(builder->getPtrTy(), llvm::ConstantInt::get(builder->getInt32Ty(), 1), "argv_empty");
                builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), argvAlloca);
            } else {
                argvAlloca = builder->CreateAlloca(builder->getPtrTy(), llvm::ConstantInt::get(builder->getInt32Ty(), (uint32_t)restCount), "argv_call");
                for (size_t i = 0; i < restCount; ++i) {
                    visit(node->arguments[argStart + i].get());
                    llvm::Value* boxedArg = boxValue(lastValue, node->arguments[argStart + i]->inferredType);
                    llvm::Value* slot = builder->CreateGEP(builder->getPtrTy(), argvAlloca, llvm::ConstantInt::get(builder->getInt32Ty(), (uint32_t)i));
                    builder->CreateStore(boxedArg, slot);
                }
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), llvm::PointerType::getUnqual(builder->getPtrTy()) },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_function_call_with_this", ft);
            llvm::Value* argcVal = llvm::ConstantInt::get(builder->getInt32Ty(), (uint32_t)restCount);
            llvm::Value* argvPtr = builder->CreateBitCast(argvAlloca, llvm::PointerType::getUnqual(builder->getPtrTy()));
            llvm::Value* res = createCall(ft, fn.getCallee(), { target, thisArg, argcVal, argvPtr });
            boxedValues.insert(res);
            lastValue = res;
            // DO NOT unbox here - let the consumer unbox if needed
            return;
        } else if (pa->name == "apply") {
            visit(pa->expression.get());
            llvm::Value* target = boxValue(lastValue, pa->expression->inferredType);

            llvm::Value* thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
            llvm::Value* argsArray = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                thisArg = boxValue(lastValue, node->arguments[0]->inferredType);
            }
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                argsArray = boxValue(lastValue, node->arguments[1]->inferredType);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
                false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_function_apply", ft);
            llvm::Value* res = createCall(ft, fn.getCallee(), { target, thisArg, argsArray });
            boxedValues.insert(res);
            lastValue = res;
            // DO NOT unbox here - let the consumer unbox if needed
            return;
        }
    }

    if (node->callee->inferredType && node->callee->inferredType->kind == TypeKind::Any) {
        // If this callee ultimately resolves to a named function specialization, prefer
        // a direct call (stable ABI, avoids ts_call_N wrappers).
        // EXCEPTION: Functions with closures must go through ts_call_N to get their context.
        auto unwrapped = unwrapCalleeExpression(node->callee.get());
        if (auto id = dynamic_cast<ast::Identifier*>(unwrapped)) {
            // Skip direct call optimization for functions that capture variables
            // They need their closure context which is only available via ts_call_N
            if (!functionsWithClosures.count(id->name)) {
            bool hasKnownSpecialization = false;
            if (module->getFunction(id->name)) {
                hasKnownSpecialization = true;
            } else {
                std::string prefix = id->name + "_";
                for (auto& fn : module->functions()) {
                    if (fn.getName().starts_with(prefix)) {
                        hasKnownSpecialization = true;
                        break;
                    }
                }
            }

            if (hasKnownSpecialization) {
                std::vector<llvm::Value*> args;
                std::vector<std::shared_ptr<Type>> argTypes;

                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* argVal = lastValue;
                    std::shared_ptr<Type> argType = arg->inferredType ? arg->inferredType : std::make_shared<Type>(TypeKind::Any);
                    args.push_back(argVal);
                    argTypes.push_back(argType);
                }

                if (id->inferredType && id->inferredType->kind == TypeKind::Function) {
                    auto funcType = std::static_pointer_cast<FunctionType>(id->inferredType);
                    for (size_t i = 0; i < argTypes.size() && i < funcType->paramTypes.size(); ++i) {
                        if (!argTypes[i]) continue;
                        if (argTypes[i]->kind != TypeKind::Undefined && argTypes[i]->kind != TypeKind::Void) continue;
                        if (!paramHasDefaultInitializer(funcType, i)) continue;
                        if (!funcType->paramTypes[i]) continue;
                        argTypes[i] = funcType->paramTypes[i];
                    }
                }

                std::string mangledName = Monomorphizer::generateMangledName(id->name, argTypes, node->resolvedTypeArguments);
                llvm::Function* func = module->getFunction(mangledName);
                if (func) {
                    std::vector<llvm::Value*> callArgs;
                    if (currentAsyncContext) {
                        callArgs.push_back(currentAsyncContext);
                    } else {
                        callArgs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
                    }

                    for (size_t i = 0; i < args.size(); ++i) {
                        llvm::Value* argVal = args[i];
                        llvm::Type* paramTy = nullptr;
                        if (i + 1 < func->getFunctionType()->getNumParams()) {
                            paramTy = func->getFunctionType()->getParamType(static_cast<unsigned>(i + 1));
                        }

                        if (paramTy && argVal && argVal->getType() != paramTy) {
                            std::shared_ptr<Type> tsArgType = (i < argTypes.size() && argTypes[i]) ? argTypes[i] : std::make_shared<Type>(TypeKind::Any);

                            if (paramTy->isPointerTy() && !argVal->getType()->isPointerTy()) {
                                argVal = boxValue(argVal, tsArgType);
                            } else if (!paramTy->isPointerTy() && argVal->getType()->isPointerTy()) {
                                argVal = unboxValue(argVal, tsArgType);
                            }

                            if (argVal->getType() != paramTy) {
                                argVal = castValue(argVal, paramTy);
                            }
                        }

                        callArgs.push_back(argVal);
                    }

                    // JavaScript allows calling functions with fewer arguments than parameters.
                    // Our compiled function prototypes have a fixed arity, so we must pad missing
                    // parameters with an `undefined`-like value. For pointer-typed params this is
                    // represented as null; for primitives we use a null/zero initializer.
                    while (callArgs.size() < func->getFunctionType()->getNumParams()) {
                        llvm::Type* missingTy = func->getFunctionType()->getParamType(static_cast<unsigned>(callArgs.size()));
                        callArgs.push_back(llvm::Constant::getNullValue(missingTy));
                    }

                    lastValue = createCall(func->getFunctionType(), func, callArgs);
                    return;
                }
            }
            } // end else (no closures - direct call allowed)
        }

        // For PropertyAccessExpression callees (method calls like obj.method()),
        // check if it's a known array/string/etc. builtin first before falling back to runtime call
        if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(unwrapped)) {
            // Try to handle as a builtin method first (e.g., arr.reverse(), arr.at())
            bool shouldTryBuiltin = false;

            // Check if expression has a known type that has builtins
            if (prop->expression->inferredType &&
                (prop->expression->inferredType->kind == TypeKind::Array ||
                 prop->expression->inferredType->kind == TypeKind::String ||
                 prop->expression->inferredType->kind == TypeKind::Object ||
                 prop->expression->inferredType->kind == TypeKind::Class)) {
                shouldTryBuiltin = true;
            }

            // Also check if expression is a known global identifier with static methods
            // (e.g., Proxy.revocable, Number.isFinite, Math.abs, Reflect.get, etc.)
            if (auto exprId = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                static const std::unordered_set<std::string> globalObjects = {
                    "Proxy", "Reflect", "Number", "Math", "Object", "Array", "String",
                    "JSON", "Symbol", "Promise", "Date", "RegExp", "console", "Buffer",
                    "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array",
                    "Uint16Array", "Int32Array", "Uint32Array", "Float32Array", "Float64Array"
                };
                if (globalObjects.count(exprId->name)) {
                    shouldTryBuiltin = true;
                }
            }

            // Also check for nested property access on known modules (e.g., path.posix.join, path.win32.join)
            if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
                if (auto innerId = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
                    // path.posix.xxx or path.win32.xxx
                    if (innerId->name == "path" && (innerProp->name == "posix" || innerProp->name == "win32")) {
                        shouldTryBuiltin = true;
                    }
                }
            }

            if (shouldTryBuiltin) {
                if (tryGenerateBuiltinCall(node, prop)) {
                    return;
                }
            }
            
            // First evaluate the object for 'this' context
            visit(prop->expression.get());
            llvm::Value* thisObj = lastValue;
            llvm::Value* thisBoxed = boxValue(thisObj, prop->expression->inferredType);
            
            // Then get the method
            visit(node->callee.get());
            llvm::Value* boxedFunc = lastValue;
            
            // Build argv array for ts_function_call_with_this
            int argc = static_cast<int>(node->arguments.size());
            
            // Allocate argv array on stack
            llvm::Value* argv = nullptr;
            if (argc > 0) {
                argv = builder->CreateAlloca(builder->getPtrTy(), builder->getInt32(argc), "argv");
                
                int idx = 0;
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* val = boxValue(lastValue, arg->inferredType);
                    
                    // Store into argv[idx]
                    llvm::Value* gep = builder->CreateGEP(builder->getPtrTy(), argv, builder->getInt32(idx));
                    builder->CreateStore(val, gep);
                    idx++;
                }
            } else {
                // Create a null pointer for empty argv
                argv = llvm::ConstantPointerNull::get(llvm::PointerType::get(builder->getContext(), 0));
            }
            
            // Call ts_function_call_with_this(boxedFunc, thisArg, argc, argv)
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
                false
            );
            llvm::FunctionCallee fn = getRuntimeFunction("ts_function_call_with_this", ft);
            
            lastValue = createCall(ft, fn.getCallee(), { boxedFunc, thisBoxed, builder->getInt32(argc), argv });
            boxedValues.insert(lastValue);
            lastValue = unboxValue(lastValue, node->inferredType);
            return;
        }

        visit(node->callee.get());
        llvm::Value* callee = boxValue(lastValue, node->callee->inferredType);
        
        std::vector<llvm::Value*> args;
        args.push_back(callee);
        
        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(builder->getPtrTy()); // func
        
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
            paramTypes.push_back(builder->getPtrTy());
        }
        
        std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
        llvm::FunctionCallee fn = getRuntimeFunction(fnName, ft);
        
        lastValue = createCall(ft, fn.getCallee(), args);
        boxedValues.insert(lastValue);
        lastValue = unboxValue(lastValue, node->inferredType);
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (id->name == "require") {
            if (node->arguments.empty()) {
                lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                return;
            }
            visit(node->arguments[0].get());
            llvm::Value* specifier = boxValue(lastValue, node->arguments[0]->inferredType);
            
            // Get current file path from IRGenerator
            llvm::Value* referrerPath = builder->CreateGlobalStringPtr(currentSourceFile);
            
            llvm::FunctionType* requireFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee requireFn = getRuntimeFunction("ts_require", requireFt);
            
            lastValue = createCall(requireFt, requireFn.getCallee(), { specifier, referrerPath });
            return;
        }

        if (id->name == "ts_module_register") {
            if (node->arguments.size() < 2) return;
            
            visit(node->arguments[0].get());
            llvm::Value* path = boxValue(lastValue, node->arguments[0]->inferredType);
            
            visit(node->arguments[1].get());
            llvm::Value* exports = boxValue(lastValue, node->arguments[1]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_module_register", ft);
            
            createCall(ft, fn.getCallee(), { path, exports });
            return;
        }

        if (id->name == "fetch") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* url = lastValue;
            
            llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                options = boxValue(lastValue, node->arguments[1]->inferredType);
            }
            
            llvm::Value* vtable = module->getGlobalVariable("Response_VTable_Global");
            if (!vtable) {
                vtable = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }

            llvm::FunctionType* fetchFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fetchFn = getRuntimeFunction("ts_fetch", fetchFt);
            
            lastValue = createCall(fetchFt, fetchFn.getCallee(), { vtable, url, options });
            return;
        }

        if (id->name == "setImmediate") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_set_immediate", ft);
            lastValue = createCall(ft, fn.getCallee(), { callback });
            return;
        }

        if (id->name == "nextTick") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_next_tick", ft);
            createCall(ft, fn.getCallee(), { callback });
            lastValue = nullptr;
            return;
        }

        if (id->name == "BigInt") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_bigint_from_value", ft);
            lastValue = createCall(ft, fn.getCallee(), { arg });
            return;
        }

        if (id->name == "Symbol") {
            llvm::Value* desc = llvm::ConstantPointerNull::get(builder->getPtrTy());
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                desc = lastValue;
            }
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_symbol_create", ft);
            lastValue = createCall(ft, fn.getCallee(), { desc });
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

    SPDLOG_DEBUG("Calling tryGenerateMemberCall");
    if (tryGenerateMemberCall(node)) {
        return;
    }

    if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get())) {
        SPDLOG_INFO("IRGenerator: Fallback property call for {}", prop->name);
        
        // First evaluate the object for 'this' context
        visit(prop->expression.get());
        llvm::Value* thisObj = lastValue;
        llvm::Value* thisBoxed = boxValue(thisObj, prop->expression->inferredType);
        
        // Then get the method
        visit(prop);
        llvm::Value* boxedFunc = lastValue;
        
        // Build argv array for ts_function_call_with_this
        int argc = static_cast<int>(node->arguments.size());
        
        // Allocate argv array on stack
        llvm::Value* argv = nullptr;
        if (argc > 0) {
            argv = builder->CreateAlloca(builder->getPtrTy(), builder->getInt32(argc), "argv");
            
            int idx = 0;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                std::shared_ptr<Type> argType = arg->inferredType;
                if (arg->getKind() == "ArrowFunction" || arg->getKind() == "FunctionExpression") {
                     if (!argType || argType->kind == TypeKind::Any) {
                         argType = std::make_shared<Type>(TypeKind::Function);
                     }
                }
                llvm::Value* val = boxValue(lastValue, argType);
                
                // Store into argv[idx]
                llvm::Value* gep = builder->CreateGEP(builder->getPtrTy(), argv, builder->getInt32(idx));
                builder->CreateStore(val, gep);
                idx++;
            }
        } else {
            // Create a null pointer for empty argv
            argv = llvm::ConstantPointerNull::get(llvm::PointerType::get(builder->getContext(), 0));
        }
        
        // Call ts_function_call_with_this(boxedFunc, thisArg, argc, argv)
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_function_call_with_this", ft);
        
        lastValue = createCall(ft, fn.getCallee(), { boxedFunc, thisBoxed, builder->getInt32(argc), argv });
        lastValue = unboxValue(lastValue, node->inferredType);
        return;
    }

    // User function call - local function variables may be boxed closures
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        // Check if this is a cell variable (captured by closure)
        if (cellVariables.count(id->name) && namedValues.count(id->name)) {
            // Load the cell pointer from the alloca
            llvm::Value* cellAlloca = namedValues[id->name];
            llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);
            
            // Get the actual function value from the cell
            llvm::FunctionType* cellGetFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee cellGetFn = getRuntimeFunction("ts_cell_get", cellGetFt);
            llvm::Value* boxedFunc = createCall(cellGetFt, cellGetFn.getCallee(), { cell });
            
            // Use ts_call_N for the function call
            std::vector<llvm::Value*> args;
            args.push_back(boxedFunc);
            
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(builder->getPtrTy()); // boxedFunc
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* v = boxValue(lastValue, arg->inferredType);
                args.push_back(v);
                paramTypes.push_back(builder->getPtrTy());
            }
            
            std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            
            lastValue = createCall(ft, fn.getCallee(), args);
            
            // Determine return type for unboxing
            std::shared_ptr<Type> returnType = node->inferredType;
            if (!returnType || returnType->kind == TypeKind::Any) {
                // Try to get return type from callee's function type
                if (id->inferredType && id->inferredType->kind == TypeKind::Function) {
                    auto funcType = std::static_pointer_cast<FunctionType>(id->inferredType);
                    returnType = funcType->returnType;
                    SPDLOG_INFO("Cell call: using callee's return type: {}", returnType ? (int)returnType->kind : -1);
                }
            }
            SPDLOG_INFO("Cell call result unbox: inferredType={}", returnType ? (int)returnType->kind : -1);
            lastValue = unboxValue(lastValue, returnType);
            return;
        }
        
        // Check local variables first
        if (namedValues.count(id->name)) {
            llvm::Value* val = namedValues[id->name];
            llvm::Type* varType = nullptr;
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
                varType = gep->getResultElementType();
            }

            if (varType && varType->isPointerTy()) {
                llvm::Value* boxedFunc = builder->CreateLoad(varType, val, id->name.c_str());
                
                // Use ts_call_N for function variables since they may be boxed closures
                std::vector<llvm::Value*> args;
                args.push_back(boxedFunc);
                
                std::vector<llvm::Type*> paramTypes;
                paramTypes.push_back(builder->getPtrTy()); // boxedFunc
                
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* v = boxValue(lastValue, arg->inferredType);
                    args.push_back(v);
                    paramTypes.push_back(builder->getPtrTy());
                }
                
                std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
                
                lastValue = createCall(ft, fn.getCallee(), args);
                lastValue = unboxValue(lastValue, node->inferredType);
                return;
            }
        }
        
        // Check global variables that hold function references
        llvm::GlobalVariable* gv = module->getGlobalVariable(id->name);
        if (gv && gv->getValueType()->isPointerTy()) {
            // This is a global variable holding a function pointer - use ts_call_N
            llvm::Value* boxedFunc = builder->CreateLoad(gv->getValueType(), gv, id->name.c_str());
            
            std::vector<llvm::Value*> args;
            args.push_back(boxedFunc);
            
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(builder->getPtrTy()); // boxedFunc
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* v = boxValue(lastValue, arg->inferredType);
                args.push_back(v);
                paramTypes.push_back(builder->getPtrTy());
            }
            
            std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            
            lastValue = createCall(ft, fn.getCallee(), args);
            lastValue = unboxValue(lastValue, node->inferredType);
            return;
        }
        
        // Check if this identifier has Function type and is a variable that holds a function
        // This handles cases where the callee is loaded from somewhere and has Function type
        // BUT: Skip this if there's a directly callable function with this name - that takes priority
        if (id->inferredType && id->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(id->inferredType);

            // Check if we have spread args being passed to fixed params
            // In this case, we need to expand the types based on expected params
            bool hasSpreadToFixedParams = false;
            for (const auto& arg : node->arguments) {
                if (auto spread = dynamic_cast<ast::SpreadElement*>(arg.get())) {
                    // It's a spread - check if target function has fixed params (not rest)
                    if (!funcType->hasRest && funcType->paramTypes.size() > 0) {
                        hasSpreadToFixedParams = true;
                        break;
                    }
                }
            }

            // If spreading to fixed params, let the normal generateCall path handle it
            // The spread expansion logic there will expand array elements into individual args
            if (hasSpreadToFixedParams) {
                SPDLOG_DEBUG("Spread to fixed params detected - letting generateCall handle expansion");
                // Fall through to normal generateCall path below
            } else {
                // Compute actual argument types from the call site, not from the declared parameter types
                // This is critical for functions with 'any' parameters that get monomorphized
                std::vector<std::shared_ptr<Type>> actualArgTypes;
                for (const auto& arg : node->arguments) {
                    std::shared_ptr<Type> argType = arg->inferredType;
                    // For SpreadElement, use the inner expression's type (the array type)
                    if (auto spread = dynamic_cast<ast::SpreadElement*>(arg.get())) {
                        if (spread->expression && spread->expression->inferredType) {
                            argType = spread->expression->inferredType;
                        }
                    }
                    actualArgTypes.push_back(argType ? argType : std::make_shared<Type>(TypeKind::Any));
                }
                std::string mangledName = Monomorphizer::generateMangledName(id->name, actualArgTypes, node->resolvedTypeArguments);

                // Check if there's a directly callable function - if so, let the normal path handle it
                llvm::Function* directFunc = module->getFunction(mangledName);
                if (!directFunc) {
                    directFunc = module->getFunction(id->name);
                }

                // Only use ts_call_N if there's NO direct function (i.e., this is truly a variable)
                if (!directFunc) {
                    SPDLOG_DEBUG("Using ts_call_N fallback path for function variable");
                    // Visit the identifier to get the function value
                    visit(id);
                    if (lastValue && lastValue->getType()->isPointerTy()) {
                        llvm::Value* boxedFunc = lastValue;

                        std::vector<llvm::Value*> args;
                        args.push_back(boxedFunc);

                        std::vector<llvm::Type*> paramTypes;
                        paramTypes.push_back(builder->getPtrTy()); // boxedFunc

                        for (auto& arg : node->arguments) {
                            visit(arg.get());
                            llvm::Value* v = boxValue(lastValue, arg->inferredType);
                            args.push_back(v);
                            paramTypes.push_back(builder->getPtrTy());
                        }

                        std::string fnName = "ts_call_" + std::to_string(node->arguments.size());
                        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), paramTypes, false);
                        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);

                        lastValue = createCall(ft, fn.getCallee(), args);
                        lastValue = unboxValue(lastValue, node->inferredType);
                        return;
                    }
                }
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
            
            std::string funcName = (id->name == "setTimeout") ? "ts_set_timeout" : "ts_set_interval";
            llvm::FunctionType* timerFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee timerFn = module->getOrInsertFunction(funcName, timerFt);
            
            llvm::Value* boxedRes = createCall(timerFt, timerFn.getCallee(), { boxedCallback, delay });
            lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
            return;
        }

        if (id->name == "clearTimeout" || id->name == "clearInterval") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* timerId = lastValue;
            llvm::Value* boxedId = boxValue(timerId, node->arguments[0]->inferredType);
            
            llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee clearFn = getRuntimeFunction("ts_clear_timer", clearFt);
            
            createCall(clearFt, clearFn.getCallee(), { boxedId });
            lastValue = nullptr;
            return;
        }

        if (id->name == "setImmediate") {
            if (node->arguments.empty()) return;
            
            visit(node->arguments[0].get());
            llvm::Value* callback = lastValue;
            llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
            
            llvm::FunctionType* timerFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee timerFn = getRuntimeFunction("ts_set_immediate", timerFt);
            
            llvm::Value* boxedRes = createCall(timerFt, timerFn.getCallee(), { boxedCallback });
            lastValue = unboxValue(boxedRes, std::make_shared<Type>(TypeKind::Int));
            return;
        }

        if (id->name == "clearImmediate") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* timerId = lastValue;
            llvm::Value* boxedId = boxValue(timerId, node->arguments[0]->inferredType);
            
            llvm::FunctionType* clearFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee clearFn = getRuntimeFunction("ts_clear_timer", clearFt);
            
            createCall(clearFt, clearFn.getCallee(), { boxedId });
            lastValue = nullptr;
            return;
        }

        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;

             llvm::FunctionType* parseFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             llvm::FunctionCallee parseFn = getRuntimeFunction("ts_parseInt", parseFt);
             
             lastValue = createCall(parseFt, parseFn.getCallee(), { arg });
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
            } else if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Any) {
                funcName = "ts_console_log_value";
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context), { paramType }, false);
            llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

            createCall(ft, logFn.getCallee(), { arg });
            lastValue = nullptr;
            return;
        }

        std::string funcName = id->name;
        
        // Get target function's expected parameter types for spread expansion
        std::vector<std::shared_ptr<Type>> expectedParamTypes;
        bool calleeHasRest = false;
        if (id->inferredType && id->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(id->inferredType);
            expectedParamTypes = funcType->paramTypes;
            calleeHasRest = funcType->hasRest;
        }
        
        std::vector<llvm::Value*> args;
        std::vector<std::shared_ptr<Type>> argTypes;
        size_t paramIndex = 0;  // Track which parameter we're filling
        
        for (auto& arg : node->arguments) {
            // Handle spread elements - expand array into individual arguments
            if (auto spread = dynamic_cast<ast::SpreadElement*>(arg.get())) {
                visit(spread->expression.get());
                llvm::Value* arrVal = lastValue;
                
                // Get element type from the spread expression's inferred type
                std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
                if (spread->expression->inferredType && spread->expression->inferredType->kind == TypeKind::Array) {
                    auto arrType = std::static_pointer_cast<ArrayType>(spread->expression->inferredType);
                    elementType = arrType->elementType;
                }
                
                // Calculate how many elements to extract (remaining fixed params before rest)
                size_t remainingFixedParams = 0;
                if (calleeHasRest && expectedParamTypes.size() > 0) {
                    // Last param is the rest array - don't expand into it
                    size_t fixedParamCount = expectedParamTypes.size() - 1;
                    remainingFixedParams = fixedParamCount > paramIndex ? fixedParamCount - paramIndex : 0;
                } else {
                    remainingFixedParams = expectedParamTypes.size() > paramIndex 
                        ? expectedParamTypes.size() - paramIndex 
                        : 0;
                }
                
                // If spreading into rest param position, pass array directly
                if (calleeHasRest && paramIndex >= expectedParamTypes.size() - 1) {
                    // We're at the rest param position - pass array directly
                    args.push_back(arrVal);
                    argTypes.push_back(spread->expression->inferredType);
                    paramIndex++;
                } else if (remainingFixedParams > 0) {
                    // Extract each element from the array for fixed params
                    for (size_t j = 0; j < remainingFixedParams; j++) {
                        llvm::Value* idx = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), j);
                        
                        // Get element using ts_array_get or specialized access
                        llvm::Value* elemVal;
                        if (elementType->kind == TypeKind::Int || elementType->kind == TypeKind::Double) {
                            // Use specialized array access
                            llvm::FunctionType* getElFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                            llvm::FunctionCallee getElFn = getRuntimeFunction("ts_array_get_elements_ptr", getElFt);
                            llvm::Value* elemPtr = createCall(getElFt, getElFn.getCallee(), { arrVal });
                            
                            llvm::Type* elemLLVMType = elementType->kind == TypeKind::Int 
                                ? llvm::Type::getInt64Ty(*context) 
                                : llvm::Type::getDoubleTy(*context);
                            llvm::Value* gepPtr = builder->CreateGEP(elemLLVMType, elemPtr, { idx }, "spread_elem_ptr");
                            elemVal = builder->CreateLoad(elemLLVMType, gepPtr, "spread_elem");
                        } else {
                            // Use inline array get for other types
                            elemVal = emitInlineArrayGet(arrVal, idx);
                        }
                        
                        args.push_back(elemVal);
                        argTypes.push_back(elementType);
                        paramIndex++;
                    }
                } else {
                    // No known param count - pass array as-is (for rest params)
                    args.push_back(arrVal);
                    argTypes.push_back(spread->expression->inferredType);
                    paramIndex++;
                }
                continue;
            }
            
            visit(arg.get());
            if (lastValue) {
                llvm::Value* argVal = lastValue;
                std::shared_ptr<Type> argType = arg->inferredType;
                if (!argType) {
                    argType = std::make_shared<Type>(TypeKind::Any);
                }
                
                // Box function and object arguments so they can be used inside the callee
                // - Function args: called via ts_call_N
                // - Object args: used with ts_value_get_property for destructuring
                if (argType && (argType->kind == TypeKind::Function ||
                               argType->kind == TypeKind::Object)) {
                    argVal = boxValue(argVal, argType);
                }
                
                args.push_back(argVal);
                argTypes.push_back(argType);
            }
        }

        // Keep specialization selection consistent with analyzer: if an argument is explicitly
        // `undefined` and the callee has a default initializer, treat the argument as the
        // declared parameter type so we select the same specialization that the monomorphizer emitted.
        if (id->inferredType && id->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(id->inferredType);
            for (size_t i = 0; i < argTypes.size() && i < funcType->paramTypes.size(); ++i) {
                if (!argTypes[i]) continue;
                if (argTypes[i]->kind != TypeKind::Undefined && argTypes[i]->kind != TypeKind::Void) continue;
                if (!paramHasDefaultInitializer(funcType, i)) continue;
                if (!funcType->paramTypes[i]) continue;
                argTypes[i] = funcType->paramTypes[i];
            }
        }

        std::string mangledName = Monomorphizer::generateMangledName(funcName, argTypes, node->resolvedTypeArguments);
        
        // Debug: log the mangled name
        std::string argTypesStr;
        for (const auto& t : argTypes) {
            if (!argTypesStr.empty()) argTypesStr += ", ";
            argTypesStr += t ? t->toString() : "null";
        }
        SPDLOG_INFO("Function call: {} -> mangledName={}, argTypes=[{}]", funcName, mangledName, argTypesStr);

        // Update inferred type from specialization if available
        for (const auto& spec : specializations) {
            if (spec.specializedName == mangledName) {
                node->inferredType = spec.returnType;
                break;
            }
        }

        llvm::Function* func = module->getFunction(mangledName);
         if (func) {
             // Add context as first argument
             std::vector<llvm::Value*> callArgs;
             if (currentContext) {
                 // Use currentContext (execution context), NOT currentAsyncContext (state machine context)
                 // currentAsyncContext is a state machine parameter and violates LLVM SSA if passed to another function
                 callArgs.push_back(currentContext);
             } else {
                 callArgs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
             }

            // Ensure call-site values match the callee's LLVM signature.
            // This is especially important for defaulted primitive parameters, which
            // are represented as boxed TsValue* so `undefined` can be detected.
            for (size_t i = 0; i < args.size(); ++i) {
                llvm::Value* argVal = args[i];
                llvm::Type* paramTy = nullptr;
                if (i + 1 < func->getFunctionType()->getNumParams()) {
                    paramTy = func->getFunctionType()->getParamType(static_cast<unsigned>(i + 1));
                }

                if (paramTy && argVal && argVal->getType() != paramTy) {
                    std::shared_ptr<Type> tsArgType = (i < argTypes.size() && argTypes[i]) ? argTypes[i] : std::make_shared<Type>(TypeKind::Any);

                    if (paramTy->isPointerTy() && !argVal->getType()->isPointerTy()) {
                        argVal = boxValue(argVal, tsArgType);
                    } else if (!paramTy->isPointerTy() && argVal->getType()->isPointerTy()) {
                        argVal = unboxValue(argVal, tsArgType);
                    }

                    if (argVal->getType() != paramTy) {
                        argVal = castValue(argVal, paramTy);
                    }
                }

                callArgs.push_back(argVal);
            }
             
             lastValue = createCall(func->getFunctionType(), func, callArgs);

             // CRITICAL: Async functions return TsValue* (boxed promise), so mark the result as boxed
             // to prevent double-boxing when the result is used
             if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
                 auto classType = std::static_pointer_cast<ClassType>(node->inferredType);
                 if (classType->name.find("Promise") == 0) {
                     boxedValues.insert(lastValue);
                 }
             }
         } else {
             SPDLOG_ERROR("Function not found: {}", mangledName);
             // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

} // namespace ts
