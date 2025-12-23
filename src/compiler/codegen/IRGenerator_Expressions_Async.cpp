#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;


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

    lastValue = emitAwait(promiseVal, node->inferredType);

    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        concreteTypes[lastValue] = std::static_pointer_cast<ClassType>(node->inferredType).get();
    }
}

llvm::Value* IRGenerator::emitAwait(llvm::Value* promiseVal, std::shared_ptr<Type> type) {
    // 2. Save state
    int nextState = asyncStateBlocks.size();
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "state" + std::to_string(nextState), builder->GetInsertBlock()->getParent());
    asyncStateBlocks.push_back(nextBB);

    // Update ctx->state
    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState), statePtr);

    // 3. Call ts_async_await
    llvm::FunctionType* awaitFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee awaitFn = module->getOrInsertFunction("ts_async_await", awaitFt);
    createCall(awaitFt, awaitFn.getCallee(), { promiseVal, currentAsyncContext });

    // 4. Return from SM function
    builder->CreateRetVoid();

    // 5. Start next state
    builder->SetInsertPoint(nextBB);
    
    // Check for error
    llvm::Value* errorPtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 2);
    llvm::Value* errorVal = builder->CreateLoad(llvm::Type::getInt8Ty(*context), errorPtr);
    llvm::Value* isError = builder->CreateICmpNE(errorVal, builder->getInt8(0));
    
    llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(*context, "await_error", builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* successBB = llvm::BasicBlock::Create(*context, "await_success", builder->GetInsertBlock()->getParent());
    
    builder->CreateCondBr(isError, errorBB, successBB);
    
    // Handle error
    builder->SetInsertPoint(errorBB);
    
    if (!catchStack.empty()) {
        // Set exception
        llvm::FunctionType* setExcFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee setExcFn = module->getOrInsertFunction("ts_set_exception", setExcFt);
        createCall(setExcFt, setExcFn.getCallee(), { currentAsyncResumedValue });
        
        // Branch to catch
        builder->CreateBr(catchStack.back());
    } else {
        // Reject promise and return
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 5);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        
        llvm::FunctionType* rejectFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee rejectFn = module->getOrInsertFunction("ts_promise_reject_internal", rejectFt);
        createCall(rejectFt, rejectFn.getCallee(), { promise, currentAsyncResumedValue });
        builder->CreateRetVoid();
    }
    
    // Handle success
    builder->SetInsertPoint(successBB);
    
    // The resumed value is in currentAsyncResumedValue
    // Unbox it to the expected type
    return unboxValue(currentAsyncResumedValue, type);
}

void IRGenerator::visitYieldExpression(ast::YieldExpression* node) {
    if (!currentAsyncContext) {
        // Should be caught by analyzer
        if (node->expression) visit(node->expression.get());
        return;
    }

    // 1. Evaluate expression
    llvm::Value* yieldVal;
    if (node->expression) {
        visit(node->expression.get());
        yieldVal = boxValue(lastValue, node->expression->inferredType);
    } else {
        yieldVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
    }

    // 2. Save state
    int nextState = asyncStateBlocks.size();
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "state" + std::to_string(nextState), builder->GetInsertBlock()->getParent());
    asyncStateBlocks.push_back(nextBB);

    // Update ctx->state
    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState), statePtr);

    // 3. Call ts_async_yield
    llvm::FunctionType* yieldFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee yieldFn = module->getOrInsertFunction("ts_async_yield", yieldFt);
    createCall(yieldFt, yieldFn.getCallee(), { yieldVal, currentAsyncContext });

    // 4. Return from SM function
    builder->CreateRetVoid();

    // 5. Start next state
    builder->SetInsertPoint(nextBB);
    
    // DEBUG: Print state entry
    {
        llvm::FunctionType* printfFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, true);
        llvm::FunctionCallee printfFn = module->getOrInsertFunction("printf", printfFt);
        createCall(printfFt, printfFn.getCallee(), { builder->CreateGlobalStringPtr("SM State %d Entry\n"), llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState) });
        
        llvm::FunctionType* fflushFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fflushFn = module->getOrInsertFunction("fflush", fflushFt);
        createCall(fflushFt, fflushFn.getCallee(), { llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    }

    // The resumed value (from next()) is in currentAsyncResumedValue
    lastValue = unboxValue(currentAsyncResumedValue, node->inferredType);
}

