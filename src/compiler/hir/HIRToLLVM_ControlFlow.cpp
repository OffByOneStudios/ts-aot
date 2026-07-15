#include "HIRToLLVM_Internal.h"

namespace ts::hir {


void HIRToLLVM::lowerBranch(HIRInstruction* inst) {
    HIRBlock* target = getOperandBlock(inst->operands[0]);
    llvm::BasicBlock* targetBB = getBlock(target);
    if (!targetBB) {
        SPDLOG_WARN("lowerBranch: null LLVM block for Branch in {} (target={}), creating unreachable fallback",
            currentHIRFunction_ ? currentHIRFunction_->mangledName : "??",
            target ? target->label : "null");
        // Create a fallback unreachable block for dangling branch targets
        // (e.g., cross-function block references from ASTToHIR)
        auto* fallback = llvm::BasicBlock::Create(context_, "branch.fallback", currentFunction_);
        auto savedIP = builder_->saveIP();
        builder_->SetInsertPoint(fallback);
        builder_->CreateUnreachable();
        builder_->restoreIP(savedIP);
        builder_->CreateBr(fallback);
        return;
    }
    builder_->CreateBr(targetBB);
}

void HIRToLLVM::lowerCondBranch(HIRInstruction* inst) {
    llvm::Value* cond = getOperandValue(inst->operands[0]);
    HIRBlock* thenBlock = getOperandBlock(inst->operands[1]);
    HIRBlock* elseBlock = getOperandBlock(inst->operands[2]);

    llvm::BasicBlock* thenBB = getBlock(thenBlock);
    llvm::BasicBlock* elseBB = getBlock(elseBlock);

    // Create fallback unreachable blocks for dangling branch targets
    if (!thenBB || !elseBB) {
        SPDLOG_WARN("lowerCondBranch: null LLVM block for CondBranch in {}, creating unreachable fallback(s)",
            currentHIRFunction_ ? currentHIRFunction_->mangledName : "??");
        auto createFallback = [&]() {
            auto* fb = llvm::BasicBlock::Create(context_, "condbr.fallback", currentFunction_);
            auto savedIP = builder_->saveIP();
            builder_->SetInsertPoint(fb);
            builder_->CreateUnreachable();
            builder_->restoreIP(savedIP);
            return fb;
        };
        if (!thenBB) thenBB = createFallback();
        if (!elseBB) elseBB = createFallback();
    }

    // Convert condition to i1 (boolean) if it's not already
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isIntegerTy(64)) {
            // Integer: compare != 0
            cond = builder_->CreateICmpNE(cond,
                llvm::ConstantInt::get(builder_->getInt64Ty(), 0), "tobool");
        } else if (cond->getType()->isIntegerTy(32)) {
            cond = builder_->CreateICmpNE(cond,
                llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "tobool");
        } else if (cond->getType()->isDoubleTy()) {
            // Double: compare != 0.0
            cond = builder_->CreateFCmpONE(cond,
                llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "tobool");
        } else if (cond->getType()->isPointerTy()) {
            // Pointer (boxed value): use runtime ts_value_to_bool for JS truthiness
            auto toBoolFn = getOrDeclareRuntimeFunction("ts_value_to_bool",
                builder_->getInt1Ty(), { getGCPtrTy() });
            cond = builder_->CreateCall(toBoolFn, { cond }, "tobool");
        }
    }

    builder_->CreateCondBr(cond, thenBB, elseBB);
}

void HIRToLLVM::lowerSwitch(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // LLVM switch instruction only supports integer types.
    // If the switch value is a double (TypeScript number), convert it to i64.
    if (val->getType()->isDoubleTy()) {
        val = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "switch.val.i64");
    } else if (val->getType()->isPointerTy()) {
        // Boxed any-type value (e.g. arguments.length in untyped JS) - unbox to i64
        auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), {getGCPtrTy()}, false);
        auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
        val = builder_->CreateCall(ft, fn.getCallee(), {val}, "switch.val.unbox");
    }

    llvm::BasicBlock* defaultBB = getBlock(inst->switchDefault);
    if (!defaultBB) {
        // Create unreachable block as default
        defaultBB = llvm::BasicBlock::Create(context_, "switch.default", currentFunction_);
        builder_->SetInsertPoint(defaultBB);
        builder_->CreateUnreachable();
        builder_->SetInsertPoint(getBlock(currentHIRFunction_->blocks.back().get()));
    }

    llvm::SwitchInst* switchInst = builder_->CreateSwitch(val, defaultBB, inst->switchCases.size());

    for (auto& [caseVal, caseBlock] : inst->switchCases) {
        llvm::BasicBlock* caseBB = getBlock(caseBlock);
        if (caseBB) {
            switchInst->addCase(
                llvm::ConstantInt::get(builder_->getInt64Ty(), caseVal),
                caseBB
            );
        }
    }
}

