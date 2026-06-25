#include "HIRToLLVM_Internal.h"

namespace ts::hir {


std::pair<int, int> HIRToLLVM::collectGeneratorCounts(HIRFunction* fn) {
    // First, count the number of yields and allocas to create resume blocks and local storage
    int yieldCount = 0;
    int allocaCount = 0;
    for (auto& block : fn->blocks) {
        for (auto& inst : block->instructions) {
            if (inst->opcode == HIROpcode::Yield || inst->opcode == HIROpcode::YieldStar) {
                yieldCount++;
            }
            if (inst->opcode == HIROpcode::Alloca) {
                allocaCount++;
            }
        }
    }
    generatorLocalCount_ = allocaCount;
    generatorNextLocalIndex_ = 0;
    return { yieldCount, allocaCount };
}

void HIRToLLVM::computeCrossYieldSpills(HIRFunction* fn, bool markerIsSuspension) {
    // ---- Cross-yield SSA liveness pre-pass ----
    // For each HIR SSA value defined before any yield, if it has at least
    // one use after that yield (linear-order approximation, safe over-set),
    // mark it for spilling. The codegen below routes every SET of a
    // spilled value through a slot in the data buffer, and every GET
    // reads from the slot — so the value survives the impl-function's
    // suspend/resume without ever crossing an LLVM basic-block edge.
    //
    // Filters:
    //   - Allocas (their result is the slot address itself, already
    //     created in impl_entry which dominates everything).
    //   - Const opcodes (re-materializable; the LLVM constant is a
    //     ConstantInt/Fp etc. which doesn't have a "defining block").
    //   - Function parameters (loaded in impl_entry, dominate everywhere).
    //   - Phi results (phi-incoming values are what need spilling).
    crossYieldSpillIds_.clear();
    crossYieldSlotOf_.clear();
    crossYieldSlotType_.clear();
    crossYieldSlotGEPs_.clear();
    // Run cross-block spill detection for ALL generators, not just those
    // with yields. 0-yield generators (e.g. gen-meth-dflt-params tests) still
    // get the state-machine impl/wrapper split, which can cause HIR-level
    // single-block SSA defs to be referenced from LLVM-level distinct
    // blocks after codegen reroutes via state-switch. Without the spill,
    // verifier reports "Instruction does not dominate all uses!".
    if (fn->isGenerator) {
        // Two over-approximations for "cross-yield-live", unioned:
        //
        //  (A) WITHIN-BLOCK yield-crossing: a value defined in some block B
        //      at instruction index Di, used in B at index Ui, with a Yield
        //      between (Di < Yi < Ui in B's instruction list). lowerYield
        //      relocates the insert point to yield_resume_N mid-block, so
        //      Ui is actually emitted in a different LLVM block from Di.
        //      This catches patterns like `'' in (yield)` and template
        //      literals containing yield.
        //
        //  (B) CROSS-BLOCK uses: a value defined in block B but used in a
        //      different block. Even if there's no yield between, the resume
        //      block(s) are reached directly from impl_entry's state-switch,
        //      so the defining LLVM block may not dominate the using one.
        //      This catches loop-header reads of pre-loop values when the
        //      loop body contains a yield.
        //
        // Filters out:
        //   - HIR Alloca results (their value IS the slot address, created in
        //     impl_entry which already dominates everything)
        //   - HIR Phi results (their incoming values are what need spilling;
        //     a phi node lives in its block and is fed by predecessors)
        //   - Function parameters and impl_entry-loaded values (loaded in
        //     impl_entry which dominates every state-switch target)
        std::unordered_map<uint32_t, HIRBlock*> defBlock;
        std::unordered_map<uint32_t, int> defIdxWithinBlock;
        std::unordered_set<uint32_t> spillCandidates;
        for (auto& block : fn->blocks) {
            int idx = 0;
            for (auto& inst : block->instructions) {
                if (inst->result) {
                    bool skipDef = inst->opcode == HIROpcode::Alloca ||
                                   inst->opcode == HIROpcode::Phi;
                    if (!skipDef) {
                        defBlock[inst->result->id] = block.get();
                        defIdxWithinBlock[inst->result->id] = idx;
                    }
                }
                ++idx;
            }
        }
        for (auto& block : fn->blocks) {
            // Pre-compute the instruction indices of Yields in this block.
            std::vector<int> yieldsInBlock;
            {
                int idx = 0;
                for (auto& inst : block->instructions) {
                    if (inst->opcode == HIROpcode::Yield ||
                        inst->opcode == HIROpcode::YieldStar) {
                        yieldsInBlock.push_back(idx);
                    } else if (markerIsSuspension &&
                               inst->opcode == HIROpcode::Call &&
                               !inst->operands.empty()) {
                        // Suspendable async generators (GEN-001 Stage 3) and
                        // eager-param sync generators also suspend at the
                        // body-started marker (state 0 -> 1).
                        if (auto* callee =
                                std::get_if<std::string>(&inst->operands[0]);
                            callee && (*callee == "ts_async_generator_body_started" ||
                                       *callee == "ts_generator_body_started")) {
                            yieldsInBlock.push_back(idx);
                        }
                    }
                    ++idx;
                }
            }
            int useIdx = 0;
            for (auto& inst : block->instructions) {
                for (auto& op : inst->operands) {
                    if (auto* opVal = std::get_if<std::shared_ptr<HIRValue>>(&op)) {
                        if (!*opVal) continue;
                        uint32_t id = (*opVal)->id;
                        auto dbIt = defBlock.find(id);
                        if (dbIt == defBlock.end()) continue;  // param/phi/alloca — skip
                        if (dbIt->second != block.get()) {
                            // (B) Cross-block use
                            spillCandidates.insert(id);
                        } else {
                            // (A) Same-block: check yield-crossing
                            int defIdx = defIdxWithinBlock[id];
                            for (int yIdx : yieldsInBlock) {
                                if (defIdx < yIdx && yIdx < useIdx) {
                                    spillCandidates.insert(id);
                                    break;
                                }
                            }
                        }
                    }
                }
                ++useIdx;
            }
        }
        for (uint32_t id : spillCandidates) {
            size_t slot = crossYieldSpillIds_.size();
            crossYieldSpillIds_.insert(id);
            crossYieldSlotOf_[id] = slot;
        }
    }
}

void HIRToLLVM::emitGeneratorWrapper(HIRFunction* fn, llvm::Function* llvmFunc,
                                     const GeneratorLoweringOpts& opts) {
    size_t crossYieldSpillCount = crossYieldSpillIds_.size();
    int allocaCount = generatorLocalCount_;

    // Now set up the wrapper function (the original function)
    // It creates AsyncContext, sets resumeFn, creates Generator, and returns it
    llvm::BasicBlock* wrapperEntry = llvm::BasicBlock::Create(context_, "wrapper_entry", llvmFunc);
    builder_->SetInsertPoint(wrapperEntry);

    // Create AsyncContext
    llvm::FunctionType* createCtxFt = llvm::FunctionType::get(
        getGCPtrTy(), {}, false);
    llvm::FunctionCallee createCtxFn = module_->getOrInsertFunction(
        "ts_async_context_create", createCtxFt);
    llvm::Value* asyncCtx = builder_->CreateCall(createCtxFt, createCtxFn.getCallee(), {}, "async_ctx");

    // Set ctx->resumeFn = impl function
    // AsyncContext layout: { TsObject base (16 bytes), int state (4), bool error (1), bool yielded (1), 2 padding, TsValue yieldedValue (16), ... }
    // state is at offset 16, error at 20, yielded at 21, yieldedValue at 24, promise at 40, pendingNextPromise at 48, generator at 56, resumeFn at 64
    // Actually let's use ts_async_context_set_resume_fn(ctx, fn) for safety
    llvm::FunctionType* setResumeFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), getGCPtrTy() },
        false
    );
    llvm::FunctionCallee setResumeFn = module_->getOrInsertFunction(
        "ts_async_context_set_resume_fn", setResumeFt);
    builder_->CreateCall(setResumeFt, setResumeFn.getCallee(), { asyncCtx, generatorImplFunc_ });

    // Capture `this` so generator-method calls bind it correctly per
    // ECMA-262. Without this, `this` references inside the generator
    // body see whatever the .next() caller's `this` happens to be
    // (typically globalThis). The wrapper runs with the original
    // receiver still in the call-this slot (set by the method dispatch
    // path before invoking us); snapshot it now and restore on each
    // resume in TsGenerator::next().
    {
        llvm::FunctionType* getThisFt = llvm::FunctionType::get(
            getGCPtrTy(), {}, false);
        llvm::FunctionCallee getThisFn = module_->getOrInsertFunction(
            "ts_get_call_this", getThisFt);
        llvm::Value* capturedThis = builder_->CreateCall(
            getThisFt, getThisFn.getCallee(), {}, "captured_this");
        llvm::FunctionType* setThisFt = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        llvm::FunctionCallee setThisFn = module_->getOrInsertFunction(
            "ts_async_context_set_this", setThisFt);
        builder_->CreateCall(setThisFt, setThisFn.getCallee(),
            { asyncCtx, capturedThis });
    }

    // Store function parameters (and reserve space for locals) in ctx->data
    {
        // Allocate a buffer for params + locals + cross-yield spill slots (8 bytes each)
        size_t numParams = fn->params.empty() ? 0 : fn->params.size();
        size_t totalSlots = numParams + allocaCount + crossYieldSpillCount;
        llvm::FunctionType* allocFt = llvm::FunctionType::get(
            getGCPtrTy(), { builder_->getInt64Ty() }, false);
        llvm::FunctionCallee allocFn = module_->getOrInsertFunction("ts_alloc", allocFt);
        llvm::Value* paramBuf = builder_->CreateCall(allocFt, allocFn.getCallee(),
            { llvm::ConstantInt::get(builder_->getInt64Ty(), std::max(totalSlots, (size_t)1) * 8) }, "param_buf");

        // Store each parameter into the buffer (if any)
        if (numParams > 0) {
        auto wrapperArgIt = llvmFunc->arg_begin();
        // Skip implicit closure param if present
        bool hasHiddenClosure = (!fn->params.empty() && fn->params[0].first == "__closure__");
        bool hasImplicitClosure = !fn->captures.empty() && !hasHiddenClosure;
        if (hasImplicitClosure) ++wrapperArgIt;

        for (size_t i = 0; i < numParams; ++i, ++wrapperArgIt) {
            llvm::Value* paramVal = &*wrapperArgIt;
            // Convert non-pointer types to pointer-sized integer for storage
            if (!paramVal->getType()->isPointerTy()) {
                if (paramVal->getType()->isDoubleTy()) {
                    paramVal = builder_->CreateBitCast(paramVal, builder_->getInt64Ty());
                    paramVal = builder_->CreateIntToPtr(paramVal, getGCPtrTy());
                } else if (paramVal->getType()->isIntegerTy()) {
                    if (paramVal->getType()->getIntegerBitWidth() < 64) {
                        paramVal = builder_->CreateZExt(paramVal, builder_->getInt64Ty());
                    }
                    paramVal = builder_->CreateIntToPtr(paramVal, getGCPtrTy());
                }
            }
            llvm::Value* slotPtr = builder_->CreateGEP(getGCPtrTy(), paramBuf,
                { llvm::ConstantInt::get(builder_->getInt64Ty(), i) }, "param_slot_" + std::to_string(i));
            builder_->CreateStore(paramVal, slotPtr);
        }
        } // end if (numParams > 0)

        // Set ctx->data = paramBuf
        llvm::FunctionType* setDataFt = llvm::FunctionType::get(
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee setDataFn = module_->getOrInsertFunction("ts_async_context_set_data", setDataFt);
        builder_->CreateCall(setDataFt, setDataFn.getCallee(), { asyncCtx, paramBuf });
    }

    // Create Generator
    // NOTE (GEN-001 Stage 3): for suspendable async generators the ctx
    // argument is passed exactly like the sync ts_generator_create(ctx) call.
    // The current Stage-2 runtime declares ts_async_generator_create_suspendable
    // as zero-arg and creates its OWN AsyncContext internally — the trivial
    // follow-up is to give it the (AsyncContext*) parameter and use it (mirror
    // of ts_generator_create), at which point gen->ctx IS this wrapper's ctx
    // and the next()/throw()/return() drive resumes the impl emitted below.
    // No compiler change is needed when that lands; the call site already
    // passes the ctx.
    llvm::FunctionType* createGeneratorFt = llvm::FunctionType::get(
        getGCPtrTy(), { getGCPtrTy() }, false);
    llvm::FunctionCallee createGeneratorFn = module_->getOrInsertFunction(
        opts.createGenFn, createGeneratorFt);
    llvm::Value* generator = rawToGCPtr(builder_->CreateCall(createGeneratorFt, createGeneratorFn.getCallee(), { asyncCtx }, "generator"));

    if (opts.isAsyncGen || opts.eagerSyncParams) {
        // GEN-001 D5: ONE synchronous invocation of the impl at gen() time.
        // State 0 runs the parameter prologue and suspends at the body-started
        // marker (state transition 0 -> 1), so parameter-binding throws escape
        // gen() synchronously while the body proper stays lazy until the first
        // next(). For sync generators a throw here propagates straight out of
        // the wrapper (no impl barrier is pushed in state 0).
        builder_->CreateCall(generatorImplFunc_->getFunctionType(),
                             generatorImplFunc_, { asyncCtx });
    }

    // Return the generator immediately (don't execute the body)
    builder_->CreateRet(gcPtrToRaw(generator));
}