void IRGenerator::generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName) {
    bool isGenerator = false;
    bool isAsync = false;
    std::vector<ast::Statement*> statements;

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        isGenerator = funcNode->isGenerator;
        isAsync = funcNode->isAsync;
        for (auto& stmt : funcNode->body) statements.push_back(stmt.get());
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        isGenerator = methodNode->isGenerator;
        isAsync = methodNode->isAsync;
        for (auto& stmt : methodNode->body) statements.push_back(stmt.get());
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        isAsync = arrowNode->isAsync;
        if (auto block = dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            for (auto& stmt : block->statements) statements.push_back(stmt.get());
        } else {
            // Single expression arrow function
            // We'll handle this specially in the SM generation
        }
    } else if (auto funcExpr = dynamic_cast<ast::FunctionExpression*>(node)) {
        isGenerator = funcExpr->isGenerator;
        isAsync = funcExpr->isAsync;
        for (auto& stmt : funcExpr->body) statements.push_back(stmt.get());
    }

    currentIsGenerator = isGenerator;
    currentIsAsync = isAsync;

    // 1. Collect variables
    std::vector<std::string> variables;
    std::map<std::string, std::shared_ptr<Type>> variableTypes;
    
    std::vector<VariableInfo> vars;
    anonVarCounter = 0; // Reset counter

    if (classType) {
        variables.push_back("this");
        variableTypes["this"] = classType;
    }

    std::vector<std::string> paramNames;
    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& stmt : funcNode->body) collectVariables(stmt.get(), vars);
        for (auto& param : funcNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& stmt : methodNode->body) collectVariables(stmt.get(), vars);
        for (auto& param : methodNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        if (auto block = dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            for (auto& stmt : block->statements) collectVariables(stmt.get(), vars);
        } else {
            collectVariables(arrowNode->body.get(), vars);
        }
        for (auto& param : arrowNode->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                paramNames.push_back(id->name);
            }
        }
    }
    
    for (const auto& var : vars) {
        if (std::find(variables.begin(), variables.end(), var.name) == variables.end()) {
            variables.push_back(var.name);
            variableTypes[var.name] = var.type;
        }
    }

    // Add parameters to collector with correct types
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (std::find(variables.begin(), variables.end(), paramNames[i]) == variables.end()) {
            variables.push_back(paramNames[i]);
        }
        if (i < argTypes.size()) {
            variableTypes[paramNames[i]] = argTypes[i];
        } else {
            variableTypes[paramNames[i]] = std::make_shared<Type>(TypeKind::Any);
        }
    }

    // Create Frame struct type
    std::vector<llvm::Type*> frameFields;
    std::map<std::string, int> frameMap;
    for (size_t i = 0; i < variables.size(); ++i) {
        llvm::Type* fieldType = getLLVMType(variableTypes[variables[i]]);
        frameFields.push_back(fieldType);
        frameMap[variables[i]] = i;
    }
    llvm::StructType* frameType = llvm::StructType::create(*context, specializedName + "_Frame");
    frameType->setBody(frameFields);

    // 2. Create the State Machine (SM) function
    std::string smName = specializedName + "_SM";
    std::vector<llvm::Type*> smArgTypes = { builder->getPtrTy(), builder->getPtrTy() }; // ctx, resumedValue
    llvm::FunctionType* smFt = llvm::FunctionType::get(builder->getVoidTy(), smArgTypes, false);
    llvm::Function* smFunc = llvm::Function::Create(smFt, llvm::Function::InternalLinkage, smName, module.get());
    addStackProtection(smFunc);

    // 3. Implement the Entry function
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", entryFunc);
    builder->SetInsertPoint(entryBB);

    // Create AsyncContext
    llvm::FunctionType* createCtxFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee createCtxFn = module->getOrInsertFunction("ts_async_context_create", createCtxFt);
    llvm::Value* ctx = createCall(createCtxFt, createCtxFn.getCallee(), {});
    
    // Set resumeFn
    llvm::Value* resumeFnPtr = builder->CreateStructGEP(asyncContextType, ctx, 8);
    builder->CreateStore(smFunc, resumeFnPtr);

    // Allocate Frame on heap
    llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
    llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
    uint64_t frameSize = module->getDataLayout().getTypeAllocSize(frameType);
    llvm::Value* frame = createCall(allocFt, allocFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), frameSize) });
    
    // Store Frame in ctx->data
    llvm::Value* dataPtr = builder->CreateStructGEP(asyncContextType, ctx, 9);
    builder->CreateStore(frame, dataPtr);

    // Copy arguments to frame
    auto argIt = entryFunc->arg_begin();
    argIt++; // Skip context
    if (classType && argIt != entryFunc->arg_end()) {
        int idx = frameMap["this"];
        llvm::Value* ptr = builder->CreateStructGEP(frameType, frame, idx);
        builder->CreateStore(argIt, ptr);
        argIt++;
    }

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
            if (argIt == entryFunc->arg_end()) break;
            auto param = funcNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                int idx = frameMap[id->name];
                llvm::Value* ptr = builder->CreateStructGEP(frameType, frame, idx);
                builder->CreateStore(argIt, ptr);
            }
            argIt++;
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (size_t i = 0; i < methodNode->parameters.size(); ++i) {
            if (argIt == entryFunc->arg_end()) break;
            auto param = methodNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                int idx = frameMap[id->name];
                llvm::Value* ptr = builder->CreateStructGEP(frameType, frame, idx);
                builder->CreateStore(argIt, ptr);
            }
            argIt++;
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        for (size_t i = 0; i < arrowNode->parameters.size(); ++i) {
            if (argIt == entryFunc->arg_end()) break;
            auto param = arrowNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                int idx = frameMap[id->name];
                llvm::Value* ptr = builder->CreateStructGEP(frameType, frame, idx);
                builder->CreateStore(argIt, ptr);
            }
            argIt++;
        }
    }

    // Call SM function for the first time (only for non-generators)
    if (!isGenerator) {
        llvm::Value* nullResumed = llvm::ConstantPointerNull::get(builder->getPtrTy());
        createCall(smFunc->getFunctionType(), smFunc, { ctx, nullResumed });
    }

    if (isGenerator) {
        if (isAsync) {
            llvm::FunctionType* createGenFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createGenFn = module->getOrInsertFunction("ts_async_generator_create", createGenFt);
            llvm::Value* gen = createCall(createGenFt, createGenFn.getCallee(), { ctx });

            llvm::FunctionType* makeObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee makeObjFn = module->getOrInsertFunction("ts_value_make_object", makeObjFt);
            lastValue = createCall(makeObjFt, makeObjFn.getCallee(), { gen });
        } else {
            llvm::FunctionType* createGenFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createGenFn = module->getOrInsertFunction("ts_generator_create", createGenFt);
            llvm::Value* gen = createCall(createGenFt, createGenFn.getCallee(), { ctx });

            llvm::FunctionType* makeObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee makeObjFn = module->getOrInsertFunction("ts_value_make_object", makeObjFt);
            lastValue = createCall(makeObjFt, makeObjFn.getCallee(), { gen });
        }
    } else {
        // Return the promise from the context
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, ctx, 5);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        
        llvm::FunctionType* makePromiseFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee makePromiseFn = module->getOrInsertFunction("ts_value_make_promise", makePromiseFt);
        lastValue = createCall(makePromiseFt, makePromiseFn.getCallee(), { promise });
    }
    builder->CreateRet(lastValue);

    // 4. Implement the SM function
    llvm::BasicBlock* smEntryBB = llvm::BasicBlock::Create(*context, "entry", smFunc);
    builder->SetInsertPoint(smEntryBB);

    // DEBUG: Print SM entry
    {
        llvm::FunctionType* printfFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, true);
        llvm::FunctionCallee printfFn = module->getOrInsertFunction("printf", printfFt);
        llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, smFunc->getArg(0), 1);
        llvm::Value* stateVal = builder->CreateLoad(llvm::Type::getInt32Ty(*context), statePtr);
        createCall(printfFt, printfFn.getCallee(), { builder->CreateGlobalStringPtr("SM Entry: ctx=%p state=%d resumedVal=%p\n"), smFunc->getArg(0), stateVal, smFunc->getArg(1) });
        
        llvm::FunctionType* fflushFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fflushFn = module->getOrInsertFunction("fflush", fflushFt);
        createCall(fflushFt, fflushFn.getCallee(), { llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    }

    llvm::Value* smCtx = smFunc->getArg(0);
    llvm::Value* smResumedVal = smFunc->getArg(1);

    // Save current state for SM generation
    auto oldAsyncContext = currentAsyncContext;
    auto oldAsyncResumedValue = currentAsyncResumedValue;
    auto oldAsyncFrame = currentAsyncFrame;
    auto oldAsyncFrameType = currentAsyncFrameType;
    auto oldAsyncFrameMap = currentAsyncFrameMap;
    auto oldAsyncStateBlocks = asyncStateBlocks;
    auto oldNamedValues = namedValues;
    auto oldBoxedVariables = boxedVariables;

    currentAsyncContext = smCtx;
    currentAsyncResumedValue = smResumedVal;
    boxedValues.insert(currentAsyncResumedValue);
    currentAsyncFrameType = frameType;
    currentAsyncFrameMap = frameMap;
    asyncStateBlocks.clear();

    // Load Frame from ctx->data
    llvm::Value* smDataPtr = builder->CreateStructGEP(asyncContextType, smCtx, 9);
    currentAsyncFrame = builder->CreateLoad(builder->getPtrTy(), smDataPtr);

    // Create the switch block
    llvm::BasicBlock* switchBB = llvm::BasicBlock::Create(*context, "switch", smFunc);
    builder->CreateBr(switchBB);
    builder->SetInsertPoint(switchBB);

    // Map variables in frame to namedValues in a block that dominates all states
    for (auto const& [name, idx] : frameMap) {
        namedValues[name] = builder->CreateStructGEP(frameType, currentAsyncFrame, idx);
    }

    // If this is a class method, ensure 'this' is in namedValues
    if (classType) {
        auto it = frameMap.find("this");
        if (it != frameMap.end()) {
            namedValues["this"] = builder->CreateStructGEP(frameType, currentAsyncFrame, it->second);
        }
    }

    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, smCtx, 1);
    llvm::Value* state = builder->CreateLoad(llvm::Type::getInt32Ty(*context), statePtr);

    // DEBUG: Print state before switch
    {
        llvm::FunctionType* printfFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, true);
        llvm::FunctionCallee printfFn = module->getOrInsertFunction("printf", printfFt);
        createCall(printfFt, printfFn.getCallee(), { builder->CreateGlobalStringPtr("SM Switch: state=%d\n"), state });
        
        llvm::FunctionType* fflushFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fflushFn = module->getOrInsertFunction("fflush", fflushFt);
        createCall(fflushFt, fflushFn.getCallee(), { llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    }

    llvm::BasicBlock* state0 = llvm::BasicBlock::Create(*context, "state0", smFunc);
    asyncStateBlocks.push_back(state0);

    // Start generating code for state 0
    builder->SetInsertPoint(state0);

    // DEBUG: Print state 0 entry
    {
        llvm::FunctionType* printfFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, true);
        llvm::FunctionCallee printfFn = module->getOrInsertFunction("printf", printfFt);
        createCall(printfFt, printfFn.getCallee(), { builder->CreateGlobalStringPtr("SM State 0 Entry\n") });
        
        llvm::FunctionType* fflushFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fflushFn = module->getOrInsertFunction("fflush", fflushFt);
        createCall(fflushFt, fflushFn.getCallee(), { llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    }

    // Generate the switch instruction
    builder->SetInsertPoint(switchBB);
    llvm::SwitchInst* sw = builder->CreateSwitch(state, state0);
    sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), state0);
    
    builder->SetInsertPoint(state0);

    anonVarCounter = 0;
    
    if (!statements.empty()) {
        for (auto stmt : statements) {
            visit(stmt);
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        if (!dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            visit(arrowNode->body.get());
            // Resolve promise with result
            llvm::Value* res = boxValue(lastValue, nullptr);
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 5);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
            createCall(resolveFt, resolveFn.getCallee(), { promise, res });
            builder->CreateRetVoid();
        }
    }

    // Finalize SM function
    if (!builder->GetInsertBlock()->getTerminator()) {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", undefFt);
        llvm::Value* undefinedVal = createCall(undefFt, undefFn.getCallee(), {});

        if (isAsync && !isGenerator) {
            // Resolve promise with undefined
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 5);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
            createCall(resolveFt, resolveFn.getCallee(), { promise, undefinedVal });
        } else if (isAsync && isGenerator) {
            // Resolve generator with undefined and done=true
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy(), builder->getInt1Ty() }, false);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_async_generator_resolve", resolveFt);
            createCall(resolveFt, resolveFn.getCallee(), { smCtx, undefinedVal, builder->getInt1(true) });
        }
        builder->CreateRetVoid();
    }

    // Update the switch with all generated states
    for (size_t i = 1; i < asyncStateBlocks.size(); ++i) {
        sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), i), asyncStateBlocks[i]);
    }

    // Restore state
    currentAsyncContext = oldAsyncContext;
    currentAsyncResumedValue = oldAsyncResumedValue;
    currentAsyncFrame = oldAsyncFrame;
    currentAsyncFrameType = oldAsyncFrameType;
    currentAsyncFrameMap = oldAsyncFrameMap;
    asyncStateBlocks = oldAsyncStateBlocks;
    namedValues = oldNamedValues;
    boxedVariables = oldBoxedVariables;
    currentIsGenerator = false;
    currentIsAsync = false;
}

} // namespace ts