void HIRToLLVM::lowerReturn(HIRInstruction* inst) {
    SPDLOG_INFO("      lowerReturn: entered, operands.size()={}", inst->operands.size());
    if (inst->operands.empty()) {
        SPDLOG_ERROR("      lowerReturn: no operands!");
        emitArenaReleaseIfFast();
        builder_->CreateRet(llvm::UndefValue::get(currentFunction_->getReturnType()));
        return;
    }
    llvm::Value* val = gcPtrToRaw(getOperandValue(inst->operands[0]));
    SPDLOG_INFO("      lowerReturn: val={}", val ? "non-null" : "null");

    // "use fast" Phase 2c: release the Temp arena frame before returning. No-op
    // unless a frame is open (arenaMarker_ is null for non-fast / async / gen
    // functions). Returning a Temp NativeArray past this point is UB by design.
    emitArenaReleaseIfFast();

    // Handle null value (e.g., from null HIRValue shared_ptr)
    if (!val) {
        SPDLOG_WARN("      lowerReturn: null value, using undefined");
        // If function returns void, just create void return
        if (currentFunction_->getReturnType()->isVoidTy()) {
            builder_->CreateRetVoid();
            return;
        }
        // Otherwise use undefined value
        val = llvm::UndefValue::get(currentFunction_->getReturnType());
        builder_->CreateRet(val);
        return;
    }

    // GEN-001 Stage 3: suspendable async-generator impl — complete the
    // CURRENT request's promise with {value, done:true} via ctx and suspend
    // for good. The impl is void(AsyncContext*): do NOT ret a generator
    // object (the wrapper already returned it at gen() time).
    if (inSuspendableAgenMode_ && asyncContext_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            boxedVal = boxPrimitiveToPtr(val);
        }

        auto completeFn = getOrDeclareRuntimeFunction("ts_agen_complete",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(completeFn, { asyncContext_, boxedVal });

        // Returns are body code, only reachable in state>=1 invocations
        // (state 0 ends at the body-started marker), so the impl barrier is
        // always pushed here — pop it on this return edge.
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});

        builder_->CreateRetVoid();
        return;
    }

    // For async generator functions, mark the async generator as done with the return value
    if (isAsyncFunction_ && isGeneratorFunction_ && generatorObject_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    getGCPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    getGCPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    getGCPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Call ts_async_generator_return to mark as done with return value
        auto returnFn = getOrDeclareRuntimeFunction("ts_async_generator_return",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, boxedVal });

        // Pop the prologue's catch-all handler (pushed in lowerFunction) so
        // its stale jmp_buf can't become a later throw's target.
        auto popHandlerFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popHandlerFn, {});

        // Return the async generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For state-machine generator impl functions (asyncContext_ set, generatorObject_ not available)
    if (isGeneratorFunction_ && asyncContext_ && !generatorObject_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    getGCPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    getGCPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    getGCPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Call ts_generator_return_via_ctx to mark as done with return value
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return_via_ctx",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { asyncContext_, boxedVal });

        builder_->CreateRetVoid();
        return;
    }

    // For regular generator functions (non-state-machine path), a return marks the generator as done
    if (isGeneratorFunction_ && generatorObject_) {
        // Box the return value if needed
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    getGCPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    getGCPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    getGCPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Call ts_generator_return to mark as done with return value
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, boxedVal });

        builder_->CreateRet(generatorObject_);
        return;
    }

    // For async functions, a regular return should resolve the promise and return it
    if (isAsyncFunction_ && asyncPromise_) {
        // Box the return value if it's not already a pointer
        llvm::Value* boxedVal = val;
        if (!val->getType()->isPointerTy()) {
            // Need to box the value for ts_promise_resolve_internal
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    getGCPtrTy(), { builder_->getInt64Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_int");
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                    getGCPtrTy(), { builder_->getDoubleTy() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_double");
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    getGCPtrTy(), { builder_->getInt1Ty() });
                boxedVal = builder_->CreateCall(boxFn, { val }, "boxed_bool");
            }
        }

        // Resolve the promise with the value
        auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(resolveFn, { gcPtrToRaw(asyncPromise_), boxedVal });

        // Pop the prologue's catch-all handler before returning so its now-stale
        // jmp_buf doesn't become the target of a later throw on this thread.
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});

        // Return the promise (wrapped for boxing)
        auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { gcPtrToRaw(asyncPromise_) }, "boxed_promise");
        builder_->CreateRet(boxedPromise);
        return;
    }

    // Get the function's declared return type
    llvm::Type* expectedRetType = currentFunction_->getReturnType();

    // Convert value to expected return type if needed
    if (val->getType() != expectedRetType) {
        if (val->getType()->isIntegerTy() && expectedRetType->isDoubleTy()) {
            // Int to Double
            val = builder_->CreateSIToFP(val, expectedRetType);
        } else if (val->getType()->isDoubleTy() && expectedRetType->isIntegerTy()) {
            // Double to Int
            val = builder_->CreateFPToSI(val, expectedRetType);
        } else if (val->getType()->isPointerTy() && expectedRetType->isIntegerTy()) {
            // Ptr to Int (for Any/Object → Int conversion)
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_int",
                builder_->getInt64Ty(), { getGCPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unbox_int_ret");
            if (expectedRetType != builder_->getInt64Ty()) {
                val = builder_->CreateTrunc(val, expectedRetType);
            }
        } else if (val->getType()->isPointerTy() && expectedRetType->isDoubleTy()) {
            // Ptr to Double (for Any/Object → number conversion, needs unboxing)
            auto unboxFn = getOrDeclareRuntimeFunction("ts_value_get_double",
                builder_->getDoubleTy(), { getGCPtrTy() });
            val = builder_->CreateCall(unboxFn, { val }, "unbox_double_ret");
        } else if (val->getType()->isIntegerTy() && expectedRetType->isPointerTy()) {
            // Int to Ptr (for Int → Any/Object conversion) - need to box
            if (val->getType()->isIntegerTy(1)) {
                // Bool needs boxing
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                    getGCPtrTy(), { builder_->getInt1Ty() });
                val = builder_->CreateCall(boxFn, { val }, "boxed_bool_ret");
            } else {
                // Integer needs boxing
                auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                    getGCPtrTy(), { builder_->getInt64Ty() });
                val = builder_->CreateCall(boxFn, { val }, "boxed_int_ret");
            }
        } else if (val->getType()->isDoubleTy() && expectedRetType->isPointerTy()) {
            // Double to Ptr (for number → Any/Object conversion, needs boxing)
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                getGCPtrTy(), { builder_->getDoubleTy() });
            val = builder_->CreateCall(boxFn, { val }, "boxed_double_ret");
        }
        // If types still don't match after conversion attempts, LLVM will error
    }

    builder_->CreateRet(val);
}

