#include "HIRToLLVM_Internal.h"

namespace ts::hir {


void HIRToLLVM::lowerCall(HIRInstruction* inst) {
    std::string funcName = getOperandString(inst->operands[0]);

    // ts_tdz_sentinel() returns the compile-time constant NANBOX_TDZ (0x9)
    // unconditionally — materialize the constant instead of a runtime call.
    // Block-scoped let/const re-seed their slots at every block ENTRY, so a
    // hot loop body paid one call per declaration per iteration (6 calls/
    // iteration in the SoA benchmark's inner loop, ~186M total). As a
    // Constant it's also exempt from GC pinning, and DSE can drop the
    // seed store entirely when the declaration's own store follows.
    if (funcName == "ts_tdz_sentinel" && inst->operands.size() == 1) {
        llvm::Value* sentinel = llvm::ConstantExpr::getIntToPtr(
            llvm::ConstantInt::get(builder_->getInt64Ty(), 9), getGCPtrTy());
        if (inst->result) setValue(inst->result, sentinel);
        return;
    }

    // "use fast": unary Math runtime calls with bit-exact LLVM intrinsic
    // equivalents lower to the intrinsic. An out-of-line ts_math_sqrt call
    // per iteration was the last non-native op in the SoA benchmark's inner
    // loop — as an intrinsic it becomes a single sqrtsd and stops blocking
    // the vectorizer. Only intrinsics with exactly ECMA-262 semantics are
    // mapped (sqrt/fabs/floor/ceil/trunc; NOT round — llvm.round is
    // half-away-from-zero, JS is half-up).
    if (fastAny_ && inst->operands.size() == 2) {
        static const std::unordered_map<std::string, llvm::Intrinsic::ID> kMathIntrinsic = {
            {"ts_math_sqrt",  llvm::Intrinsic::sqrt},
            {"ts_math_abs",   llvm::Intrinsic::fabs},
            {"ts_math_floor", llvm::Intrinsic::floor},
            {"ts_math_ceil",  llvm::Intrinsic::ceil},
            {"ts_math_trunc", llvm::Intrinsic::trunc},
        };
        auto iit = kMathIntrinsic.find(funcName);
        if (iit != kMathIntrinsic.end()) {
            llvm::Value* a = getOperandValue(inst->operands[1]);
            if (a) {
                llvm::Type* t = a->getType();
                if (t->isIntegerTy())
                    a = builder_->CreateSIToFP(a, builder_->getDoubleTy());
                else if (t->isPointerTy()) {
                    auto ft = llvm::FunctionType::get(builder_->getDoubleTy(),
                                                      { getGCPtrTy() }, false);
                    auto fn = module_->getOrInsertFunction("ts_value_get_double", ft);
                    a = builder_->CreateCall(ft, fn.getCallee(), { a });
                }
                if (a->getType()->isDoubleTy()) {
                    llvm::Function* intr = llvm::Intrinsic::getDeclaration(
                        module_.get(), iit->second, { builder_->getDoubleTy() });
                    llvm::Value* r = builder_->CreateCall(intr, { a });
                    if (inst->result) setValue(inst->result, r);
                    return;
                }
            }
        }
    }

    // GEN-001 Stage 3: in suspendable-agen mode the body-started marker IS
    // suspension point 0 -> 1 (D5). Lower it like a yield's suspend/resume
    // sequence minus value plumbing: set_state(1) + ret void + relocate to
    // resume block 1. No runtime call to ts_async_generator_body_started is
    // emitted, and NO handler pop: the state-0 (param prologue) invocation
    // never pushed the impl barrier.
    if (inSuspendableAgenMode_ && funcName == "ts_async_generator_body_started") {
        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn,
            { asyncContext_, builder_->getInt32(nextState) });
        llvm::BasicBlock* suspendedBlock = builder_->GetInsertBlock();
        builder_->CreateRetVoid();

        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            // Record the suspension-relocation hop so lowerPhi's predecessor
            // DFS can follow emission across the `ret void` (Stage 4b).
            agenSuspendRelocation_[suspendedBlock] =
                yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(yieldResumeBlocks_[currentYieldState_]);
            // GEN-001 Stage 6: the marker is the SuspendedStart suspension —
            // a gen.throw()/gen.return() issued before the first next() must
            // be delivered here, not run the body as if next() was called.
            // No user try encloses the prologue, so no re-arm targets: mode 1
            // ts_throws straight into the impl barrier (reject + done), mode
            // 2 takes the forced-return block ({value, done:true}). Mode 0
            // falls through into the body; the resumed value is unused (the
            // first next()'s argument is discarded per spec).
            emitAgenResumeModeDispatch({});
        }
        currentYieldState_++;
        return;
    }

    // Eager-param SYNC generator: the body-started marker is suspension point
    // 0 -> 1. Lower like a yield's suspend minus value plumbing: set_state(1) +
    // ret void + relocate to resume block. No ts_async_context_yield is
    // emitted, so `yielded` stays false and the wrapper's eager invocation just
    // returns the generator; the first next() resumes the body here. A throw in
    // the state-0 parameter prologue propagates straight out of the wrapper
    // (no impl barrier / no handler to pop in state 0).
    if (isGeneratorFunction_ && !isAsyncFunction_ && asyncContext_ != nullptr &&
        funcName == "ts_generator_body_started") {
        int nextState = currentYieldState_ + 1;
        auto setStateFn = getOrDeclareRuntimeFunction("ts_async_context_set_state",
            builder_->getVoidTy(), { getGCPtrTy(), builder_->getInt32Ty() });
        builder_->CreateCall(setStateFn,
            { asyncContext_, builder_->getInt32(nextState) });
        llvm::BasicBlock* suspendedBlock = builder_->GetInsertBlock();
        builder_->CreateRetVoid();
        if (currentYieldState_ < static_cast<int>(yieldResumeBlocks_.size())) {
            agenSuspendRelocation_[suspendedBlock] =
                yieldResumeBlocks_[currentYieldState_];
            builder_->SetInsertPoint(yieldResumeBlocks_[currentYieldState_]);
        }
        currentYieldState_++;
        return;
    }

    if (funcName.find("_constructor") != std::string::npos) {
        SPDLOG_DEBUG("lowerCall: constructor call '{}' in func '{}'",
            funcName, currentFunction_ ? currentFunction_->getName().str() : "null");
    }

    // Try handler registry first - this is the new modular approach
    if (auto* result = HandlerRegistry::instance().tryLower(funcName, inst, *this)) {
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // NOTE: Console functions (ts_console_log, etc.) are now handled by ConsoleHandler
    // via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // Handle ts_value_to_bool - returns bool (i1), not ptr
    if (funcName == "ts_value_to_bool") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::Value* result;
        if (arg->getType()->isIntegerTy(1)) {
            // Already a boolean - use directly
            result = arg;
        } else if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getInt1Ty(), { getGCPtrTy() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_to_bool", ft);
            result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        } else if (arg->getType()->isDoubleTy()) {
            // Double: truthiness = not zero and not NaN
            result = builder_->CreateFCmpONE(arg,
                llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "tobool");
        } else if (arg->getType()->isIntegerTy(64)) {
            // i64: truthiness = not zero
            result = builder_->CreateICmpNE(arg,
                llvm::ConstantInt::get(builder_->getInt64Ty(), 0), "tobool");
        } else {
            // Fallback: cast to ptr and call ts_value_to_bool
            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder_->getInt1Ty(), { getGCPtrTy() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_to_bool", ft);
            llvm::Value* ptrArg = builder_->CreateIntToPtr(arg, getGCPtrTy());
            result = builder_->CreateCall(ft, fn.getCallee(), { ptrArg });
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_is_undefined - inline NaN boxing check (0x0A) + nullptr
    // Runtime semantics: nullptr is also treated as undefined (for default params)
    if (funcName == "ts_value_is_undefined") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::Value* result;
        if (arg->getType()->isPointerTy()) {
            // Inline: ptrtoint == NANBOX_UNDEFINED (0x0A) || arg == nullptr
            llvm::Value* raw = builder_->CreatePtrToInt(arg, builder_->getInt64Ty(), "nb.is_undef_raw");
            llvm::Value* isUndef = builder_->CreateICmpEQ(raw,
                llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0A), "nb.is_undef");
            llvm::Value* isNull = builder_->CreateICmpEQ(arg,
                llvm::ConstantPointerNull::get(getGCPtrTy()), "nb.is_nullptr");
            result = builder_->CreateOr(isUndef, isNull, "nb.is_undef_or_null");
        } else {
            // Non-pointer types (double, i64, i1) are never undefined
            result = builder_->getFalse();
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_is_null - inline NaN boxing check (0x02) + raw nullptr check
    if (funcName == "ts_value_is_null") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Inline: ptrtoint == NANBOX_NULL (0x02) || arg == nullptr
        llvm::Value* raw = builder_->CreatePtrToInt(arg, builder_->getInt64Ty(), "nb.is_null_raw");
        llvm::Value* boxedCheck = builder_->CreateICmpEQ(raw,
            llvm::ConstantInt::get(builder_->getInt64Ty(), 0x02), "nb.is_null_boxed");
        llvm::Value* rawNullCheck = builder_->CreateICmpEQ(arg, llvm::ConstantPointerNull::get(getGCPtrTy()));
        llvm::Value* result = builder_->CreateOr(boxedCheck, rawNullCheck, "nb.is_null");
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_is_nullish - inline NaN boxing check (0x02 || 0x0A || nullptr)
    if (funcName == "ts_value_is_nullish") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::Value* raw = builder_->CreatePtrToInt(arg, builder_->getInt64Ty(), "nb.is_nullish_raw");
        llvm::Value* isNull = builder_->CreateICmpEQ(raw,
            llvm::ConstantInt::get(builder_->getInt64Ty(), 0x02), "nb.is_null2");
        llvm::Value* isUndef = builder_->CreateICmpEQ(raw,
            llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0A), "nb.is_undef2");
        llvm::Value* isRawNull = builder_->CreateICmpEQ(arg, llvm::ConstantPointerNull::get(getGCPtrTy()));
        llvm::Value* result = builder_->CreateOr(builder_->CreateOr(isNull, isUndef), isRawNull, "nb.is_nullish");
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // with-write strict variants: trailing i64 strict flag (mixed
    // signatures mislink through the generic all-ptr lowering).
    if (funcName == "ts_with_set_ref_s" || funcName == "ts_with_set_ref_or_global_s" ||
        funcName == "ts_with_unref_fallback_set") {
        llvm::Value* a1 = getOperandValue(inst->operands[1]);
        llvm::Value* a2 = getOperandValue(inst->operands[2]);
        llvm::Value* a3 = getOperandValue(inst->operands[3]);
        llvm::Value* a4 = getOperandValue(inst->operands[4]);
        if (a4->getType()->isPointerTy())
            a4 = builder_->CreatePtrToInt(a4, builder_->getInt64Ty());
        else if (a4->getType()->isDoubleTy())
            a4 = builder_->CreateFPToSI(a4, builder_->getInt64Ty());
        bool returnsPtr = (funcName == "ts_with_set_ref_s");
        llvm::FunctionType* ft = llvm::FunctionType::get(
            returnsPtr ? (llvm::Type*)getGCPtrTy() : (llvm::Type*)builder_->getVoidTy(),
            { getGCPtrTy(), getGCPtrTy(), getGCPtrTy(), builder_->getInt64Ty() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a1, a2, a3, a4 });
        if (returnsPtr && inst->result) setValue(inst->result, result);
        return;
    }

    // ts_super_builtin_call(this, nameStr, argc:i64, a0) -> void. Mixed
    // i64 param — the generic lowering would declare all-ptr and mislink.
    if (funcName == "ts_super_builtin_call") {
        llvm::Value* a1 = getOperandValue(inst->operands[1]);
        llvm::Value* a2 = getOperandValue(inst->operands[2]);
        llvm::Value* a3 = getOperandValue(inst->operands[3]);
        llvm::Value* a4 = getOperandValue(inst->operands[4]);
        if (a3->getType()->isPointerTy())
            a3 = builder_->CreatePtrToInt(a3, builder_->getInt64Ty());
        else if (a3->getType()->isDoubleTy())
            a3 = builder_->CreateFPToSI(a3, builder_->getInt64Ty());
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy(), getGCPtrTy(), builder_->getInt64Ty(), getGCPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_super_builtin_call", ft);
        builder_->CreateCall(ft, fn.getCallee(), { a1, a2, a3, a4 });
        return;
    }

    // Handle ts_instanceof_array - returns bool (i1), not ptr. Same shape
    // as ts_array_is_array but also walks the prototype chain for
    // `class C extends Array` instances.
    if (funcName == "ts_instanceof_array") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_instanceof_array", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_array_is_array - returns bool (i1), not ptr
    if (funcName == "ts_array_is_array") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_is_array", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_make_bool - inline NaN boxing
    if (funcName == "ts_value_make_bool") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Convert to i1 for emitInlineBoxBool
        llvm::Value* boolVal;
        if (arg->getType()->isIntegerTy(1)) {
            boolVal = arg;
        } else if (arg->getType()->isIntegerTy(64)) {
            boolVal = builder_->CreateICmpNE(arg, llvm::ConstantInt::get(builder_->getInt64Ty(), 0), "to_bool");
        } else if (arg->getType()->isIntegerTy(32)) {
            boolVal = builder_->CreateICmpNE(arg, llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "to_bool");
        } else {
            boolVal = builder_->getTrue(); // fallback
        }
        llvm::Value* result = emitInlineBoxBool(boolVal);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_string_eq / ts_string_ne - returns bool (i1), not ptr
    // Arguments may be boxed TsValue* (from get_prop.static on dynamic objects) -
    // need to extract raw TsString* via ts_value_get_string before comparing.
    // ts_value_get_string handles both raw TsString* (magic check) and boxed TsValue*
    // (STRING_PTR type), so it's safe to always call.
    if (funcName == "ts_string_eq" || funcName == "ts_string_ne") {
        llvm::Value* a = getOperandValue(inst->operands[1]);
        llvm::Value* b = getOperandValue(inst->operands[2]);

        // Always extract raw TsString* via ts_value_get_string - it handles both
        // raw TsString* (by checking magic at offset 0) and boxed TsValue*
        // (by checking type field). This is safe and idempotent.
        llvm::FunctionType* getStrFt = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy() }, false);
        auto getStrFn = module_->getOrInsertFunction("ts_value_get_string", getStrFt);
        if (a->getType()->isPointerTy()) {
            a = builder_->CreateCall(getStrFt, getStrFn.getCallee(), { a }, "str_a");
        }
        if (b->getType()->isPointerTy()) {
            b = builder_->CreateCall(getStrFt, getStrFn.getCallee(), { b }, "str_b");
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_eq", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { a, b });
        if (funcName == "ts_string_ne") {
            result = builder_->CreateNot(result, "str_ne");
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_object_has_property - returns bool (i1), not ptr
    if (funcName == "ts_object_has_property") {
        llvm::Value* obj = getOperandValue(inst->operands[1]);
        llvm::Value* key = getOperandValue(inst->operands[2]);
        // Box key and obj to ptr if needed (e.g., `0 in params` passes double literal)
        if (key->getType()->isDoubleTy()) {
            key = emitInlineBoxFloat(key);
        } else if (key->getType()->isIntegerTy(64)) {
            key = emitInlineBoxInt(key);
        } else if (key->getType()->isIntegerTy(1)) {
            auto ft2 = llvm::FunctionType::get(getGCPtrTy(), {builder_->getInt1Ty()}, false);
            auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", ft2);
            key = builder_->CreateCall(ft2, boxFn.getCallee(), {key});
        }
        if (obj->getType()->isDoubleTy()) {
            obj = emitInlineBoxFloat(obj);
        } else if (obj->getType()->isIntegerTy(64)) {
            obj = emitInlineBoxInt(obj);
        } else if (obj->getType()->isIntegerTy(1)) {
            auto ft2 = llvm::FunctionType::get(getGCPtrTy(), {builder_->getInt1Ty()}, false);
            auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", ft2);
            obj = builder_->CreateCall(ft2, boxFn.getCallee(), {obj});
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_has_property", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, key });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle string conversion functions - they take specific types, not ptr
    // If the argument is a pointer (boxed value), unbox it first
    if (funcName == "ts_string_from_int") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the int
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getInt64Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_int");
        } else if (arg->getType()->isDoubleTy()) {
            // Math.floor/ceil/etc. return double; convert to i64
            arg = builder_->CreateFPToSI(arg, builder_->getInt64Ty());
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { builder_->getInt64Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_int", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_double") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the double
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getDoubleTy(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_double");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { builder_->getDoubleTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_double", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_bool") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // If arg is a pointer (boxed value), unbox it to get the bool
        if (arg->getType()->isPointerTy()) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getInt1Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg }, "unbox_bool");
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { builder_->getInt1Ty() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_string_from_bool", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (funcName == "ts_string_from_value") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        llvm::Value* result;
        if (arg->getType()->isIntegerTy(64)) {
            // i64 -> string via ts_string_from_int
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
            auto fn = module_->getOrInsertFunction("ts_string_from_int", ft);
            result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        } else if (arg->getType()->isDoubleTy()) {
            // f64 -> string via ts_string_from_double
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_string_from_double", ft);
            result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        } else if (arg->getType()->isIntegerTy(1)) {
            // bool -> string via ts_string_from_bool
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt1Ty() }, false);
            auto fn = module_->getOrInsertFunction("ts_string_from_bool", ft);
            result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        } else {
            // Default: treat as ptr (boxed TsValue*) -> ts_string_from_value
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_string_from_value", ft);
            result = builder_->CreateCall(ft, fn.getCallee(), { arg });
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // NOTE: Array functions (ts_array_create, ts_array_concat, ts_array_push) are now
    // handled by ArrayHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    if (funcName == "ts_object_install_accessor_dynamic") {
        // (TsValue* obj, TsValue* key, TsValue* fn, i64 isSetter) -> void
        llvm::Value* objArg = boxPrimitiveToPtr(getOperandValue(inst->operands[1]));
        llvm::Value* keyArg = boxPrimitiveToPtr(getOperandValue(inst->operands[2]));
        llvm::Value* fnArg  = boxPrimitiveToPtr(getOperandValue(inst->operands[3]));
        llvm::Value* setArg = getOperandValue(inst->operands[4]);
        if (setArg->getType()->isPointerTy()) {
            setArg = builder_->CreatePtrToInt(setArg, builder_->getInt64Ty());
        } else if (!setArg->getType()->isIntegerTy(64)) {
            setArg = builder_->CreateZExt(setArg, builder_->getInt64Ty());
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy(), getGCPtrTy(), getGCPtrTy(), builder_->getInt64Ty() },
            false);
        llvm::FunctionCallee fn =
            module_->getOrInsertFunction("ts_object_install_accessor_dynamic", ft);
        builder_->CreateCall(ft, fn.getCallee(),
                             { objArg, keyArg, fnArg, setArg });
        return;
    }

    if (funcName == "ts_object_assign") {
        // ts_object_assign(TsValue* target, TsValue* source) - returns TsValue*
        // Object.assign can have multiple sources: Object.assign(target, src1, src2, ...)
        // We need to call ts_object_assign for each source in sequence.
        // Primitive target/source operands (i1/i64/double) are boxed via
        // boxPrimitiveToPtr so the runtime signature ((ptr, ptr) -> ptr)
        // is honored — Object.assign(true, src) etc.
        llvm::Value* target = boxPrimitiveToPtr(getOperandValue(inst->operands[1]));
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_assign", ft);

        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* source = boxPrimitiveToPtr(getOperandValue(inst->operands[i]));
            target = builder_->CreateCall(ft, fn.getCallee(), { target, source });
        }

        if (inst->result) {
            setValue(inst->result, target);
        }
        return;
    }

    // NOTE: Math.* functions (floor, ceil, round, trunc, abs, sqrt, sin, cos, tan, log, exp,
    // sign, fround, cbrt, sinh, cosh, tanh, asinh, acosh, atanh, asin, acos, atan, expm1,
    // log10, log2, log1p, pow, atan2, hypot, min, max, random, clz32, imul) are now handled
    // by MathHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // NOTE: Timer functions (setTimeout, setInterval, setImmediate, clearTimeout, clearInterval,
    // clearImmediate) are now handled by TimerHandler via HandlerRegistry - dead inline code
    // removed in HIR-004 Phase 6

    // NOTE: Number.* functions (isFinite, isNaN, isInteger, isSafeInteger) are now handled
    // by MathHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // Handle Object.is(value1, value2) - needs to box both arguments
    if (funcName == "ts_object_is") {
        // Pad missing args with undefined NaN-box (i64 10 → ptr).
        // Object.is(undefined, undefined) returns true; this matches spec
        // for `Object.is()` (zero args) and `Object.is(x)` (one arg).
        llvm::Value* undefBoxed = builder_->CreateIntToPtr(
            llvm::ConstantInt::get(builder_->getInt64Ty(), 10),
            getGCPtrTy());
        llvm::Value* val1 = inst->operands.size() > 1
            ? getOperandValue(inst->operands[1]) : undefBoxed;
        llvm::Value* val2 = inst->operands.size() > 2
            ? getOperandValue(inst->operands[2]) : undefBoxed;

        // Box val1 based on type. ts_value_make_bool's canonical signature
        // is ptr(i32), so widen i1 → i32 first.
        if (val1->getType()->isIntegerTy(64)) {
            val1 = builder_->CreateCall(getTsValueMakeInt(), { val1 });
        } else if (val1->getType()->isDoubleTy()) {
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
            val1 = builder_->CreateCall(ft, fn.getCallee(), { val1 });
        } else if (val1->getType()->isIntegerTy(1)) {
            llvm::Value* w = builder_->CreateZExt(val1, builder_->getInt32Ty());
            val1 = builder_->CreateCall(getTsValueMakeBool(), { w });
        }
        // For pointers, assume already boxed or wrap if needed
        if (!val1->getType()->isPointerTy()) {
            val1 = builder_->CreateIntToPtr(val1, getGCPtrTy());
        }

        // Box val2 based on type
        if (val2->getType()->isIntegerTy(64)) {
            val2 = builder_->CreateCall(getTsValueMakeInt(), { val2 });
        } else if (val2->getType()->isDoubleTy()) {
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
            val2 = builder_->CreateCall(ft, fn.getCallee(), { val2 });
        } else if (val2->getType()->isIntegerTy(1)) {
            llvm::Value* w = builder_->CreateZExt(val2, builder_->getInt32Ty());
            val2 = builder_->CreateCall(getTsValueMakeBool(), { w });
        }
        if (!val2->getType()->isPointerTy()) {
            val2 = builder_->CreateIntToPtr(val2, getGCPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_object_is", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { val1, val2 });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // NOTE: BigInt functions (ts_bigint_create_str, ts_bigint_add, ts_bigint_sub,
    // ts_bigint_mul, ts_bigint_div, ts_bigint_mod, ts_bigint_lt, ts_bigint_le,
    // ts_bigint_gt, ts_bigint_ge, ts_bigint_eq, ts_bigint_ne) are now handled by
    // BigIntHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // NOTE: Array methods (ts_array_find, ts_array_findLast, ts_array_findIndex,
    // ts_array_findLastIndex, ts_array_some, ts_array_every, ts_array_slice, ts_array_concat)
    // are now handled by ArrayHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // NOTE: Map and Set functions (ts_map_set, ts_map_get, ts_map_has, ts_map_delete, ts_map_clear,
    // ts_map_size, ts_set_add, ts_set_has, ts_set_delete, ts_set_clear, ts_set_size) are now handled
    // by MapSetHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6
    // The boxForMapSet helper has also been removed as MapSetHandler has its own implementation.

    // NOTE: Path functions (ts_path_basename, ts_path_dirname, ts_path_extname, ts_path_join,
    // ts_path_normalize, ts_path_resolve, ts_path_relative, ts_path_isAbsolute) are now handled
    // by PathHandler via HandlerRegistry - dead inline code removed in HIR-004 Phase 6

    // Handle JSON module functions
    if (funcName == "ts_json_stringify") {
        // ts_json_stringify(void* obj, void* replacer, void* space) -> TsString*
        // replacer and space are optional - pass null if not provided
        // Helper: box any primitive (i64/i1/i32/double) into a TsValue ptr.
        auto boxAny = [&](llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isPointerTy()) return v;
            if (v->getType()->isIntegerTy(64)) {
                return builder_->CreateCall(getTsValueMakeInt(), { v });
            }
            if (v->getType()->isIntegerTy(1)) {
                llvm::Value* w = builder_->CreateZExt(v, builder_->getInt32Ty());
                return builder_->CreateCall(getTsValueMakeBool(), { w });
            }
            if (v->getType()->isIntegerTy(32)) {
                return builder_->CreateCall(getTsValueMakeBool(), { v });
            }
            if (v->getType()->isDoubleTy()) {
                auto ft0 = llvm::FunctionType::get(
                    getGCPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn0 = module_->getOrInsertFunction("ts_value_make_double", ft0);
                return builder_->CreateCall(ft0, fn0.getCallee(), { v });
            }
            return builder_->CreateIntToPtr(v, getGCPtrTy());
        };
        llvm::Value* obj = inst->operands.size() > 1
            ? boxAny(getOperandValue(inst->operands[1]))
            : llvm::ConstantPointerNull::get(getGCPtrTy());
        llvm::Value* replacer = llvm::ConstantPointerNull::get(getGCPtrTy());
        llvm::Value* space = llvm::ConstantPointerNull::get(getGCPtrTy());
        if (inst->operands.size() > 2) {
            replacer = boxAny(getOperandValue(inst->operands[2]));
        }
        if (inst->operands.size() > 3) {
            space = getOperandValue(inst->operands[3]);
            // Box space argument if it's a primitive (e.g., JSON.stringify(obj, null, 2))
            if (space->getType()->isIntegerTy(64)) {
                llvm::FunctionType* boxFt = llvm::FunctionType::get(
                    getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                space = builder_->CreateCall(boxFt, boxFn.getCallee(), { space });
            } else if (space->getType()->isDoubleTy()) {
                // Convert double to int first, then box
                llvm::Value* intSpace = builder_->CreateFPToSI(space, builder_->getInt64Ty());
                llvm::FunctionType* boxFt = llvm::FunctionType::get(
                    getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                space = builder_->CreateCall(boxFt, boxFn.getCallee(), { intSpace });
            } else if (space->getType()->isIntegerTy(1)) {
                space = llvm::ConstantPointerNull::get(getGCPtrTy());
            }
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy(), getGCPtrTy(), getGCPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_json_stringify", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, replacer, space });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle parseInt(string, radix?). Dispatch to ts_parseInt_radix which
    // accepts an optional radix and handles ECMA-262 §19.2.5 semantics:
    // - radix == 0 || undefined: auto-detect (0x prefix → 16, else 10)
    // - radix == 16: strip 0x prefix if present
    // - other radix: parse with that radix
    // - returns NaN (boxed double) for unparseable
    if (funcName == "parseInt") {
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Box arg if primitive so runtime sees TsValue*
        if (arg->getType()->isIntegerTy(64)) {
            arg = builder_->CreateCall(getTsValueMakeInt(), { arg });
        } else if (arg->getType()->isDoubleTy()) {
            arg = builder_->CreateCall(getTsValueMakeDouble(), { arg });
        } else if (arg->getType()->isIntegerTy(1)) {
            llvm::Value* widened = builder_->CreateZExt(arg, builder_->getInt32Ty());
            arg = builder_->CreateCall(getTsValueMakeBool(), { widened });
        } else if (arg->getType()->isIntegerTy(32)) {
            arg = builder_->CreateCall(getTsValueMakeBool(), { arg });
        }
        // Radix: explicit arg or undefined sentinel.
        llvm::Value* radixArg;
        if (inst->operands.size() >= 3) {
            radixArg = getOperandValue(inst->operands[2]);
            if (radixArg->getType()->isIntegerTy(64)) {
                radixArg = builder_->CreateCall(getTsValueMakeInt(), { radixArg });
            } else if (radixArg->getType()->isDoubleTy()) {
                radixArg = builder_->CreateCall(getTsValueMakeDouble(), { radixArg });
            }
        } else {
            radixArg = llvm::ConstantPointerNull::get(getGCPtrTy());
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy(), getGCPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_parseInt_radix", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { arg, radixArg });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle global isNaN/isFinite -> redirect to Number.isNaN/isFinite handlers
    if (funcName == "isNaN" || funcName == "isFinite") {
        // No arg → coerce undefined to NaN. isNaN(undefined)=true,
        // isFinite(undefined)=false. Use NaN sentinel for both.
        if (inst->operands.size() < 2) {
            llvm::Value* result = funcName == "isNaN"
                ? builder_->getInt1(true)
                : builder_->getInt1(false);
            if (inst->result) setValue(inst->result, result);
            return;
        }
        llvm::Value* arg = getOperandValue(inst->operands[1]);
        // Global isNaN/isFinite coerce to number first, but for our purposes
        // we can use the Number.* versions which work on doubles
        if (arg->getType()->isPointerTy()) {
            // Boxed value: spec ToNumber (ES 19.2.2/19.2.3) — throws
            // TypeError on a Symbol (incl. @@toPrimitive returning one);
            // ts_value_get_double silently coerced to NaN.
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(
                builder_->getDoubleTy(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_to_number", unboxFt);
            arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { arg });
        } else if (arg->getType()->isIntegerTy(64)) {
            arg = builder_->CreateSIToFP(arg, builder_->getDoubleTy());
        } else if (arg->getType()->isIntegerTy(1)) {
            arg = builder_->CreateUIToFP(arg, builder_->getDoubleTy());
        }
        // Now arg is double - use LLVM intrinsics directly
        llvm::Value* result;
        if (funcName == "isNaN") {
            // isNaN(x) = x != x (IEEE 754)
            result = builder_->CreateFCmpUNO(arg, arg);
        } else {
            // isFinite(x) = !isNaN(x) && x != +inf && x != -inf
            llvm::Value* isNotNaN = builder_->CreateFCmpORD(arg, arg);
            llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), false);
            llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder_->getDoubleTy(), true);
            llvm::Value* notPosInf = builder_->CreateFCmpONE(arg, posInf);
            llvm::Value* notNegInf = builder_->CreateFCmpONE(arg, negInf);
            result = builder_->CreateAnd(isNotNaN, builder_->CreateAnd(notPosInf, notNegInf));
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_strict_eq - returns ptr (boxed bool TsValue*), takes two ptr args
    if (funcName == "ts_value_strict_eq") {
        llvm::Value* lhs = getOperandValue(inst->operands[1]);
        llvm::Value* rhs = getOperandValue(inst->operands[2]);
        // Ensure both args are pointers (box if needed)
        if (!lhs->getType()->isPointerTy()) {
            lhs = boxArgumentForDynamicCall(lhs, inst->operands[1]);
        }
        if (!rhs->getType()->isPointerTy()) {
            rhs = boxArgumentForDynamicCall(rhs, inst->operands[2]);
        }
        auto ft = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        auto fn = module_->getOrInsertFunction("ts_value_strict_eq", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { lhs, rhs }, "strict_eq");
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Handle ts_value_strict_eq_bool - returns bool (i1), takes two ptr args
    if (funcName == "ts_value_strict_eq_bool") {
        llvm::Value* lhs = getOperandValue(inst->operands[1]);
        llvm::Value* rhs = getOperandValue(inst->operands[2]);
        // Ensure both args are pointers (box if needed)
        if (!lhs->getType()->isPointerTy()) {
            lhs = boxArgumentForDynamicCall(lhs, inst->operands[1]);
        }
        if (!rhs->getType()->isPointerTy()) {
            rhs = boxArgumentForDynamicCall(rhs, inst->operands[2]);
        }
        auto ft = llvm::FunctionType::get(
            builder_->getInt1Ty(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        auto fn = module_->getOrInsertFunction("ts_value_strict_eq_bool", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { lhs, rhs }, "strict_eq");
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // String.indexOf with start position: redirect to ts_string_indexOf_from
    if ((funcName == "ts_string_indexOf" || funcName == "ts_path_indexOf") && inst->operands.size() >= 4) {
        auto* spec = ::hir::LoweringRegistry::instance().lookup("ts_string_indexOf_from");
        if (spec) {
            llvm::Value* result = lowerRegisteredCall(inst, *spec);
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Array.indexOf/lastIndexOf/includes with fromIndex: redirect to _from variant.
    // Operand layout: [funcName, receiver, value, fromIndex]
    if (inst->operands.size() >= 4 &&
        (funcName == "ts_array_indexOf" || funcName == "ts_array_lastIndexOf" ||
         funcName == "ts_array_includes")) {
        auto* spec = ::hir::LoweringRegistry::instance().lookup(funcName + "_from");
        if (spec) {
            llvm::Value* result = lowerRegisteredCall(inst, *spec);
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Try registry-based lowering for runtime functions
    if (auto* spec = ::hir::LoweringRegistry::instance().lookup(funcName)) {
        llvm::Value* result = lowerRegisteredCall(inst, *spec);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Generic function call
    // First, if the HIR module has a function whose unmangled name matches
    // funcName but whose mangledName differs (e.g., counter-form `inner_0`
    // produced by Monomorphizer for nested user functions), retarget
    // funcName to the mangled name so the call site resolves to the real
    // definition rather than creating a dangling external declaration
    // under the bare name.
    if (!module_->getFunction(funcName) && hirModule_) {
        // Counter-mangled match: funcName="inner" matches hirFn->name="inner_0"
        // (ASTToHIR appends `_<digits>` to nested function declarations to
        // disambiguate from possible same-named hoisted siblings). The
        // call site is emitted with the bare name; without retargeting,
        // we'd create a dangling external declaration `@inner` that the
        // linker can't resolve.
        auto isCounterSuffix = [](const std::string& full,
                                   const std::string& base) {
            if (full.size() <= base.size() + 1) return false;
            if (full.compare(0, base.size(), base) != 0) return false;
            if (full[base.size()] != '_') return false;
            for (size_t i = base.size() + 1; i < full.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(full[i])))
                    return false;
            }
            return true;
        };
        for (const auto& hirFn : hirModule_->functions) {
            const std::string& hn = hirFn->name;
            bool simpleMatch = (hn == funcName);
            bool counterMatch = isCounterSuffix(hn, funcName);
            if ((simpleMatch || counterMatch) &&
                !hirFn->mangledName.empty() &&
                hirFn->mangledName != funcName) {
                funcName = hirFn->mangledName;
                break;
            }
        }
    }
    llvm::Function* fn = module_->getFunction(funcName);
    if (!fn) {
        // Function not yet in LLVM module. Try to find it in the HIR module
        // to create a forward declaration with the correct signature.
        // This handles cross-module static method calls where the callee
        // is compiled after the caller (e.g., imported class static methods).
        bool foundInHIR = false;
        if (hirModule_) {
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->mangledName == funcName || hirFn->name == funcName) {
                    std::vector<llvm::Type*> paramTypes;
                    for (const auto& param : hirFn->params) {
                        paramTypes.push_back(getLLVMType(param.second));
                    }
                    llvm::Type* retTy = hirFn->returnType ? getLLVMType(hirFn->returnType) : getGCPtrTy();
                    llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTypes, false);
                    // Always use the mangled name so the forward declaration
                    // matches the eventual definition's symbol.
                    const std::string& declName = hirFn->mangledName.empty()
                        ? funcName
                        : hirFn->mangledName;
                    fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, declName, module_.get());
                    funcName = declName;
                    foundInHIR = true;
                    break;
                }
            }
        }
        if (!foundInHIR) {
            // Declare as external with generic signature: ptr(ptr, ptr, ...)
            std::vector<llvm::Type*> paramTypes;
            for (size_t i = 1; i < inst->operands.size(); ++i) {
                paramTypes.push_back(getGCPtrTy());
            }
            // Special-case functions that return non-ptr types
            llvm::Type* retType = getGCPtrTy();
            if (funcName == "ts_to_number") {
                retType = builder_->getDoubleTy();
            }
            // Special-case ts_set_last_call_argc: void(i64) not ptr(ptr)
            if (funcName == "ts_set_last_call_argc") {
                paramTypes.clear();
                paramTypes.push_back(builder_->getInt64Ty());
                retType = builder_->getVoidTy();
            }
            // "use fast" NativeArray ctor: ptr ts_native_array_new(i64 length,
            // i64 allocKind). The generic all-ptr signature would mislink the
            // integer args; declare it explicitly. (get/set/dispose/length are
            // lowered with explicit signatures in their handlers, not here.)
            if (funcName == "ts_native_array_new") {
                paramTypes.clear();
                paramTypes.push_back(builder_->getInt64Ty());
                paramTypes.push_back(builder_->getInt64Ty());
                retType = getGCPtrTy();
            }
            // GC verification-harness builtins (GC-001): doubles / bool / void.
            if (funcName == "ts_gc_dbg_collection_count" ||
                funcName == "ts_gc_dbg_live_size" ||
                funcName == "ts_gc_verify_now") {
                retType = builder_->getDoubleTy();   // double()
            } else if (funcName == "ts_gc_dbg_is_nursery") {
                retType = builder_->getInt1Ty();     // bool(ptr)
            } else if (funcName == "ts_gc_minor_collect" ||
                       funcName == "ts_gc_force_collect") {
                retType = builder_->getVoidTy();     // void()
            }
            // Runtime symbols (prefix `ts_`) come from libtsruntime and must
            // be ExternalLinkage so the linker resolves them. Anything else
            // reaching this fallback is a user-defined / monomorphized call
            // target whose definition was never emitted (e.g. `print_any`,
            // `Proxy_any_any`, harness JS functions hoisted into
            // syntheticFunctions — see Monomorphizer.cpp:2164 findFunction
            // gap). Emit a stub returning `undefined` so the link succeeds
            // and any actual invocation produces a runtime-level result
            // rather than a confusing linker error. Mirrors the existing
            // lowerLoadFunction stub pattern at HIRToLLVM.cpp:7395.
            bool isRuntimeSymbol = funcName.size() >= 3 && funcName[0] == 't' &&
                                   funcName[1] == 's' && funcName[2] == '_';

            // A bare `ArrayBuffer(x)` (no `new`) lowers to a call of the builtin
            // ctor global `ArrayBuffer_any`, which the runtime defines as a
            // require-`new` guard that throws TypeError. These names are NOT
            // ts_-prefixed and return a GCPtr, so without this guard they fall
            // into the WeakAny varargs-stub path below. That is silently WRONG
            // under --shared-runtime: a `weak` local definition is not overridden
            // by a dllimport symbol (archive members load lazily; the weak def
            // already "resolves" the reference so the import thunk is never
            // pulled), so the stub's `return undefined` shadows the DLL's real
            // throwing guard and `ArrayBuffer()` silently returns undefined
            // instead of throwing. (Static links happen to work because
            // TsGlobals.obj is pulled in for other reasons and its STRONG
            // definition overrides the weak one.) Emitting an External variadic
            // DECLARATION instead forces the linker to resolve the real symbol
            // in BOTH modes. Narrowed to the 10 require-`new` ctors whose full
            // arity set (base + _any..._any_any_any_any) is confirmed exported by
            // tsruntime_shared.dll -- forcing External for a name the DLL does
            // not export (e.g. Date_any, Error_any) would break the link, so this
            // set MUST stay in sync with TS_CTOR_REQUIRES_NEW_STUBS in
            // TsGlobals.cpp. See memory shared-runtime-ctor-stub-divergence.
            bool isRequireNewCtorVariant = false;
            {
                static const std::unordered_set<std::string> kRequireNewCtors = {
                    "ArrayBuffer", "DataView", "FinalizationRegistry", "Map",
                    "Promise", "Proxy", "Set", "WeakMap", "WeakRef", "WeakSet"};
                std::string base = funcName;
                int anyGroups = 0;
                const std::string kAny = "_any";
                while (base.size() > kAny.size() &&
                       base.compare(base.size() - kAny.size(), kAny.size(), kAny) == 0) {
                    base.erase(base.size() - kAny.size());
                    if (++anyGroups > 4) break;  // macro only defines arities 0-4
                }
                if (anyGroups <= 4 && kRequireNewCtors.count(base)) {
                    isRequireNewCtorVariant = true;
                }
            }

            llvm::FunctionType* ft;
            if (isRequireNewCtorVariant) {
                // External variadic declaration -> resolves to the real runtime
                // guard (DLL export or static lib), which throws the require-`new`
                // TypeError. No weak body, so nothing to shadow it.
                ft = llvm::FunctionType::get(retType, {}, /*isVarArg=*/true);
                fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                            funcName, module_.get());
            } else if (!isRuntimeSymbol && retType == getGCPtrTy()) {
                // **Varargs stub**: harness JS functions (e.g. `assertThrowsInstanceOf`
                // from test262 sm shell) are called with DIFFERENT arities at
                // different sites (2 args at some calls, 3 at others). Declaring
                // the stub with the FIRST call's arity makes later calls with
                // different arity fail the verifier with "Incorrect number of
                // arguments passed to called function!". Declare with varargs
                // (`ptr (...)`) so any arity is accepted.
                ft = llvm::FunctionType::get(retType, {}, /*isVarArg=*/true);
                // WeakAnyLinkage: emit a stub body returning undefined, but
                // allow the linker to replace it with any STRONG definition
                // (e.g. real `parseFloat` in the runtime, real specialization
                // emitted by another TU). This prevents shadowing while still
                // resolving missing-symbol link errors for genuinely-undefined
                // monomorphizer specializations (e.g. `Proxy_any_any`,
                // `print_any` from harness JS hoisted into syntheticFunctions).
                fn = llvm::Function::Create(ft, llvm::Function::WeakAnyLinkage,
                                            funcName, module_.get());
                auto* bb = llvm::BasicBlock::Create(context_, "entry", fn);
                llvm::IRBuilder<> stubBuilder(bb);
                auto undefFn = module_->getOrInsertFunction(
                    "ts_value_make_undefined",
                    llvm::FunctionType::get(getGCPtrTy(), {}, false));
                stubBuilder.CreateRet(stubBuilder.CreateCall(undefFn));
            } else {
                ft = llvm::FunctionType::get(retType, paramTypes, false);
                fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, funcName, module_.get());
            }
        }
    }

    // Gather arguments, converting types to match function signature
    // For user-defined functions, look up the callee's HIR parameter types.
    // If a callee param is String-typed, skip boxing (callee expects raw TsString*).
    // If a callee param is Any-typed, still box (callee expects boxed TsValue*).
    auto userParamIt = userFunctionParams_.find(funcName);
    std::vector<llvm::Value*> args;
    llvm::FunctionType* fnType = fn->getFunctionType();
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        size_t paramIdx = i - 1;
        if (paramIdx < fnType->getNumParams()) {
            llvm::Type* expectedType = fnType->getParamType(paramIdx);
            // Pass the callee's HIR param type if this is a user function
            std::shared_ptr<HIRType> calleeParamType = nullptr;
            if (userParamIt != userFunctionParams_.end() && paramIdx < userParamIt->second.size()) {
                calleeParamType = userParamIt->second[paramIdx];
            }
            arg = coerceArgToType(arg, expectedType, inst->operands[i], calleeParamType);
        }
        args.push_back(arg);
    }

    // Pad missing optional arguments with defaults (for default parameters)
    while (args.size() < fnType->getNumParams()) {
        llvm::Type* expectedType = fnType->getParamType(args.size());
        if (expectedType->isPointerTy())
            args.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(expectedType)));
        else if (expectedType->isIntegerTy())
            args.push_back(llvm::ConstantInt::get(expectedType, 0));
        else if (expectedType->isDoubleTy())
            args.push_back(llvm::ConstantFP::get(expectedType, 0.0));
        else
            args.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
    }

    llvm::Value* result = builder_->CreateCall(fn, args);
    if (inst->result) {
        // The callee returns void but this call has a result value that a
        // consumer may read as an operand — e.g. `new`'s ts_construct_select
        // over a derived-class constructor whose return type is void (either
        // declared void, or propagated to void by TypePropagation after the
        // construct_select was lowered against an Any result). A void CreateCall
        // result cannot be passed as an argument (LLVM verify: "Call parameter
        // type does not match function signature"). Substitute `undefined` so the
        // consumer receives a valid GC pointer; a genuinely unused void result
        // is harmless.
        if (fn->getReturnType()->isVoidTy()) {
            auto undefFn = module_->getOrInsertFunction(
                "ts_value_make_undefined",
                llvm::FunctionType::get(getGCPtrTy(), {}, false));
            result = builder_->CreateCall(undefFn);
        }
        setValue(inst->result, result);
    }
}

//==============================================================================
// Registry-Based Call Lowering
//==============================================================================

llvm::Value* HIRToLLVM::lowerRegisteredCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // Check for variadic handling
    if (spec.variadicHandling != ::hir::VariadicHandling::None) {
        switch (spec.variadicHandling) {
            case ::hir::VariadicHandling::TypeDispatch:
                return lowerTypeDispatchCall(inst, spec);
            case ::hir::VariadicHandling::PackArray:
                return lowerPackArrayCall(inst, spec);
            case ::hir::VariadicHandling::Inline:
                // Inline handling is done elsewhere (e.g., Math.min/max)
                // Fall through to standard handling for now
                break;
            default:
                break;
        }
    }

    // Build LLVM function type
    llvm::Type* retTy = spec.returnType
        ? spec.returnType(context_)
        : builder_->getVoidTy();

    std::vector<llvm::Type*> argTys;
    for (const auto& argType : spec.argTypes) {
        argTys.push_back(argType(context_));
    }

    auto* ft = llvm::FunctionType::get(retTy, argTys, spec.isVariadic);
    auto fn = module_->getOrInsertFunction(spec.runtimeFuncName, ft);

    // For string methods (ts_string_*), the first argument is the string receiver.
    // It may be a boxed TsValue* (e.g. from array element access like argv[i]).
    // ts_value_get_string safely handles both raw TsString* and boxed TsValue*,
    // so we always unbox the receiver for string methods.
    bool isStringMethod = spec.runtimeFuncName.find("ts_string_") == 0
        && spec.runtimeFuncName.find("ts_string_decoder_") != 0
        // ts_string_ctor's first arg is a VALUE to stringify (String(value)),
        // not a string receiver — and it must accept a Symbol WITHOUT the
        // ts_value_get_string coercion (which throws on symbols).
        && spec.runtimeFuncName != "ts_string_ctor";

    // Convert arguments
    std::vector<llvm::Value*> llvmArgs;
    for (size_t i = 1; i < inst->operands.size() && (i - 1) < spec.argConversions.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);

        // Unbox string receiver for ts_string_* methods
        if (isStringMethod && i == 1 && arg->getType()->isPointerTy()) {
            auto getStrFt = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
            auto getStrFn = module_->getOrInsertFunction("ts_value_get_string", getStrFt);
            arg = builder_->CreateCall(getStrFt, getStrFn.getCallee(), { arg }, "str_recv");
        }

        // For Box conversion on pointer types, check if arg is a function pointer
        // Function pointers (llvm::Function*) must be wrapped via ts_value_make_function,
        // not ts_value_make_object, which would create an OBJECT_PTR that crashes in ts_extract_proxy.
        // Also check HIR type for function-typed values that aren't direct llvm::Function*.
        if (spec.argConversions[i - 1] == ::hir::ArgConversion::Box && arg->getType()->isPointerTy()) {
            bool isFunction = llvm::isa<llvm::Function>(arg);
            if (!isFunction) {
                // Also check HIR type for indirect function references
                auto* hirVal = std::get_if<std::shared_ptr<ts::hir::HIRValue>>(&inst->operands[i]);
                if (hirVal && *hirVal && (*hirVal)->type && (*hirVal)->type->kind == ts::hir::HIRTypeKind::Function)
                    isFunction = true;
            }
            bool isString = false;
            if (!isFunction) {
                auto* hirVal = std::get_if<std::shared_ptr<ts::hir::HIRValue>>(&inst->operands[i]);
                if (hirVal && *hirVal && (*hirVal)->type && (*hirVal)->type->kind == ts::hir::HIRTypeKind::String)
                    isString = true;
            }
            if (isFunction && llvm::isa<llvm::Function>(arg)) {
                // Generate a native function trampoline that adapts the calling convention.
                // AOT functions have typed parameters (double, ptr, i64, etc.) but the runtime
                // dispatch (ts_call_N) passes TsValue* boxed arguments.
                // The trampoline has the native calling convention: (void* ctx, int argc, TsValue** argv)
                // and unboxes arguments before calling the actual function.
                llvm::Function* targetFn = llvm::cast<llvm::Function>(arg);
                llvm::FunctionType* targetFnType = targetFn->getFunctionType();

                // Create trampoline function
                std::string trampolineName = targetFn->getName().str() + "__native_trampoline";
                auto* trampolineFnType = llvm::FunctionType::get(
                    getGCPtrTy(),
                    { getGCPtrTy(), builder_->getInt32Ty(), getGCPtrTy() },
                    false);
                auto* trampolineFn = llvm::Function::Create(
                    trampolineFnType, llvm::Function::InternalLinkage,
                    trampolineName, module_.get());

                // Save current insertion point
                auto savedIP = builder_->GetInsertPoint();
                auto* savedBB = builder_->GetInsertBlock();

                // Build trampoline body
                auto* trampolineEntry = llvm::BasicBlock::Create(context_, "entry", trampolineFn);
                builder_->SetInsertPoint(trampolineEntry);

                auto trampolineArgs = trampolineFn->arg_begin();
                llvm::Value* tramCtx = &*trampolineArgs++;   // void* ctx (unused)
                llvm::Value* tramArgc = &*trampolineArgs++;   // int argc
                llvm::Value* tramArgv = &*trampolineArgs++;   // TsValue** argv

                // Extract and unbox arguments from argv based on target function's parameter types
                std::vector<llvm::Value*> callArgs;
                for (unsigned pi = 0; pi < targetFnType->getNumParams(); ++pi) {
                    llvm::Type* paramType = targetFnType->getParamType(pi);
                    // Load argv[pi]
                    llvm::Value* idx = llvm::ConstantInt::get(builder_->getInt32Ty(), pi);
                    llvm::Value* argSlotPtr = builder_->CreateGEP(getGCPtrTy(), tramArgv, idx);
                    llvm::Value* boxedArg = builder_->CreateLoad(getGCPtrTy(), argSlotPtr);

                    if (paramType->isDoubleTy()) {
                        auto unboxFt = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
                        auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFt);
                        callArgs.push_back(builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedArg }));
                    } else if (paramType->isIntegerTy(64)) {
                        auto unboxFt = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                        auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFt);
                        callArgs.push_back(builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedArg }));
                    } else if (paramType->isIntegerTy(1)) {
                        auto unboxFt = llvm::FunctionType::get(builder_->getInt1Ty(), { getGCPtrTy() }, false);
                        auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
                        callArgs.push_back(builder_->CreateCall(unboxFt, unboxFn.getCallee(), { boxedArg }));
                    } else {
                        // Pointer type - pass through (might be TsValue* or raw ptr)
                        callArgs.push_back(boxedArg);
                    }
                }

                // Call the target function
                llvm::Value* callResult = builder_->CreateCall(targetFnType, targetFn, callArgs);

                // Box the result if needed
                llvm::Type* retType = targetFnType->getReturnType();
                if (retType->isVoidTy()) {
                    builder_->CreateRet(llvm::ConstantPointerNull::get(getGCPtrTy()));
                } else if (retType->isDoubleTy()) {
                    auto boxFt2 = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
                    auto boxFn2 = module_->getOrInsertFunction("ts_value_make_double", boxFt2);
                    llvm::Value* boxed = builder_->CreateCall(boxFt2, boxFn2.getCallee(), { callResult });
                    builder_->CreateRet(boxed);
                } else if (retType->isIntegerTy(64)) {
                    auto boxFt2 = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                    auto boxFn2 = module_->getOrInsertFunction("ts_value_make_int", boxFt2);
                    llvm::Value* boxed = builder_->CreateCall(boxFt2, boxFn2.getCallee(), { callResult });
                    builder_->CreateRet(boxed);
                } else {
                    // Pointer return - assume already boxed or raw
                    builder_->CreateRet(callResult);
                }

                // Restore insertion point
                builder_->SetInsertPoint(savedBB, savedIP);

                // Wrap trampoline as native function
                auto nativeFt = llvm::FunctionType::get(getGCPtrTy(),
                    { getGCPtrTy(), getGCPtrTy() }, false);
                auto nativeFn = module_->getOrInsertFunction("ts_value_make_native_function", nativeFt);
                auto nullCtx = llvm::ConstantPointerNull::get(getGCPtrTy());
                arg = builder_->CreateCall(nativeFt, nativeFn.getCallee(), { trampolineFn, nullCtx });
            } else if (isFunction) {
                // Non-direct function reference - fall back to ts_value_make_function
                auto boxFt = llvm::FunctionType::get(getGCPtrTy(),
                    { getGCPtrTy(), getGCPtrTy() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_function", boxFt);
                auto nullCtx = llvm::ConstantPointerNull::get(getGCPtrTy());
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { gcPtrToRaw(arg), nullCtx });
            } else if (isString) {
                auto boxFt = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_string", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { gcPtrToRaw(arg) });
            } else {
                arg = convertArg(arg, spec.argConversions[i - 1]);
            }
        } else {
            arg = convertArg(arg, spec.argConversions[i - 1]);
        }

        // Coerce to expected LLVM type if needed (e.g., f64 literal -> i64 param)
        size_t argIdx = i - 1;
        if (argIdx < argTys.size() && arg->getType() != argTys[argIdx]) {
            llvm::Type* expected = argTys[argIdx];
            if (arg->getType()->isDoubleTy() && expected->isIntegerTy(64))
                arg = emitSaturatingFPToSI(builder_.get(), arg, builder_->getInt64Ty());
            else if (arg->getType()->isDoubleTy() && expected->isIntegerTy(32))
                arg = emitSaturatingFPToSI(builder_.get(), arg, builder_->getInt32Ty());
            else if (arg->getType()->isIntegerTy(64) && expected->isDoubleTy())
                arg = builder_->CreateSIToFP(arg, builder_->getDoubleTy());
            else if (arg->getType()->isIntegerTy(64) && expected->isIntegerTy(32))
                arg = builder_->CreateTrunc(arg, builder_->getInt32Ty());
            else if (arg->getType()->isIntegerTy(32) && expected->isIntegerTy(64))
                arg = builder_->CreateSExt(arg, builder_->getInt64Ty());
            else if (arg->getType()->isIntegerTy(1) && expected->isIntegerTy(32))
                arg = builder_->CreateZExt(arg, builder_->getInt32Ty());
            else if (arg->getType()->isIntegerTy(1) && expected->isIntegerTy(64))
                arg = builder_->CreateZExt(arg, builder_->getInt64Ty());
            else if (arg->getType()->isPointerTy() && expected->isIntegerTy(64)) {
                // Pointer is likely a boxed TsValue* - unbox via ts_value_get_int
                auto getIntFt = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto getIntFn = module_->getOrInsertFunction("ts_value_get_int", getIntFt);
                arg = builder_->CreateCall(getIntFt, getIntFn.getCallee(), { arg });
            }
            else if (arg->getType()->isIntegerTy(64) && expected->isPointerTy()) {
                // i64 -> ptr: box as a properly NaN-boxed TsValue*.
                // Raw inttoptr would produce ptr null for the value 0
                // and unaligned junk for small ints — that bypasses
                // NaN-boxing entirely and breaks every runtime caller
                // that expects a real boxed value (the discovery case
                // was Object.defineProperty(obj, +0, {}) where the
                // numeric prop key arrived as i64 0 and became ptr null).
                // ts_value_make_int produces the correct NaN-tagged
                // representation, preserving the boxing scheme.
                arg = convertArg(arg, ::hir::ArgConversion::Box);
            }
            else if (arg->getType()->isIntegerTy(1) && expected->isPointerTy()) {
                // bool -> ptr: box as NaN-tagged TsValue*.
                arg = convertArg(arg, ::hir::ArgConversion::Box);
            }
            else if (arg->getType()->isDoubleTy() && expected->isPointerTy()) {
                // f64 -> ptr: box the double value
                arg = convertArg(arg, ::hir::ArgConversion::Box);
            }
            else if (arg->getType()->isIntegerTy(32) && expected->isPointerTy()) {
                // i32 -> ptr: extend to i64 then box.
                arg = builder_->CreateSExt(arg, builder_->getInt64Ty());
                arg = convertArg(arg, ::hir::ArgConversion::Box);
            }
            else if (arg->getType()->isIntegerTy(1) && expected->isDoubleTy()) {
                // bool -> f64
                arg = builder_->CreateUIToFP(arg, builder_->getDoubleTy());
            }
            else if (arg->getType()->isPointerTy() && expected->isDoubleTy()) {
                // ptr (boxed TsValue*) -> f64: unbox the double
                auto unboxFt = llvm::FunctionType::get(builder_->getDoubleTy(), {getGCPtrTy()}, false);
                auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFt);
                arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), {gcPtrToRaw(arg)});
            }
            else if (arg->getType()->isPointerTy() && expected->isIntegerTy(1)) {
                // ptr (boxed TsValue*) -> bool: unbox the bool
                auto unboxFt = llvm::FunctionType::get(builder_->getInt1Ty(), {getGCPtrTy()}, false);
                auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
                arg = builder_->CreateCall(unboxFt, unboxFn.getCallee(), {gcPtrToRaw(arg)});
            }
            else if (arg->getType()->isPointerTy() && expected->isPointerTy() &&
                     arg->getType()->getPointerAddressSpace() != expected->getPointerAddressSpace()) {
                // GC pointer address space mismatch: addrspace(1) -> addrspace(0) or vice versa
                arg = builder_->CreateAddrSpaceCast(arg, expected);
            }
        }

        llvmArgs.push_back(arg);
    }

    // Pad missing optional arguments with sentinel values.
    // Pointers: nullptr (extension/C functions check for null).
    // Integers: INT64_MIN sentinel (runtime functions check for this to mean "not provided").
    // Using INT64_MIN instead of 0 because 0 is a valid argument for many methods
    // (e.g., string.substring(2, 0) is valid JS that swaps to substring(0, 2)).
    size_t numProvidedArgs = inst->operands.size() - 1;  // -1 for function name
    while (llvmArgs.size() < argTys.size()) {
        size_t idx = llvmArgs.size();
        llvm::Type* expectedType = argTys[idx];
        if (expectedType->isPointerTy()) {
            llvmArgs.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(expectedType)));
        } else if (expectedType->isIntegerTy()) {
            llvmArgs.push_back(llvm::ConstantInt::get(expectedType, INT64_MIN, /*isSigned=*/true));
        } else if (expectedType->isDoubleTy()) {
            llvmArgs.push_back(llvm::ConstantFP::get(expectedType, 0.0));
        } else {
            llvmArgs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
    }

    // Standard call
    llvm::Value* result;
    if (retTy->isVoidTy()) {
        builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
        result = llvm::ConstantPointerNull::get(getGCPtrTy());
    } else {
        result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
    }

    // Handle return
    return handleReturn(result, spec.returnHandling);
}

