#include "IRGenerator.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

void IRGenerator::visitReturnStatement(ast::ReturnStatement* node) {
    emitLocation(node);
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::Type* retType = func->getReturnType();

    llvm::Value* val = nullptr;
    if (node->expression) {
        visit(node->expression.get());
        val = lastValue;
        
        // For lambda returns that return ptr, always box if value is not ptr
        if (retType->isPointerTy() && val && !val->getType()->isPointerTy()) {
            val = boxValue(val, node->expression->inferredType);
        } else if (currentReturnType && currentReturnType->kind == TypeKind::Any) {
            val = boxValue(val, node->expression->inferredType);
        } else {
            val = castValue(val, retType);
        }
    } else {
        if (retType->isVoidTy()) {
            // val remains nullptr
        } else if (retType->isPointerTy()) {
            val = llvm::ConstantPointerNull::get(builder->getPtrTy());
        } else {
            val = llvm::ConstantInt::get(retType, 0);
        }
    }

    if (currentAsyncContext) {
        // Resolve promise and return void
        llvm::Value* boxedVal = val;
        if (!boxedVal) {
            llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
            boxedVal = createCall(undefFt, undefFn.getCallee(), {});
        } else {
            boxedVal = boxValue(boxedVal, node->expression ? node->expression->inferredType : nullptr);
        }

        if (currentIsAsync && !currentIsGenerator) {
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 5);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
            createCall(resolveFt, resolveFn.getCallee(), { promise, boxedVal });
        }

        if (currentIsGenerator) {
            if (currentIsAsync) {
                llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getInt1Ty() }, false);
                llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_async_generator_resolve", resolveFt);
                createCall(resolveFt, resolveFn.getCallee(), { currentAsyncContext, boxedVal, builder->getInt1(true) });
            } else {
                // Set ctx->yieldedValue = boxedVal
                // Set ctx->done = true
                llvm::Value* yieldedValuePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 4);
                // TsValue is a struct { type, data }
                // boxedVal is a TsValue*
                builder->CreateMemCpy(yieldedValuePtr, llvm::MaybeAlign(8), boxedVal, llvm::MaybeAlign(8), 16);

                llvm::Value* donePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 2);
                builder->CreateStore(builder->getInt1(true), donePtr);
            }
        }

        builder->CreateRetVoid();
    } else {
        if (!finallyStack.empty()) {
            if (val) {
                builder->CreateStore(val, currentReturnValueAlloca);
            } else if (!retType->isVoidTy()) {
                // If we have a return without value in a function that returns a pointer, return undefined
                llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
                llvm::Value* undef = createCall(undefFt, undefFn.getCallee(), {});
                builder->CreateStore(undef, currentReturnValueAlloca);
            }
            builder->CreateStore(builder->getInt1(true), currentShouldReturnAlloca);
            
            llvm::Function* popFn = getRuntimeFunction("ts_pop_exception_handler");
            createCall(popFn->getFunctionType(), popFn, {});
            
            builder->CreateBr(finallyStack.back().finallyBB);
        } else {
            if (val) {
                builder->CreateRet(val);
            } else if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                // Return undefined/null for non-void functions with no return value
                llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
                llvm::Value* undef = createCall(undefFt, undefFn.getCallee(), {});
                builder->CreateRet(undef);
            }
        }
    }
    
    // Start a new block for dead code after return
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitExpressionStatement(ast::ExpressionStatement* node) {
    emitLocation(node);
    visit(node->expression.get());
}

void IRGenerator::visitIfStatement(ast::IfStatement* node) {
    emitLocation(node);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;
    if (!condValue) return;

    // Convert condition to bool
    condValue = emitToBoolean(condValue, node->condition->inferredType);

    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*context, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont");

    bool hasElse = (node->elseStatement != nullptr);

    builder->CreateCondBr(condValue, thenBB, hasElse ? elseBB : mergeBB);

    // Emit then block
    builder->SetInsertPoint(thenBB);
    visit(node->thenStatement.get());
    
    // If the then block doesn't already have a terminator (like return), branch to merge
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBB);
    }

    // Emit else block
    if (hasElse) {
        func->insert(func->end(), elseBB);
        builder->SetInsertPoint(elseBB);
        visit(node->elseStatement.get());
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBB);
        }
    }

    // Emit merge block
    func->insert(func->end(), mergeBB);
    builder->SetInsertPoint(mergeBB);
}