void HIRToLLVM::emitGeneratorImplPrologue(HIRFunction* fn,
                                          const GeneratorLoweringOpts& opts,
                                          int yieldCount) {
    size_t crossYieldSpillCount = crossYieldSpillIds_.size();
    int allocaCount = generatorLocalCount_;

    // Now build the implementation function (state machine)
    currentFunction_ = generatorImplFunc_;
    llvm::Argument* ctxArg = generatorImplFunc_->getArg(0);
    ctxArg->setName("ctx");
    asyncContext_ = ctxArg;

    // Create entry block with state dispatch
    llvm::BasicBlock* implEntry = llvm::BasicBlock::Create(context_, "impl_entry", generatorImplFunc_);
    builder_->SetInsertPoint(implEntry);

    // Load state: ctx->state (offset 16 in AsyncContext after TsObject base)
    llvm::FunctionType* getStateFt = llvm::FunctionType::get(
        builder_->getInt32Ty(),
        { getGCPtrTy() },
        false
    );
    llvm::FunctionCallee getStateFn = module_->getOrInsertFunction(
        "ts_async_context_get_state", getStateFt);
    llvm::Value* state = builder_->CreateCall(getStateFt, getStateFn.getCallee(), { asyncContext_ }, "state");

    // Load ctx->data buffer (contains params + locals storage)
    // This is loaded in impl_entry which dominates all blocks, so it's available everywhere
    {
        llvm::FunctionType* getDataFt = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy() }, false);
        llvm::FunctionCallee getDataFn = module_->getOrInsertFunction("ts_async_context_get_data", getDataFt);
        generatorDataBuf_ = builder_->CreateCall(getDataFt, getDataFn.getCallee(), { asyncContext_ }, "data_buf");

        // Load function parameters from the buffer
        for (size_t i = 0; i < fn->params.size(); ++i) {
            llvm::Value* slotPtr = builder_->CreateGEP(getGCPtrTy(), generatorDataBuf_,
                { llvm::ConstantInt::get(builder_->getInt64Ty(), i) }, "param_slot_" + std::to_string(i));
            llvm::Value* paramVal = builder_->CreateLoad(getGCPtrTy(), slotPtr, fn->params[i].first + "_loaded");

            // Convert back from pointer to the expected type
            auto& paramType = fn->params[i].second;
            if (paramType && paramType->kind == HIRTypeKind::Float64) {
                paramVal = builder_->CreatePtrToInt(paramVal, builder_->getInt64Ty());
                paramVal = builder_->CreateBitCast(paramVal, builder_->getDoubleTy());
            } else if (paramType && paramType->kind == HIRTypeKind::Int64) {
                paramVal = builder_->CreatePtrToInt(paramVal, builder_->getInt64Ty());
            } else if (paramType && paramType->kind == HIRTypeKind::Bool) {
                paramVal = builder_->CreatePtrToInt(paramVal, builder_->getInt64Ty());
                paramVal = builder_->CreateTrunc(paramVal, builder_->getInt1Ty());
            }
            // For ptr/object/any/string types, the value is already a pointer

            valueMap_[static_cast<uint32_t>(i)] = paramVal;
        }

        // Pre-create GEPs for all local variable slots in impl_entry
        // This ensures they dominate all uses in any block
        generatorLocalSlots_.clear();
        size_t numParams = fn->params.size();
        for (int i = 0; i < allocaCount; ++i) {
            size_t slotIndex = numParams + i;
            llvm::Value* slotPtr = builder_->CreateGEP(getGCPtrTy(), generatorDataBuf_,
                { llvm::ConstantInt::get(builder_->getInt64Ty(), slotIndex) },
                "gen_local_" + std::to_string(i));
            generatorLocalSlots_.push_back(slotPtr);
        }

        // Pre-create GEPs for cross-yield SSA spill slots. Indexed by
        // crossYieldSlotOf_[hir_value_id]. Created in impl_entry so they
        // dominate every block reachable from the state-switch.
        crossYieldSlotGEPs_.assign(crossYieldSpillCount, nullptr);
        for (const auto& [id, slot] : crossYieldSlotOf_) {
            size_t slotIndex = numParams + allocaCount + slot;
            llvm::Value* slotPtr = builder_->CreateGEP(getGCPtrTy(), generatorDataBuf_,
                { llvm::ConstantInt::get(builder_->getInt64Ty(), slotIndex) },
                "gen_xy_" + std::to_string(id));
            crossYieldSlotGEPs_[slot] = slotPtr;
        }
    }

    // GEN-001 Stage 3 (D6): per-invocation exception barrier for suspendable
    // async generators. Pushed ONLY for resumed invocations (state >= 1): an
    // uncaught throw in body code rejects the current request's promise via
    // ts_agen_complete_reject. The state-0 (parameter prologue) invocation
    // does NOT push it, so param-binding throws escape gen() synchronously.
    // Pop balance: the state-0 suspend edge (the body-started marker) emits
    // NO pop; every other suspend/return edge (yields, yield* element
    // suspends, returns, forced-return, generator_done) pops unconditionally —
    // body code only ever runs in state>=1 invocations, which always pushed.
    llvm::BasicBlock* agenDispatchBB = nullptr;
    if (opts.isAsyncGen) {
        llvm::BasicBlock* barrierBB = llvm::BasicBlock::Create(
            context_, "agen_barrier", generatorImplFunc_);
        llvm::BasicBlock* rejectBB = llvm::BasicBlock::Create(
            context_, "agen_impl_reject", generatorImplFunc_);
        agenDispatchBB = llvm::BasicBlock::Create(
            context_, "agen_dispatch", generatorImplFunc_);

        // setjmp semantics require the impl frame to stay valid for longjmp.
        generatorImplFunc_->addFnAttr(llvm::Attribute::NoInline);

        llvm::Value* isResumed = builder_->CreateICmpNE(
            state, builder_->getInt32(0), "agen_is_resumed");
        builder_->CreateCondBr(isResumed, barrierBB, agenDispatchBB);

        builder_->SetInsertPoint(barrierBB);
        auto pushFn = getOrDeclareRuntimeFunction("ts_push_exception_handler",
            getGCPtrTy(), {});
        llvm::Value* jmpBuf = builder_->CreateCall(pushFn, {});
#ifdef _WIN32
        // Win64 two-arg form: _setjmp(jmp_buf, frame_ptr). The frame pointer
        // is REQUIRED for SEH integration (NULL aborts the longjmp).
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(), { getGCPtrTy(), getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto frameAddrFn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::frameaddress, { getGCPtrTy() });
        llvm::Value* framePtr = builder_->CreateCall(
            frameAddrFn, { builder_->getInt32(0) });
        auto* setjmpCall = builder_->CreateCall(setjmpFn, { jmpBuf, framePtr });
        setjmpCall->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCall;
#else
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(), { getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto* setjmpCallPosix = builder_->CreateCall(setjmpFn, { jmpBuf });
        setjmpCallPosix->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCallPosix;
#endif
        llvm::Value* isException = builder_->CreateICmpNE(setjmpResult,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));
        builder_->CreateCondBr(isException, rejectBB, agenDispatchBB);

        // agen_impl_reject: ts_throw already popped this handler before the
        // longjmp. Reject the current request's promise, mark the generator
        // done, clear the exception slot, and return (impl is void).
        builder_->SetInsertPoint(rejectBB);
        auto getExcFn = getOrDeclareRuntimeFunction("ts_get_exception",
            getGCPtrTy(), {});
        llvm::Value* exc = builder_->CreateCall(getExcFn, {});
        auto rejectFn = getOrDeclareRuntimeFunction("ts_agen_complete_reject",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(rejectFn, { asyncContext_, exc });
        auto clearExcFn = getOrDeclareRuntimeFunction("ts_set_exception",
            builder_->getVoidTy(), { getGCPtrTy() });
        builder_->CreateCall(clearExcFn,
            { llvm::ConstantPointerNull::get(getGCPtrTy()) });
        builder_->CreateRetVoid();

        // The state-dispatch switch below is emitted into agen_dispatch.
        builder_->SetInsertPoint(agenDispatchBB);
    }

    // Create blocks for each state
    // State 0 = initial (start of generator)
    // State 1..N = resume after yield 1..N
    // State N+1 = done (generator finished)

    // Create LLVM basic blocks for HIR blocks (these go in the impl function)
    for (auto& block : fn->blocks) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context_, block->label, generatorImplFunc_);
        blockMap_[block.get()] = bb;
    }

    // Create the done block
    generatorDoneBlock_ = llvm::BasicBlock::Create(context_, "generator_done", generatorImplFunc_);

    // Create resume blocks for each yield point (state 1, 2, 3, ...)
    for (int i = 0; i < yieldCount; i++) {
        llvm::BasicBlock* resumeBlock = llvm::BasicBlock::Create(
            context_,
            "yield_resume_" + std::to_string(i + 1),
            generatorImplFunc_
        );
        yieldResumeBlocks_.push_back(resumeBlock);
    }

    // Create the state dispatch switch
    llvm::BasicBlock* firstHIRBlock = fn->blocks.empty() ? generatorDoneBlock_ : blockMap_[fn->blocks[0].get()];
    llvm::SwitchInst* stateSwitch = builder_->CreateSwitch(state, generatorDoneBlock_, yieldCount + 1);

    // State 0 -> start of generator (first HIR block)
    stateSwitch->addCase(builder_->getInt32(0), firstHIRBlock);

    // States 1..N -> resume after corresponding yield
    for (int i = 0; i < yieldCount; i++) {
        stateSwitch->addCase(builder_->getInt32(i + 1), yieldResumeBlocks_[i]);
    }

    // Build the done block - just return without setting yielded.
    // Impl functions are normally void, but match the actual return type
    // defensively to avoid a verifier mismatch if the function was
    // declared with a non-void return (some derived-class super-prop
    // paths route a non-impl function through here — see superPropOrdering).
    builder_->SetInsertPoint(generatorDoneBlock_);
    {
        if (opts.isAsyncGen) {
            // generator_done is the switch default — only reachable with an
            // out-of-range state, which is necessarily != 0, so the impl
            // barrier was pushed this invocation. Pop it to keep the handler
            // stack balanced (the runtime's drive safety-net settles the
            // request promise).
            auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
                builder_->getVoidTy(), {});
            builder_->CreateCall(popFn, {});
        }
        llvm::Type* retTy = currentFunction_->getReturnType();
        if (retTy->isVoidTy()) {
            builder_->CreateRetVoid();
        } else {
            builder_->CreateRet(llvm::UndefValue::get(retTy));
        }
    }
}

