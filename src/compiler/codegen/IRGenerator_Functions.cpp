#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

void IRGenerator::generatePrototypes(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        std::vector<llvm::Type*> argTypes;
        for (const auto& argType : spec.argTypes) {
            argTypes.push_back(getLLVMType(argType));
        }

        llvm::Type* returnType = getLLVMType(spec.returnType);
        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, argTypes, false);

        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, spec.specializedName, module.get());
    }
}

void IRGenerator::generateBodies(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        llvm::Function* function = module->getFunction(spec.specializedName);
        if (!function) continue;

        if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (methodNode->isAbstract || !methodNode->hasBody) continue;
        }

        if (!function->empty()) continue;

        bool isAsync = false;
        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            isAsync = funcNode->isAsync;
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            isAsync = methodNode->isAsync;
        }

        if (isAsync) {
            generateAsyncFunctionBody(function, spec.node, spec.argTypes, spec.classType, spec.specializedName);
            continue;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        namedValues.clear();
        currentClass = spec.classType;
        typeEnvironment.clear();
        currentAsyncFrame = nullptr;
        currentAsyncContext = nullptr;

        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Populate type environment
            for (size_t i = 0; i < funcNode->typeParameters.size(); ++i) {
                if (i < spec.typeArguments.size()) {
                    typeEnvironment[funcNode->typeParameters[i]->name] = spec.typeArguments[i];
                }
            }

            unsigned idx = 0;
            for (auto& arg : function->args()) {
                if (idx < funcNode->parameters.size()) {
                    auto param = funcNode->parameters[idx].get();
                    if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                        arg.setName(id->name);
                    }
                    generateDestructuring(&arg, spec.argTypes[idx], param->name.get());
                }
                idx++;
            }

            for (auto& stmt : funcNode->body) {
                visit(stmt.get());
            }
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            auto argIt = function->arg_begin();
            if (!methodNode->isStatic) {
                // Handle 'this' parameter (first argument)
                if (argIt != function->arg_end()) {
                    argIt->setName("this");
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, "this", argIt->getType());
                    builder->CreateStore(&*argIt, alloca);
                    namedValues["this"] = alloca;
                    ++argIt;
                }
            }

            // Handle explicit parameters
            unsigned idx = 0;
            while (argIt != function->arg_end() && idx < methodNode->parameters.size()) {
                auto param = methodNode->parameters[idx].get();
                if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    argIt->setName(id->name);
                }
                
                unsigned argTypeIdx = methodNode->isStatic ? idx : idx + 1;
                if (argTypeIdx < spec.argTypes.size()) {
                    generateDestructuring(&*argIt, spec.argTypes[argTypeIdx], param->name.get());
                }
                
                ++argIt;
                ++idx;
            }

            for (auto& stmt : methodNode->body) {
                visit(stmt.get());
            }
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::Type* retType = function->getReturnType();
            if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                builder->CreateRet(llvm::Constant::getNullValue(retType));
            }
        }
    }
}