void HIRToLLVM::lowerReturnVoid(HIRInstruction* inst) {
    // GEN-001 Stage 3: suspendable async-generator impl — complete the
    // CURRENT request's promise with {undefined, done:true} via ctx. The impl
    // is void(AsyncContext*); the wrapper already returned the generator.
    if (inSuspendableAgenMode_ && asyncContext_) {
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            getGCPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        auto completeFn = getOrDeclareRuntimeFunction("ts_agen_complete",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(completeFn, { asyncContext_, undefinedVal });

        // Return edges are body code (state>=1 invocations only) — the impl
        // barrier is pushed; pop it before suspending for good.
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});

        builder_->CreateRetVoid();
        return;
    }

    // For async generator functions, a void return marks the async generator as done with undefined
    if (isAsyncFunction_ && isGeneratorFunction_ && generatorObject_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            getGCPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Call ts_async_generator_return to mark as done with undefined
        auto returnFn = getOrDeclareRuntimeFunction("ts_async_generator_return",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, undefinedVal });

        // Pop the prologue's catch-all handler (pushed in lowerFunction).
        auto popHandlerFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popHandlerFn, {});

        // Return the async generator object
        builder_->CreateRet(generatorObject_);
        return;
    }

    // For state-machine generator impl functions (asyncContext_ set, generatorObject_ not available)
    if (isGeneratorFunction_ && asyncContext_ && !generatorObject_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            getGCPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Call ts_generator_return_via_ctx to mark as done with undefined
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return_via_ctx",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { asyncContext_, undefinedVal });

        // State-machine impl functions are normally declared void, but a
        // non-state-machine derived-class method that the codegen mistakenly
        // routes here can have a non-void return type (e.g. superPropOrdering
        // hit this with a ptr-returning method). Match the actual return type
        // rather than blindly emitting RetVoid.
        llvm::Type* retTy = currentFunction_->getReturnType();
        if (retTy->isVoidTy()) {
            builder_->CreateRetVoid();
        } else {
            builder_->CreateRet(llvm::UndefValue::get(retTy));
        }
        return;
    }

    // For regular generator functions (non-state-machine path), a void return marks as done
    if (isGeneratorFunction_ && generatorObject_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            getGCPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Call ts_generator_return to mark as done with undefined
        auto returnFn = getOrDeclareRuntimeFunction("ts_generator_return",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(returnFn, { generatorObject_, undefinedVal });

        builder_->CreateRet(generatorObject_);
        return;
    }

    // For async functions (not generators), a void return should resolve the promise with undefined
    if (isAsyncFunction_ && asyncPromise_) {
        // Get undefined value
        auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
            getGCPtrTy(), {});
        llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");

        // Resolve the promise with undefined
        auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(resolveFn, { gcPtrToRaw(asyncPromise_), undefinedVal });

        // Pop the prologue's catch-all handler before returning.
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});

        // Return the promise (wrapped for boxing)
        auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { gcPtrToRaw(asyncPromise_) }, "boxed_promise");
        builder_->CreateRet(boxedPromise);
        return;
    }

    // "use fast" Phase 2c: release the Temp arena frame on the normal (non-
    // async/gen) void return. No-op unless a frame is open.
    emitArenaReleaseIfFast();

    // Check if the function's return type is void or non-void
    llvm::Type* expectedRetType = currentFunction_->getReturnType();
    if (expectedRetType->isVoidTy()) {
        builder_->CreateRetVoid();
    } else {
        // Function expects a non-void return but we have ReturnVoid
        // Return a default value (undefined for ptr, 0 for int, 0.0 for double)
        if (expectedRetType->isPointerTy()) {
            // Return undefined (boxed)
            auto undefFn = getOrDeclareRuntimeFunction("ts_value_make_undefined",
                getGCPtrTy(), {});
            llvm::Value* undefinedVal = builder_->CreateCall(undefFn, {}, "undefined");
            builder_->CreateRet(undefinedVal);
        } else if (expectedRetType->isDoubleTy()) {
            builder_->CreateRet(llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0));
        } else if (expectedRetType->isIntegerTy(64)) {
            builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
        } else if (expectedRetType->isIntegerTy(1)) {
            builder_->CreateRet(llvm::ConstantInt::get(builder_->getInt1Ty(), 0));
        } else {
            // For any other type, return a null pointer (best effort)
            builder_->CreateRet(llvm::Constant::getNullValue(expectedRetType));
        }
    }
}

void HIRToLLVM::lowerUnreachable(HIRInstruction* inst) {
    builder_->CreateUnreachable();
}

//==============================================================================
// Phi and Select
//==============================================================================

void HIRToLLVM::lowerPhi(HIRInstruction* inst) {
    llvm::Type* type = getLLVMType(inst->result->type);
    llvm::BasicBlock* phiBlock = builder_->GetInsertBlock();
    llvm::PHINode* phi = builder_->CreatePHI(type, inst->phiIncoming.size(), "phi");

    // Track which LLVM blocks have already been added to avoid duplicate entries.
    // Duplicates can arise when multiple HIR blocks map to the same LLVM block
    // (e.g., after block splitting for write barriers or GC safepoints).
    llvm::SmallPtrSet<llvm::BasicBlock*, 8> addedBlocks;

    for (auto& [val, block] : inst->phiIncoming) {
        llvm::BasicBlock* llvmBlock = getBlock(block);
        if (!llvmBlock) continue;

        // Check if llvmBlock is an actual predecessor of phiBlock.
        // Short-circuit operators (&&, ||) may create phi nodes where one
        // incoming edge becomes dead after constant branch folding.
        bool isPredecessor = false;
        for (auto* pred : llvm::predecessors(phiBlock)) {
            if (pred == llvmBlock) {
                isPredecessor = true;
                break;
            }
        }
        if (!isPredecessor) {
            // Walk forward from llvmBlock to handle block splitting (write
            // barriers, GC safepoints, NaN-box unboxing create new blocks
            // within the same HIR block). Use DFS to traverse through
            // multi-successor blocks (e.g., NaN unboxing: br i1 → int/flt → merge).
            llvm::SmallVector<llvm::BasicBlock*, 8> stack = {llvmBlock};
            llvm::SmallPtrSet<llvm::BasicBlock*, 16> visited;
            while (!stack.empty() && !isPredecessor) {
                auto* candidate = stack.pop_back_val();
                if (!candidate || visited.count(candidate)) continue;
                visited.insert(candidate);
                // Depth limit: must comfortably exceed the fragment count a
                // single HIR block can split into. Each NativeArray bounds
                // check adds 2 blocks (na.oob + na.cont) and unbox diamonds
                // add 3 — a fast function doing ~15 checked accesses in one
                // block blew the old limit of 32, silently DROPPING a phi
                // edge (verifier: "PHINode should have one entry for each
                // predecessor"). Keep a cap only as a runaway guard.
                if (visited.size() > 1024) break;
                // Suspendable-agen suspension points end a block with
                // `ret void` and relocate emission into a yield_resume_N
                // block (GEN-001 Stage 4b) — follow the recorded hop so the
                // walk can cross the suspension. Empty map outside
                // suspendable-agen mode (flag-off unaffected).
                if (!agenSuspendRelocation_.empty()) {
                    auto reloc = agenSuspendRelocation_.find(candidate);
                    if (reloc != agenSuspendRelocation_.end()) {
                        stack.push_back(reloc->second);
                    }
                }
                auto* term = candidate->getTerminator();
                if (!term) continue;
                for (unsigned i = 0; i < term->getNumSuccessors(); ++i) {
                    auto* succ = term->getSuccessor(i);
                    if (succ == phiBlock) {
                        llvmBlock = candidate;
                        isPredecessor = true;
                        break;
                    }
                    stack.push_back(succ);
                }
            }
        }
        if (!isPredecessor) {
            // Edge was folded away (e.g., constant branch). Skip this entry.
            continue;
        }

        llvm::Value* llvmVal = nullptr;

        // For phi nodes, GC pin reloads must be in the SOURCE block (before
        // its terminator), not the merge block where the phi lives.
        // getValue() would insert the reload at the current insert point
        // (the merge block), violating SSA dominance.
        auto pinIt = gcPinAllocas_.find(val->id);
        if (pinIt != gcPinAllocas_.end()) {
            llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
            builder_->SetInsertPoint(llvmBlock->getTerminator());
            llvmVal = builder_->CreateLoad(getGCPtrTy(), pinIt->second, "gc.reload");
        } else {
            // No GC pin - use getValue() normally (returns the raw SSA value)
            llvmVal = getValue(val);
        }

        if (llvmVal) {
            // Coerce incoming value type to match phi type if needed
            if (llvmVal->getType() != type) {
                llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
                builder_->SetInsertPoint(llvmBlock->getTerminator());

                if (type->isPointerTy()) {
                    if (llvmVal->getType()->isIntegerTy(1)) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), {builder_->getInt1Ty()}, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
                        llvmVal = builder_->CreateCall(ft, fn.getCallee(), {llvmVal});
                    } else if (llvmVal->getType()->isIntegerTy(64)) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), {builder_->getInt64Ty()}, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                        llvmVal = builder_->CreateCall(ft, fn.getCallee(), {llvmVal});
                    } else if (llvmVal->getType()->isDoubleTy()) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), {builder_->getDoubleTy()}, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                        llvmVal = builder_->CreateCall(ft, fn.getCallee(), {llvmVal});
                    } else if (llvmVal->getType()->isIntegerTy()) {
                        llvmVal = builder_->CreateIntToPtr(llvmVal, type);
                    }
                } else if (type->isIntegerTy(1) && llvmVal->getType()->isPointerTy()) {
                    llvmVal = builder_->CreateICmpNE(llvmVal, llvm::ConstantPointerNull::get(getGCPtrTy()), "phi_tobool");
                } else if (type->isDoubleTy() && llvmVal->getType()->isIntegerTy(64)) {
                    llvmVal = builder_->CreateSIToFP(llvmVal, type);
                } else if (type->isIntegerTy(64) && llvmVal->getType()->isDoubleTy()) {
                    llvmVal = builder_->CreateFPToSI(llvmVal, type);
                }
            }
            // Skip duplicate entries from the same LLVM block
            if (addedBlocks.count(llvmBlock)) continue;
            addedBlocks.insert(llvmBlock);
            phi->addIncoming(llvmVal, llvmBlock);
        }
    }

    setValue(inst->result, phi);
}

