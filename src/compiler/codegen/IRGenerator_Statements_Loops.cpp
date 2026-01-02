#include "IRGenerator.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

void IRGenerator::visitWhileStatement(ast::WhileStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "whilecond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "whileloop");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "whileafter");

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    visit(node->condition.get());
    llvm::Value* condValue = lastValue;

    if (!condValue) {
        SPDLOG_ERROR("While condition evaluated to null");
        return;
    }

    // Convert condition to bool
    condValue = emitToBoolean(condValue, node->condition->inferredType);

    builder->CreateCondBr(condValue, loopBB, afterBB);

    // Detect while (i < arr.length)
    std::string indexVar;
    std::string arrayVar;
    bool isSafeLoop = false;

    if (auto bin = dynamic_cast<ast::BinaryExpression*>(node->condition.get())) {
        if (bin->op == "<") {
            if (auto leftId = dynamic_cast<ast::Identifier*>(bin->left.get())) {
                indexVar = leftId->name;
                if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(bin->right.get())) {
                    if (prop->name == "length") {
                        if (auto arrId = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                            arrayVar = arrId->name;
                            isSafeLoop = true;
                        }
                    }
                } else if (auto rightId = dynamic_cast<ast::Identifier*>(bin->right.get())) {
                    llvm::Value* rightVal = nullptr;
                    if (namedValues.count(rightId->name)) {
                        rightVal = namedValues[rightId->name];
                    } else {
                        rightVal = module->getGlobalVariable(rightId->name);
                    }

                    if (rightVal && lengthAliases.count(rightVal)) {
                        arrayVar = lengthAliases[rightVal];
                        isSafeLoop = true;
                    }
                }
            }
        }
    }

    // Push loop info
    LoopInfo info;
    info.continueBlock = condBB;
    info.breakBlock = afterBB;
    info.finallyStackDepth = finallyStack.size();
    if (isSafeLoop) {
        info.safeIndices[indexVar] = arrayVar;
    }
    loopStack.push_back(info);

    auto oldBreak = currentBreakBB;
    auto oldContinue = currentContinueBB;
    currentBreakBB = afterBB;
    currentContinueBB = condBB;

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    currentBreakBB = oldBreak;
    currentContinueBB = oldContinue;

    // Pop loop info
    loopStack.pop_back();

    // Jump back to condition
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(condBB);
    }

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
}