void IRGenerator::visitSwitchStatement(ast::SwitchStatement* node) {
    visit(node->expression.get());
    llvm::Value* switchVal = lastValue;
    if (!switchVal) return;

    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Create blocks
    std::vector<llvm::BasicBlock*> clauseBlocks;
    for (size_t i = 0; i < node->clauses.size(); ++i) {
        clauseBlocks.push_back(llvm::BasicBlock::Create(*context, "case", function));
    }
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "switch.end", function);
    
    // Find default
    int defaultIdx = -1;
    for (size_t i = 0; i < node->clauses.size(); ++i) {
        if (dynamic_cast<ast::DefaultClause*>(node->clauses[i].get())) {
            defaultIdx = i;
            break;
        }
    }
    
    llvm::BasicBlock* defaultBB = (defaultIdx != -1) ? clauseBlocks[defaultIdx] : mergeBB;

    // Dispatch
    bool useSwitchInst = switchVal->getType()->isIntegerTy();
    if (useSwitchInst) {
        for (auto& clause : node->clauses) {
             if (auto cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                 if (!dynamic_cast<ast::NumericLiteral*>(cc->expression.get())) {
                     useSwitchInst = false;
                     break;
                 }
             }
        }
    }

    if (useSwitchInst) {
        llvm::SwitchInst* swInst = builder->CreateSwitch(switchVal, defaultBB, node->clauses.size());
        for (size_t i = 0; i < node->clauses.size(); ++i) {
            if (auto cc = dynamic_cast<ast::CaseClause*>(node->clauses[i].get())) {
                auto numLit = static_cast<ast::NumericLiteral*>(cc->expression.get());
                int64_t val = (int64_t)numLit->value;
                swInst->addCase(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(switchVal->getType(), val)), clauseBlocks[i]);
            }
        }
    } else {
        // If-Else chain
        for (size_t i = 0; i < node->clauses.size(); ++i) {
            if (auto cc = dynamic_cast<ast::CaseClause*>(node->clauses[i].get())) {
                llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(*context, "check_case", function);
                builder->CreateBr(checkBB);
                builder->SetInsertPoint(checkBB);
                
                visit(cc->expression.get());
                llvm::Value* caseVal = lastValue;
                
                llvm::Value* cmp = nullptr;
                if (switchVal->getType()->isIntegerTy()) {
                    cmp = builder->CreateICmpEQ(switchVal, caseVal, "cmp");
                } else if (switchVal->getType()->isDoubleTy()) {
                    cmp = builder->CreateFCmpOEQ(switchVal, caseVal, "cmp");
                } else {
                    // String comparison
                    llvm::FunctionType* eqFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false);
                    llvm::FunctionCallee eqFn = module->getOrInsertFunction("ts_string_eq", eqFt);
                    cmp = createCall(eqFt, eqFn.getCallee(), { switchVal, caseVal });
                }
                
                llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "next_check", function);
                builder->CreateCondBr(cmp, clauseBlocks[i], nextBB);
                builder->SetInsertPoint(nextBB);
            }
        }
        builder->CreateBr(defaultBB);
    }

    // Push loop info
    llvm::BasicBlock* enclosingContinue = nullptr;
    for (auto it = loopStack.rbegin(); it != loopStack.rend(); ++it) {
        if (it->continueBlock) {
            enclosingContinue = it->continueBlock;
            break;
        }
    }
    LoopInfo info;
    info.continueBlock = enclosingContinue;
    info.breakBlock = mergeBB;
    info.finallyStackDepth = finallyStack.size();
    loopStack.push_back(info);

    auto oldBreak = currentBreakBB;
    auto oldContinue = currentContinueBB;
    currentBreakBB = mergeBB;
    currentContinueBB = enclosingContinue;

    // Generate bodies
    for (size_t i = 0; i < node->clauses.size(); ++i) {
        builder->SetInsertPoint(clauseBlocks[i]);
        
        auto& clause = node->clauses[i];
        if (auto cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
            for (auto& stmt : cc->statements) {
                visit(stmt.get());
            }
        } else if (auto dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
            for (auto& stmt : dc->statements) {
                visit(stmt.get());
            }
        }
        
        // Fallthrough
        if (!builder->GetInsertBlock()->getTerminator()) {
            if (i + 1 < node->clauses.size()) {
                builder->CreateBr(clauseBlocks[i+1]);
            } else {
                builder->CreateBr(mergeBB);
            }
        }
    }

    currentBreakBB = oldBreak;
    currentContinueBB = oldContinue;

    loopStack.pop_back();
    builder->SetInsertPoint(mergeBB);
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
}