llvm::Value* HIRToLLVM::convertArg(llvm::Value* arg, ::hir::ArgConversion conv) {
    switch (conv) {
        case ::hir::ArgConversion::None:
            return arg;

        case ::hir::ArgConversion::Box: {
            // Box the value based on its LLVM type
            llvm::Type* argType = arg->getType();

            if (argType->isIntegerTy(64)) {
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            } else if (argType->isDoubleTy()) {
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            } else if (argType->isIntegerTy(1)) {
                // ts_value_make_bool expects i32, extend i1 to i32
                auto fn = getTsValueMakeBool();
                llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt32Ty(), "bool_ext");
                return builder_->CreateCall(fn, { extended });
            } else if (argType->isPointerTy()) {
                // Already a pointer, box as object
                // Cast from GC address space if needed
                arg = gcPtrToRaw(arg);
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_object", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
            return arg;
        }

        case ::hir::ArgConversion::Unbox: {
            arg = gcPtrToRaw(arg);  // Normalize GC pointer for runtime call
            auto ft = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_get_object", ft);
            return builder_->CreateCall(ft, fn.getCallee(), { arg });
        }

        case ::hir::ArgConversion::ToI64: {
            if (arg->getType()->isDoubleTy()) {
                return emitSaturatingFPToSI(builder_.get(), arg, builder_->getInt64Ty());
            } else if (arg->getType()->isPointerTy()) {
                // Pointer is likely a boxed TsValue* - unbox via ts_value_get_int
                auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
            return arg;
        }

        case ::hir::ArgConversion::ToF64: {
            if (arg->getType()->isIntegerTy(64)) {
                return builder_->CreateSIToFP(arg, builder_->getDoubleTy());
            } else if (arg->getType()->isPointerTy()) {
                // Pointer is likely a boxed TsValue* - unbox via ts_value_get_double
                auto ft = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_double", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
            return arg;
        }

        case ::hir::ArgConversion::ToI32: {
            if (arg->getType()->isIntegerTy(64)) {
                return builder_->CreateTrunc(arg, builder_->getInt32Ty());
            } else if (arg->getType()->isPointerTy()) {
                // Pointer is likely a boxed TsValue* - unbox via ts_value_get_int then truncate
                auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
                auto i64Val = builder_->CreateCall(ft, fn.getCallee(), { arg });
                return builder_->CreateTrunc(i64Val, builder_->getInt32Ty());
            }
            return arg;
        }

        case ::hir::ArgConversion::ToBool: {
            if (arg->getType()->isIntegerTy()) {
                return builder_->CreateICmpNE(arg,
                    llvm::ConstantInt::get(arg->getType(), 0));
            }
            return arg;
        }

        case ::hir::ArgConversion::PtrToInt:
            return builder_->CreatePtrToInt(arg, builder_->getInt64Ty());

        case ::hir::ArgConversion::IntToPtr:
            return builder_->CreateIntToPtr(arg, getGCPtrTy());
    }
    return arg;
}

llvm::Value* HIRToLLVM::coerceArgToType(llvm::Value* arg, llvm::Type* expectedType,
                                        const HIROperand& operand,
                                        std::shared_ptr<HIRType> calleeParamType) {
    llvm::Type* argType = arg->getType();

    // When both are ptr, check HIR type to see if we need to box a concrete value
    // for an 'any' parameter. A raw TsString* or TsObject* needs to be boxed to TsValue*.
    // For user-defined functions, only skip boxing if the callee param is String-typed.
    // If the callee param is Any-typed, we still need to box.
    if (argType == expectedType && argType->isPointerTy()) {
        // If we know the callee's param type and it's String, skip boxing -
        // the callee expects raw TsString*, not boxed TsValue*.
        bool calleeExpectsString = calleeParamType &&
            (calleeParamType->kind == HIRTypeKind::String);
        if (!calleeExpectsString) {
            // Check if the operand has a concrete HIR type that needs boxing
            if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
                if (*hirVal && (*hirVal)->type) {
                    auto hirKind = (*hirVal)->type->kind;
                    if (hirKind == HIRTypeKind::String) {
                        auto ft = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
                        auto fn = module_->getOrInsertFunction("ts_value_make_string", ft);
                        return builder_->CreateCall(ft, fn.getCallee(), { arg });
                    }
                    // For other concrete types (Object, Array, Function, etc.),
                    // they may already be boxed or need object boxing
                }
            }
        }
        return arg;
    }

    if (argType == expectedType) return arg;

    // Handle GC pointer address space mismatch: addrspace(1) <-> addrspace(0)
    if (argType->isPointerTy() && expectedType->isPointerTy() &&
        argType->getPointerAddressSpace() != expectedType->getPointerAddressSpace()) {
        return builder_->CreateAddrSpaceCast(arg, expectedType);
    }

    // Need to convert: arg type doesn't match expected
    if (expectedType->isPointerTy()) {
        // Expected ptr (Any/object param) - box the concrete value
        if (argType->isDoubleTy()) {
            return convertArg(arg, ::hir::ArgConversion::Box);
        } else if (argType->isIntegerTy(64)) {
            return convertArg(arg, ::hir::ArgConversion::Box);
        } else if (argType->isIntegerTy(1)) {
            return convertArg(arg, ::hir::ArgConversion::Box);
        }
    } else if (expectedType->isDoubleTy()) {
        // Expected double but got something else
        if (argType->isIntegerTy(64)) {
            return builder_->CreateSIToFP(arg, builder_->getDoubleTy());
        } else if (argType->isPointerTy()) {
            // Unbox ptr to double
            auto ft = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_get_double", ft);
            return builder_->CreateCall(ft, fn.getCallee(), { arg });
        }
    } else if (expectedType->isIntegerTy(64)) {
        // Expected i64 but got something else
        if (argType->isDoubleTy()) {
            return builder_->CreateFPToSI(arg, builder_->getInt64Ty());
        } else if (argType->isPointerTy()) {
            auto ft = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
            return builder_->CreateCall(ft, fn.getCallee(), { arg });
        }
    } else if (expectedType->isIntegerTy(1)) {
        // Expected i1 (bool) but got something else
        if (argType->isPointerTy()) {
            auto ft = llvm::FunctionType::get(builder_->getInt1Ty(), { getGCPtrTy() }, false);
            auto fn = module_->getOrInsertFunction("ts_value_get_bool", ft);
            return builder_->CreateCall(ft, fn.getCallee(), { arg });
        } else if (argType->isIntegerTy(64)) {
            return builder_->CreateICmpNE(arg, llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
        } else if (argType->isDoubleTy()) {
            return builder_->CreateFCmpONE(arg, llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0));
        }
    }
    return arg;
}

llvm::Value* HIRToLLVM::handleReturn(llvm::Value* result, ::hir::ReturnHandling handling) {
    switch (handling) {
        case ::hir::ReturnHandling::Void:
            return llvm::ConstantPointerNull::get(getGCPtrTy());

        case ::hir::ReturnHandling::Raw:
            return result;

        case ::hir::ReturnHandling::Boxed:
            // Result is already boxed, just return it
            return result;

        case ::hir::ReturnHandling::BoxResult: {
            // Box the raw result
            llvm::Type* resType = result->getType();
            if (resType->isIntegerTy(64)) {
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            } else if (resType->isDoubleTy()) {
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_double", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            } else if (resType->isIntegerTy(1)) {
                auto ft = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt1Ty() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { result });
            }
            return result;
        }
    }
    return result;
}