llvm::BasicBlock* HIRToLLVM::getOrCreateAgenForcedReturnBlock() {
    if (agenForcedReturnBB_) {
        return agenForcedReturnBB_;
    }
    llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
    agenForcedReturnBB_ = llvm::BasicBlock::Create(
        context_, "agen_forced_return", generatorImplFunc_);
    builder_->SetInsertPoint(agenForcedReturnBB_);

    // gen.return(v) resumed us with mode 2: complete the generator with the
    // argument (resumedValue carries it for all modes). finally-block
    // execution is a later refinement (GEN-001 Stage 6), matching sync gens.
    auto getResumedFn = getOrDeclareRuntimeFunction(
        "ts_async_context_get_resumed_value", getGCPtrTy(), { getGCPtrTy() });
    llvm::Value* retVal = builder_->CreateCall(
        getResumedFn, { asyncContext_ }, "agen_forced_ret_val");
    auto completeFn = getOrDeclareRuntimeFunction("ts_agen_complete",
        builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
    builder_->CreateCall(completeFn, { asyncContext_, retVal });

    // Reached only from a resume-mode dispatch (state >= 1 invocation), so
    // the impl-entry barrier is pushed — pop it before suspending for good.
    auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
        builder_->getVoidTy(), {});
    builder_->CreateCall(popFn, {});
    builder_->CreateRetVoid();
    return agenForcedReturnBB_;
}