void IRGenerator::visitForStatement(ast::ForStatement* node) {
    llvm::Function* func = builder->GetInsertBlock()->getParent();

    // Detect for (let i = 0; i < arr.length; i++)
    std::string indexVar;
    std::string arrayVar;
    bool isSafeLoop = false;

    if (node->initializer) {
        if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
            if (auto id = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                // Check if initializer is 0
                bool isZero = false;
                if (auto lit = dynamic_cast<ast::NumericLiteral*>(varDecl->initializer.get())) {
                    if (lit->value == 0) isZero = true;
                }
                
                if (isZero) {
                    indexVar = id->name;
                }
            }
        }
    }

    if (!indexVar.empty() && node->condition) {
        if (auto bin = dynamic_cast<ast::BinaryExpression*>(node->condition.get())) {
            if (bin->op == "<" || bin->op == "<=") {
                if (auto leftId = dynamic_cast<ast::Identifier*>(bin->left.get())) {
                    if (leftId->name == indexVar) {
                        if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(bin->right.get())) {
                            if (prop->name == "length") {
                                if (auto arrId = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                                    arrayVar = arrId->name;
                                    isSafeLoop = true;
                                }
                            }
                        } else if (auto rightId = dynamic_cast<ast::Identifier*>(bin->right.get())) {
                            // Check if rightId is an alias for some array's length
                            llvm::Value* rightVal = nullptr;
                            if (namedValues.count(rightId->name)) {
                                rightVal = namedValues[rightId->name];
                            } else {
                                rightVal = module->getGlobalVariable(rightId->name);
                            }

                            if (rightVal) {
                                if (lengthAliases.count(rightVal)) {
                                    arrayVar = lengthAliases[rightVal];
                                    isSafeLoop = true;
                                }
                            }
                        } else if (auto lit = dynamic_cast<ast::NumericLiteral*>(bin->right.get())) {
                            // Constant bound is also safe for promotion
                            isSafeLoop = true;
                        }
                    }
                }
            }
        }
    }

    // Verify increment is i++ or ++i or i += 1
    if (isSafeLoop && node->incrementor) {
        bool validIncrement = false;
        if (auto unary = dynamic_cast<ast::PostfixUnaryExpression*>(node->incrementor.get())) {
            if (unary->op == "++") {
                if (auto id = dynamic_cast<ast::Identifier*>(unary->operand.get())) {
                    if (id->name == indexVar) validIncrement = true;
                }
            }
        } else if (auto preUnary = dynamic_cast<ast::PrefixUnaryExpression*>(node->incrementor.get())) {
            if (preUnary->op == "++") {
                if (auto id = dynamic_cast<ast::Identifier*>(preUnary->operand.get())) {
                    if (id->name == indexVar) validIncrement = true;
                }
            }
        } else if (auto bin = dynamic_cast<ast::BinaryExpression*>(node->incrementor.get())) {
            if (bin->op == "+=") {
                if (auto id = dynamic_cast<ast::Identifier*>(bin->left.get())) {
                    if (id->name == indexVar) {
                        if (auto lit = dynamic_cast<ast::NumericLiteral*>(bin->right.get())) {
                            if (lit->value == 1) validIncrement = true;
                        }
                    }
                }
            }
        }
        if (!validIncrement) isSafeLoop = false;
    }

    // If safe, promote indexVar to i64
    if (isSafeLoop) {
        forcedVariableTypes[indexVar] = llvm::Type::getInt64Ty(*context);
    }

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forcond", func);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forloop");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forinc");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forafter");

    // Emit initializer
    if (node->initializer) {
        visit(node->initializer.get());
    }

    // Clear promotion hint after initializer (it's now in namedValues as an i64 alloca)
    if (isSafeLoop) {
        forcedVariableTypes.erase(indexVar);
    }

    // Jump to condition
    builder->CreateBr(condBB);

    // Emit condition
    builder->SetInsertPoint(condBB);
    if (node->condition) {
        visit(node->condition.get());
        llvm::Value* condValue = lastValue;

        if (!condValue) {
            SPDLOG_ERROR("For condition evaluated to null");
            return;
        }

        // Convert condition to bool
        condValue = emitToBoolean(condValue, node->condition->inferredType);

        builder->CreateCondBr(condValue, loopBB, afterBB);
    } else {
        // Infinite loop
        builder->CreateBr(loopBB);
    }

    // Push loop info
    LoopInfo info;
    info.continueBlock = incBB;
    info.breakBlock = afterBB;
    info.finallyStackDepth = finallyStack.size();
    if (isSafeLoop) {
        info.safeIndices[indexVar] = arrayVar;
    }
    loopStack.push_back(info);

    auto oldBreak = currentBreakBB;
    auto oldContinue = currentContinueBB;
    currentBreakBB = afterBB;
    currentContinueBB = incBB;

    // Emit loop body
    func->insert(func->end(), loopBB);
    builder->SetInsertPoint(loopBB);
    visit(node->body.get());
    
    currentBreakBB = oldBreak;
    currentContinueBB = oldContinue;

    // Pop loop info
    loopStack.pop_back();
    
    // Jump to incrementor
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    // Emit incrementor
    func->insert(func->end(), incBB);
    builder->SetInsertPoint(incBB);
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    if (node->incrementor) {
        visit(node->incrementor.get());
    }
    builder->CreateBr(condBB);

    // Emit after block
    func->insert(func->end(), afterBB);
    builder->SetInsertPoint(afterBB);
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
}