void IRGenerator::visitArrowFunction(ast::ArrowFunction* node) {
    static int lambdaCounter = 0;
    std::string name = "lambda_" + std::to_string(lambdaCounter++);
    
    std::vector<llvm::Type*> argTypes;
    for (auto& param : node->parameters) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::Type* retType = builder->getPtrTy(); // TsValue*
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    auto oldNamedValues = namedValues;

    if (node->isAsync) {
        std::vector<std::shared_ptr<Type>> argTypes;
        auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
        for (size_t i = 0; i < node->parameters.size(); ++i) {
            if (funcType && i < funcType->paramTypes.size()) {
                argTypes.push_back(funcType->paramTypes[i]);
            } else {
                argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
        generateAsyncFunctionBody(function, node, argTypes, nullptr, name);
        builder->SetInsertPoint(oldBB);
        namedValues = oldNamedValues;
        lastValue = function;
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);
    
    namedValues.clear();
    
    unsigned idx = 0;
    auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
    for (auto& arg : function->args()) {
        auto param = node->parameters[idx].get();
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
        }
        std::shared_ptr<Type> paramType = (funcType && idx < funcType->paramTypes.size()) ? funcType->paramTypes[idx] : std::make_shared<Type>(TypeKind::Any);
        
        // Unbox the argument
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        idx++;
    }
    
    visit(node->body.get());
    
    if (lastValue) {
        // Box the return value
        std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
        llvm::Value* boxedRet = boxValue(lastValue, returnType);
        builder->CreateRet(boxedRet);
    } else {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = function;
}

void IRGenerator::generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName) {
    // Save current async state for nested functions
    auto oldAsyncContext = currentAsyncContext;
    auto oldAsyncFrame = currentAsyncFrame;
    auto oldAsyncFrameMap = currentAsyncFrameMap;
    auto oldAsyncFrameType = currentAsyncFrameType;
    auto oldAsyncStateBlocks = asyncStateBlocks;
    auto oldAsyncDispatcherBB = asyncDispatcherBB;
    auto oldAsyncResumedValue = currentAsyncResumedValue;

    // 1. Collect variables for the frame
    std::vector<VariableInfo> frameVars;
    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
            auto param = funcNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                frameVars.push_back({id->name, argTypes[i], getLLVMType(argTypes[i])});
            }
        }
        for (auto& stmt : funcNode->body) collectVariables(stmt.get(), frameVars);
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        if (!methodNode->isStatic) {
            frameVars.push_back({"this", classType, getLLVMType(classType)});
        }
        for (size_t i = 0; i < methodNode->parameters.size(); ++i) {
            auto param = methodNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                unsigned argIdx = methodNode->isStatic ? i : i + 1;
                frameVars.push_back({id->name, argTypes[argIdx], getLLVMType(argTypes[argIdx])});
            }
        }
        for (auto& stmt : methodNode->body) collectVariables(stmt.get(), frameVars);
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        for (size_t i = 0; i < arrowNode->parameters.size(); ++i) {
            auto param = arrowNode->parameters[i].get();
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                frameVars.push_back({id->name, argTypes[i], getLLVMType(argTypes[i])});
            }
        }
        collectVariables(arrowNode->body.get(), frameVars);
    }

    // 2. Create Frame Type
    std::vector<llvm::Type*> frameTypes;
    currentAsyncFrameMap.clear();
    for (size_t i = 0; i < frameVars.size(); ++i) {
        frameTypes.push_back(frameVars[i].llvmType);
        currentAsyncFrameMap[frameVars[i].name] = i;
    }
    currentAsyncFrameType = llvm::StructType::create(*context, frameTypes, specializedName + "_frame");

    // 3. Create SM function
    std::vector<llvm::Type*> smArgTypes = { builder->getPtrTy(), builder->getPtrTy() };
    llvm::FunctionType* smFT = llvm::FunctionType::get(builder->getVoidTy(), smArgTypes, false);
    llvm::Function* smFunc = llvm::Function::Create(smFT, llvm::Function::InternalLinkage, specializedName + "_sm", module.get());
    smFunc->getArg(0)->setName("ctx");
    smFunc->getArg(1)->setName("resumedValue");

    if (!asyncContextType) {
        // Define AsyncContext struct type
        std::vector<llvm::Type*> ctxFields = {
            builder->getPtrTy(),              // vtable
            llvm::Type::getInt32Ty(*context), // state
            builder->getPtrTy(),              // promise
            builder->getPtrTy(),              // resumeFn
            builder->getPtrTy()               // frame
        };
        asyncContextType = llvm::StructType::create(*context, ctxFields, "AsyncContext");
    }

    // 4. Generate Entry Function
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", entryFunc);
    builder->SetInsertPoint(entryBB);
    
    // Name the arguments
    unsigned argIdx = 0;
    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& arg : entryFunc->args()) {
            if (argIdx < funcNode->parameters.size()) {
                if (auto id = dynamic_cast<ast::Identifier*>(funcNode->parameters[argIdx]->name.get())) {
                    arg.setName(id->name);
                }
            }
            argIdx++;
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& arg : entryFunc->args()) {
            if (argIdx == 0 && !methodNode->isStatic) {
                arg.setName("this");
            } else {
                size_t paramIdx = methodNode->isStatic ? argIdx : argIdx - 1;
                if (paramIdx < methodNode->parameters.size()) {
                    if (auto id = dynamic_cast<ast::Identifier*>(methodNode->parameters[paramIdx]->name.get())) {
                        arg.setName(id->name);
                    }
                }
            }
            argIdx++;
        }
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        for (auto& arg : entryFunc->args()) {
            if (argIdx < arrowNode->parameters.size()) {
                if (auto id = dynamic_cast<ast::Identifier*>(arrowNode->parameters[argIdx]->name.get())) {
                    arg.setName(id->name);
                }
            }
            argIdx++;
        }
    }

    llvm::FunctionCallee ctxCreateFn = module->getOrInsertFunction("ts_async_context_create", builder->getPtrTy());
    llvm::Value* ctx = builder->CreateCall(ctxCreateFn);
    
    llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", builder->getPtrTy(), llvm::Type::getInt64Ty(*context));
    uint64_t frameSize = module->getDataLayout().getTypeAllocSize(currentAsyncFrameType);
    llvm::Value* frame = builder->CreateCall(allocFn, { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), frameSize) });
    frame = builder->CreateBitCast(frame, llvm::PointerType::getUnqual(currentAsyncFrameType));

    // 2. Initialize AsyncContext
    // ctx->resumeFn = smFn
    llvm::Value* resumeFnPtr = builder->CreateStructGEP(asyncContextType, ctx, 3);
    builder->CreateStore(smFunc, resumeFnPtr);

    // ctx->frame = frame
    llvm::Value* framePtr = builder->CreateStructGEP(asyncContextType, ctx, 4);
    builder->CreateStore(frame, framePtr);

    // Store parameters into the frame
    argIdx = 0;
    for (auto& arg : entryFunc->args()) {
        std::string argName = arg.getName().str();
        if (currentAsyncFrameMap.count(argName)) {
            llvm::Value* argFramePtr = builder->CreateStructGEP(currentAsyncFrameType, frame, currentAsyncFrameMap[argName]);
            
            llvm::Value* val = &arg;
            if (auto arrow = dynamic_cast<ast::ArrowFunction*>(node)) {
                // Arrow functions always take TsValue*, so we must unbox
                auto funcType = std::static_pointer_cast<FunctionType>(arrow->inferredType);
                std::shared_ptr<Type> paramType = (funcType && argIdx < funcType->paramTypes.size()) ? funcType->paramTypes[argIdx] : std::make_shared<Type>(TypeKind::Any);
                val = unboxValue(val, paramType);
            }
            
            builder->CreateStore(val, argFramePtr);
        }
        argIdx++;
    }

    // 3. Call SM function for the first time
    builder->CreateCall(smFunc, { ctx, llvm::ConstantPointerNull::get(builder->getPtrTy()) });
    
    // Return ctx->promise
    llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, ctx, 2);
    llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
    builder->CreateRet(promise);

    // --- SM Function Body ---
    llvm::BasicBlock* smEntryBB = llvm::BasicBlock::Create(*context, "entry", smFunc);
    builder->SetInsertPoint(smEntryBB);
    
    currentAsyncContext = smFunc->getArg(0);
    currentAsyncResumedValue = smFunc->getArg(1);
    
    llvm::Value* frameLoad = builder->CreateLoad(builder->getPtrTy(), builder->CreateStructGEP(asyncContextType, currentAsyncContext, 4));
    currentAsyncFrame = builder->CreateBitCast(frameLoad, llvm::PointerType::getUnqual(currentAsyncFrameType));
    
    asyncDispatcherBB = llvm::BasicBlock::Create(*context, "dispatcher", smFunc);
    asyncStateBlocks.clear();
    asyncStateBlocks.push_back(llvm::BasicBlock::Create(*context, "state0", smFunc));
    
    builder->CreateBr(asyncDispatcherBB);
    
    // Dispatcher logic
    builder->SetInsertPoint(asyncDispatcherBB);
    llvm::Value* state = builder->CreateLoad(llvm::Type::getInt32Ty(*context), builder->CreateStructGEP(asyncContextType, currentAsyncContext, 1));
    
    llvm::SwitchInst* sw = builder->CreateSwitch(state, asyncStateBlocks[0]);
    sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), asyncStateBlocks[0]);
    
    // Start state 0
    builder->SetInsertPoint(asyncStateBlocks[0]);
    
    // Re-populate namedValues with GEPs into the frame for the SM function
    namedValues.clear();
    for (auto const& [name, index] : currentAsyncFrameMap) {
        namedValues[name] = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, index);
    }

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(node)) {
        for (auto& stmt : funcNode->body) visit(stmt.get());
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(node)) {
        for (auto& stmt : methodNode->body) visit(stmt.get());
    } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(node)) {
        visit(arrowNode->body.get());
        
        if (!builder->GetInsertBlock()->getTerminator() && lastValue) {
            // Arrow function with expression body - resolve promise with result
            llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
            llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 2);
            llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
            
            auto funcType = std::static_pointer_cast<FunctionType>(arrowNode->inferredType);
            std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
            llvm::Value* boxedRet = boxValue(lastValue, returnType);
            
            builder->CreateCall(resolveFn, { promise, boxedRet });
            builder->CreateRetVoid();
        }
    }

    if (!builder->GetInsertBlock()->getTerminator()) {
        // Implicit return undefined
        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve_internal", builder->getVoidTy(), builder->getPtrTy(), builder->getPtrTy());
        
        llvm::Value* promisePtr = builder->CreateStructGEP(asyncContextType, currentAsyncContext, 2);
        llvm::Value* promise = builder->CreateLoad(builder->getPtrTy(), promisePtr);
        
        llvm::FunctionCallee undefFn = module->getOrInsertFunction("ts_value_make_undefined", builder->getPtrTy());
        llvm::Value* undefined = builder->CreateCall(undefFn);
        
        builder->CreateCall(resolveFn, { promise, undefined });
        builder->CreateRetVoid();
    }
    
    // Update switch with all states created during visit
    for (size_t i = 1; i < asyncStateBlocks.size(); ++i) {
        sw->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), (int)i), asyncStateBlocks[i]);
    }

    // Restore async state
    currentAsyncContext = oldAsyncContext;
    currentAsyncFrame = oldAsyncFrame;
    currentAsyncFrameMap = oldAsyncFrameMap;
    currentAsyncFrameType = oldAsyncFrameType;
    asyncStateBlocks = oldAsyncStateBlocks;
    asyncDispatcherBB = oldAsyncDispatcherBB;
    currentAsyncResumedValue = oldAsyncResumedValue;
}

} // namespace ts