//==============================================================================
// Variadic Function Lowering Helpers
//==============================================================================

std::string HIRToLLVM::getTypeSuffix(llvm::Value* arg, const ::hir::LoweringSpec& spec) {
    llvm::Type* argType = arg->getType();

    // Check available suffixes in spec
    const auto& suffixes = spec.typeDispatchSuffixes;

    if (argType->isIntegerTy(64)) {
        // Check for _int suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_int") return "_int";
        }
    } else if (argType->isDoubleTy()) {
        // Check for _double suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_double") return "_double";
        }
    } else if (argType->isIntegerTy(1)) {
        // Check for _bool suffix
        for (const auto& suffix : suffixes) {
            if (suffix == "_bool") return "_bool";
        }
    } else if (argType->isPointerTy()) {
        // For pointers, prefer _value (handles both strings and objects)
        for (const auto& suffix : suffixes) {
            if (suffix == "_value") return "_value";
        }
        // Fall back to _string, then _object
        for (const auto& suffix : suffixes) {
            if (suffix == "_string") return "_string";
        }
        for (const auto& suffix : suffixes) {
            if (suffix == "_object") return "_object";
        }
    }

    // Return default suffix if no specific match
    return spec.defaultSuffix;
}

llvm::Value* HIRToLLVM::lowerTypeDispatchCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // TypeDispatch: Call type-specific functions for each variadic argument
    // e.g., console.log(42, "hello") -> ts_console_log_int(42); ts_console_log_string("hello");

    size_t restIndex = spec.restParamIndex;
    llvm::Value* lastResult = llvm::ConstantPointerNull::get(getGCPtrTy());

    // Process each argument starting at restParamIndex
    // operands[0] is the function/method name, operands[1+] are arguments
    for (size_t i = restIndex + 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);

        // Determine the type suffix for this argument
        std::string suffix = getTypeSuffix(arg, spec);

        // Build the type-specific function name
        std::string funcName = spec.runtimeFuncName + suffix;

        // Determine the LLVM type for this argument
        llvm::Type* argType = arg->getType();
        llvm::Type* paramType = argType;

        // Get the return type from spec
        llvm::Type* retTy = spec.returnType
            ? spec.returnType(context_)
            : builder_->getVoidTy();

        // Create function type and call
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, { paramType }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        lastResult = builder_->CreateCall(ft, fn.getCallee(), { arg });
    }

    return handleReturn(lastResult, spec.returnHandling);
}