void HIRToLLVM::lowerSelect(HIRInstruction* inst) {
    llvm::Value* cond = getOperandValue(inst->operands[0]);
    llvm::Value* trueVal = getOperandValue(inst->operands[1]);
    llvm::Value* falseVal = getOperandValue(inst->operands[2]);

    // Ensure condition is i1 (boolean) - LLVM select requires i1 for first operand
    if (!cond->getType()->isIntegerTy(1)) {
        if (cond->getType()->isPointerTy()) {
            // For NaN-boxed values (any type), use ts_value_to_bool for proper JS truthiness.
            // A simple null-check is wrong because NaN-boxed undefined/false/0/null/""
            // are non-null pointers but falsy in JavaScript.
            auto ft = llvm::FunctionType::get(builder_->getInt1Ty(), {getGCPtrTy()}, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_to_bool", ft);
            cond = builder_->CreateCall(ft, fn.getCallee(), {cond}, "cond_bool");
        } else if (cond->getType()->isIntegerTy()) {
            // Convert integer to boolean (non-zero check)
            cond = builder_->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0), "cond_bool");
        } else if (cond->getType()->isDoubleTy()) {
            // Convert double to boolean (non-zero and not NaN)
            cond = builder_->CreateFCmpONE(cond, llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "cond_bool");
        }
    }

    // Ensure both operands have the same type for LLVM select
    if (trueVal->getType() != falseVal->getType()) {
        // Unify types: prefer ptr (box non-ptr to TsValue*)
        if (trueVal->getType()->isPointerTy() && !falseVal->getType()->isPointerTy()) {
            falseVal = convertArg(falseVal, ::hir::ArgConversion::Box);
        } else if (!trueVal->getType()->isPointerTy() && falseVal->getType()->isPointerTy()) {
            trueVal = convertArg(trueVal, ::hir::ArgConversion::Box);
        } else if (trueVal->getType()->isDoubleTy() && falseVal->getType()->isIntegerTy(64)) {
            falseVal = builder_->CreateSIToFP(falseVal, builder_->getDoubleTy());
        } else if (trueVal->getType()->isIntegerTy(64) && falseVal->getType()->isDoubleTy()) {
            trueVal = builder_->CreateSIToFP(trueVal, builder_->getDoubleTy());
        }
    }

    llvm::Value* result = builder_->CreateSelect(cond, trueVal, falseVal, "select");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCopy(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    setValue(inst->result, val);
}

//==============================================================================
// Exception Handling
//==============================================================================

void HIRToLLVM::lowerSetupTry(HIRInstruction* inst) {
    // SetupTry: push exception handler, call setjmp, return result
    // Returns bool: true = exception path, false = normal entry
    setValue(inst->result, emitTryHandlerPushAndSetjmp());
}

llvm::Value* HIRToLLVM::emitTryHandlerPushAndSetjmp() {
    // Call ts_push_exception_handler() - returns jmp_buf pointer
    auto pushFn = getOrDeclareRuntimeFunction("ts_push_exception_handler",
        getGCPtrTy(), {});
    llvm::Value* jmpBuf = builder_->CreateCall(pushFn, {});

    // Mark the containing function as noinline — setjmp semantics require
    // the stack frame to remain valid for longjmp to return to.
    if (auto* parentFn = builder_->GetInsertBlock()->getParent()) {
        parentFn->addFnAttr(llvm::Attribute::NoInline);
    }

    // Call setjmp - platform-specific signature
    // Returns 0 on normal entry, non-zero when returning from longjmp
#ifdef _WIN32
    // Windows: _setjmp(jmp_buf, frame_ptr)
    // The frame pointer is REQUIRED for Win64 SEH integration.
    // Passing NULL causes longjmp to abort (STATUS_BREAKPOINT 0x80000003).
    auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
        builder_->getInt32Ty(),
        {getGCPtrTy(), getGCPtrTy()});
    // Mark _setjmp as returns_twice so LLVM doesn't optimize across it
    if (auto* fn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
        fn->addFnAttr(llvm::Attribute::ReturnsTwice);
    }
    auto frameAddrFn = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::frameaddress, {getGCPtrTy()});
    llvm::Value* framePtr = builder_->CreateCall(frameAddrFn, {builder_->getInt32(0)});
    auto* setjmpCall = builder_->CreateCall(setjmpFn, {jmpBuf, framePtr});
    setjmpCall->addFnAttr(llvm::Attribute::ReturnsTwice);
    llvm::Value* setjmpResult = setjmpCall;
#else
    // Linux/POSIX: _setjmp(jmp_buf) - doesn't save signal mask (faster)
    auto setjmpFn = getOrDeclareRuntimeFunction("_setjmp",
        builder_->getInt32Ty(),
        {getGCPtrTy()});
    // Mark _setjmp as returns_twice so LLVM doesn't optimize across it
    if (auto* fn = llvm::dyn_cast<llvm::Function>(setjmpFn.getCallee())) {
        fn->addFnAttr(llvm::Attribute::ReturnsTwice);
    }
    auto* setjmpCallPosix = builder_->CreateCall(setjmpFn, {jmpBuf});
    setjmpCallPosix->addFnAttr(llvm::Attribute::ReturnsTwice);
    llvm::Value* setjmpResult = setjmpCallPosix;
#endif

    // Convert result to bool: true if non-zero (exception path)
    llvm::Value* isException = builder_->CreateICmpNE(setjmpResult,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0));

    return isException;
}

