#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;
void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    if (node->isOptional) {
        visit(node->callee.get());
        llvm::Value* callee = lastValue;

        llvm::Value* boxedCallee = boxValue(callee, node->callee->inferredType);
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = module->getOrInsertFunction("ts_value_is_nullish", isNullishFt);
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

    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
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
            llvm::FunctionCallee fetchFn = module->getOrInsertFunction("ts_fetch", fetchFt);
            
            lastValue = createCall(fetchFt, fetchFn.getCallee(), { vtable, url, options });
            return;
        }

        if (id->name == "BigInt") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = boxValue(lastValue, node->arguments[0]->inferredType);
            
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_bigint_from_value", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_symbol_create", ft);
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
        visit(prop);
        llvm::Value* boxedFunc = lastValue;
        
        if (node->arguments.empty()) {
            llvm::FunctionType* callFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee callFn = module->getOrInsertFunction("ts_call_0", callFt);
            lastValue = createCall(callFt, callFn.getCallee(), { boxedFunc });
            
            lastValue = unboxValue(lastValue, node->inferredType);
            return;
        } else {
            std::vector<llvm::Value*> args;
            args.push_back(boxedFunc);
            
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(builder->getPtrTy()); // func
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = boxValue(lastValue, arg->inferredType);
                args.push_back(val);
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
                
                // CFI Check
                emitCFICheck(funcPtr, "TsFunction");

                std::vector<llvm::Value*> args;
                std::vector<llvm::Type*> argTypes;
                
                // Add context first
                if (currentAsyncContext) {
                    args.push_back(currentAsyncContext);
                } else {
                    args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
                }
                argTypes.push_back(builder->getPtrTy());

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
                lastValue = createCall(ft, funcPtr, args);
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
            llvm::FunctionCallee clearFn = module->getOrInsertFunction("ts_clear_timer", clearFt);
            
            createCall(clearFt, clearFn.getCallee(), { boxedId });
            lastValue = nullptr;
            return;
        }

        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;

             llvm::FunctionType* parseFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             llvm::FunctionCallee parseFn = module->getOrInsertFunction("ts_parseInt", parseFt);
             
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
            if (currentAsyncContext) {
                callArgs.push_back(currentAsyncContext);
            } else {
                callArgs.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
            }
            for (auto arg : args) callArgs.push_back(arg);
            
            lastValue = createCall(func->getFunctionType(), func, callArgs);
        } else {
            SPDLOG_ERROR("Function not found: {}", mangledName);
            // TODO: Error handling
            lastValue = nullptr;
        }
    }
}

} // namespace ts