llvm::Value* HIRToLLVM::lowerPackArrayCall(HIRInstruction* inst, const ::hir::LoweringSpec& spec) {
    // PackArray: Pack rest arguments into a TsArray, then call the runtime function
    // e.g., Array.of(1, 2, 3) -> arr = ts_array_create(); ts_array_push(arr, 1); ts_array_push(arr, 2); ...

    size_t restIndex = spec.restParamIndex;

    // Create a new array for the rest arguments
    auto createFt = llvm::FunctionType::get(getGCPtrTy(), {}, false);
    auto createFn = module_->getOrInsertFunction("ts_array_create", createFt);
    llvm::Value* restArray = rawToGCPtr(builder_->CreateCall(createFt, createFn.getCallee(), {}));

    // Push each rest argument to the array
    auto pushFt = llvm::FunctionType::get(builder_->getInt64Ty(),
        { getGCPtrTy(), getGCPtrTy() }, false);
    auto pushFn = module_->getOrInsertFunction("ts_array_push", pushFt);

    // operands[0] is the function name, operands[1+] are arguments
    for (size_t i = restIndex + 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);

        // Box the argument if needed
        arg = convertArg(arg, ::hir::ArgConversion::Box);

        // Push to array
        builder_->CreateCall(pushFt, pushFn.getCallee(), { gcPtrToRaw(restArray), arg });
    }

    // Now call the actual runtime function with the packed array
    // Build fixed arguments (before restParamIndex) + the rest array
    std::vector<llvm::Value*> llvmArgs;

    // Add fixed arguments (if any)
    for (size_t i = 1; i <= restIndex && i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        if (i - 1 < spec.argConversions.size()) {
            arg = convertArg(arg, spec.argConversions[i - 1]);
        }
        llvmArgs.push_back(arg);
    }

    // Add the rest array
    llvmArgs.push_back(gcPtrToRaw(restArray));

    // Build LLVM function type
    llvm::Type* retTy = spec.returnType
        ? spec.returnType(context_)
        : builder_->getVoidTy();

    std::vector<llvm::Type*> argTys;
    for (size_t i = 0; i < spec.argTypes.size(); ++i) {
        argTys.push_back(spec.argTypes[i](context_));
    }

    // If spec doesn't include the rest array type, add it
    if (argTys.size() < llvmArgs.size()) {
        argTys.push_back(getGCPtrTy());
    }

    auto* ft = llvm::FunctionType::get(retTy, argTys, false);
    auto fn = module_->getOrInsertFunction(spec.runtimeFuncName, ft);

    // Call the function
    llvm::Value* result;
    if (retTy->isVoidTy()) {
        builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
        result = llvm::ConstantPointerNull::get(getGCPtrTy());
    } else {
        result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
    }

    return handleReturn(result, spec.returnHandling);
}


}  // namespace ts::hir