void IRGenerator::visitForOfStatement(ast::ForOfStatement* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();

    if (node->isAwait) {
        std::string baseName = "forof_" + std::to_string((uintptr_t)node);
        
        // 1. Evaluate iterable
        visit(node->expression.get());
        llvm::Value* iterableVal = boxValue(lastValue, node->expression->inferredType);
        
        // 2. Get iterator: iterator = ts_async_iterator_get(iterable)
        llvm::FunctionType* getIterFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getIterFn = getRuntimeFunction("ts_async_iterator_get", getIterFt);
        llvm::Value* iterator = createCall(getIterFt, getIterFn.getCallee(), { iterableVal });

        llvm::Value* iteratorVar = nullptr;
        if (currentAsyncFrame) {
            iteratorVar = namedValues[baseName + "_iterator"];
        } else {
            iteratorVar = createEntryBlockAlloca(function, baseName + "_iterator", builder->getPtrTy());
        }
        builder->CreateStore(iterator, iteratorVar);

        // 3. Loop
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "loop.cond", function);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "loop.body", function);
        llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "loop.end", function);
        
        builder->CreateBr(condBB);
        builder->SetInsertPoint(condBB);
        builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
        
        llvm::Value* iter = builder->CreateLoad(builder->getPtrTy(), iteratorVar);

        // 4. Call iterator.next()
        llvm::FunctionType* nextFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee nextFn = getRuntimeFunction("ts_async_iterator_next", nextFt);
        
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
        llvm::Value* undefinedVal = createCall(undefFt, undefFn.getCallee(), {});
        
        llvm::Value* promise = createCall(nextFt, nextFn.getCallee(), { iter, undefinedVal });
        
        // 5. Await promise
        llvm::Value* result = emitAwait(promise, nullptr); // result is { value, done }
        
        // 5. Check done
        llvm::Value* doneName = builder->CreateGlobalStringPtr("done");
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);
        llvm::Value* tsDoneName = createCall(createFt, createFn.getCallee(), { doneName });
        llvm::Value* boxedDoneName = boxValue(tsDoneName, std::make_shared<Type>(TypeKind::String));
        
        llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getPropFn = getRuntimeFunction("ts_value_get_property", getPropFt);
        
        llvm::Value* doneValBoxed = createCall(getPropFt, getPropFn.getCallee(), { result, boxedDoneName });
        llvm::Value* doneVal = unboxValue(doneValBoxed, std::make_shared<Type>(TypeKind::Boolean));
        
        builder->CreateCondBr(doneVal, afterBB, bodyBB);
        
        builder->SetInsertPoint(bodyBB);
        
        // 6. Get value
        llvm::Value* valueName = builder->CreateGlobalStringPtr("value");
        llvm::Value* tsValueName = createCall(createFt, createFn.getCallee(), { valueName });
        llvm::Value* boxedValueName = boxValue(tsValueName, std::make_shared<Type>(TypeKind::String));
        llvm::Value* valueValBoxed = createCall(getPropFt, getPropFn.getCallee(), { result, boxedValueName });
        
        // 7. Destructure value
        if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
            std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
            // Try to get element type from iterable type
            if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
                if (classType->typeArguments.size() > 0) {
                    elementType = classType->typeArguments[0];
                }
            }
            
            llvm::Value* unboxed = unboxValue(valueValBoxed, elementType);
            generateDestructuring(unboxed, elementType, varDecl->name.get());
        }
        
        // 8. Body
        LoopInfo info;
        info.continueBlock = condBB;
        info.breakBlock = afterBB;
        info.finallyStackDepth = finallyStack.size();
        loopStack.push_back(info);

        auto oldBreak = currentBreakBB;
        auto oldContinue = currentContinueBB;
        currentBreakBB = afterBB;
        currentContinueBB = condBB;

        visit(node->body.get());

        currentBreakBB = oldBreak;
        currentContinueBB = oldContinue;

        loopStack.pop_back();
        
        builder->CreateBr(condBB);
        builder->SetInsertPoint(afterBB);
        builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
        return;
    }
    
    // Create loop variables
    std::string baseName = "forof_" + std::to_string((uintptr_t)node);
    llvm::Value* indexVar = nullptr;
    llvm::Value* iterableVar = nullptr;
    llvm::Value* lengthVar = nullptr;

    if (currentAsyncFrame) {
        indexVar = namedValues[baseName + "_index"];
        iterableVar = namedValues[baseName + "_iterable"];
        lengthVar = namedValues[baseName + "_length"];
    } else {
        indexVar = createEntryBlockAlloca(function, baseName + "_index", llvm::Type::getInt64Ty(*context));
        iterableVar = createEntryBlockAlloca(function, baseName + "_iterable", builder->getPtrTy());
        lengthVar = createEntryBlockAlloca(function, baseName + "_length", llvm::Type::getInt64Ty(*context));
    }

    // Evaluate iterable
    visit(node->expression.get());
    llvm::Value* iterable = lastValue;
    if (!iterable) return;
    builder->CreateStore(iterable, iterableVar);

    bool isString = false;
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::String) {
        isString = true;
    }

    // Initialize index
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    // Get length
    llvm::Value* lengthVal = nullptr;
    if (isString) {
        llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee lenFn = getRuntimeFunction("ts_string_length", lenFt);
        lengthVal = createCall(lenFt, lenFn.getCallee(), { iterable });
    } else {
        llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee lenFn = getRuntimeFunction("ts_array_length", lenFt);
        lengthVal = createCall(lenFt, lenFn.getCallee(), { iterable });
    }
    builder->CreateStore(lengthVal, lengthVar);

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "loop.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "loop.body", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "loop.inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "loop.end", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* currIndex = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx");
    llvm::Value* currLength = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthVar, "len");
    llvm::Value* cond = builder->CreateICmpSLT(currIndex, currLength, "loopcond");
    builder->CreateCondBr(cond, bodyBB, afterBB);

    builder->SetInsertPoint(bodyBB);

    llvm::Value* currIterable = builder->CreateLoad(builder->getPtrTy(), iterableVar, "iter");
    llvm::Value* elementVal = nullptr;
    std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
    }
    
    if (isString) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) };
        llvm::FunctionType* substrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
        llvm::FunctionCallee substrFn = getRuntimeFunction("ts_string_substring", substrFt);
        llvm::Value* nextIndex = builder->CreateAdd(currIndex, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
        elementVal = createCall(substrFt, substrFn.getCallee(), { currIterable, currIndex, nextIndex });
    } else if (elementType->kind == TypeKind::Int || elementType->kind == TypeKind::Double) {
        // Specialized array access for Int/Double
        llvm::Type* llvmElemType = (elementType->kind == TypeKind::Double) 
            ? llvm::Type::getDoubleTy(*context) 
            : llvm::Type::getInt64Ty(*context);
        
        llvm::StructType* tsArrayType = llvm::StructType::getTypeByName(*context, "TsArray");
        llvm::Value* elementsPtrPtr = builder->CreateStructGEP(tsArrayType, currIterable, 1);
        llvm::Value* elementsPtr = builder->CreateLoad(builder->getPtrTy(), elementsPtrPtr);
        llvm::Value* ptr = builder->CreateGEP(llvmElemType, elementsPtr, { currIndex });
        elementVal = builder->CreateLoad(llvmElemType, ptr);
    } else {
        std::vector<llvm::Type*> args = { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) };
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), args, false);
        llvm::FunctionCallee getFn = getRuntimeFunction("ts_array_get", getFt);
        elementVal = createCall(getFt, getFn.getCallee(), { currIterable, currIndex });
    }

    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        llvm::Value* unboxed = unboxValue(elementVal, elementType);
        generateDestructuring(unboxed, elementType, varDecl->name.get());
    }

    LoopInfo info;
    info.continueBlock = incBB;
    info.breakBlock = afterBB;
    info.finallyStackDepth = finallyStack.size();
    loopStack.push_back(info);

    auto oldBreak = currentBreakBB;
    auto oldContinue = currentContinueBB;
    currentBreakBB = afterBB;
    currentContinueBB = incBB;

    visit(node->body.get());

    currentBreakBB = oldBreak;
    currentContinueBB = oldContinue;

    loopStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    llvm::Value* reloadIdx = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "idx.reload");
    llvm::Value* nextIdx = builder->CreateAdd(reloadIdx, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1));
    builder->CreateStore(nextIdx, indexVar);
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
}