void IRGenerator::visitTryStatement(ast::TryStatement* node) {
    emitLocation(node);
    
    if (currentIsAsync) {
        llvm::Function* currentFn = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try", currentFn);
        llvm::BasicBlock* catchBB = llvm::BasicBlock::Create(*context, "catch", currentFn);
        llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "finally", currentFn);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "try_merge", currentFn);

        auto oldReturnBB = currentReturnBB;
        auto oldBreakBB = currentBreakBB;
        auto oldContinueBB = currentContinueBB;

        // Update targets to point to finally
        currentReturnBB = finallyBB;
        if (currentBreakBB) currentBreakBB = finallyBB;
        if (currentContinueBB) currentContinueBB = finallyBB;

        // Setup pending exception storage in the frame
        std::string baseName = "try_" + std::to_string((uintptr_t)node);
        llvm::Value* pendingExc = namedValues[baseName + "_pendingExc"];
        if (!pendingExc) {
            // Fallback to a local alloca if frame mapping failed (should not happen)
            pendingExc = createEntryBlockAlloca(builder->GetInsertBlock()->getParent(), baseName + "_pendingExc", builder->getPtrTy());
        }
        builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), pendingExc);
        
        llvm::Value* globalPendingExc = namedValues["pendingExc"];
        if (globalPendingExc) {
            builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), globalPendingExc);
        }
        
        catchStack.push_back({ catchBB ? catchBB : (finallyBB ? finallyBB : currentReturnBB), pendingExc });
        finallyStack.push_back({ finallyBB, pendingExc, oldReturnBB, oldBreakBB, oldContinueBB, false, false });

        builder->CreateBr(tryBB);

        // --- Try Block ---
        builder->SetInsertPoint(tryBB);
        for (auto& stmt : node->tryBlock) {
            visit(stmt.get());
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(finallyBB ? finallyBB : mergeBB);
        }
        catchStack.pop_back();

        // --- Catch Block ---
        if (catchBB) {
            builder->SetInsertPoint(catchBB);
            if (node->catchClause) {
                llvm::Value* exc = builder->CreateLoad(builder->getPtrTy(), pendingExc);
                // Clear both block-specific and top-level pendingExc
                builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), pendingExc);
                builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), namedValues["pendingExc"]);
                
                if (node->catchClause->variable) {
                    generateDestructuring(exc, std::make_shared<Type>(TypeKind::Any), node->catchClause->variable.get());
                }

                if (finallyBB) catchStack.push_back({ finallyBB, pendingExc });
                for (auto& stmt : node->catchClause->block) {
                    visit(stmt.get());
                }
                if (finallyBB) catchStack.pop_back();

                if (!builder->GetInsertBlock()->getTerminator()) {
                    builder->CreateBr(finallyBB ? finallyBB : mergeBB);
                }
            } else {
                // Clear both block-specific and top-level pendingExc
                builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), pendingExc);
                builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), namedValues["pendingExc"]);
                builder->CreateBr(finallyBB ? finallyBB : mergeBB);
            }
        }

        // --- Finally Block ---
        builder->SetInsertPoint(finallyBB);
        auto info = finallyStack.back();
        finallyStack.pop_back();

        for (auto& stmt : node->finallyBlock) {
            visit(stmt.get());
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            // Check for rethrow
            llvm::Value* toRethrow = builder->CreateLoad(builder->getPtrTy(), pendingExc);
            llvm::Value* shouldRethrow = builder->CreateIsNotNull(toRethrow);
            
            llvm::BasicBlock* rethrowBB = llvm::BasicBlock::Create(*context, "rethrow", currentFn);
            llvm::BasicBlock* checkReturnBB = llvm::BasicBlock::Create(*context, "check_return", currentFn);
            builder->CreateCondBr(shouldRethrow, rethrowBB, checkReturnBB);
            
            builder->SetInsertPoint(rethrowBB);
            // Store in global pendingExc and return
            builder->CreateStore(toRethrow, namedValues["pendingExc"]);
            builder->CreateBr(info.nextFinallyBB);

            // Check for return
            builder->SetInsertPoint(checkReturnBB);
            llvm::Value* shouldReturn = builder->CreateLoad(builder->getInt1Ty(), currentShouldReturnAlloca);
            llvm::BasicBlock* checkBreakBB = llvm::BasicBlock::Create(*context, "check_break", currentFn);
            builder->CreateCondBr(shouldReturn, info.nextFinallyBB, checkBreakBB);

            // Check for break
            builder->SetInsertPoint(checkBreakBB);
            llvm::Value* shouldBreak = builder->CreateLoad(builder->getInt1Ty(), currentShouldBreakAlloca);
            llvm::BasicBlock* checkContinueBB = llvm::BasicBlock::Create(*context, "check_continue", currentFn);
            if (info.nextBreakBB) {
                builder->CreateCondBr(shouldBreak, info.nextBreakBB, checkContinueBB);
            } else {
                builder->CreateBr(checkContinueBB);
            }

            // Check for continue
            builder->SetInsertPoint(checkContinueBB);
            llvm::Value* shouldContinue = builder->CreateLoad(builder->getInt1Ty(), currentShouldContinueAlloca);
            if (info.nextContinueBB) {
                builder->CreateCondBr(shouldContinue, info.nextContinueBB, mergeBB);
            } else {
                builder->CreateBr(mergeBB);
            }
        }

        builder->SetInsertPoint(mergeBB);
        currentReturnBB = oldReturnBB;
        currentBreakBB = oldBreakBB;
        currentContinueBB = oldContinueBB;
        return;
    }

    llvm::Function* pushFn = getRuntimeFunction("ts_push_exception_handler");
    llvm::Function* popFn = getRuntimeFunction("ts_pop_exception_handler");
    llvm::Function* setjmpFn = getRuntimeFunction("_setjmp");
    llvm::Function* getExcFn = getRuntimeFunction("ts_get_exception");

    llvm::Function* currentFn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try", currentFn);
    llvm::BasicBlock* catchBB = llvm::BasicBlock::Create(*context, "catch", currentFn);
    llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "finally", currentFn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "try_merge", currentFn);

    // 0. Setup pending exception storage
    llvm::Value* pendingExc = createEntryBlockAlloca(currentFn, "pendingExc", builder->getPtrTy());
    builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), pendingExc);

    // Push to finally stack
    llvm::BasicBlock* nextFinallyBB = finallyStack.empty() ? currentReturnBB : finallyStack.back().finallyBB;
    
    bool breakIsFinally = false;
    if (!finallyStack.empty()) {
        for (const auto& f : finallyStack) {
            if (f.finallyBB == currentBreakBB) {
                breakIsFinally = true;
                break;
            }
        }
    }
    
    bool continueIsFinally = false;
    if (!finallyStack.empty()) {
        for (const auto& f : finallyStack) {
            if (f.finallyBB == currentContinueBB) {
                continueIsFinally = true;
                break;
            }
        }
    }
    
    finallyStack.push_back({finallyBB, pendingExc, nextFinallyBB, currentBreakBB, currentContinueBB, breakIsFinally, continueIsFinally});

    auto oldBreakBB = currentBreakBB;
    auto oldContinueBB = currentContinueBB;
    currentBreakBB = finallyBB;
    currentContinueBB = finallyBB;

    // 1. Push exception handler for the TRY block
    llvm::Value* jmpBuf = createCall(pushFn->getFunctionType(), pushFn, {});
    llvm::Value* framePtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
    llvm::Value* setjmpRes = createCall(setjmpFn->getFunctionType(), setjmpFn, {jmpBuf, framePtr});

    llvm::Value* isCatch = builder->CreateICmpNE(setjmpRes, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
    builder->CreateCondBr(isCatch, catchBB, tryBB);

    // --- Try Block ---
    builder->SetInsertPoint(tryBB);
    catchStack.push_back({ catchBB, nullptr });
    for (auto& stmt : node->tryBlock) {
        visit(stmt.get());
    }
    catchStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        createCall(popFn->getFunctionType(), popFn, {});
        builder->CreateBr(finallyBB);
    }

    // --- Catch Block ---
    builder->SetInsertPoint(catchBB);
    llvm::Value* exc = createCall(getExcFn->getFunctionType(), getExcFn, {});
    
    if (node->catchClause) {
        llvm::BasicBlock* catchBodyBB = llvm::BasicBlock::Create(*context, "catch_body", currentFn);
        llvm::BasicBlock* catchThrowBB = llvm::BasicBlock::Create(*context, "catch_throw", currentFn);

        llvm::Value* jmpBuf2 = createCall(pushFn->getFunctionType(), pushFn, {});
        llvm::Value* setjmpRes2 = createCall(setjmpFn->getFunctionType(), setjmpFn, {jmpBuf2, framePtr});
        
        llvm::Value* isCatch2 = builder->CreateICmpNE(setjmpRes2, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
        builder->CreateCondBr(isCatch2, catchThrowBB, catchBodyBB);
        
        builder->SetInsertPoint(catchBodyBB);
        if (node->catchClause->variable) {
            generateDestructuring(exc, std::make_shared<Type>(TypeKind::Any), node->catchClause->variable.get());
        }
        for (auto& stmt : node->catchClause->block) {
            visit(stmt.get());
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            createCall(popFn->getFunctionType(), popFn, {});
            builder->CreateBr(finallyBB);
        }
        
        builder->SetInsertPoint(catchThrowBB);
        llvm::Value* newExc = createCall(getExcFn->getFunctionType(), getExcFn, {});
        builder->CreateStore(newExc, pendingExc);
        builder->CreateBr(finallyBB);
    } else {
        builder->CreateStore(exc, pendingExc);
        builder->CreateBr(finallyBB);
    }

    currentBreakBB = oldBreakBB;
    currentContinueBB = oldContinueBB;

    // --- Finally Block ---
    builder->SetInsertPoint(finallyBB);
    auto info = finallyStack.back();
    finallyStack.pop_back();

    for (auto& stmt : node->finallyBlock) {
        visit(stmt.get());
    }
    
    if (!builder->GetInsertBlock()->getTerminator()) {
        // 1. Check for rethrow
        llvm::Value* toRethrow = builder->CreateLoad(builder->getPtrTy(), pendingExc);
        llvm::Value* shouldRethrow = builder->CreateICmpNE(toRethrow, llvm::ConstantPointerNull::get(builder->getPtrTy()));
        
        llvm::BasicBlock* rethrowBB = llvm::BasicBlock::Create(*context, "rethrow", currentFn);
        llvm::BasicBlock* checkReturnBB = llvm::BasicBlock::Create(*context, "check_return", currentFn);
        builder->CreateCondBr(shouldRethrow, rethrowBB, checkReturnBB);
        
        builder->SetInsertPoint(rethrowBB);
        llvm::Function* throwFn = getRuntimeFunction("ts_throw");
        createCall(throwFn->getFunctionType(), throwFn, {toRethrow});
        builder->CreateUnreachable();

        // 2. Check for return
        builder->SetInsertPoint(checkReturnBB);
        llvm::Value* shouldReturn = builder->CreateLoad(builder->getInt1Ty(), currentShouldReturnAlloca);
        
        llvm::BasicBlock* checkBreakBB = llvm::BasicBlock::Create(*context, "check_break", currentFn);
        
        if (info.nextFinallyBB != currentReturnBB) {
            llvm::BasicBlock* popAndRetBB = llvm::BasicBlock::Create(*context, "pop_and_ret", currentFn);
            builder->CreateCondBr(shouldReturn, popAndRetBB, checkBreakBB);
            builder->SetInsertPoint(popAndRetBB);
            createCall(popFn->getFunctionType(), popFn, {});
            builder->CreateBr(info.nextFinallyBB);
        } else {
            builder->CreateCondBr(shouldReturn, info.nextFinallyBB, checkBreakBB);
        }

        // 3. Check for break
        builder->SetInsertPoint(checkBreakBB);
        llvm::Value* shouldBreak = builder->CreateLoad(builder->getInt1Ty(), currentShouldBreakAlloca);
        
        llvm::BasicBlock* checkContinueBB = llvm::BasicBlock::Create(*context, "check_continue", currentFn);
        if (info.nextBreakBB) {
            if (info.nextBreakIsFinally) {
                llvm::BasicBlock* popAndBreakBB = llvm::BasicBlock::Create(*context, "pop_and_break", currentFn);
                builder->CreateCondBr(shouldBreak, popAndBreakBB, checkContinueBB);
                builder->SetInsertPoint(popAndBreakBB);
                createCall(popFn->getFunctionType(), popFn, {});
                builder->CreateBr(info.nextBreakBB);
            } else {
                builder->CreateCondBr(shouldBreak, info.nextBreakBB, checkContinueBB);
            }
        } else {
            builder->CreateBr(checkContinueBB);
        }

        // 4. Check for continue
        builder->SetInsertPoint(checkContinueBB);
        llvm::Value* shouldContinue = builder->CreateLoad(builder->getInt1Ty(), currentShouldContinueAlloca);
        if (info.nextContinueBB) {
            if (info.nextContinueIsFinally) {
                llvm::BasicBlock* popAndContinueBB = llvm::BasicBlock::Create(*context, "pop_and_continue", currentFn);
                builder->CreateCondBr(shouldContinue, popAndContinueBB, mergeBB);
                builder->SetInsertPoint(popAndContinueBB);
                createCall(popFn->getFunctionType(), popFn, {});
                builder->CreateBr(info.nextContinueBB);
            } else {
                builder->CreateCondBr(shouldContinue, info.nextContinueBB, mergeBB);
            }
        } else {
            builder->CreateBr(mergeBB);
        }

        builder->SetInsertPoint(mergeBB);
    }
}

