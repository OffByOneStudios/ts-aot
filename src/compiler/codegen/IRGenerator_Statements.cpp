#include "IRGenerator.h"

namespace ts {

void IRGenerator::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        
        if (lastValue) {
            llvm::Function* func = builder->GetInsertBlock()->getParent();
            llvm::Type* retType = func->getReturnType();
            
            if (lastValue->getType() != retType) {
                if (retType->isDoubleTy() && lastValue->getType()->isIntegerTy(64)) {
                    lastValue = builder->CreateSIToFP(lastValue, retType, "casttmp");
                } else if (retType->isIntegerTy(64) && lastValue->getType()->isDoubleTy()) {
                    lastValue = builder->CreateFPToSI(lastValue, retType, "casttmp");
                }
            }
            builder->CreateRet(lastValue);
        } else {
             // Error?
        }
    } else {
        builder->CreateRetVoid();
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

void IRGenerator::visitWhileStatement(ast::WhileStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "whilecond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "whileloop");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "whileafter");

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;

    if (!condValue) {
        llvm::errs() << "Error: While condition evaluated to null\n";
        return;
    }

    // Convert condition to bool
    if (condValue->getType()->isDoubleTy()) {
        condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
    } else if (condValue->getType()->isIntegerTy()) {
        condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(condValue->getType(), 0), "ifcond");
    }

    builder->CreateCondBr(condValue, loopBB, afterBB);

    // Push loop info
    loopStack.push_back({ condBB, afterBB });

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    // Pop loop info
    loopStack.pop_back();

    // Jump back to condition
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForStatement(ast::ForStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forcond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forloop");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forinc");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forafter");

    // Emit initializer
    if (node->initializer) {
        visit(node->initializer.get());
    }

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    if (node->condition) {
        visit(node->condition.get());
        llvm::Value* condValue = lastValue;

        if (!condValue) {
            llvm::errs() << "Error: For condition evaluated to null\n";
            return;
        }

        // Convert condition to bool
        if (condValue->getType()->isDoubleTy()) {
            condValue = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");
        } else if (condValue->getType()->isIntegerTy()) {
            condValue = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(condValue->getType(), 0), "ifcond");
        }

        builder->CreateCondBr(condValue, loopBB, afterBB);
    } else {
        // Infinite loop
        builder->CreateBr(loopBB);
    }

    // Push loop info
    loopStack.push_back({ incBB, afterBB });

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    // Pop loop info
    loopStack.pop_back();
    
    // Jump to incrementor
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    // Emit incrementor
    func->insert(func->end(), incBB);
    builder->SetInsertPoint(incBB);
    if (node->incrementor) {
        visit(node->incrementor.get());
    }
    builder->CreateBr(condBB);

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
}

void IRGenerator::visitForOfStatement(ast::ForOfStatement* node) {
    // Lowering:
    // for (const x of arr) { ... }
    // ->
    // int i = 0;
    // int len = arr.length;
    // while (i < len) {
    //   x = arr[i];
    //   ...
    //   i++;
    // }

    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Evaluate iterable
    visit(node->expression.get());
    llvm::Value* iterable = lastValue;
    if (!iterable) return;

    bool isString = false;
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::String) {
        isString = true;
    }

    // Create loop variables
    llvm::AllocaInst* indexVar = createEntryBlockAlloca(function, "forof_index", llvm::Type::getInt64Ty(*context));
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    // Get length
    llvm::Value* lengthVal = nullptr;
    if (isString) {
        llvm::Function* lenFn = module->getFunction("ts_string_length");
        if (!lenFn) {
             // Declare it
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             lenFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_length", module.get());
        }
        lengthVal = builder->CreateCall(lenFn, { iterable }, "len");
    } else {
        // Array
        llvm::Function* lenFn = module->getFunction("ts_array_length");
        if (!lenFn) {
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
             lenFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_length", module.get());
        }
        lengthVal = builder->CreateCall(lenFn, { iterable }, "len");
    }

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "loop.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "loop.body", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "loop.end", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    // Check condition: i < length
    llvm::Value* currIndex = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx");
    llvm::Value* cond = builder->CreateICmpSLT(currIndex, lengthVal, "loopcond");
    builder->CreateCondBr(cond, bodyBB, afterBB);

    builder->SetInsertPoint(bodyBB);

    // Get element
    llvm::Value* elementVal = nullptr;
    if (isString) {
        // String: substring(i, i+1) or charCodeAt?
        // TS for-of on string yields 1-char strings.
        llvm::Function* substrFn = module->getFunction("ts_string_substring");
        if (!substrFn) {
             std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
             substrFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_substring", module.get());
        }
        llvm::Value* nextIndex = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
        elementVal = builder->CreateCall(substrFn, { iterable, currIndex, nextIndex }, "char");
    } else {
        // Array: get(i)
        llvm::Function* getFn = module->getFunction("ts_array_get");
        if (!getFn) {
             std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) };
             llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), args, false); // Returns int64_t
             getFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_array_get", module.get());
        }
        
        elementVal = builder->CreateCall(getFn, { iterable, currIndex }, "elem");
    }

    // Bind loop variable
    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
        }
        generateDestructuring(elementVal, elementType, varDecl->name.get());
    }

    // Body
    loopStack.push_back({condBB, afterBB}); // Continue goes to increment (which is at end of body here)
    visit(node->body.get());
    loopStack.pop_back();

    // Increment
    llvm::Value* nextIdx = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
    builder->CreateStore(nextIdx, indexVar);
    
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
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
                    llvm::Function* eqFn = module->getFunction("ts_string_eq");
                    if (!eqFn) {
                        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
                        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), args, false);
                        eqFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_eq", module.get());
                    }
                    cmp = builder->CreateCall(eqFn, { switchVal, caseVal }, "cmp");
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

    // 1. Push exception handler and get jmp_buf
    llvm::Value* jmpBuf = builder->CreateCall(pushFn);

    // 2. Call _setjmp
    // On Windows x64, second arg is frame pointer.
    llvm::Value* framePtr = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(*context));
    llvm::Value* setjmpRes = builder->CreateCall(setjmpFn, {jmpBuf, framePtr});

    llvm::Value* isCatch = builder->CreateICmpNE(setjmpRes, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));

    llvm::Function* currentFn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try", currentFn);
    llvm::BasicBlock* catchBB = llvm::BasicBlock::Create(*context, "catch", currentFn);
    llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "finally", currentFn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "try_merge", currentFn);

    builder->CreateCondBr(isCatch, catchBB, tryBB);

    // --- Try Block ---
    builder->SetInsertPoint(tryBB);
    for (auto& stmt : node->tryBlock) {
        visit(stmt.get());
    }
    // If we reach here, no exception was thrown, so pop the handler
    builder->CreateCall(popFn);
    builder->CreateBr(finallyBB);

    // --- Catch Block ---
    builder->SetInsertPoint(catchBB);
    // The handler was already popped by ts_throw
    if (node->catchClause) {
        if (node->catchClause->variable) {
            llvm::Value* exc = builder->CreateCall(getExcFn);
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
    
    builder->CreateCall(throwFn, {exc});
    builder->CreateUnreachable(); // throw never returns
}

void IRGenerator::visitBreakStatement(ast::BreakStatement* node) {
    if (loopStack.empty()) {
        llvm::errs() << "Error: Break statement outside of loop\n";
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
        llvm::errs() << "Error: Continue statement outside of loop\n";
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
        llvm::errs() << "Error: Variable initializer evaluated to null\n";
        return;
    }

    generateDestructuring(initVal, node->initializer->inferredType, node->name.get());
}

} // namespace ts
