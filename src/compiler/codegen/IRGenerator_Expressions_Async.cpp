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

    // 2. Save state
    int nextState = asyncStateBlocks.size();
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "state" + std::to_string(nextState), builder->GetInsertBlock()->getParent());
    asyncStateBlocks.push_back(nextBB);

    // Update ctx->state
    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1);
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), nextState), statePtr);

    // 3. Call ts_async_await
    llvm::errs() << "Calling ts_async_await\n";
    llvm::FunctionType* awaitFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee awaitFn = module->getOrInsertFunction("ts_async_await", awaitFt);
    createCall(awaitFt, awaitFn.getCallee(), { promiseVal, currentAsyncContext });

    // 4. Return from SM function
    builder->CreateRetVoid();

    // 5. Start next state
    builder->SetInsertPoint(nextBB);
    
    // Check for error
    llvm::Value* errorPtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 2);
    llvm::Value* isError = builder->CreateLoad(llvm::Type::getInt1Ty(*context), errorPtr);
    
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
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 3);
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
    lastValue = unboxValue(currentAsyncResumedValue, node->inferredType);
}

void IRGenerator::generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName) {
    llvm::errs() << "Generating async body for " << specializedName << "\n";
    llvm::errs() << "  Entry function return type: "; entryFunc->getReturnType()->print(llvm::errs()); llvm::errs() << "\n";
    
    // 1. Collect variables
    llvm::errs() << "  Collecting variables...\n";
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
    llvm::errs() << "  Collected " << vars.size() << " variables.\n";

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
    llvm::errs() << "  Creating Frame struct type...\n";
    std::vector<llvm::Type*> frameFields;
    std::map<std::string, int> frameMap;
    for (size_t i = 0; i < variables.size(); ++i) {
        frameFields.push_back(getLLVMType(variableTypes[variables[i]]));
        frameMap[variables[i]] = i;
    }
    llvm::StructType* frameType = llvm::StructType::create(*context, specializedName + "_Frame");
    frameType->setBody(frameFields);

    // 2. Create the State Machine (SM) function
    llvm::errs() << "  Creating SM function...\n";
    std::string smName = specializedName + "_SM";
    std::vector<llvm::Type*> smArgTypes = { builder->getPtrTy(), builder->getPtrTy() }; // ctx, resumedValue
    llvm::FunctionType* smFt = llvm::FunctionType::get(builder->getVoidTy(), smArgTypes, false);
    llvm::Function* smFunc = llvm::Function::Create(smFt, llvm::Function::InternalLinkage, smName, module.get());
    addStackProtection(smFunc);

    // 3. Implement the Entry function
    llvm::errs() << "  Implementing Entry function...\n";
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", entryFunc);
    builder->SetInsertPoint(entryBB);

    // Create AsyncContext
    llvm::errs() << "Calling ts_async_context_create\n";
    llvm::FunctionType* createCtxFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
    llvm::FunctionCallee createCtxFn = module->getOrInsertFunction("ts_async_context_create", createCtxFt);
    llvm::Value* ctx = createCall(createCtxFt, createCtxFn.getCallee(), {});
    
    // Set resumeFn
    llvm::Value* resumeFnPtr = builder->CreateStructGEP(asyncContextType, ctx, 4);
    builder->CreateStore(smFunc, resumeFnPtr);

    // Allocate Frame on heap
    llvm::errs() << "Calling ts_alloc\n";
    llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
    llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
    uint64_t frameSize = module->getDataLayout().getTypeAllocSize(frameType);
    llvm::Value* frame = createCall(allocFt, allocFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), frameSize) });
    
    // Store Frame in ctx->data
    llvm::Value* dataPtr = builder->CreateStructGEP(asyncContextType, ctx, 5);
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

    // Call SM function for the first time
    llvm::errs() << "Calling SM function\n";
    llvm::Value* nullResumed = llvm::ConstantPointerNull::get(builder->getPtrTy());
    
    llvm::errs() << "  SM Function Type: "; smFunc->getFunctionType()->print(llvm::errs()); llvm::errs() << "\n";
    llvm::errs() << "  ctx Type: "; ctx->getType()->print(llvm::errs()); llvm::errs() << "\n";
    llvm::errs() << "  nullResumed Type: "; nullResumed->getType()->print(llvm::errs()); llvm::errs() << "\n";
    
    createCall(smFunc->getFunctionType(), smFunc, { ctx, nullResumed });

    // Return the promise from the context
    llvm::errs() << "Calling ts_value_make_promise\n";
    llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, ctx, 3);
    llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
    
    llvm::FunctionType* makePromiseFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee makePromiseFn = module->getOrInsertFunction("ts_value_make_promise", makePromiseFt);
    
    llvm::errs() << "  makePromiseFt: "; makePromiseFt->print(llvm::errs()); llvm::errs() << "\n";
    llvm::errs() << "  promise Type: "; promise->getType()->print(llvm::errs()); llvm::errs() << "\n";

    lastValue = createCall(makePromiseFt, makePromiseFn.getCallee(), { promise });
    llvm::errs() << "  lastValue Type: "; lastValue->getType()->print(llvm::errs()); llvm::errs() << "\n";
    builder->CreateRet(lastValue);

    // 4. Implement the SM function
    llvm::errs() << "Implementing SM function: " << smName << "\n";
    llvm::BasicBlock* smEntryBB = llvm::BasicBlock::Create(*context, "entry", smFunc);
    builder->SetInsertPoint(smEntryBB);

    llvm::Value* smCtx = smFunc->getArg(0);
    llvm::Value* smResumedVal = smFunc->getArg(1);

    currentAsyncContext = smCtx;
    currentAsyncResumedValue = smResumedVal;
    currentAsyncFrameType = frameType;
    currentAsyncFrameMap = frameMap;
    asyncStateBlocks.clear();

    // Load Frame from ctx->data
    llvm::Value* smDataPtr = builder->CreateStructGEP(asyncContextType, smCtx, 5);
    currentAsyncFrame = builder->CreateLoad(builder->getPtrTy(), smDataPtr);

    // Create the switch block
    llvm::BasicBlock* switchBB = llvm::BasicBlock::Create(*context, "switch", smFunc);
    builder->CreateBr(switchBB);
    builder->SetInsertPoint(switchBB);

    llvm::Value* statePtr = builder->CreateStructGEP(asyncContextType, smCtx, 1);
    llvm::Value* state = builder->CreateLoad(llvm::Type::getInt32Ty(*context), statePtr);

    llvm::BasicBlock* state0 = llvm::BasicBlock::Create(*context, "state0", smFunc);
    asyncStateBlocks.push_back(state0);

    // Start generating code for state 0
    builder->SetInsertPoint(state0);

    auto oldNamedValues = namedValues;
    anonVarCounter = 0;
    
    // Map variables in frame to namedValues
    for (auto const& [name, idx] : frameMap) {
        llvm::Value* ptr = builder->CreateStructGEP(frameType, currentAsyncFrame, idx);
        namedValues[name] = ptr;
        if (variableTypes.count(name)) {
            auto type = variableTypes[name];
            if (type && type->kind == TypeKind::Class) {
                concreteTypes[ptr] = type;
            }
        }
    }

    // If this is a class method, ensure 'this' is in namedValues
    if (classType) {
        auto it = frameMap.find("this");
        if (it != frameMap.end()) {
            namedValues["this"] = builder->CreateStructGEP(frameType, currentAsyncFrame, it->second);
        }
    }

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& stmt : funcNode->body) {
            llvm::errs() << "Visiting statement in SM: " << stmt->getKind() << "\n";
            visit(stmt.get());
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& stmt : methodNode->body) {
            visit(stmt.get());
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        if (auto block = dynamic_cast<ast::BlockStatement*>(arrowNode->body.get())) {
            for (auto& stmt : block->statements) {
                visit(stmt.get());
            }
        } else {
            visit(arrowNode->body.get());
            // Resolve promise with result
            llvm::Value* res = boxValue(lastValue, arrowNode->inferredType);
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 3);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
            createCall(resolveFt, resolveFn.getCallee(), { promise, res });
            builder->CreateRetVoid();
        }
    }

    // Finalize SM function
    if (!builder->GetInsertBlock()->getTerminator()) {
        // Resolve promise with undefined
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, smCtx, 3);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getVoidTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", resolveFt);
        
        llvm::Value* undefinedVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
             // Already handled above for expression body
        } else {
            createCall(resolveFt, resolveFn.getCallee(), { promise, undefinedVal });
        }
        builder->CreateRetVoid();
    }

    // Now create the switch
    builder->SetInsertPoint(switchBB);
    llvm::SwitchInst* sw = builder->CreateSwitch(state, asyncStateBlocks[0], asyncStateBlocks.size());
    for (size_t i = 0; i < asyncStateBlocks.size(); ++i) {
        sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), i), asyncStateBlocks[i]);
    }

    namedValues = oldNamedValues;
    currentAsyncContext = nullptr;
    currentAsyncResumedValue = nullptr;
    currentAsyncFrame = nullptr;
    currentAsyncFrameType = nullptr;
    currentAsyncFrameMap.clear();
    llvm::errs() << "Finished generating async body for " << specializedName << "\n";
}

} // namespace ts


