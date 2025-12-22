#include "IRGenerator.h"

namespace ts {
using namespace ast;

void IRGenerator::visitReturnStatement(ast::ReturnStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::Type* retType = func->getReturnType();

    llvm::Value* val = nullptr;
    if (node->expression) {
        visit(node->expression.get());
        val = lastValue;
        
        if (currentReturnType && currentReturnType->kind == TypeKind::Any) {
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
        if (!boxedVal) boxedVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
        else boxedVal = boxValue(boxedVal, node->expression ? node->expression->inferredType : nullptr);

        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 3);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        
        llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
        createCall(resolveFt, resolveFn.getCallee(), { promise, boxedVal });
        builder->CreateRetVoid();
    } else {
        if (val) {
            builder->CreateRet(val);
        } else {
            llvm::Type* retType = builder->GetInsertBlock()->getParent()->getReturnType();
            if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                builder->CreateRet(llvm::Constant::getNullValue(retType));
            }
        }
    }
}

void IRGenerator::visitExpressionStatement(ast::ExpressionStatement* node) {
    visit(node->expression.get());
}

void IRGenerator::visitIfStatement(ast::IfStatement* node) {
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;
    if (!condValue) return;

    // Convert condition to bool
    if (condValue->getType()->isIntegerTy(64)) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(*context, llvm::APInt(64, 0)), "ifcond");
    } else if (condValue->getType()->isDoubleTy()) {
        condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
    } else if (condValue->getType()->isPointerTy()) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantPointerNull::get(builder->getPtrTy()), "ifcond");
    }

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
    loopStack.push_back({enclosingContinue, mergeBB});

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

    loopStack.pop_back();
    builder->SetInsertPoint(mergeBB);
}

void IRGenerator::visitTryStatement(ast::TryStatement* node) {
    llvm::Function* pushFn = getRuntimeFunction("ts_push_exception_handler");
    llvm::Function* popFn = getRuntimeFunction("ts_pop_exception_handler");
    llvm::Function* setjmpFn = getRuntimeFunction("_setjmp");
    llvm::Function* getExcFn = getRuntimeFunction("ts_get_exception");

    llvm::Function* currentFn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try", currentFn);
    llvm::BasicBlock* catchBB = llvm::BasicBlock::Create(*context, "catch", currentFn);
    llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "finally", currentFn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "try_merge", currentFn);

    // Push catchBB to stack for async handling
    catchStack.push_back(catchBB);

    // 1. Push exception handler and get jmp_buf
    llvm::Value* jmpBuf = createCall(pushFn->getFunctionType(), pushFn, {});

    // 2. Call _setjmp
    // On Windows x64, second arg is frame pointer.
    llvm::Value* framePtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
    llvm::Value* setjmpRes = createCall(setjmpFn->getFunctionType(), setjmpFn, {jmpBuf, framePtr});

    llvm::Value* isCatch = builder->CreateICmpNE(setjmpRes, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));

    builder->CreateCondBr(isCatch, catchBB, tryBB);

    // --- Try Block ---
    builder->SetInsertPoint(tryBB);
    for (auto& stmt : node->tryBlock) {
        visit(stmt.get());
    }
    
    // Pop catchBB from stack
    catchStack.pop_back();

    // If we reach here, no exception was thrown, so pop the handler
    createCall(popFn->getFunctionType(), popFn, {});
    builder->CreateBr(finallyBB);

    // --- Catch Block ---
    builder->SetInsertPoint(catchBB);
    // The handler was already popped by ts_throw
    if (node->catchClause) {
        if (node->catchClause->variable) {
            llvm::Value* exc = createCall(getExcFn->getFunctionType(), getExcFn, {});
            generateDestructuring(exc, std::make_shared<Type>(TypeKind::Any), node->catchClause->variable.get());
        }
        for (auto& stmt : node->catchClause->block) {
            visit(stmt.get());
        }
    }
    builder->CreateBr(finallyBB);

    // --- Finally Block ---
    builder->SetInsertPoint(finallyBB);
    for (auto& stmt : node->finallyBlock) {
        visit(stmt.get());
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
}

void IRGenerator::visitThrowStatement(ast::ThrowStatement* node) {
    llvm::Function* throwFn = getRuntimeFunction("ts_throw");
    visit(node->expression.get());
    llvm::Value* exc = lastValue;
    
    // Ensure exc is a pointer (all TS objects are pointers)
    if (!exc->getType()->isPointerTy()) {
        // This shouldn't happen for objects, but might for primitives if we don't box them
        // For now, just cast to ptr
        exc = builder->CreateIntToPtr(exc, llvm::PointerType::getUnqual(*context));
    }
    
    createCall(throwFn->getFunctionType(), throwFn, {exc});
    builder->CreateUnreachable(); // throw never returns
}

void IRGenerator::visitBreakStatement(ast::BreakStatement* node) {
    if (loopStack.empty()) {
        return;
    }
    builder->CreateBr(loopStack.back().breakBlock);
    // Start a new block for dead code after break
    llvm::Function* func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(*context, "dead", func);
    builder->SetInsertPoint(deadBB);
}

void IRGenerator::visitContinueStatement(ast::ContinueStatement* node) {
    if (loopStack.empty()) {
        return;
    }
    builder->CreateBr(loopStack.back().continueBlock);
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

    generateDestructuring(initVal, node->initializer->inferredType, node->name.get());
}} // namespace ts