void IRGenerator::visitForInStatement(ast::ForInStatement* node) {
    llvm::Function* function = builder->GetInsertBlock()->getParent();
    
    // Create loop variables
    std::string baseName = "forin_" + std::to_string((uintptr_t)node);
    llvm::Value* indexVar = nullptr;
    llvm::Value* keysVar = nullptr;
    llvm::Value* lengthVar = nullptr;

    if (currentAsyncFrame) {
        indexVar = namedValues[baseName + "_index"];
        keysVar = namedValues[baseName + "_keys"];
        lengthVar = namedValues[baseName + "_length"];
    } else {
        indexVar = createEntryBlockAlloca(function, baseName + "_index", llvm::Type::getInt64Ty(*context));
        keysVar = createEntryBlockAlloca(function, baseName + "_keys", builder->getPtrTy());
        lengthVar = createEntryBlockAlloca(function, baseName + "_length", llvm::Type::getInt64Ty(*context));
    }

    visit(node->expression.get());
    llvm::Value* obj = lastValue;
    if (!obj) return;

    llvm::Value* keys = nullptr;
    if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Class)) {
        llvm::FunctionType* createArrFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false);
        llvm::FunctionCallee createArrFn = getRuntimeFunction("ts_array_create", createArrFt);
        keys = createCall(createArrFt, createArrFn.getCallee(), {});

        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);

        std::vector<std::string> fieldNames;
        if (node->expression->inferredType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
            for (auto const& [name, type] : objType->fields) fieldNames.push_back(name);
        } else {
            auto clsType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
            auto current = clsType;
            while (current) {
                for (auto const& [name, type] : current->fields) fieldNames.push_back(name);
                current = current->baseClass;
            }
        }

        for (const auto& name : fieldNames) {
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(name);
            llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
            createCall(pushFt, pushFn.getCallee(), { keys, tsStr });
        }
    } else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
        llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
        llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee keysFn = getRuntimeFunction("ts_object_keys", keysFt);
        llvm::Value* boxedKeys = createCall(keysFt, keysFn.getCallee(), { boxedObj });
        
        llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
        keys = createCall(getObjFt, getObjFn.getCallee(), { boxedKeys });
    } else {
        llvm::FunctionType* keysFt = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false);
        llvm::FunctionCallee keysFn = getRuntimeFunction("ts_map_keys", keysFt);
        keys = createCall(keysFt, keysFn.getCallee(), { obj });
    }
    builder->CreateStore(keys, keysVar);

    // Get length
    llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { llvm::PointerType::getUnqual(*context) }, false);
    llvm::FunctionCallee lenFn = getRuntimeFunction("ts_array_length", lenFt);
    llvm::Value* lengthVal = createCall(lenFt, lenFn.getCallee(), { keys });
    builder->CreateStore(lengthVal, lengthVar);

    // Initialize index
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0), indexVar);

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "forin_cond", function);
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(*context, "forin_loop", function);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(*context, "forin_inc", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "forin_after", function);

    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);

    llvm::Value* index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "index");
    llvm::Value* currLength = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthVar, "len");
    llvm::Value* cond = builder->CreateICmpSLT(index, currLength, "forin_cond_tmp");
    builder->CreateCondBr(cond, loopBB, afterBB);

    builder->SetInsertPoint(loopBB);

    llvm::Value* currKeys = builder->CreateLoad(builder->getPtrTy(), keysVar, "keys");
    llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
    llvm::FunctionCallee getFn = getRuntimeFunction("ts_array_get", getFt);
    llvm::Value* key = createCall(getFt, getFn.getCallee(), { currKeys, index });
    boxedValues.insert(key);

    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get())) {
        generateDestructuring(key, std::make_shared<ts::Type>(ts::TypeKind::String), varDecl->name.get());
    }

    LoopInfo info;
    info.continueBlock = incBB;
    info.breakBlock = afterBB;
    info.finallyStackDepth = finallyStack.size();
    loopStack.push_back(info);

    auto oldBreak = currentBreakBB;
    auto oldContinue = currentContinueBB;
    currentBreakBB = afterBB;
    currentContinueBB = incBB;

    visit(node->body.get());

    currentBreakBB = oldBreak;
    currentContinueBB = oldContinue;

    loopStack.pop_back();

    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(incBB);
    }

    builder->SetInsertPoint(incBB);
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    index = builder->CreateLoad(llvm::Type::getInt64Ty(*context), indexVar, "index");
    llvm::Value* nextIndex = builder->CreateAdd(index, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1), "next_index");
    builder->CreateStore(nextIndex, indexVar);
    builder->CreateBr(condBB);

    builder->SetInsertPoint(afterBB);
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
}

} // namespace ts