llvm::Value* HIRToLLVM::emitAgenResumeModeDispatch(
    const std::vector<HIRBlock*>& tryCatchTargets) {
    // Builder is positioned at the start of a yield resume block.
    auto getModeFn = getOrDeclareRuntimeFunction(
        "ts_async_context_get_resume_mode",
        builder_->getInt32Ty(), { getGCPtrTy() });
    llvm::Value* mode = builder_->CreateCall(
        getModeFn, { asyncContext_ }, "agen_resume_mode");

    llvm::Function* implFunc = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* throwBB = llvm::BasicBlock::Create(
        context_, "agen_resume_throw", implFunc);
    llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(
        context_, "agen_resume_next", implFunc);

    llvm::SwitchInst* sw = builder_->CreateSwitch(mode, nextBB, 2);
    sw->addCase(builder_->getInt32(1), throwBB);
    sw->addCase(builder_->getInt32(2), getOrCreateAgenForcedReturnBlock());

    auto getResumedFn = getOrDeclareRuntimeFunction(
        "ts_async_context_get_resumed_value", getGCPtrTy(), { getGCPtrTy() });

    // mode 1 (gen.throw): re-arm the user try handlers enclosing this yield
    // (GEN-001 Stage 6), then raise the argument at the suspension point —
    // caught by the innermost re-armed handler. With no enclosing user try it
    // walks to the impl-entry barrier and rejects the current request's
    // promise.
    builder_->SetInsertPoint(throwBB);
    emitRearmTryHandlers(tryCatchTargets);
    llvm::Value* throwArg = builder_->CreateCall(
        getResumedFn, { asyncContext_ }, "agen_throw_arg");
    auto throwFn = getOrDeclareRuntimeFunction("ts_throw",
        builder_->getVoidTy(), { getGCPtrTy() });
    builder_->CreateCall(throwFn, { throwArg });
    builder_->CreateUnreachable();

    // mode 0 (next): re-arm the enclosing user try handlers so body code after
    // the resume is protected again, then the yield expression's value is the
    // resumed value. (mode 2 takes the forced-return block directly with NO
    // re-arm — that path pops only the impl barrier, keeping the pop balance.)
    builder_->SetInsertPoint(nextBB);
    emitRearmTryHandlers(tryCatchTargets);
    return builder_->CreateCall(getResumedFn, { asyncContext_ }, "resumed_value");
}