void HIRToLLVM::emitSuspendHandlerPops(size_t n) {
    if (n == 0) return;
    auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
        builder_->getVoidTy(), {});
    for (size_t i = 0; i < n; ++i) {
        builder_->CreateCall(popFn, {});
    }
}

void HIRToLLVM::emitRearmTryHandlers(
    const std::vector<HIRBlock*>& tryCatchTargets) {
    // GEN-001 Stage 6: a generator suspension returned from the impl function,
    // so the setjmp handlers of enclosing user try scopes (pushed in a
    // PREVIOUS invocation's frame) are gone — we popped them on the suspend
    // edge. Re-arm each scope in THIS invocation's frame, outermost first so
    // the innermost handler ends on top of the stack, targeting the same HIR
    // catch dispatch blocks lowerSetupTry used. Any value the catch path uses
    // across the suspension is already routed through the heap data buffer by
    // the cross-yield spill pre-pass (cross-block uses are spill candidates),
    // so branching to the catch block from here is dominance-safe.
    for (HIRBlock* catchTarget : tryCatchTargets) {
        llvm::BasicBlock* catchBB = getBlock(catchTarget);
        if (!catchBB) continue;
        llvm::Value* isException = emitTryHandlerPushAndSetjmp();
        llvm::Function* fn = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(
            context_, "rearm_cont", fn);
        builder_->CreateCondBr(isException, catchBB, contBB);
        builder_->SetInsertPoint(contBB);
    }
}

void HIRToLLVM::lowerThrow(HIRInstruction* inst) {
    llvm::Value* exception = getOperandValue(inst->operands[0]);

    // Box primitive exception values before handing off to the runtime, since
    // ts_throw takes a TsValue* (the global currentException is a TsValue*).
    if (!exception->getType()->isPointerTy()) {
        if (exception->getType()->isIntegerTy(64)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                getGCPtrTy(), { builder_->getInt64Ty() });
            exception = builder_->CreateCall(boxFn, { exception }, "boxed_exc_int");
        } else if (exception->getType()->isDoubleTy()) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                getGCPtrTy(), { builder_->getDoubleTy() });
            exception = builder_->CreateCall(boxFn, { exception }, "boxed_exc_dbl");
        } else if (exception->getType()->isIntegerTy(1)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                getGCPtrTy(), { builder_->getInt1Ty() });
            exception = builder_->CreateCall(boxFn, { exception }, "boxed_exc_bool");
        }
    }

    // Uniform path: ts_throw walks the exceptionStack. For an async function
    // with no enclosing user try/catch, the prologue's catch-all handler
    // (installed in lowerFunction) catches the throw and converts it into a
    // promise rejection. For sync code or async code with user try/catch, the
    // user's nearer handler runs first. lowerThrow no longer special-cases
    // async functions.
    auto throwFn = getOrDeclareRuntimeFunction("ts_throw",
        builder_->getVoidTy(), { getGCPtrTy() });
    builder_->CreateCall(throwFn, { exception });
    builder_->CreateUnreachable();
}

void HIRToLLVM::lowerGetException(HIRInstruction* inst) {
    // GetException: call ts_get_exception to get current exception value

    auto getFn = getOrDeclareRuntimeFunction("ts_get_exception",
        getGCPtrTy(), {});
    llvm::Value* exception = builder_->CreateCall(getFn, {});

    setValue(inst->result, exception);
}

void HIRToLLVM::lowerClearException(HIRInstruction* inst) {
    // ClearException: call ts_set_exception(nullptr)

    auto setFn = getOrDeclareRuntimeFunction("ts_set_exception",
        builder_->getVoidTy(), {getGCPtrTy()});
    builder_->CreateCall(setFn, {llvm::ConstantPointerNull::get(getGCPtrTy())});
}

void HIRToLLVM::lowerPopHandler(HIRInstruction* inst) {
    // PopHandler: call ts_pop_exception_handler

    auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
        builder_->getVoidTy(), {});
    builder_->CreateCall(popFn, {});
}

//==============================================================================
// Async/Await Instructions
//==============================================================================

void HIRToLLVM::lowerAwait(HIRInstruction* inst) {
    // Await instruction: %r = await %promise
    // For a simple implementation (without full state machine), we call ts_promise_await
    // which blocks until the promise resolves.
    // TODO: Implement full state machine for proper async/await

    llvm::Value* promiseVal = getOperandValue(inst->operands[0]);

    // Handle null operand (e.g., await on void call result)
    if (!promiseVal) {
        promiseVal = llvm::ConstantPointerNull::get(getGCPtrTy());
    }

    // If the value is not a pointer (e.g., inlined async returned a raw value),
    // we need to box it first. In JavaScript, await on a non-promise value
    // simply returns the value itself.
    if (!promiseVal->getType()->isPointerTy()) {
        // Box the value first
        if (promiseVal->getType()->isIntegerTy(64)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                getGCPtrTy(), { builder_->getInt64Ty() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_int");
        } else if (promiseVal->getType()->isDoubleTy()) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                getGCPtrTy(), { builder_->getDoubleTy() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_double");
        } else if (promiseVal->getType()->isIntegerTy(1)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                getGCPtrTy(), { builder_->getInt1Ty() });
            promiseVal = builder_->CreateCall(boxFn, { promiseVal }, "boxed_bool");
        }
    }

    // Call ts_promise_await(promise) -> TsValue* (the resolved value)
    auto awaitFn = getOrDeclareRuntimeFunction("ts_promise_await",
        getGCPtrTy(), { getGCPtrTy() });
    llvm::Value* result = builder_->CreateCall(awaitFn, { promiseVal }, "await_result");

    setValue(inst->result, result);
}