void IRGenerator::visitThrowStatement(ast::ThrowStatement* node) {
    emitLocation(node);
    visit(node->expression.get());
    llvm::Value* exc = lastValue;
    
    if (!exc) {
        SPDLOG_ERROR("visitThrowStatement: expression did not produce a value");
        return;
    }

    // Ensure the exception is boxed
    if (!boxedValues.count(exc)) {
        exc = boxValue(exc, node->expression->inferredType);
    }
    
    if (currentIsAsync) {
        // Store in top-level pendingExc
        llvm::Value* pendingExcAlloca = namedValues["pendingExc"];
        builder->CreateStore(exc, pendingExcAlloca);
        
        if (!catchStack.empty()) {
            // Store in the target's pendingExc
            if (catchStack.back().pendingExc) {
                builder->CreateStore(exc, catchStack.back().pendingExc);
            }
            builder->CreateBr(catchStack.back().catchBB);
        } else {
            builder->CreateBr(currentReturnBB);
        }
        return;
    }

    llvm::Function* throwFn = getRuntimeFunction("ts_throw");
    createCall(throwFn->getFunctionType(), throwFn, {exc});
    builder->CreateUnreachable(); // throw never returns
}

void IRGenerator::visitBreakStatement(ast::BreakStatement* node) {
    if (loopStack.empty() && !currentBreakBB) {
        return;
    }
    
    if (currentBreakBB) {
        bool isFinally = false;
        for (const auto& f : finallyStack) {
            if (f.finallyBB == currentBreakBB) {
                isFinally = true;
                break;
            }
        }

        if (isFinally) {
            builder->CreateStore(builder->getInt1(true), currentShouldBreakAlloca);
            
            llvm::Function* popFn = getRuntimeFunction("ts_pop_exception_handler");
            createCall(popFn->getFunctionType(), popFn, {});
        }
        builder->CreateBr(currentBreakBB);
    }

    // Start a new block for dead code after break
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitContinueStatement(ast::ContinueStatement* node) {
    if (loopStack.empty() && !currentContinueBB) {
        return;
    }

    if (currentContinueBB) {
        bool isFinally = false;
        for (const auto& f : finallyStack) {
            if (f.finallyBB == currentContinueBB) {
                isFinally = true;
                break;
            }
        }

        if (isFinally) {
            builder->CreateStore(builder->getInt1(true), currentShouldContinueAlloca);
            
            llvm::Function* popFn = getRuntimeFunction("ts_pop_exception_handler");
            createCall(popFn->getFunctionType(), popFn, {});
        }
        builder->CreateBr(currentContinueBB);
    }

    // Start a new block for dead code after continue
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitBlockStatement(ast::BlockStatement* node) {
    for (const auto& stmt : node->statements) {
        visit(stmt.get());
    }
}

void IRGenerator::visitVariableDeclaration(ast::VariableDeclaration* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Determine type from initializer
    visit(node->initializer.get());
    llvm::Value* initVal = lastValue;
    
    if (!initVal) {
        return;
    }

    // Check if we need to coerce the value to match the variable's resolved type
    // This handles cases where function returns int64 but variable expects double
    std::shared_ptr<ts::Type> varType = node->resolvedType;
    if (!varType) {
        varType = node->initializer->inferredType;
    }
    
    if (varType && varType->kind == TypeKind::Double && initVal->getType()->isIntegerTy()) {
        // Convert int to double for TypeScript 'number' type
        initVal = builder->CreateSIToFP(initVal, llvm::Type::getDoubleTy(*context), "itod");
    } else if (varType && varType->kind == TypeKind::Int && initVal->getType()->isDoubleTy()) {
        // Convert double to int
        initVal = builder->CreateFPToSI(initVal, llvm::Type::getInt64Ty(*context), "dtoi");
    }

    generateDestructuring(initVal, varType, node->name.get());
}} // namespace ts