void HIRToLLVM::lowerFunction(HIRFunction* fn) {
    SPDLOG_INFO("Lowering function: {}", fn->mangledName);

    // Check if the function already has a hidden closure parameter from ASTToHIR
    // Arrow functions and function expressions add __closure__ as first param for call_indirect
    bool hasHiddenClosureParam = (!fn->params.empty() && fn->params[0].first == "__closure__");

    // Get the forward-declared function (or create it if not yet declared)
    llvm::Function* llvmFunc = module_->getFunction(fn->mangledName);
    if (!llvmFunc) {
        // Function wasn't forward-declared, create it now
        // For async and generator functions, the return type is always ptr (Promise*/Generator*)
        llvm::Type* returnType = (fn->isAsync || fn->isGenerator) ? getGCPtrTy() : getLLVMType(fn->returnType);

        // Check for void return type with non-void Return instructions
        if (returnType->isVoidTy()) {
            for (auto& block : fn->blocks) {
                for (auto& inst : block->instructions) {
                    if (inst->opcode == HIROpcode::Return) {
                        returnType = getGCPtrTy();
                        goto lowerReturnTypeFixed;
                    }
                }
            }
            lowerReturnTypeFixed:;
        }

        std::vector<llvm::Type*> paramTypes;

        // Only add implicit closure param if function has captures AND doesn't already have one
        if (!fn->captures.empty() && !hasHiddenClosureParam) {
            paramTypes.push_back(getGCPtrTy());  // TsClosure* __closure
        }

        for (auto& param : fn->params) {
            paramTypes.push_back(getLLVMType(param.second));
        }

        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            fn->mangledName,
            module_.get()
        );
        if (enableGCStatepoints_) {
            llvmFunc->setGC("ts-aot-gc");
        }
    }

    // If function was forward-declared but doesn't have GC attr yet, add it
    if (enableGCStatepoints_ && !llvmFunc->hasGC()) {
        llvmFunc->setGC("ts-aot-gc");
    }

    // Has closure context if captures exist, either from implicit param or explicit __closure__ param
    bool hasCaptureParams = !fn->captures.empty();

    // Set current function
    currentFunction_ = llvmFunc;
    currentHIRFunction_ = fn;
    isAsyncFunction_ = fn->isAsync;
    isGeneratorFunction_ = fn->isGenerator;

    // Clear stale debug location from previous function
    if (emitDebugInfo_) {
        builder_->SetCurrentDebugLocation(llvm::DebugLoc());
    }
    stackAllocCount_ = 0;
    stackAllocBytes_ = 0;
    valueMap_.clear();
    gcPinAllocas_.clear();
    blockMap_.clear();
    closureParam_ = nullptr;
    capturedVarCells_.clear();
    capturedVarCellSlots_.clear();
    flatObjectShapes_.clear();
    scalarReplacedObjects_.clear();
    asyncPromise_ = nullptr;
    generatorObject_ = nullptr;
    asyncContext_ = nullptr;
    currentYieldState_ = 0;
    yieldResumeBlocks_.clear();
    generatorDoneBlock_ = nullptr;
    generatorImplFunc_ = nullptr;
    inSuspendableAgenMode_ = false;
    agenForcedReturnBB_ = nullptr;
    agenSuspendRelocation_.clear();

    // GEN-001 Stage 3: suspendable async-generator lowering (flag-gated by
    // TSAOT_SUSPEND_AGEN=1). Reuses the sync state-machine helpers with
    // isAsyncGen options; the eager branch below stays compiled verbatim for
    // flag-off.
    //
    // Stage 4b: the suspendable model REQUIRES the ts_async_generator_body_
    // started marker (suspension point 0 — the param-prologue/body split).
    // Class-method bodies are lowered by the spec MethodDefinition path in
    // ASTToHIR, which does NOT emit the marker; without it the whole body
    // would run synchronously in state 0 at gen() time — no barrier pushed
    // (protocol TypeErrors escaped uncaught) and no pendingNextPromise
    // (yields dropped, "$DONE never called"): the dominant -447 of the Stage
    // 4 flag-on sweep. Marker-less async gens take the EAGER lowering below
    // (exact flag-off behavior) until the marker gains a class-method
    // emission site in its own gated stage.
    int agenMarkerCount = 0;
    if (suspendAsyncGen_ && fn->isAsync && fn->isGenerator) {
        for (auto& block : fn->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Call && !inst->operands.empty()) {
                    if (auto* callee = std::get_if<std::string>(&inst->operands[0]);
                        callee && *callee == "ts_async_generator_body_started") {
                        agenMarkerCount++;
                    }
                }
            }
        }
    }
    if (suspendAsyncGen_ && fn->isAsync && fn->isGenerator && agenMarkerCount > 0) {
        currentYieldState_ = 0;
        yieldResumeBlocks_.clear();
        generatorDoneBlock_ = nullptr;
        generatorImplFunc_ = nullptr;
        asyncContext_ = nullptr;
        inSuspendableAgenMode_ = true;

        GeneratorLoweringOpts opts;
        opts.isAsyncGen = true;
        opts.createGenFn = "ts_async_generator_create_suspendable";

        // Count yields/allocas as for sync gens, then add the
        // ts_async_generator_body_started marker(s) as suspension points:
        // the marker consumes the state-0 -> state-1 transition (param
        // prologue suspends there), so yields occupy states 2..N+1 and the
        // impl needs yieldCount + markerCount resume blocks.
        int yieldCount = collectGeneratorCounts(fn).first;
        yieldCount += agenMarkerCount;

        // Cross-suspension SSA liveness — the marker is a suspension point
        // too, so defs crossing it within a block must spill.
        computeCrossYieldSpills(fn, /*markerIsSuspension=*/true);

        // Create the implementation function (state machine)
        // Signature: void impl(AsyncContext* ctx)
        std::string implName = fn->mangledName + "$impl";
        llvm::FunctionType* implFuncType = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy() },  // AsyncContext*
            false
        );
        generatorImplFunc_ = llvm::Function::Create(
            implFuncType,
            llvm::Function::InternalLinkage,
            implName,
            module_.get()
        );
        if (enableGCStatepoints_) {
            generatorImplFunc_->setGC("ts-aot-gc");
        }

        // Wrapper: AsyncContext + resumeFn + `this` capture + data buffer +
        // suspendable generator create + ONE synchronous impl invocation
        // (param prologue, suspends at the marker) + ret gen.
        emitGeneratorWrapper(fn, llvmFunc, opts);

        // Impl entry: state load, data reloads, slot GEPs, the conditional
        // per-invocation exception barrier (D6) and the state switch.
        emitGeneratorImplPrologue(fn, opts, yieldCount);

        // Lower each HIR block in the impl function
        for (auto& block : fn->blocks) {
            lowerBlock(block.get());
        }

        // Restore state
        currentFunction_ = nullptr;
        currentHIRFunction_ = nullptr;
        isAsyncFunction_ = false;
        isGeneratorFunction_ = false;
        inSuspendableAgenMode_ = false;
        agenForcedReturnBB_ = nullptr;
        agenSuspendRelocation_.clear();
        closureParam_ = nullptr;
        asyncPromise_ = nullptr;
        generatorObject_ = nullptr;
        asyncContext_ = nullptr;
        generatorImplFunc_ = nullptr;
        generatorDataBuf_ = nullptr;
        generatorLocalCount_ = 0;
        generatorNextLocalIndex_ = 0;
        generatorLocalSlots_.clear();
        crossYieldSpillIds_.clear();
        crossYieldSlotOf_.clear();
        crossYieldSlotType_.clear();
        crossYieldSlotGEPs_.clear();
        return;  // Suspendable agen fully lowered
    }

    // For async generator functions (both isAsync and isGenerator), create an AsyncGenerator
    if (fn->isAsync && fn->isGenerator) {
        // Create entry block for async generator setup
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "async_gen_entry", llvmFunc);
        builder_->SetInsertPoint(entryBB);

        // Create an AsyncGenerator: ts_async_generator_create() -> TsAsyncGenerator*
        llvm::FunctionType* createAsyncGenFt = llvm::FunctionType::get(
            getGCPtrTy(), {}, false);
        llvm::FunctionCallee createAsyncGenFn = module_->getOrInsertFunction(
            "ts_async_generator_create", createAsyncGenFt);
        generatorObject_ = builder_->CreateCall(createAsyncGenFt, createAsyncGenFn.getCallee(), {}, "async_generator");
        // Note: async generators don't use asyncPromise_ - they yield Promises directly
    }
    // For async functions (not generators), create a Promise
    else if (fn->isAsync) {
        // Create entry block for async setup
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "async_entry", llvmFunc);
        builder_->SetInsertPoint(entryBB);

        // Create a Promise: ts_promise_create() -> TsPromise*
        llvm::FunctionType* createPromiseFt = llvm::FunctionType::get(
            getGCPtrTy(), {}, false);
        llvm::FunctionCallee createPromiseFn = module_->getOrInsertFunction(
            "ts_promise_create", createPromiseFt);
        asyncPromise_ = rawToGCPtr(builder_->CreateCall(createPromiseFt, createPromiseFn.getCallee(), {}, "promise"));
    }
    // For generator functions (not async), create a state machine
    else if (fn->isGenerator) {
        // Reset generator state tracking
        currentYieldState_ = 0;
        yieldResumeBlocks_.clear();
        generatorDoneBlock_ = nullptr;
        generatorImplFunc_ = nullptr;
        asyncContext_ = nullptr;

        // Stage 1 (GEN-001): sync-only lowering options.
        GeneratorLoweringOpts opts;

        // Count yields and allocas to size resume blocks and local storage
        // (also sets generatorLocalCount_ / generatorNextLocalIndex_).
        int yieldCount = collectGeneratorCounts(fn).first;

        // Eager-parameter sync generators (dstr/dflt-params family): a
        // ts_generator_body_started marker after the parameter prologue is an
        // extra suspension point (state 0 -> 1). When present, the wrapper
        // invokes the impl once at gen() time so parameter throws escape gen()
        // synchronously; the body stays lazy until the first next(). Gated on
        // marker presence so markerless sync generators are unchanged.
        int syncMarkerCount = 0;
        for (auto& block : fn->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Call && !inst->operands.empty()) {
                    if (auto* callee = std::get_if<std::string>(&inst->operands[0]);
                        callee && *callee == "ts_generator_body_started") {
                        syncMarkerCount++;
                    }
                }
            }
        }
        opts.eagerSyncParams = (syncMarkerCount > 0);
        yieldCount += syncMarkerCount;

        // Cross-yield SSA liveness pre-pass (populates crossYieldSpillIds_ /
        // crossYieldSlotOf_; see the helper for the full algorithm notes). The
        // marker is a suspension point too when eager params are on.
        computeCrossYieldSpills(fn, /*markerIsSuspension=*/opts.eagerSyncParams);

        // Create the implementation function (state machine)
        // Signature: void impl(AsyncContext* ctx)
        std::string implName = fn->mangledName + "$impl";
        llvm::FunctionType* implFuncType = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy() },  // AsyncContext*
            false
        );
        generatorImplFunc_ = llvm::Function::Create(
            implFuncType,
            llvm::Function::InternalLinkage,
            implName,
            module_.get()
        );
        if (enableGCStatepoints_) {
            generatorImplFunc_->setGC("ts-aot-gc");
        }

        // Emit the wrapper function body: AsyncContext + resumeFn + `this`
        // capture + param/local/spill data buffer + generator create + ret.
        emitGeneratorWrapper(fn, llvmFunc, opts);

        // Emit the impl-function entry: state load, data-buffer reload,
        // local/spill slot GEPs, HIR/resume block creation, state switch
        // and the generator_done block.
        emitGeneratorImplPrologue(fn, opts, yieldCount);

        // Lower each HIR block in the impl function
        for (auto& block : fn->blocks) {
            lowerBlock(block.get());
        }

        // Restore state
        currentFunction_ = llvmFunc;
        asyncContext_ = nullptr;
        generatorImplFunc_ = nullptr;

        // Skip the normal block lowering below since we already did it
        currentFunction_ = nullptr;
        currentHIRFunction_ = nullptr;
        isAsyncFunction_ = false;
        isGeneratorFunction_ = false;
        closureParam_ = nullptr;
        asyncPromise_ = nullptr;
        generatorObject_ = nullptr;
        generatorDataBuf_ = nullptr;
        generatorLocalCount_ = 0;
        generatorNextLocalIndex_ = 0;
        generatorLocalSlots_.clear();
        crossYieldSpillIds_.clear();
        crossYieldSlotOf_.clear();
        crossYieldSlotType_.clear();
        crossYieldSlotGEPs_.clear();
        return;  // Exit early - we've handled everything for generators
    }

    // Create LLVM basic blocks for HIR blocks
    for (auto& block : fn->blocks) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context_, block->label, llvmFunc);
        blockMap_[block.get()] = bb;
    }

    // For async functions, emit a top-level exception handler in the entry block
    // before branching to the first HIR block. This converts any uncaught throw
    // inside the function body into a promise rejection. With this in place,
    // `lowerThrow` can use the normal ts_throw/longjmp path uniformly — user
    // try/catch blocks install their own handlers on top of this one, so they
    // intercept throws as expected; only truly uncaught throws walk up to this
    // prologue handler and become a rejected promise. Skip for async generators
    // (they don't use asyncPromise_; they yield Promises directly).
    if (fn->isAsync && !fn->isGenerator && asyncPromise_ && !fn->blocks.empty()) {
        llvm::BasicBlock* firstBlock = blockMap_[fn->blocks[0].get()];
        llvm::BasicBlock* asyncRejectBB = llvm::BasicBlock::Create(
            context_, "async.reject", llvmFunc);

        // setjmp semantics require the frame to remain valid for longjmp,
        // so the containing function must not be inlined.
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);

        auto pushFn = getOrDeclareRuntimeFunction("ts_push_exception_handler",
            getGCPtrTy(), {});
        llvm::Value* jmpBuf = builder_->CreateCall(pushFn, {});