void HIRToLLVM::lowerAsyncReturn(HIRInstruction* inst) {
    // AsyncReturn: resolve the promise and return it
    // async_return %val

    llvm::Value* val = getOperandValue(inst->operands[0]);

    // Box the value if needed (for ts_promise_resolve_internal which expects TsValue*)
    // For now, assume values coming from HIR are already properly typed

    // Call ts_promise_resolve_internal(promise, value)
    auto resolveFn = getOrDeclareRuntimeFunction("ts_promise_resolve_internal",
        builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
    builder_->CreateCall(resolveFn, { gcPtrToRaw(asyncPromise_), val });

    // Return the promise (wrapped in ts_value_make_promise for boxing)
    auto makePromiseFn = getOrDeclareRuntimeFunction("ts_value_make_promise",
        getGCPtrTy(), { getGCPtrTy() });
    llvm::Value* boxedPromise = builder_->CreateCall(makePromiseFn, { gcPtrToRaw(asyncPromise_) }, "boxed_promise");

    builder_->CreateRet(boxedPromise);
}

void HIRToLLVM::lowerYield(HIRInstruction* inst) {
    // Yield instruction: %r = yield %value
    // For generators with state machine, this:
    // 1. Stores the yielded value in ctx->yieldedValue
    // 2. Sets ctx->yielded = true
    // 3. Sets ctx->state to next state
    // 4. Returns from the impl function
    // 5. Continues in the corresponding resume block

    llvm::Value* yieldVal = getOperandValue(inst->operands[0]);

    // Box the value if it's not already a pointer
    if (!yieldVal->getType()->isPointerTy()) {
        if (yieldVal->getType()->isIntegerTy(64)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_int",
                getGCPtrTy(), { builder_->getInt64Ty() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_int");
        } else if (yieldVal->getType()->isDoubleTy()) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_double",
                getGCPtrTy(), { builder_->getDoubleTy() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_double");
        } else if (yieldVal->getType()->isIntegerTy(1)) {
            auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_bool",
                getGCPtrTy(), { builder_->getInt1Ty() });
            yieldVal = builder_->CreateCall(boxFn, { yieldVal }, "boxed_bool");
        }
    }

    if (inSuspendableAgenMode_ && asyncContext_ != nullptr) {
        // GEN-001 Stage 3 suspendable async generator:
        //   awaited = ts_agen_await_operand(v)      (D2: Await(value); a
        //       rejection ts_throws in-frame, catchable by a user try whose
        //       handler was pushed in THIS invocation)
        //   ts_agen_suspend_yield(ctx, awaited)     (settles the request's
        //       promise with {value, done:false})
        //   set_state(n); pop impl barrier; ret void (suspend)
        //   resume block: mode dispatch (D3) — next/throw/forced-return.
        auto awaitOpFn = getOrDeclareRuntimeFunction("ts_agen_await_operand",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* awaited = builder_->CreateCall(
            awaitOpFn, { yieldVal }, "agen_awaited_operand");

        auto suspendFn = getOrDeclareRuntimeFunction("ts_agen_suspend_yield",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(suspendFn, { asyncContext_, awaited });

        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn,
            { asyncContext_, builder_->getInt32(nextState) });

        // GEN-001 Stage 6 pop balance: the impl function RETURNS at this
        // suspension, so every user try handler still armed here would
        // otherwise leak a stale entry pointing at this (dead) frame. Pop the
        // user handlers first (they were pushed after the barrier, so they
        // are on top), then the impl barrier. Yields are only reachable in
        // state>=1 invocations (state 0 ends at the body-started marker), so
        // the impl barrier is always pushed here.
        emitSuspendHandlerPops(inst->tryCatchTargets.size());
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});
        llvm::BasicBlock* suspendedBlock = builder_->GetInsertBlock();
        builder_->CreateRetVoid();

        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            // Record the suspension-relocation hop for lowerPhi (Stage 4b).
            agenSuspendRelocation_[suspendedBlock] =
                yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(yieldResumeBlocks_[currentYieldState_]);
            llvm::Value* resumedValue =
                emitAgenResumeModeDispatch(inst->tryCatchTargets);
            setValue(inst->result, resumedValue);
        }

        currentYieldState_++;
    } else if (isAsyncFunction_ && isGeneratorFunction_) {
        // Async generator: yield produces a Promise
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_generator_yield",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldFn, { yieldVal }, "async_yield_result");
        setValue(inst->result, result);
    } else if (isGeneratorFunction_ && asyncContext_ != nullptr) {
        // Generator with state machine - use the new implementation
        // Call ts_async_context_yield(ctx, value) to store value and set yielded=true
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_context_yield",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(yieldFn, { asyncContext_, yieldVal });

        // Set state to next state (currentYieldState_ + 1)
        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn, { asyncContext_, builder_->getInt32(nextState) });

        // GEN-001 Stage 6 pop balance: pop the user try handlers still armed
        // at this suspension before the impl returns. Leaving them pushed is
        // the E2 latent bug — the entries point at this frame, which dies on
        // the `ret void` below, and a later throw longjmps into the dead
        // frame, corrupting the process-wide handler stack.
        emitSuspendHandlerPops(inst->tryCatchTargets.size());

        // Return from the impl function (suspend)
        builder_->CreateRetVoid();

        // Move to the corresponding resume block for subsequent instructions
        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            llvm::BasicBlock* resumeBlock = yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(resumeBlock);

            // GEN-001 Stage 6 re-arm: re-push the enclosing try scopes'
            // handlers in THIS invocation's frame (same catch targets) so a
            // throw after the resume is caught by the user's try again.
            emitRearmTryHandlers(inst->tryCatchTargets);

            // Get the resumed value from ctx->resumedValue for the yield result
            auto getResumedFn = getOrDeclareRuntimeFunction("ts_async_context_get_resumed_value",
                getGCPtrTy(), { getGCPtrTy() });
            llvm::Value* resumedValue = builder_->CreateCall(getResumedFn, { asyncContext_ }, "resumed_value");
            setValue(inst->result, resumedValue);
        }

        currentYieldState_++;
    } else {
        // Regular generator (fallback to old implementation)
        auto yieldFn = getOrDeclareRuntimeFunction("ts_generator_yield",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldFn, { yieldVal }, "yield_result");
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerYieldStar(HIRInstruction* inst) {
    // YieldStar instruction: %r = yield* %iterable
    // Delegates to another generator or iterable.
    // For state-machine generators, we inline the delegation loop:
    //   iter = ts_iterator_get(iterable)
    //   loop:
    //     result = ts_iterator_next(iter, null)
    //     if ts_iterator_result_done(result) goto done
    //     val = ts_iterator_result_value(result)
    //     yield val  (state machine yield - suspend and resume)
    //     goto loop
    //   done:
    //     delegatedResult = ts_iterator_result_value(result)

    llvm::Value* iterableVal = getOperandValue(inst->operands[0]);

    // Box primitive operands to ptr — ts_iterator_get expects a ptr receiver,
    // but iterableVal can be an i1 (boolean) / i64 / double when the iterable
    // comes from a `'' in obj`-style boolean expression or other primitive.
    // Without boxing, the call-arg type mismatch fails verifier with
    // "Call parameter type does not match function signature!".
    if (!iterableVal->getType()->isPointerTy()) {
        iterableVal = boxPrimitiveToPtr(iterableVal);
    }

    if (inSuspendableAgenMode_ && asyncContext_ != nullptr) {
        // GEN-001 Stage 3 (D7 core): suspendable async-generator yield* —
        // the sync inline delegation loop with ONE suspension state covering
        // all iterations, plus the async additions: GetIterator(value, async)
        // protocol via ts_agen_get_async_iterator (its TypeErrors ts_throw
        // into the impl barrier), and ts_agen_await_operand on each step
        // result and each yielded value. Resume modes inside yield* are
        // NEXT-scope for Stage 3; throw/return take the same dispatch as a
        // plain yield (delegate forwarding is Stage 7).
        llvm::Function* currentFunc = builder_->GetInsertBlock()->getParent();

        auto getAsyncIterFn = getOrDeclareRuntimeFunction("ts_agen_get_async_iterator",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* iterator = builder_->CreateCall(
            getAsyncIterFn, { iterableVal }, "agen_delegate_iter");

        // Persist across suspensions in ctx->delegateIterator.
        auto setDelegateFn = getOrDeclareRuntimeFunction("ts_async_context_set_delegate_iterator",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(setDelegateFn, { asyncContext_, iterator });

        llvm::BasicBlock* preheaderBB = builder_->GetInsertBlock();
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(
            context_, "agen_yield_star_loop", currentFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(
            context_, "agen_yield_star_check", currentFunc);
        llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(
            context_, "agen_yield_star_done", currentFunc);

        builder_->CreateBr(loopBB);

        // Loop header: reload iterator from ctx and run ONE delegation step
        // via the runtime helper (GEN-001 Stage 4b). ts_agen_delegate_step
        // carries the eager drain's shape tolerances: legacy plain-TsArray
        // "iterator-likes" walked by index (cursor on the AsyncContext),
        // lenient .next lookup, pump-await of promise-shaped step results,
        // and the result-not-an-object protocol TypeError. The value sent
        // into next(v) on resume is forwarded to the inner iterator.
        builder_->SetInsertPoint(loopBB);
        llvm::PHINode* sentArg = builder_->CreatePHI(
            getGCPtrTy(), 2, "agen_sent_arg");
        sentArg->addIncoming(
            llvm::ConstantPointerNull::get(getGCPtrTy()), preheaderBB);

        auto getDelegateFn = getOrDeclareRuntimeFunction("ts_async_context_get_delegate_iterator",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* curIter = builder_->CreateCall(
            getDelegateFn, { asyncContext_ }, "agen_cur_iter");

        auto stepFn = getOrDeclareRuntimeFunction("ts_agen_delegate_step",
            getGCPtrTy(), { getGCPtrTy(), getGCPtrTy(), getGCPtrTy() });
        llvm::Value* stepResult = builder_->CreateCall(
            stepFn, { asyncContext_, curIter, sentArg }, "agen_iter_result");
        builder_->CreateBr(checkBB);

        // Result check: shared by the next-step path (loopBB) and the Stage-7
        // throw/return forwarding path (delegate throw()/return() results are
        // treated exactly like a step result: done -> the yield* completes
        // with the value; not done -> yield the value and stay suspended).
        builder_->SetInsertPoint(checkBB);
        llvm::PHINode* iterResult = builder_->CreatePHI(
            getGCPtrTy(), 2, "agen_step_result");
        iterResult->addIncoming(stepResult, loopBB);

        auto awaitOpFn = getOrDeclareRuntimeFunction("ts_agen_await_operand",
            getGCPtrTy(), { getGCPtrTy() });

        auto doneFn = getOrDeclareRuntimeFunction("ts_iterator_result_done",
            builder_->getInt1Ty(), { getGCPtrTy() });
        llvm::Value* isDone = builder_->CreateCall(
            doneFn, { iterResult }, "agen_is_done");

        llvm::BasicBlock* yieldBB = llvm::BasicBlock::Create(
            context_, "agen_yield_star_yield", currentFunc);
        builder_->CreateCondBr(isDone, doneBB, yieldBB);

        builder_->SetInsertPoint(yieldBB);
        auto valueFn = getOrDeclareRuntimeFunction("ts_iterator_result_value",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* stepValue = builder_->CreateCall(
            valueFn, { iterResult }, "agen_delegate_value");

        // AsyncGeneratorYield awaits the value before yielding it.
        llvm::Value* awaitedValue = builder_->CreateCall(
            awaitOpFn, { stepValue }, "agen_delegate_value_awaited");

        auto suspendFn = getOrDeclareRuntimeFunction("ts_agen_suspend_yield",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(suspendFn, { asyncContext_, awaitedValue });

        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn,
            { asyncContext_, builder_->getInt32(nextState) });

        // Suspend edge in a state>=1 invocation: pop the user try handlers
        // armed at this yield* (GEN-001 Stage 6 pop balance), then the impl
        // barrier.
        emitSuspendHandlerPops(inst->tryCatchTargets.size());
        auto popFn = getOrDeclareRuntimeFunction("ts_pop_exception_handler",
            builder_->getVoidTy(), {});
        builder_->CreateCall(popFn, {});
        builder_->CreateRetVoid();

        // Resume block: mode dispatch (GEN-001 Stage 6 re-arm + Stage 7
        // delegate forwarding).
        // - mode 0 (next): re-arm handlers; the resumed value (gen.next(v)'s
        //   argument) feeds the inner iterator's next via the loop-header PHI.
        // - modes 1/2 (gen.throw / gen.return): FORWARD the completion to the
        //   delegate iterator per 27.6.3.7 via ts_agen_delegate_resume.
        //   Handlers are re-armed BEFORE the call so protocol TypeErrors and
        //   throws from the delegate's throw()/return() land in the enclosing
        //   user try. The helper returns a step-result object (routed through
        //   checkBB like a delegate_step result) or NULL when it completed
        //   the generator itself (ts_agen_complete already ran) — the NULL
        //   path pops the re-armed user handlers + the impl barrier and
        //   suspends for good.
        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            builder_->SetInsertPoint(yieldResumeBlocks_[currentYieldState_]);

            auto getModeFn = getOrDeclareRuntimeFunction(
                "ts_async_context_get_resume_mode",
                builder_->getInt32Ty(), { getGCPtrTy() });
            llvm::Value* mode = builder_->CreateCall(
                getModeFn, { asyncContext_ }, "agen_resume_mode");

            llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(
                context_, "agen_ystar_resume_next", currentFunc);
            llvm::BasicBlock* fwdBB = llvm::BasicBlock::Create(
                context_, "agen_ystar_resume_fwd", currentFunc);
            llvm::SwitchInst* sw = builder_->CreateSwitch(mode, nextBB, 2);
            sw->addCase(builder_->getInt32(1), fwdBB);
            sw->addCase(builder_->getInt32(2), fwdBB);

            auto getResumedFn = getOrDeclareRuntimeFunction(
                "ts_async_context_get_resumed_value",
                getGCPtrTy(), { getGCPtrTy() });

            // mode 0: re-arm and loop with the sent value.
            builder_->SetInsertPoint(nextBB);
            emitRearmTryHandlers(inst->tryCatchTargets);
            llvm::Value* resumedValue = builder_->CreateCall(
                getResumedFn, { asyncContext_ }, "resumed_value");
            sentArg->addIncoming(resumedValue, builder_->GetInsertBlock());
            builder_->CreateBr(loopBB);

            // modes 1/2: forward to the delegate iterator.
            builder_->SetInsertPoint(fwdBB);
            emitRearmTryHandlers(inst->tryCatchTargets);
            llvm::Value* fwdArg = builder_->CreateCall(
                getResumedFn, { asyncContext_ }, "agen_fwd_arg");
            llvm::Value* fwdIter = builder_->CreateCall(
                getDelegateFn, { asyncContext_ }, "agen_fwd_iter");
            auto resumeFwdFn = getOrDeclareRuntimeFunction(
                "ts_agen_delegate_resume", getGCPtrTy(),
                { getGCPtrTy(), getGCPtrTy(), builder_->getInt32Ty(),
                  getGCPtrTy() });
            llvm::Value* fwdResult = builder_->CreateCall(
                resumeFwdFn, { asyncContext_, fwdIter, mode, fwdArg },
                "agen_fwd_result");
            llvm::Value* genCompleted = builder_->CreateICmpEQ(
                fwdResult, llvm::ConstantPointerNull::get(getGCPtrTy()),
                "agen_fwd_completed");
            llvm::BasicBlock* fwdEndBB = builder_->GetInsertBlock();
            llvm::BasicBlock* completeBB = llvm::BasicBlock::Create(
                context_, "agen_ystar_fwd_complete", currentFunc);
            builder_->CreateCondBr(genCompleted, completeBB, checkBB);
            iterResult->addIncoming(fwdResult, fwdEndBB);

            // Generator completed inside the helper: balance the handler
            // stack (user handlers were re-armed above; the impl barrier is
            // armed in every state>=1 invocation) and suspend for good.
            builder_->SetInsertPoint(completeBB);
            emitSuspendHandlerPops(inst->tryCatchTargets.size());
            auto barrierPopFn = getOrDeclareRuntimeFunction(
                "ts_pop_exception_handler", builder_->getVoidTy(), {});
            builder_->CreateCall(barrierPopFn, {});
            builder_->CreateRetVoid();
        }

        currentYieldState_++;

        // Done: clear the delegate iterator; yield* evaluates to the final
        // result's value.
        builder_->SetInsertPoint(doneBB);
        llvm::Value* nullIter = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(context_, 0));
        builder_->CreateCall(setDelegateFn, { asyncContext_, nullIter });
        llvm::Value* returnVal = builder_->CreateCall(
            valueFn, { iterResult }, "agen_delegate_return");
        setValue(inst->result, returnVal);
    } else if (isGeneratorFunction_ && asyncContext_ != nullptr && !isAsyncFunction_) {
        // State-machine generator: inline the delegation loop
        // The iterator is stored in ctx->delegateIterator so it persists across
        // state machine calls (each yield suspends and resumes the impl function).
        llvm::Function* currentFunc = builder_->GetInsertBlock()->getParent();

        // Get iterator from iterable
        auto getIterFn = getOrDeclareRuntimeFunction("ts_iterator_get",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* iterator = builder_->CreateCall(getIterFn, { iterableVal }, "delegate_iter");

        // Store iterator in ctx->delegateIterator (persists across state machine calls)
        auto setDelegateFn = getOrDeclareRuntimeFunction("ts_async_context_set_delegate_iterator",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(setDelegateFn, { asyncContext_, iterator });

        // Create blocks for the delegation loop
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(context_, "yield_star_loop", currentFunc);
        llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(context_, "yield_star_done", currentFunc);

        builder_->CreateBr(loopBB);

        // Loop header: load iterator from ctx and call next()
        builder_->SetInsertPoint(loopBB);
        auto getDelegateFn = getOrDeclareRuntimeFunction("ts_async_context_get_delegate_iterator",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* curIter = builder_->CreateCall(getDelegateFn, { asyncContext_ }, "cur_iter");

        auto nextFn = getOrDeclareRuntimeFunction("ts_iterator_next",
            getGCPtrTy(), { getGCPtrTy(), getGCPtrTy() });
        llvm::Value* nullVal = llvm::ConstantPointerNull::get(llvm::PointerType::get(context_, 0));
        llvm::Value* iterResult = builder_->CreateCall(nextFn, { curIter, nullVal }, "iter_result");

        // Check if done
        auto doneFn = getOrDeclareRuntimeFunction("ts_iterator_result_done",
            builder_->getInt1Ty(), { getGCPtrTy() });
        llvm::Value* isDone = builder_->CreateCall(doneFn, { iterResult }, "is_done");

        // Create yield block (not done - yield the value)
        llvm::BasicBlock* yieldBB = llvm::BasicBlock::Create(context_, "yield_star_yield", currentFunc);
        builder_->CreateCondBr(isDone, doneBB, yieldBB);

        builder_->SetInsertPoint(yieldBB);

        // Extract value from iterator result
        auto valueFn = getOrDeclareRuntimeFunction("ts_iterator_result_value",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* yieldVal = builder_->CreateCall(valueFn, { iterResult }, "delegate_value");

        // Yield the value using state machine mechanism
        auto yieldFn = getOrDeclareRuntimeFunction("ts_async_context_yield",
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() });
        builder_->CreateCall(yieldFn, { asyncContext_, yieldVal });

        // Set state to next state
        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn, { asyncContext_, builder_->getInt32(nextState) });

        // GEN-001 Stage 6 pop balance: pop the user try handlers armed at
        // this yield* before the impl returns (see lowerYield).
        emitSuspendHandlerPops(inst->tryCatchTargets.size());

        // Return from impl function (suspend)
        builder_->CreateRetVoid();

        // Resume block: after next() is called again, we resume here and loop back
        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            llvm::BasicBlock* resumeBlock = yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(resumeBlock);
            // GEN-001 Stage 6 re-arm: re-push the enclosing try scopes'
            // handlers in this invocation's frame before re-entering the
            // delegation loop.
            emitRearmTryHandlers(inst->tryCatchTargets);
            // Loop back to check next delegate value
            builder_->CreateBr(loopBB);
        }

        currentYieldState_++;

        // Done block: delegation is complete, clear delegate iterator and extract return value
        builder_->SetInsertPoint(doneBB);
        // Clear the delegate iterator
        llvm::Value* nullIter = llvm::ConstantPointerNull::get(llvm::PointerType::get(context_, 0));
        builder_->CreateCall(setDelegateFn, { asyncContext_, nullIter });
        llvm::Value* returnVal = builder_->CreateCall(valueFn, { iterResult }, "delegate_return");
        setValue(inst->result, returnVal);
    } else if (isAsyncFunction_ && isGeneratorFunction_) {
        // Async generator: yield* delegates to async iterable
        // For now, use the simple runtime function approach
        auto yieldStarFn = getOrDeclareRuntimeFunction("ts_async_generator_yield_star",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldStarFn, { iterableVal }, "async_yield_star_result");
        setValue(inst->result, result);
    } else {
        // Fallback: call runtime function
        auto yieldStarFn = getOrDeclareRuntimeFunction("ts_generator_yield_star",
            getGCPtrTy(), { getGCPtrTy() });
        llvm::Value* result = builder_->CreateCall(yieldStarFn, { iterableVal }, "yield_star_result");
        setValue(inst->result, result);
    }
}

//==============================================================================
// Runtime Function Helpers
//==============================================================================


}  // namespace ts::hir