#ifdef _WIN32
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(),
            { getGCPtrTy(), getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto frameAddrFn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::frameaddress, { getGCPtrTy() });
        llvm::Value* framePtr = builder_->CreateCall(
            frameAddrFn, { builder_->getInt32(0) });
        auto* setjmpCall = builder_->CreateCall(setjmpFn, { jmpBuf, framePtr });
        setjmpCall->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCall;
#else
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(), { getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto* setjmpCallPosix = builder_->CreateCall(setjmpFn, { jmpBuf });
        setjmpCallPosix->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCallPosix;
#endif
        llvm::Value* isException = builder_->CreateICmpNE(setjmpResult,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));
        builder_->CreateCondBr(isException, asyncRejectBB, firstBlock);

        // async.reject: ts_throw has already popped its own handler before
        // longjmp'ing here. Fetch the pending exception, reject the promise,
        // clear the runtime exception slot, and return the rejected promise.
        builder_->SetInsertPoint(asyncRejectBB);
        auto getExcFn = getOrDeclareRuntimeFunction("ts_get_exception",
            getGCPtrTy(), {});
        llvm::Value* exc = builder_->CreateCall(getExcFn, {});

        auto rejectFn = getOrDeclareRuntimeFunction("ts_promise_reject_internal",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(rejectFn, { gcPtrToRaw(asyncPromise_), exc });

        auto clearExcFn = getOrDeclareRuntimeFunction("ts_set_exception",
            builder_->getVoidTy(), { getGCPtrTy() });
        builder_->CreateCall(clearExcFn,
            { llvm::ConstantPointerNull::get(getGCPtrTy()) });

        auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* boxedPromise = builder_->CreateCall(
            makePromiseFn, { gcPtrToRaw(asyncPromise_) }, "rejected_promise");
        builder_->CreateRet(boxedPromise);
    } else if (fn->isAsync && fn->isGenerator && generatorObject_ && !fn->blocks.empty()) {
        // Async-generator eager body: same setjmp barrier as async functions,
        // but an uncaught throw is recorded on the generator (the first
        // next() promise rejects) instead of escaping gen() synchronously —
        // e.g. the yield* GetIterator TypeErrors (the test262 "abrupt
        // completion closes iter" cluster).
        llvm::BasicBlock* firstBlock = blockMap_[fn->blocks[0].get()];
        llvm::BasicBlock* agenRejectBB = llvm::BasicBlock::Create(
            context_, "agen.reject", llvmFunc);

        llvmFunc->addFnAttr(llvm::Attribute::NoInline);

        auto pushFn = getOrDeclareRuntimeFunction("ts_push_exception_handler",
            getGCPtrTy(), {});
        llvm::Value* jmpBuf = builder_->CreateCall(pushFn, {});

#ifdef _WIN32
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(),
            { getGCPtrTy(), getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto frameAddrFn = llvm::Intrinsic::getDeclaration(
            module_.get(), llvm::Intrinsic::frameaddress, { getGCPtrTy() });
        llvm::Value* framePtr = builder_->CreateCall(
            frameAddrFn, { builder_->getInt32(0) });
        auto* setjmpCall = builder_->CreateCall(setjmpFn, { jmpBuf, framePtr });
        setjmpCall->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCall;
#else
        auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
            builder_->getInt32Ty(), { getGCPtrTy() });
        if (auto* sjFn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
            sjFn->addFnAttr(llvm::Attribute::ReturnsTwice);
        }
        auto* setjmpCallPosix = builder_->CreateCall(setjmpFn, { jmpBuf });
        setjmpCallPosix->addFnAttr(llvm::Attribute::ReturnsTwice);
        llvm::Value* setjmpResult = setjmpCallPosix;
#endif
        llvm::Value* isException = builder_->CreateICmpNE(setjmpResult,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));
        builder_->CreateCondBr(isException, agenRejectBB, firstBlock);

        // agen.reject: ts_throw already popped its handler before longjmp.
        // Only async-iteration PROTOCOL throws (flagged by the runtime's
        // yield* machinery) become a pending rejection on the generator;
        // anything else — parameter-binding errors lowered as the body
        // prologue, plain body throws — re-throws synchronously out of
        // gen(), preserving the spec's FormalParameters semantics that the
        // dstr/dflt-params test family asserts with assert.throws.
        builder_->SetInsertPoint(agenRejectBB);
        auto getExcFn = getOrDeclareRuntimeFunction("ts_get_exception",
            getGCPtrTy(), {});
        llvm::Value* exc = builder_->CreateCall(getExcFn, {});

        // Reject for yield*-protocol throws OR any throw after the
        // parameter prologue (gen->bodyStarted); re-throw synchronously
        // only for param-binding errors (dstr/dflt-params semantics).
        auto shouldRejectFn = getOrDeclareRuntimeFunction(
            "ts_agen_should_reject", builder_->getInt32Ty(), { getGCPtrTy() });
        llvm::Value* protoFlag = builder_->CreateCall(
            shouldRejectFn, { generatorObject_ });
        llvm::Value* isProtocol = builder_->CreateICmpNE(protoFlag,
            llvm::ConstantInt::get(builder_->getInt32Ty(), 0));

        llvm::BasicBlock* recordBB = llvm::BasicBlock::Create(
            context_, "agen.record", llvmFunc);
        llvm::BasicBlock* rethrowBB = llvm::BasicBlock::Create(
            context_, "agen.rethrow", llvmFunc);
        builder_->CreateCondBr(isProtocol, recordBB, rethrowBB);

        builder_->SetInsertPoint(recordBB);
        auto setGenExcFn = getOrDeclareRuntimeFunction(
            "ts_async_generator_set_exception",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(setGenExcFn, { generatorObject_, exc });

        auto clearExcFn = getOrDeclareRuntimeFunction("ts_set_exception",
            builder_->getVoidTy(), { getGCPtrTy() });
        builder_->CreateCall(clearExcFn,
            { llvm::ConstantPointerNull::get(getGCPtrTy()) });

        builder_->CreateRet(generatorObject_);

        // Non-protocol: propagate to the next outer handler (assert.throws
        // try/catch or top-level). Also pop the generator's eager-body stack
        // entry, which ts_async_generator_return never got to do.
        builder_->SetInsertPoint(rethrowBB);
        auto abortGenFn = getOrDeclareRuntimeFunction(
            "ts_async_generator_abort",
            builder_->getVoidTy(), { getGCPtrTy() });
        builder_->CreateCall(abortGenFn, { generatorObject_ });
        auto rethrowFn = getOrDeclareRuntimeFunction("ts_throw",
            builder_->getVoidTy(), { getGCPtrTy() });
        builder_->CreateCall(rethrowFn, { exc });
        builder_->CreateUnreachable();
    } else if (fn->isAsync && !fn->blocks.empty()) {
        // Async (non-generator) without a promise: retain the simple br.
        llvm::BasicBlock* firstBlock = blockMap_[fn->blocks[0].get()];
        builder_->CreateBr(firstBlock);
    }

    // Map function parameters to values
    auto argIt = llvmFunc->arg_begin();
    auto argEnd = llvmFunc->arg_end();

    // If we have captures AND no explicit __closure__ param, the first LLVM argument is the implicit closure context.
    // Defensive: only walk past an actual existing arg. The LLVM function may have been
    // created without an implicit closure slot when HIR has captures (bytecodePatternMatching
    // hit a crash dereferencing an end iterator at setName for the closure).
    if (hasCaptureParams && !hasHiddenClosureParam && argIt != argEnd) {
        argIt->setName("__closure");
        closureParam_ = &*argIt;
        ++argIt;
    }

    for (size_t i = 0; i < fn->params.size() && argIt != argEnd; ++i, ++argIt) {
        argIt->setName(fn->params[i].first);
        // Create a value mapping for the parameter
        // Parameters are represented as values with IDs starting from 0
        valueMap_[static_cast<uint32_t>(i)] = &*argIt;

        // If this is the explicit __closure__ param, also set it as the closure context
        if (fn->params[i].first == "__closure__" && hasCaptureParams) {
            closureParam_ = &*argIt;
        }
    }

    // Pre-scan: lower all Alloca instructions from ALL blocks first.
    // This ensures their valueMap_ entries exist before any block references them.
    // Alloca instructions are always moved to the LLVM entry block anyway, so
    // processing them early is correct. Without this, a block that references an
    // alloca defined in a later block (e.g., forof.end referencing a closure alloca
    // inside the loop body) would fail because the alloca's value ID is not yet in
    // valueMap_ when the referencing block is lowered.
    if (!fn->blocks.empty()) {
        // Set builder to first block so InsertPointGuard in lowerAlloca can save/restore
        builder_->SetInsertPoint(blockMap_[fn->blocks[0].get()]);
        for (auto& block : fn->blocks) {
            for (auto& inst : block->instructions) {
                if (inst->opcode == HIROpcode::Alloca) {
                    lowerAlloca(inst.get());
                }
            }
        }
    }

    // Compute Reverse Post-Order (RPO) for block lowering.
    // RPO ensures predecessors are lowered before successors (except loop back-edges),
    // which guarantees that value definitions are in valueMap_ before their uses.
    // This fixes cases where default parameter handling creates value-producing
    // blocks that appear after merge blocks in the sequential fn->blocks order.
    std::vector<HIRBlock*> rpoOrder;
    {
        // Build a fresh successor map from terminators.  The cached
        // block->successors vector can become stale after DCE or other
        // optimisation passes modify control flow, so derive edges from
        // the actual Branch / CondBranch / Switch operands.
        std::unordered_map<HIRBlock*, std::vector<HIRBlock*>> succMap;
        for (auto& block : fn->blocks) {
            auto& succs = succMap[block.get()];
            if (block->instructions.empty()) continue;
            HIRInstruction* term = block->instructions.back().get();
            switch (term->opcode) {
                case HIROpcode::Branch:
                    if (!term->operands.empty()) {
                        if (auto* blk = std::get_if<HIRBlock*>(&term->operands[0]))
                            succs.push_back(*blk);
                    }
                    break;
                case HIROpcode::CondBranch:
                    if (term->operands.size() >= 3) {
                        if (auto* thenBlk = std::get_if<HIRBlock*>(&term->operands[1]))
                            succs.push_back(*thenBlk);
                        if (auto* elseBlk = std::get_if<HIRBlock*>(&term->operands[2]))
                            succs.push_back(*elseBlk);
                    }
                    break;
                case HIROpcode::Switch:
                    if (term->switchDefault)
                        succs.push_back(term->switchDefault);
                    for (auto& [val, target] : term->switchCases)
                        succs.push_back(target);
                    break;
                default:
                    break;
            }
        }

        std::unordered_set<HIRBlock*> visited;
        std::vector<HIRBlock*> postOrder;

        // Iterative DFS post-order traversal using explicit stack
        // Stack entries: (block, index into successors to visit next)
        std::vector<std::pair<HIRBlock*, size_t>> stack;
        if (!fn->blocks.empty()) {
            HIRBlock* entry = fn->blocks[0].get();
            visited.insert(entry);
            stack.push_back({entry, 0});
        }
        while (!stack.empty()) {
            auto& [block, idx] = stack.back();
            auto& succs = succMap[block];
            if (idx < succs.size()) {
                HIRBlock* succ = succs[idx++];
                if (succ && !visited.count(succ)) {
                    visited.insert(succ);
                    stack.push_back({succ, 0});
                }
            } else {
                postOrder.push_back(block);
                stack.pop_back();
            }
        }

        // Append any blocks not reachable from entry (shouldn't happen after DCE,
        // but ensures we don't silently drop blocks)
        for (auto& block : fn->blocks) {
            if (!visited.count(block.get())) {
                postOrder.push_back(block.get());
            }
        }

        // Reverse post-order = predecessors before successors
        rpoOrder.assign(postOrder.rbegin(), postOrder.rend());
    }

    // Emit coverage counter at function entry (counter 0)
    if (emitCoverage_ && !rpoOrder.empty() && rpoOrder[0]) {
        llvm::BasicBlock* entryBB = getBlock(rpoOrder[0]);
        if (entryBB) {
            builder_->SetInsertPoint(entryBB, entryBB->begin());
            // Simple hash: use function name hash for now
            uint64_t funcHash = std::hash<std::string>{}(fn->mangledName);
            emitCoverageIncrement(fn->mangledName, funcHash, 1, 0);

            // Record coverage function info for mapping
            CoverageFunctionInfo covInfo;
            covInfo.funcName = fn->mangledName;
            covInfo.sourceFile = fn->sourceFile;
            covInfo.funcHash = funcHash;
            covInfo.numCounters = 1;
            if (fn->sourceLine > 0) {
                uint16_t fileIdx = 0;
                for (uint16_t i = 0; i < hirModule_->sourceFiles.size(); ++i) {
                    if (hirModule_->sourceFiles[i] == fn->sourceFile) { fileIdx = i; break; }
                }
                // Estimate function end line from last instruction
                uint32_t endLine = fn->sourceLine;
                for (auto& block : fn->blocks) {
                    for (auto& inst : block->instructions) {
                        if (inst->sourceLine > endLine) endLine = inst->sourceLine;
                    }
                }
                covInfo.regions.push_back({fileIdx, fn->sourceLine, 1, endLine, 1});
            }
            coverageFunctions_.push_back(std::move(covInfo));
        }
    }

    // Lower each block in RPO order
    for (size_t bi = 0; bi < rpoOrder.size(); ++bi) {
        if (!rpoOrder[bi]) {
            SPDLOG_WARN("RPO: null block at index {} in func {}", bi, fn->mangledName);
            continue;
        }
        lowerBlock(rpoOrder[bi]);
    }

    // Terminator safety net (skip for generators — their impl function has
    // its own terminator-emission scheme via the state-machine + done block,
    // handled earlier in this function).
    //
    // Block-splitting helpers in HIRToLLVM (NaN-box unboxing diamonds at
    // ~2868/2910/2947, write-barrier inserts, GC safepoints, etc.) leave
    // the IRBuilder positioned on a freshly-created "merge" block that
    // initially has only a PHI and no terminator. The block is intended
    // to be the continuation of the surrounding HIR block. If the LAST
    // HIR instruction of a function happens to land in one of these split
    // sub-blocks AND the source function didn't end with an explicit
    // `return` (e.g. a top-level script that runs off the end), the
    // continuation block is left without a terminator. LLVM verifier
    // rejects the module with "Basic Block ... does not have terminator!".
    //
    // Walk every block in the LLVM function; if any lacks a terminator,
    // append a default return matching the function's return type. This
    // mirrors the dispatch in lowerReturnVoid (~line 8830). The fix only
    // touches blocks that are *already* unterminated; legitimately-
    // terminated blocks are untouched.
    if (currentFunction_) {
        llvm::Type* expectedRetType = currentFunction_->getReturnType();
        auto emitDefaultReturn = [&]() {
            if (expectedRetType->isVoidTy()) {
                builder_->CreateRetVoid();
            } else if (expectedRetType->isPointerTy()) {
                auto undefFn = getOrDeclareRuntimeFunction(
                    "ts_value_make_undefined",
                    getGCPtrTy(), {});
                llvm::Value* undefVal = builder_->CreateCall(undefFn, {}, "undef.tail");
                builder_->CreateRet(undefVal);
            } else if (expectedRetType->isDoubleTy()) {
                builder_->CreateRet(llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0));
            } else if (expectedRetType->isIntegerTy(64)) {
                builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
            } else if (expectedRetType->isIntegerTy(1)) {
                builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt1Ty(), 0));
            } else {
                builder_->CreateRet(llvm::Constant::getNullValue(expectedRetType));
            }
        };
        for (auto& bb : *currentFunction_) {
            if (bb.empty() || !bb.back().isTerminator()) {
                builder_->SetInsertPoint(&bb);
                emitDefaultReturn();
                continue;
            }
            // Also fix RET terminators whose value type doesn't match the
            // declared return type (e.g. derived-class super-prop paths can
            // route to a generator-state-machine done-block that emits ret
            // void while the function is declared ptr — superPropOrdering
            // hit this with "ret void <badref> ptr" from the LLVM verifier).
            llvm::Instruction& term = bb.back();
            if (auto* retInst = llvm::dyn_cast<llvm::ReturnInst>(&term)) {
                llvm::Type* retValTy = retInst->getReturnValue()
                    ? retInst->getReturnValue()->getType()
                    : builder_->getVoidTy();
                if (retValTy != expectedRetType) {
                    retInst->eraseFromParent();
                    builder_->SetInsertPoint(&bb);
                    emitDefaultReturn();
                }
            }
        }
    }

    currentFunction_ = nullptr;
    currentHIRFunction_ = nullptr;
    isAsyncFunction_ = false;
    isGeneratorFunction_ = false;
    closureParam_ = nullptr;
    asyncPromise_ = nullptr;
    generatorObject_ = nullptr;
    asyncContext_ = nullptr;
    currentYieldState_ = 0;
    yieldResumeBlocks_.clear();
    generatorDoneBlock_ = nullptr;
    generatorImplFunc_ = nullptr;
}


}  // namespace ts::hir
