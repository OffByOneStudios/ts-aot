#include "HIRToLLVM_Internal.h"

namespace ts::hir {


void HIRToLLVM::lowerBlock(HIRBlock* block) {
    SPDLOG_INFO("  Lowering block: {}", block->label);
    llvm::BasicBlock* bb = getBlock(block);
    if (!bb) return;

    builder_->SetInsertPoint(bb);

    // Validate all operands before lowering (catch HIR corruption early)
    for (size_t i = 0; i < block->instructions.size(); ++i) {
        auto& inst = block->instructions[i];
        for (size_t j = 0; j < inst->operands.size(); ++j) {
            auto idx = inst->operands[j].index();
            if (idx > 6) {
                SPDLOG_ERROR("HIR CORRUPTION: block={} instr={} opcode={} operand[{}].index()={} func={}",
                    block->label, i, static_cast<int>(inst->opcode), j, idx,
                    currentHIRFunction_ ? currentHIRFunction_->mangledName : "?");
            }
        }
    }

    // Lower each instruction
    currentBlockLabel_ = block->label;
    for (size_t i = 0; i < block->instructions.size(); ++i) {
        auto& inst = block->instructions[i];
        currentInstrIndex_ = i;
        lowerInstruction(inst.get());
        // Stop if the current block already has a terminator (e.g., from a fallback
        // branch). Emitting more instructions would cause "terminator in middle" errors.
        if (builder_->GetInsertBlock()->getTerminator()) break;
    }
}

void HIRToLLVM::lowerInstruction(HIRInstruction* inst) {
    // Set debug location for this instruction
    if (emitDebugInfo_ && currentFunction_) {
        if (auto* sp = currentFunction_->getSubprogram()) {
            if (inst->sourceLine > 0) {
                builder_->SetCurrentDebugLocation(
                    llvm::DILocation::get(context_, inst->sourceLine, inst->sourceColumn, sp));
            }
        } else {
            // No subprogram — clear debug location to avoid misattribution
            builder_->SetCurrentDebugLocation(llvm::DebugLoc());
        }
    }

    switch (inst->opcode) {
        // Constants
        case HIROpcode::ConstInt:       lowerConstInt(inst); break;
        case HIROpcode::ConstFloat:     lowerConstFloat(inst); break;
        case HIROpcode::ConstBool:      lowerConstBool(inst); break;
        case HIROpcode::ConstString:    lowerConstString(inst); break;
        case HIROpcode::ConstCString:   lowerConstCString(inst); break;
        case HIROpcode::ConstNull:      lowerConstNull(inst); break;
        case HIROpcode::ConstUndefined: lowerConstUndefined(inst); break;
        case HIROpcode::ConstRawNullPtr: {
            llvm::Value* nullPtr = llvm::ConstantPointerNull::get(getGCPtrTy());
            setValue(inst->result, nullPtr);
            break;
        }

        // Integer arithmetic
        case HIROpcode::AddI64: lowerAddI64(inst); break;
        case HIROpcode::SubI64: lowerSubI64(inst); break;
        case HIROpcode::MulI64: lowerMulI64(inst); break;
        case HIROpcode::DivI64: lowerDivI64(inst); break;
        case HIROpcode::ModI64: lowerModI64(inst); break;
        case HIROpcode::NegI64: lowerNegI64(inst); break;

        // Float arithmetic
        case HIROpcode::AddF64: lowerAddF64(inst); break;
        case HIROpcode::SubF64: lowerSubF64(inst); break;
        case HIROpcode::MulF64: lowerMulF64(inst); break;
        case HIROpcode::DivF64: lowerDivF64(inst); break;
        case HIROpcode::ModF64: lowerModF64(inst); break;
        case HIROpcode::NegF64: lowerNegF64(inst); break;

        // String operations
        case HIROpcode::StringConcat: lowerStringConcat(inst); break;

        // Bitwise
        case HIROpcode::AndI64:  lowerAndI64(inst); break;
        case HIROpcode::OrI64:   lowerOrI64(inst); break;
        case HIROpcode::XorI64:  lowerXorI64(inst); break;
        case HIROpcode::ShlI64:  lowerShlI64(inst); break;
        case HIROpcode::ShrI64:  lowerShrI64(inst); break;
        case HIROpcode::UShrI64: lowerUShrI64(inst); break;
        case HIROpcode::NotI64:  lowerNotI64(inst); break;

        // Integer comparisons
        case HIROpcode::CmpEqI64: lowerCmpEqI64(inst); break;
        case HIROpcode::CmpNeI64: lowerCmpNeI64(inst); break;
        case HIROpcode::CmpLtI64: lowerCmpLtI64(inst); break;
        case HIROpcode::CmpLeI64: lowerCmpLeI64(inst); break;
        case HIROpcode::CmpGtI64: lowerCmpGtI64(inst); break;
        case HIROpcode::CmpGeI64: lowerCmpGeI64(inst); break;

        // Float comparisons
        case HIROpcode::CmpEqF64: lowerCmpEqF64(inst); break;
        case HIROpcode::CmpNeF64: lowerCmpNeF64(inst); break;
        case HIROpcode::CmpLtF64: lowerCmpLtF64(inst); break;
        case HIROpcode::CmpLeF64: lowerCmpLeF64(inst); break;
        case HIROpcode::CmpGtF64: lowerCmpGtF64(inst); break;
        case HIROpcode::CmpGeF64: lowerCmpGeF64(inst); break;

        // Pointer comparisons
        case HIROpcode::CmpEqPtr: lowerCmpEqPtr(inst); break;
        case HIROpcode::CmpNePtr: lowerCmpNePtr(inst); break;

        // Boolean operations
        case HIROpcode::LogicalAnd: lowerLogicalAnd(inst); break;
        case HIROpcode::LogicalOr:  lowerLogicalOr(inst); break;
        case HIROpcode::LogicalNot: lowerLogicalNot(inst); break;

        // Type conversions
        case HIROpcode::CastI64ToF64:  lowerCastI64ToF64(inst); break;
        case HIROpcode::CastF64ToI64:  lowerCastF64ToI64(inst); break;
        case HIROpcode::CastBoolToI64: lowerCastBoolToI64(inst); break;

        // Boxing
        case HIROpcode::BoxInt:    lowerBoxInt(inst); break;
        case HIROpcode::BoxFloat:  lowerBoxFloat(inst); break;
        case HIROpcode::BoxBool:   lowerBoxBool(inst); break;
        case HIROpcode::BoxString: lowerBoxString(inst); break;
        case HIROpcode::BoxObject: lowerBoxObject(inst); break;

        // Unboxing
        case HIROpcode::UnboxInt:    lowerUnboxInt(inst); break;
        case HIROpcode::UnboxFloat:  lowerUnboxFloat(inst); break;
        case HIROpcode::UnboxBool:   lowerUnboxBool(inst); break;
        case HIROpcode::UnboxString: lowerUnboxString(inst); break;
        case HIROpcode::UnboxObject: lowerUnboxObject(inst); break;

        // Type checking
        case HIROpcode::TypeCheck:  lowerTypeCheck(inst); break;
        case HIROpcode::TypeOf:     lowerTypeOf(inst); break;
        case HIROpcode::InstanceOf: lowerInstanceOf(inst); break;

        // GC operations
        case HIROpcode::GCAlloc:       lowerGCAlloc(inst); break;
        case HIROpcode::GCAllocArray:  lowerGCAllocArray(inst); break;
        case HIROpcode::GCStore:       lowerGCStore(inst); break;
        case HIROpcode::GCLoad:        lowerGCLoad(inst); break;
        case HIROpcode::Safepoint:     lowerSafepoint(inst); break;
        case HIROpcode::SafepointPoll: lowerSafepointPoll(inst); break;

        // Memory operations
        case HIROpcode::Alloca:        lowerAlloca(inst); break;
        case HIROpcode::Load:          lowerLoad(inst); break;
        case HIROpcode::Store:         lowerStore(inst); break;
        case HIROpcode::GetElementPtr: lowerGetElementPtr(inst); break;

        // Object operations
        case HIROpcode::NewObject:        lowerNewObject(inst); break;
        case HIROpcode::NewObjectDynamic: lowerNewObjectDynamic(inst); break;
        case HIROpcode::GetPropStatic:    lowerGetPropStatic(inst); break;
        case HIROpcode::GetPropDynamic:   lowerGetPropDynamic(inst); break;
        case HIROpcode::SetPropStatic:    lowerSetPropStatic(inst); break;
        case HIROpcode::SetPropDynamic:   lowerSetPropDynamic(inst); break;
        case HIROpcode::HasProp:          lowerHasProp(inst); break;
        case HIROpcode::DeleteProp:       lowerDeleteProp(inst); break;

        // Array operations
        case HIROpcode::NewArrayBoxed:  lowerNewArrayBoxed(inst); break;
        case HIROpcode::NewArrayTyped:  lowerNewArrayTyped(inst); break;
        case HIROpcode::GetElem:        lowerGetElem(inst); break;
        case HIROpcode::SetElem:        lowerSetElem(inst); break;
        case HIROpcode::GetElemTyped:   lowerGetElemTyped(inst); break;
        case HIROpcode::SetElemTyped:   lowerSetElemTyped(inst); break;
        case HIROpcode::ArrayLength:    lowerArrayLength(inst); break;
        case HIROpcode::ArrayPush:      lowerArrayPush(inst); break;

        // Calls
        case HIROpcode::Call:         lowerCall(inst); break;
        case HIROpcode::CallMethod:   lowerCallMethod(inst); break;
        case HIROpcode::CallVirtual:  lowerCallVirtual(inst); break;
        case HIROpcode::CallIndirect: lowerCallIndirect(inst); break;
        case HIROpcode::CallValueWithThis: lowerCallValueWithThis(inst); break;
        case HIROpcode::ConstructFromValue: lowerConstructFromValue(inst); break;

        // Globals
        case HIROpcode::LoadGlobal:   lowerLoadGlobal(inst); break;
        case HIROpcode::StoreGlobal:  lowerStoreGlobal(inst); break;
        case HIROpcode::LoadFunction: lowerLoadFunction(inst); break;

        // Closures
        case HIROpcode::MakeClosure:   lowerMakeClosure(inst); break;
        case HIROpcode::LoadCapture:   lowerLoadCapture(inst); break;
        case HIROpcode::StoreCapture:  lowerStoreCapture(inst); break;
        case HIROpcode::LoadCaptureFromClosure:  lowerLoadCaptureFromClosure(inst); break;
        case HIROpcode::StoreCaptureFromClosure: lowerStoreCaptureFromClosure(inst); break;

        // Control flow
        case HIROpcode::Branch:      lowerBranch(inst); break;
        case HIROpcode::CondBranch:  lowerCondBranch(inst); break;
        case HIROpcode::Switch:      lowerSwitch(inst); break;
        case HIROpcode::Return:      lowerReturn(inst); break;
        case HIROpcode::ReturnVoid:  lowerReturnVoid(inst); break;
        case HIROpcode::Unreachable: lowerUnreachable(inst); break;

        // Phi and Select
        case HIROpcode::Phi:    lowerPhi(inst); break;
        case HIROpcode::Select: lowerSelect(inst); break;
        case HIROpcode::Copy:   lowerCopy(inst); break;

        // Exception handling
        case HIROpcode::SetupTry:       lowerSetupTry(inst); break;
        case HIROpcode::Throw:          lowerThrow(inst); break;
        case HIROpcode::GetException:   lowerGetException(inst); break;
        case HIROpcode::ClearException: lowerClearException(inst); break;
        case HIROpcode::PopHandler:     lowerPopHandler(inst); break;

        // Async/Await
        case HIROpcode::Await:          lowerAwait(inst); break;
        case HIROpcode::AsyncReturn:    lowerAsyncReturn(inst); break;

        // Generator/Yield
        case HIROpcode::Yield:          lowerYield(inst); break;
        case HIROpcode::YieldStar:      lowerYieldStar(inst); break;

        // Checked arithmetic (with overflow detection)
        case HIROpcode::AddI64Checked: lowerAddI64Checked(inst); break;
        case HIROpcode::SubI64Checked: lowerSubI64Checked(inst); break;
        case HIROpcode::MulI64Checked: lowerMulI64Checked(inst); break;

        // Strategy B Phase 8b: generic HIR opcodes (Add..CmpGe, GetProp,
        // SetProp) are guaranteed to be rewritten by SpecializationPass before
        // HIRToLLVM runs. If one slips through, that's a SpecializationPass
        // bug — assert(false) gives a loud crash with a stack trace in debug
        // builds and compiles to __builtin_unreachable() in release.
        case HIROpcode::Add:
        case HIROpcode::Sub:
        case HIROpcode::Mul:
        case HIROpcode::Div:
        case HIROpcode::Mod:
        case HIROpcode::Neg:
        case HIROpcode::CmpEq:
        case HIROpcode::CmpNe:
        case HIROpcode::CmpLt:
        case HIROpcode::CmpLe:
        case HIROpcode::CmpGt:
        case HIROpcode::CmpGe:
        case HIROpcode::GetProp:
        case HIROpcode::SetProp:
            assert(false && "Generic HIR opcode reached HIRToLLVM — SpecializationPass should have rewritten it");
            break;

        default:
            SPDLOG_ERROR("Unknown HIR opcode: {}", static_cast<int>(inst->opcode));
            break;
    }
}

//==============================================================================
// Constant Instructions
//==============================================================================

void HIRToLLVM::lowerConstInt(HIRInstruction* inst) {
    int64_t value = getOperandInt(inst->operands[0]);
    llvm::Value* result = llvm::ConstantInt::get(builder_->getInt64Ty(), value);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstFloat(HIRInstruction* inst) {
    double value = std::get<double>(inst->operands[0]);
    llvm::Value* result = llvm::ConstantFP::get(builder_->getDoubleTy(), value);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstBool(HIRInstruction* inst) {
    bool value = std::get<bool>(inst->operands[0]);
    llvm::Value* result = llvm::ConstantInt::get(builder_->getInt1Ty(), value ? 1 : 0);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstString(HIRInstruction* inst) {
    std::string value = getOperandString(inst->operands[0]);

    // Create global string constant (the global array preserves all bytes).
    llvm::Value* strPtr = createGlobalString(value);

    llvm::Value* result;
    if (value.find('\0') != std::string::npos) {
        // The literal contains an embedded NUL (e.g. "a\0b"); ts_string_create
        // would strlen-truncate it, so pass the explicit byte length.
        auto fn = getOrDeclareRuntimeFunction("ts_string_create_len", getGCPtrTy(),
                                              {getGCPtrTy(), builder_->getInt64Ty()});
        llvm::Value* lenVal = llvm::ConstantInt::get(builder_->getInt64Ty(), (uint64_t)value.size());
        result = builder_->CreateCall(fn, {strPtr, lenVal});
    } else {
        auto fn = getTsStringCreate();
        result = builder_->CreateCall(fn, {strPtr});
    }
    result = rawToGCPtr(result);  // Mark as GC-managed for statepoints
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstCString(HIRInstruction* inst) {
    std::string value = getOperandString(inst->operands[0]);

    // Create global string constant (raw C string, no TsString wrapper)
    llvm::Value* result = createGlobalString(value);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstNull(HIRInstruction* inst) {
    // NaN-box null sentinel (0x02) - distinct from undefined (0x0A) and C++ nullptr (0x0)
    llvm::Value* intVal = llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0000000000000002ULL);
    llvm::Value* result = builder_->CreateIntToPtr(intVal, getGCPtrTy());
    setValue(inst->result, result);
}

void HIRToLLVM::lowerConstUndefined(HIRInstruction* inst) {
    // NaN-box undefined sentinel (0x0A) - distinct from null (0x02) and C++ nullptr (0x0)
    llvm::Value* intVal = llvm::ConstantInt::get(builder_->getInt64Ty(), 0x000000000000000AULL);
    llvm::Value* result = builder_->CreateIntToPtr(intVal, getGCPtrTy());
    setValue(inst->result, result);
}

//==============================================================================
// Integer Arithmetic
//==============================================================================

void HIRToLLVM::lowerAddI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    auto unboxFn = getTsValueGetInt();
    lhs = coerceToI64Operand(builder_.get(), lhs, unboxFn, "unbox_lhs");
    rhs = coerceToI64Operand(builder_.get(), rhs, unboxFn, "unbox_rhs");
    llvm::Value* result = builder_->CreateAdd(lhs, rhs, "add");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSubI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    auto unboxFn = getTsValueGetInt();
    lhs = coerceToI64Operand(builder_.get(), lhs, unboxFn, "unbox_lhs");
    rhs = coerceToI64Operand(builder_.get(), rhs, unboxFn, "unbox_rhs");
    llvm::Value* result = builder_->CreateSub(lhs, rhs, "sub");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerMulI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    auto unboxFn = getTsValueGetInt();
    lhs = coerceToI64Operand(builder_.get(), lhs, unboxFn, "unbox_lhs");
    rhs = coerceToI64Operand(builder_.get(), rhs, unboxFn, "unbox_rhs");
    llvm::Value* result = builder_->CreateMul(lhs, rhs, "mul");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDivI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    auto unboxFn = getTsValueGetInt();
    lhs = coerceToI64Operand(builder_.get(), lhs, unboxFn, "unbox_lhs");
    rhs = coerceToI64Operand(builder_.get(), rhs, unboxFn, "unbox_rhs");
    llvm::Value* result = builder_->CreateSDiv(lhs, rhs, "div");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerModI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    auto unboxFn = getTsValueGetInt();
    lhs = coerceToI64Operand(builder_.get(), lhs, unboxFn, "unbox_lhs");
    rhs = coerceToI64Operand(builder_.get(), rhs, unboxFn, "unbox_rhs");
    llvm::Value* result = builder_->CreateSRem(lhs, rhs, "mod");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNegI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    auto unboxFn = getTsValueGetInt();
    val = coerceToI64Operand(builder_.get(), val, unboxFn, "unbox_val");
    llvm::Value* result = builder_->CreateNeg(val, "neg");
    setValue(inst->result, result);
}

//==============================================================================
// Checked Integer Arithmetic (with overflow detection)
//==============================================================================

void HIRToLLVM::lowerAddI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed add with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::sadd_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "add_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "add_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "add_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "add_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

void HIRToLLVM::lowerSubI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed sub with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::ssub_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "sub_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "sub_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "sub_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "sub_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

void HIRToLLVM::lowerMulI64Checked(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    HIRBlock* overflowBlock = getOperandBlock(inst->operands[2]);

    // Use LLVM's signed mul with overflow intrinsic
    llvm::Function* intrinsic = llvm::Intrinsic::getDeclaration(
        module_.get(), llvm::Intrinsic::smul_with_overflow, {builder_->getInt64Ty()});
    llvm::Value* resultStruct = builder_->CreateCall(intrinsic, {lhs, rhs}, "mul_overflow");

    // Extract result and overflow flag
    llvm::Value* result = builder_->CreateExtractValue(resultStruct, 0, "mul_result");
    llvm::Value* overflow = builder_->CreateExtractValue(resultStruct, 1, "mul_overflow_flag");

    setValue(inst->result, result);

    // Create continuation block for the non-overflow case
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
        context_, "mul_no_overflow", currentFunction_);

    // Branch to overflow block if overflow occurred
    builder_->CreateCondBr(overflow, getBlock(overflowBlock), continueBB);
    builder_->SetInsertPoint(continueBB);
}

//==============================================================================
// Float Arithmetic
//==============================================================================

void HIRToLLVM::lowerAddF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFAdd(lhs, rhs, "fadd");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerSubF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFSub(lhs, rhs, "fsub");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerMulF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFMul(lhs, rhs, "fmul");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerDivF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFDiv(lhs, rhs, "fdiv");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerModF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Convert operands to double if they're integers or pointers (boxed values)
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFRem(lhs, rhs, "fmod");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNegF64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // Convert to double if integer or pointer (boxed value)
    if (val->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        val = builder_->CreateCall(unboxFn, {val});
    } else if (val->getType()->isIntegerTy()) {
        val = builder_->CreateSIToFP(val, builder_->getDoubleTy());
    }

    llvm::Value* result = builder_->CreateFNeg(val, "fneg");
    setValue(inst->result, result);
}

//==============================================================================
// String Operations
//==============================================================================

void HIRToLLVM::lowerStringConcat(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Get HIR types to determine if conversion is needed
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;

    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    // Helper lambda to convert value to string based on type
    auto convertToString = [&](llvm::Value* val, std::shared_ptr<HIRType> type) -> llvm::Value* {
        // For pointer types (String, Any, Object, Class), extract the string pointer.
        // Use ts_string_extract_ptr for String type (avoids flattening CONS strings).
        // Use ts_value_get_string for other types (may involve value-to-string conversion).
        if (val->getType()->isPointerTy()) {
            if (type && type->kind == HIRTypeKind::String) {
                // Use extract_ptr which returns raw string pointer WITHOUT flattening CONS
                auto fn = getOrDeclareRuntimeFunction(
                    "ts_string_extract_ptr",
                    getGCPtrTy(),
                    { getGCPtrTy() }
                );
                return builder_->CreateCall(fn, { gcPtrToRaw(val) });
            }
            if (!type || type->kind == HIRTypeKind::Any || type->kind == HIRTypeKind::Object ||
                type->kind == HIRTypeKind::Class) {
                auto fn = getOrDeclareRuntimeFunction(
                    "ts_value_get_string",
                    getGCPtrTy(),
                    { getGCPtrTy() }
                );
                return builder_->CreateCall(fn, { gcPtrToRaw(val) });
            }
        }

        if (!type || type->kind == HIRTypeKind::String) {
            return val; // Already a string (non-pointer type, shouldn't happen but handle it)
        }

        if (type->kind == HIRTypeKind::Float64 || val->getType()->isDoubleTy()) {
            // Convert double to string
            auto fn = getOrDeclareRuntimeFunction(
                "ts_double_to_string",
                getGCPtrTy(),
                { builder_->getDoubleTy(), builder_->getInt64Ty() }
            );
            return builder_->CreateCall(fn, { val, llvm::ConstantInt::get(builder_->getInt64Ty(), 10) });
        }

        if (type->kind == HIRTypeKind::Int64 || val->getType()->isIntegerTy(64)) {
            // Convert int to string
            auto fn = getOrDeclareRuntimeFunction(
                "ts_int_to_string",
                getGCPtrTy(),
                { builder_->getInt64Ty(), builder_->getInt64Ty() }
            );
            llvm::Value* intVal = val;
            if (val->getType()->isPointerTy()) {
                auto unboxFn = getOrDeclareRuntimeFunction(
                    "ts_value_get_int",
                    builder_->getInt64Ty(),
                    { getGCPtrTy() }
                );
                intVal = builder_->CreateCall(unboxFn, { gcPtrToRaw(val) }, "unbox_int_for_str");
            }
            return builder_->CreateCall(fn, { intVal, llvm::ConstantInt::get(builder_->getInt64Ty(), 10) });
        }

        if (type->kind == HIRTypeKind::Bool || val->getType()->isIntegerTy(1)) {
            // Convert bool to string using select
            auto trueStr = createGlobalString("true");
            auto falseStr = createGlobalString("false");
            auto strFn = getTsStringCreate();
            // Don't wrap with rawToGCPtr - these flow to ts_string_concat which expects ptr
            llvm::Value* trueVal = builder_->CreateCall(strFn, {trueStr});
            llvm::Value* falseVal = builder_->CreateCall(strFn, {falseStr});
            llvm::Value* boolVal = val;
            if (val->getType()->isPointerTy()) {
                // Unbox the boolean from TsValue*
                auto unboxFn = getOrDeclareRuntimeFunction(
                    "ts_value_get_bool",
                    builder_->getInt1Ty(),
                    { getGCPtrTy() }
                );
                boolVal = builder_->CreateCall(unboxFn, { gcPtrToRaw(val) }, "unbox_bool");
            } else if (val->getType()->isIntegerTy(64)) {
                boolVal = builder_->CreateICmpNE(val, llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
            }
            return builder_->CreateSelect(boolVal, trueVal, falseVal);
        }

        // For any other types (Array, Function, etc.) - pass through
        // The pointer types (Any, Object, Class, String) are already handled above
        return val;
    };

    // Pin lhsStr to a stack alloca so the conservative GC can see it
    // during any allocations triggered by convertToString(rhs).
    // This is needed because lhsStr is a local temp (not an HIR value),
    // so the systemic gc.pin in setValue() doesn't cover it.
    llvm::AllocaInst* lhsPin;
    {
        llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
        llvm::BasicBlock* entryBB = &currentFunction_->getEntryBlock();
        builder_->SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
        lhsPin = builder_->CreateAlloca(getGCPtrTy(), nullptr, "gc_pin_lhs");
    }

    llvm::Value* lhsStr = convertToString(lhs, lhsType);
    builder_->CreateStore(gcPtrToRaw(lhsStr), lhsPin);

    llvm::Value* rhsStr = convertToString(rhs, rhsType);

    // Reload lhsStr from alloca (GC may have run during rhs evaluation)
    llvm::Value* lhsReloaded = builder_->CreateLoad(getGCPtrTy(), lhsPin, "lhs_reloaded");

    // Call ts_string_concat(void* a, void* b) -> void*
    auto fn = getOrDeclareRuntimeFunction(
        "ts_string_concat",
        getGCPtrTy(),
        { getGCPtrTy(), getGCPtrTy() }
    );
    llvm::Value* result = builder_->CreateCall(fn, { lhsReloaded, gcPtrToRaw(rhsStr) }, "strcat");

    result = rawToGCPtr(result);  // Mark as GC-managed for statepoints
    setValue(inst->result, result);  // setValue() will create gc.pin alloca
}

//==============================================================================
// Bitwise Operations
//==============================================================================

// Helper to ensure value is i64 for bitwise operations (convert from f64 or unbox if needed)
llvm::Value* HIRToLLVM::ensureI64ForBitwise(llvm::Value* val) {
    if (val->getType()->isDoubleTy()) {
        return builder_->CreateFPToSI(val, builder_->getInt64Ty(), "toi64");
    }
    if (val->getType()->isPointerTy()) {
        // Unbox pointer (TsValue*) to i64
        auto unboxFn = getTsValueGetInt();
        return builder_->CreateCall(unboxFn, {val});
    }
    if (val->getType()->isIntegerTy(1)) {
        // Bool: ToNumber gives 0/1 — widen to i64 via ZExt before bitwise.
        // Without this the subsequent CreateTrunc(i1, i32) fails LLVM
        // verification ("DestTy too big for Trunc").
        return builder_->CreateZExt(val, builder_->getInt64Ty(), "bool_to_i64");
    }
    return val;
}

void HIRToLLVM::lowerAndI64(HIRInstruction* inst) {
    // JavaScript & semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // AND on 32-bit values
    llvm::Value* result32 = builder_->CreateAnd(lhs32, rhs32, "and32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerOrI64(HIRInstruction* inst) {
    // JavaScript | semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // OR on 32-bit values
    llvm::Value* result32 = builder_->CreateOr(lhs32, rhs32, "or32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerXorI64(HIRInstruction* inst) {
    // JavaScript ^ semantics (ES5 11.10):
    // Both operands are converted to 32-bit integers, result is signed 32-bit

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32_l");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "toi32_r");

    // XOR on 32-bit values
    llvm::Value* result32 = builder_->CreateXor(lhs32, rhs32, "xor32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShlI64(HIRInstruction* inst) {
    // JavaScript << semantics (ES5 11.7.1):
    // 1. ToInt32(lhs) - truncate to 32 bits, treat as signed
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is a signed 32-bit integer

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Left shift on 32-bit value
    llvm::Value* result32 = builder_->CreateShl(lhs32, rhs32, "shl32");

    // Sign-extend to 64-bit (result is signed 32-bit)
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64 using signed conversion
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerShrI64(HIRInstruction* inst) {
    // JavaScript >> semantics (ES5 11.7.2):
    // 1. ToInt32(lhs) - truncate to 32 bits, treat as signed
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is a signed 32-bit integer (arithmetic shift preserves sign)

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "toi32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Arithmetic (signed) shift right on 32-bit value
    llvm::Value* result32 = builder_->CreateAShr(lhs32, rhs32, "ashr32");

    // Sign-extend to 64-bit (preserves signed semantics)
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64 using signed conversion
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUShrI64(HIRInstruction* inst) {
    // JavaScript >>> semantics (ES5 11.7.3):
    // 1. ToUint32(lhs) - truncate to 32 bits, treat as unsigned
    // 2. Shift amount is rhs & 0x1F (5 bits)
    // 3. Result is an unsigned 32-bit integer (0 to 4294967295)

    llvm::Value* lhs = ensureI64ForBitwise(getOperandValue(inst->operands[0]));
    llvm::Value* rhs = ensureI64ForBitwise(getOperandValue(inst->operands[1]));

    // Truncate to 32-bit (ToUint32 - the truncation gives us the low 32 bits)
    llvm::Value* lhs32 = builder_->CreateTrunc(lhs, builder_->getInt32Ty(), "tou32");
    llvm::Value* rhs32 = builder_->CreateTrunc(rhs, builder_->getInt32Ty(), "shamt32");

    // Mask shift amount to 5 bits (JS spec: shift by rhs & 0x1F)
    rhs32 = builder_->CreateAnd(rhs32,
        llvm::ConstantInt::get(builder_->getInt32Ty(), 0x1F), "shamt_masked");

    // Logical (unsigned) shift right on 32-bit value
    llvm::Value* result32 = builder_->CreateLShr(lhs32, rhs32, "lshr32");

    // Zero-extend to 64-bit (preserves unsigned semantics)
    llvm::Value* result64 = builder_->CreateZExt(result32, builder_->getInt64Ty(), "zext64");

    // Convert to f64 using UNSIGNED conversion (UIToFP) to get correct positive value
    llvm::Value* result = builder_->CreateUIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerNotI64(HIRInstruction* inst) {
    // JavaScript ~ semantics (ES5 11.4.8):
    // ToInt32(operand), then complement, result is signed 32-bit

    llvm::Value* val = ensureI64ForBitwise(getOperandValue(inst->operands[0]));

    // Truncate to 32-bit (ToInt32)
    llvm::Value* val32 = builder_->CreateTrunc(val, builder_->getInt32Ty(), "toi32");

    // Bitwise NOT on 32-bit value
    llvm::Value* result32 = builder_->CreateNot(val32, "not32");

    // Sign-extend to 64-bit
    llvm::Value* result64 = builder_->CreateSExt(result32, builder_->getInt64Ty(), "sext64");

    // Convert to f64
    llvm::Value* result = builder_->CreateSIToFP(result64, builder_->getDoubleTy(), "tof64");
    setValue(inst->result, result);
}

//==============================================================================
// Integer Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Check if this is a pointer comparison with null
    // Use LLVM-level check: if both are pointers and one is ConstantPointerNull
    bool lhsIsPointer = lhs->getType()->isPointerTy();
    bool rhsIsPointer = rhs->getType()->isPointerTy();
    bool lhsIsNull = llvm::isa<llvm::ConstantPointerNull>(lhs);
    bool rhsIsNull = llvm::isa<llvm::ConstantPointerNull>(rhs);

    if (lhsIsPointer && rhsIsPointer && (lhsIsNull || rhsIsNull)) {
        // Direct pointer comparison for obj === null or null === obj
        llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
        setValue(inst->result, result);
        return;
    }

    // Also check HIR types for object/class comparisons (e.g., obj1 === obj2)
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    bool isObjectComparison = (lhsType && (lhsType->kind == HIRTypeKind::Class ||
                                           lhsType->kind == HIRTypeKind::Object ||
                                           lhsType->kind == HIRTypeKind::Array ||
                                           lhsType->kind == HIRTypeKind::Ptr)) ||
                              (rhsType && (rhsType->kind == HIRTypeKind::Class ||
                                           rhsType->kind == HIRTypeKind::Object ||
                                           rhsType->kind == HIRTypeKind::Array ||
                                           rhsType->kind == HIRTypeKind::Ptr));

    // A STRING on either side means content equality. Union-typed results
    // (e.g. URLSearchParams.get's `string | null`) carry an object-ish HIR
    // kind, so `get(k) === "lit"` matched isObjectComparison and compared
    // two distinct TsString allocations by POINTER — always false.
    bool eitherIsString = (lhsType && lhsType->kind == HIRTypeKind::String) ||
                          (rhsType && rhsType->kind == HIRTypeKind::String);

    if (lhsIsPointer && rhsIsPointer && isObjectComparison && !eitherIsString) {
        // Direct pointer comparison for object === object checks
        llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
        setValue(inst->result, result);
        return;
    }

    // When both sides are pointers (boxed values), use the runtime's
    // ts_value_strict_eq_bool which handles string comparison by content,
    // number comparison, and identity comparison correctly.
    // ts_value_get_int returns 0 for non-numeric NaN-boxed values (strings,
    // objects), so unboxing both as int64 and comparing would incorrectly
    // return true for any two non-numeric values.
    if (lhsIsPointer && rhsIsPointer) {
        auto ft = llvm::FunctionType::get(
            builder_->getInt1Ty(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        auto fn = module_->getOrInsertFunction("ts_value_strict_eq_bool", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), {lhs, rhs}, "strict_eq");
        setValue(inst->result, result);
        return;
    }

    // Handle boxed values (pointers) by unboxing
    // When comparing with a boolean (i1), use ts_value_get_bool instead of ts_value_get_int
    // because ts_value_get_int returns 0 for BOOLEAN TsValues
    bool otherIsBool = false;
    if (lhsIsPointer && !rhsIsPointer && rhs->getType()->isIntegerTy(1)) otherIsBool = true;
    if (rhsIsPointer && !lhsIsPointer && lhs->getType()->isIntegerTy(1)) otherIsBool = true;

    if (lhsIsPointer) {
        if (otherIsBool) {
            auto unboxFn = getTsValueGetBool();
            lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
            lhs = builder_->CreateICmpNE(lhs, llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "to_i1");
        } else {
            auto unboxFn = getTsValueGetInt();
            lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
        }
    }
    if (rhsIsPointer) {
        if (otherIsBool) {
            auto unboxFn = getTsValueGetBool();
            rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
            rhs = builder_->CreateICmpNE(rhs, llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "to_i1");
        } else {
            auto unboxFn = getTsValueGetInt();
            rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
        }
    }

    // Handle type mismatch between i64 and i1 (boolean literal comparisons like x === true)
    if (lhs->getType()->isIntegerTy(64) && rhs->getType()->isIntegerTy(1)) {
        // Extend i1 to i64 for comparison
        rhs = builder_->CreateZExt(rhs, builder_->getInt64Ty(), "bool_to_i64");
    } else if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(64)) {
        // Extend i1 to i64 for comparison
        lhs = builder_->CreateZExt(lhs, builder_->getInt64Ty(), "bool_to_i64");
    }

    // If either side is f64, coerce both to f64 and use fcmp.
    if (lhs->getType()->isDoubleTy() || rhs->getType()->isDoubleTy()) {
        if (lhs->getType()->isIntegerTy(64)) {
            lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy(), "i64_to_f64");
        }
        if (rhs->getType()->isIntegerTy(64)) {
            rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy(), "i64_to_f64");
        }
        llvm::Value* result = builder_->CreateFCmpOEQ(lhs, rhs, "feq");
        setValue(inst->result, result);
        return;
    }

    llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "eq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // Check if this is a pointer comparison with null
    // Use LLVM-level check: if both are pointers and one is ConstantPointerNull
    bool lhsIsPointer = lhs->getType()->isPointerTy();
    bool rhsIsPointer = rhs->getType()->isPointerTy();
    bool lhsIsNull = llvm::isa<llvm::ConstantPointerNull>(lhs);
    bool rhsIsNull = llvm::isa<llvm::ConstantPointerNull>(rhs);

    if (lhsIsPointer && rhsIsPointer && (lhsIsNull || rhsIsNull)) {
        // Direct pointer comparison for obj !== null or null !== obj
        llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
        setValue(inst->result, result);
        return;
    }

    // Also check HIR types for object/class comparisons (e.g., obj1 !== obj2)
    std::shared_ptr<HIRType> lhsType = nullptr;
    std::shared_ptr<HIRType> rhsType = nullptr;
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*hirVal) lhsType = (*hirVal)->type;
    }
    if (auto* hirVal = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[1])) {
        if (*hirVal) rhsType = (*hirVal)->type;
    }

    bool isObjectComparison = (lhsType && (lhsType->kind == HIRTypeKind::Class ||
                                           lhsType->kind == HIRTypeKind::Object ||
                                           lhsType->kind == HIRTypeKind::Array ||
                                           lhsType->kind == HIRTypeKind::Ptr)) ||
                              (rhsType && (rhsType->kind == HIRTypeKind::Class ||
                                           rhsType->kind == HIRTypeKind::Object ||
                                           rhsType->kind == HIRTypeKind::Array ||
                                           rhsType->kind == HIRTypeKind::Ptr));

    // See lowerCmpEqI64: a string on either side means content inequality,
    // not pointer inequality (union-typed string results vs literals).
    bool eitherIsStringNe = (lhsType && lhsType->kind == HIRTypeKind::String) ||
                            (rhsType && rhsType->kind == HIRTypeKind::String);

    if (lhsIsPointer && rhsIsPointer && isObjectComparison && !eitherIsStringNe) {
        // Direct pointer comparison for object !== object checks
        llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
        setValue(inst->result, result);
        return;
    }

    // When both sides are pointers, use ts_value_strict_eq_bool and negate
    if (lhsIsPointer && rhsIsPointer) {
        auto ft = llvm::FunctionType::get(
            builder_->getInt1Ty(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        auto fn = module_->getOrInsertFunction("ts_value_strict_eq_bool", ft);
        llvm::Value* eq = builder_->CreateCall(ft, fn.getCallee(), {lhs, rhs}, "strict_eq");
        llvm::Value* result = builder_->CreateNot(eq, "strict_ne");
        setValue(inst->result, result);
        return;
    }

    // Handle boxed values (pointers) by unboxing
    // When comparing with a boolean (i1), use ts_value_get_bool instead of ts_value_get_int
    bool otherIsBoolNe = false;
    if (lhsIsPointer && !rhsIsPointer && rhs->getType()->isIntegerTy(1)) otherIsBoolNe = true;
    if (rhsIsPointer && !lhsIsPointer && lhs->getType()->isIntegerTy(1)) otherIsBoolNe = true;

    if (lhsIsPointer) {
        if (otherIsBoolNe) {
            auto unboxFn = getTsValueGetBool();
            lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
            lhs = builder_->CreateICmpNE(lhs, llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "to_i1");
        } else {
            auto unboxFn = getTsValueGetInt();
            lhs = builder_->CreateCall(unboxFn, {lhs}, "unbox_lhs");
        }
    }
    if (rhsIsPointer) {
        if (otherIsBoolNe) {
            auto unboxFn = getTsValueGetBool();
            rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
            rhs = builder_->CreateICmpNE(rhs, llvm::ConstantInt::get(builder_->getInt32Ty(), 0), "to_i1");
        } else {
            auto unboxFn = getTsValueGetInt();
            rhs = builder_->CreateCall(unboxFn, {rhs}, "unbox_rhs");
        }
    }

    // Handle type mismatch between i64 and i1 (boolean literal comparisons like x !== true)
    if (lhs->getType()->isIntegerTy(64) && rhs->getType()->isIntegerTy(1)) {
        // Extend i1 to i64 for comparison
        rhs = builder_->CreateZExt(rhs, builder_->getInt64Ty(), "bool_to_i64");
    } else if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(64)) {
        // Extend i1 to i64 for comparison
        lhs = builder_->CreateZExt(lhs, builder_->getInt64Ty(), "bool_to_i64");
    }

    // If either side is f64 (e.g., result of `~object` lowering via SIToFP),
    // coerce both to f64 and use fcmp — icmp requires matched integer types.
    // Affects `~object !== ~1` style comparisons where bitwise-not produces
    // f64 to match JS number semantics.
    if (lhs->getType()->isDoubleTy() || rhs->getType()->isDoubleTy()) {
        if (lhs->getType()->isIntegerTy(64)) {
            lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy(), "i64_to_f64");
        }
        if (rhs->getType()->isIntegerTy(64)) {
            rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy(), "i64_to_f64");
        }
        llvm::Value* result = builder_->CreateFCmpUNE(lhs, rhs, "fne");
        setValue(inst->result, result);
        return;
    }

    llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ne");
    setValue(inst->result, result);
}

// Promote a (lhs, rhs) pair for an integer-typed ordering compare to
// the right form. After ptr->i64 unboxing, if either side ends up f64
// (a mismatch the SpecializationPass left in place when one operand
// is a literal-folded constant that kept its f64 form), promote both
// sides to f64 and return true so the caller emits FCmp* instead of
// ICmpS*. ECMA-262 §7.2.13 Abstract Relational Comparison promotes
// both operands to Number; matching either side's f64 form is the
// spec-correct choice. Without this the verifier rejects the IR with
// "Both operands to ICmp instruction are not of the same type".
#define NORMALIZE_ORDERING_OPS(LHS, RHS) ([&]() -> bool {                       \
    if ((LHS)->getType()->isPointerTy()) {                                       \
        auto _u = getTsValueGetInt();                                            \
        (LHS) = builder_->CreateCall(_u, {(LHS)}, "unbox_lhs");                   \
    }                                                                            \
    if ((RHS)->getType()->isPointerTy()) {                                       \
        auto _u = getTsValueGetInt();                                            \
        (RHS) = builder_->CreateCall(_u, {(RHS)}, "unbox_rhs");                   \
    }                                                                            \
    bool _lf = (LHS)->getType()->isDoubleTy();                                   \
    bool _rf = (RHS)->getType()->isDoubleTy();                                   \
    if (_lf || _rf) {                                                            \
        if (!_lf) (LHS) = builder_->CreateSIToFP((LHS), builder_->getDoubleTy(), \
            "lhs_to_f64");                                                       \
        if (!_rf) (RHS) = builder_->CreateSIToFP((RHS), builder_->getDoubleTy(), \
            "rhs_to_f64");                                                       \
        return true;                                                             \
    }                                                                            \
    return false;                                                                \
}())

void HIRToLLVM::lowerCmpLtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = NORMALIZE_ORDERING_OPS(lhs, rhs)
        ? builder_->CreateFCmpOLT(lhs, rhs, "flt")
        : builder_->CreateICmpSLT(lhs, rhs, "lt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = NORMALIZE_ORDERING_OPS(lhs, rhs)
        ? builder_->CreateFCmpOLE(lhs, rhs, "fle")
        : builder_->CreateICmpSLE(lhs, rhs, "le");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = NORMALIZE_ORDERING_OPS(lhs, rhs)
        ? builder_->CreateFCmpOGT(lhs, rhs, "fgt")
        : builder_->CreateICmpSGT(lhs, rhs, "gt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeI64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = NORMALIZE_ORDERING_OPS(lhs, rhs)
        ? builder_->CreateFCmpOGE(lhs, rhs, "fge")
        : builder_->CreateICmpSGE(lhs, rhs, "ge");
    setValue(inst->result, result);
}

//==============================================================================
// Float Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    // Use runtime call (not inline unbox) because operands may be TsString* needing coercion
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOEQ(lhs, rhs, "feq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    // Use UNORDERED not-equal (UNE), not ordered (ONE): JS requires
    // `NaN != NaN` and `NaN !== NaN` to be true. FCmpONE returns false when
    // either operand is NaN (the comparison is unordered), so `n !== n` — the
    // canonical "is NaN" idiom used throughout JS/lodash — wrongly yielded
    // false. UNE differs from ONE only when an operand is NaN, so non-NaN
    // comparisons are unaffected. Mirrors CmpEqF64's OEQ (NaN==NaN -> false).
    llvm::Value* result = builder_->CreateFCmpUNE(lhs, rhs, "fne");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOLT(lhs, rhs, "flt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpLeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOLE(lhs, rhs, "fle");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGtF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOGT(lhs, rhs, "fgt");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpGeF64(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    // Type coercion: convert i64 to f64 if needed
    if (lhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        lhs = builder_->CreateCall(unboxFn, {lhs});
    } else if (lhs->getType()->isIntegerTy()) {
        lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
    }
    if (rhs->getType()->isPointerTy()) {
        auto unboxFn = getTsValueGetDouble();
        rhs = builder_->CreateCall(unboxFn, {rhs});
    } else if (rhs->getType()->isIntegerTy()) {
        rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
    }
    llvm::Value* result = builder_->CreateFCmpOGE(lhs, rhs, "fge");
    setValue(inst->result, result);
}

//==============================================================================
// Pointer Comparisons
//==============================================================================

void HIRToLLVM::lowerCmpEqPtr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpEQ(lhs, rhs, "ptreq");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCmpNePtr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);
    llvm::Value* result = builder_->CreateICmpNE(lhs, rhs, "ptrne");
    setValue(inst->result, result);
}

//==============================================================================
// Boolean Operations
//==============================================================================

void HIRToLLVM::lowerLogicalAnd(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // JavaScript semantics: && returns lhs if falsy, rhs if lhs is truthy.
    // When both operands are i1 (boolean), use simple AND for efficiency.
    if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(1)) {
        llvm::Value* result = builder_->CreateAnd(lhs, rhs, "land");
        setValue(inst->result, result);
        return;
    }

    // General case: short-circuit with value propagation
    // Box operands to ptr if needed for phi node compatibility (inline NaN boxing)
    auto boxIfNeeded = [this](llvm::Value* val) -> llvm::Value* {
        if (val->getType()->isPointerTy()) return val;
        if (val->getType()->isIntegerTy(64)) return emitInlineBoxInt(val);
        if (val->getType()->isDoubleTy()) return emitInlineBoxFloat(val);
        if (val->getType()->isIntegerTy(1)) return emitInlineBoxBool(val);
        // Handle i32 and other integer types (e.g., boolean getters returning i32)
        if (val->getType()->isIntegerTy()) {
            llvm::Value* ext = builder_->CreateZExt(val, builder_->getInt64Ty());
            return emitInlineBoxInt(ext);
        }
        return val;
    };

    llvm::Value* lhsBoxed = boxIfNeeded(lhs);

    // Convert lhs to boolean for the branch
    llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
        builder_->getInt1Ty(), { getGCPtrTy() }, false);
    llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
    llvm::Value* lhsTruthy = builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { lhsBoxed }, "tobool");

    llvm::Function* curFn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(context_, "land.rhs", curFn);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context_, "land.end", curFn);

    llvm::BasicBlock* lhsBB = builder_->GetInsertBlock();
    builder_->CreateCondBr(lhsTruthy, rhsBB, endBB);

    // RHS block - evaluate and box rhs
    builder_->SetInsertPoint(rhsBB);
    llvm::Value* rhsBoxed = boxIfNeeded(rhs);
    llvm::BasicBlock* rhsEndBB = builder_->GetInsertBlock();  // may differ after boxing calls
    builder_->CreateBr(endBB);

    // Merge: lhs if falsy, rhs if lhs was truthy
    builder_->SetInsertPoint(endBB);
    llvm::PHINode* phi = builder_->CreatePHI(getGCPtrTy(), 2, "land.val");
    phi->addIncoming(lhsBoxed, lhsBB);
    phi->addIncoming(rhsBoxed, rhsEndBB);
    setValue(inst->result, phi);
}

void HIRToLLVM::lowerLogicalOr(HIRInstruction* inst) {
    llvm::Value* lhs = getOperandValue(inst->operands[0]);
    llvm::Value* rhs = getOperandValue(inst->operands[1]);

    // JavaScript semantics: || returns lhs if truthy, rhs if lhs is falsy.
    // When both operands are i1 (boolean), use simple OR for efficiency.
    if (lhs->getType()->isIntegerTy(1) && rhs->getType()->isIntegerTy(1)) {
        llvm::Value* result = builder_->CreateOr(lhs, rhs, "lor");
        setValue(inst->result, result);
        return;
    }

    // General case: short-circuit with value propagation
    // Box operands to ptr if needed for phi node compatibility (inline NaN boxing)
    auto boxIfNeeded = [this](llvm::Value* val) -> llvm::Value* {
        if (val->getType()->isPointerTy()) return val;
        if (val->getType()->isIntegerTy(64)) return emitInlineBoxInt(val);
        if (val->getType()->isDoubleTy()) return emitInlineBoxFloat(val);
        if (val->getType()->isIntegerTy(1)) return emitInlineBoxBool(val);
        // Handle i32 and other integer types (e.g., boolean getters returning i32)
        if (val->getType()->isIntegerTy()) {
            llvm::Value* ext = builder_->CreateZExt(val, builder_->getInt64Ty());
            return emitInlineBoxInt(ext);
        }
        return val;
    };

    llvm::Value* lhsBoxed = boxIfNeeded(lhs);

    // Convert lhs to boolean for the branch
    llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
        builder_->getInt1Ty(), { getGCPtrTy() }, false);
    llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
    llvm::Value* lhsTruthy = builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { lhsBoxed }, "tobool");

    llvm::Function* curFn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(context_, "lor.rhs", curFn);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context_, "lor.end", curFn);

    llvm::BasicBlock* lhsBB = builder_->GetInsertBlock();
    builder_->CreateCondBr(lhsTruthy, endBB, rhsBB);

    // RHS block - evaluate and box rhs
    builder_->SetInsertPoint(rhsBB);
    llvm::Value* rhsBoxed = boxIfNeeded(rhs);
    llvm::BasicBlock* rhsEndBB = builder_->GetInsertBlock();  // may differ after boxing calls
    builder_->CreateBr(endBB);

    // Merge: lhs if truthy, rhs if lhs was falsy
    builder_->SetInsertPoint(endBB);
    llvm::PHINode* phi = builder_->CreatePHI(getGCPtrTy(), 2, "lor.val");
    phi->addIncoming(lhsBoxed, lhsBB);
    phi->addIncoming(rhsBoxed, rhsEndBB);
    setValue(inst->result, phi);
}

void HIRToLLVM::lowerLogicalNot(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    llvm::Value* result;
    if (val->getType()->isPointerTy()) {
        // For pointer types (boxed values or objects), we need to:
        // 1. Call ts_value_to_bool to convert to actual boolean
        // 2. Then negate the result
        llvm::FunctionType* toBoolFt = llvm::FunctionType::get(
            builder_->getInt1Ty(), { getGCPtrTy() }, false);
        llvm::FunctionCallee toBoolFn = module_->getOrInsertFunction("ts_value_to_bool", toBoolFt);
        llvm::Value* boolVal = builder_->CreateCall(toBoolFt, toBoolFn.getCallee(), { val }, "tobool");
        result = builder_->CreateNot(boolVal, "lnot");
    } else if (val->getType()->isIntegerTy(1)) {
        // Already a boolean - just negate
        result = builder_->CreateNot(val, "lnot");
    } else if (val->getType()->isIntegerTy()) {
        // Integer type - convert to boolean first (non-zero check)
        llvm::Value* boolVal = builder_->CreateICmpNE(
            val, llvm::ConstantInt::get(val->getType(), 0), "tobool");
        result = builder_->CreateNot(boolVal, "lnot");
    } else if (val->getType()->isDoubleTy()) {
        // Double type - !x is true when x is 0 or NaN. Use FCmpUNE
        // (NaN comparisons unordered → true → fail truthy → !val=true).
        // Actually use FCmpONE so NaN compares not-equal: NaN!=0 ordered
        // is FALSE, so the truthy result becomes false, then Not → true.
        // Match JS ToBoolean: NaN is falsy, so !NaN = true. FCmpONE(NaN,0)=false
        // → Not = true. Correct.
        llvm::Value* boolVal = builder_->CreateFCmpONE(
            val, llvm::ConstantFP::get(builder_->getDoubleTy(), 0.0), "tobool");
        result = builder_->CreateNot(boolVal, "lnot");
    } else {
        // Fall back to CreateNot (may fail for unsupported types)
        result = builder_->CreateNot(val, "lnot");
    }

    setValue(inst->result, result);
}

//==============================================================================
// Type Conversions
//==============================================================================

void HIRToLLVM::lowerCastI64ToF64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "i2f");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCastF64ToI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "f2i");
    setValue(inst->result, result);
}

void HIRToLLVM::lowerCastBoolToI64(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = builder_->CreateZExt(val, builder_->getInt64Ty(), "b2i");
    setValue(inst->result, result);
}

//==============================================================================
// Inline NaN-Boxing Helpers
//==============================================================================

// BoxInt: i64 → ptr (NaN-boxed)
// If value fits in int32, tag with 0xFFFE prefix. Otherwise convert to double and bias.
llvm::Value* HIRToLLVM::emitInlineBoxInt(llvm::Value* val) {
    // Guard: if the LLVM value is a double (e.g., JS number from untyped code),
    // convert to i64 first. HIR BoxInt may receive f64 values from dynamic paths.
    if (val->getType()->isDoubleTy()) {
        val = builder_->CreateFPToSI(val, builder_->getInt64Ty(), "nb.f2i");
    }
    // Check if value fits in int32 range
    llvm::Value* trunc = builder_->CreateTrunc(val, builder_->getInt32Ty(), "nb.trunc");
    llvm::Value* sext = builder_->CreateSExt(trunc, builder_->getInt64Ty(), "nb.sext");
    llvm::Value* fits = builder_->CreateICmpEQ(val, sext, "nb.fits_i32");

    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* int32BB = llvm::BasicBlock::Create(context_, "nb.int32", fn);
    llvm::BasicBlock* doubleBB = llvm::BasicBlock::Create(context_, "nb.double", fn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "nb.merge", fn);

    builder_->CreateCondBr(fits, int32BB, doubleBB);

    // Int32 path: tag with 0xFFFE prefix
    builder_->SetInsertPoint(int32BB);
    llvm::Value* masked = builder_->CreateAnd(val,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x00000000FFFFFFFFULL), "nb.masked");
    llvm::Value* tagged = builder_->CreateOr(masked,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0xFFFE000000000000ULL), "nb.tagged");
    llvm::Value* ptr1 = builder_->CreateIntToPtr(tagged, getGCPtrTy(), "nb.ptr_i32");
    builder_->CreateBr(mergeBB);

    // Double path: convert to double, bias
    builder_->SetInsertPoint(doubleBB);
    llvm::Value* dbl = builder_->CreateSIToFP(val, builder_->getDoubleTy(), "nb.dbl");
    llvm::Value* bits = builder_->CreateBitCast(dbl, builder_->getInt64Ty(), "nb.bits");
    llvm::Value* biased = builder_->CreateAdd(bits,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0002000000000000ULL), "nb.biased");
    llvm::Value* ptr2 = builder_->CreateIntToPtr(biased, getGCPtrTy(), "nb.ptr_dbl");
    builder_->CreateBr(mergeBB);

    // Merge
    builder_->SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder_->CreatePHI(getGCPtrTy(), 2, "nb.boxed_int");
    phi->addIncoming(ptr1, int32BB);
    phi->addIncoming(ptr2, doubleBB);
    return phi;
}

// UnboxInt: ptr (NaN-boxed) → i64
// Check top 16 bits for int32 tag, else treat as biased double.
llvm::Value* HIRToLLVM::emitInlineUnboxInt(llvm::Value* val) {
    llvm::Value* raw = builder_->CreatePtrToInt(val, builder_->getInt64Ty(), "nb.raw");
    llvm::Value* top16 = builder_->CreateLShr(raw,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 48), "nb.top16");
    llvm::Value* isInt32 = builder_->CreateICmpEQ(top16,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0xFFFE), "nb.is_i32");

    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* intBB = llvm::BasicBlock::Create(context_, "nb.unbox_int", fn);
    llvm::BasicBlock* fltBB = llvm::BasicBlock::Create(context_, "nb.unbox_flt", fn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "nb.unbox_merge", fn);

    builder_->CreateCondBr(isInt32, intBB, fltBB);

    // Int32 path: extract lower 32 bits and sign-extend
    builder_->SetInsertPoint(intBB);
    llvm::Value* lo32 = builder_->CreateTrunc(raw, builder_->getInt32Ty(), "nb.lo32");
    llvm::Value* result_int = builder_->CreateSExt(lo32, builder_->getInt64Ty(), "nb.sext_i32");
    builder_->CreateBr(mergeBB);

    // Float path: unbias and convert to i64
    builder_->SetInsertPoint(fltBB);
    llvm::Value* unbiased = builder_->CreateSub(raw,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0002000000000000ULL), "nb.unbiased");
    llvm::Value* dbl = builder_->CreateBitCast(unbiased, builder_->getDoubleTy(), "nb.dbl");
    llvm::Value* result_flt = builder_->CreateFPToSI(dbl, builder_->getInt64Ty(), "nb.fptosi");
    builder_->CreateBr(mergeBB);

    // Merge
    builder_->SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder_->CreatePHI(builder_->getInt64Ty(), 2, "nb.unboxed_int");
    phi->addIncoming(result_int, intBB);
    phi->addIncoming(result_flt, fltBB);
    return phi;
}

// BoxFloat: double → ptr (NaN-boxed)
// Bias the IEEE754 bits by 2^49.
llvm::Value* HIRToLLVM::emitInlineBoxFloat(llvm::Value* val) {
    llvm::Value* bits = builder_->CreateBitCast(val, builder_->getInt64Ty(), "nb.f_bits");
    llvm::Value* biased = builder_->CreateAdd(bits,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0002000000000000ULL), "nb.f_biased");
    return builder_->CreateIntToPtr(biased, getGCPtrTy(), "nb.boxed_float");
}

// UnboxFloat: ptr (NaN-boxed) → double
// Check for int32 tag first, otherwise unbias.
llvm::Value* HIRToLLVM::emitInlineUnboxFloat(llvm::Value* val) {
    llvm::Value* raw = builder_->CreatePtrToInt(val, builder_->getInt64Ty(), "nb.uf_raw");
    llvm::Value* top16 = builder_->CreateLShr(raw,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 48), "nb.uf_top16");
    llvm::Value* isInt32 = builder_->CreateICmpEQ(top16,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0xFFFE), "nb.uf_is_i32");

    llvm::Function* fn = builder_->GetInsertBlock()->getParent();
    llvm::BasicBlock* intBB = llvm::BasicBlock::Create(context_, "nb.uf_int", fn);
    llvm::BasicBlock* fltBB = llvm::BasicBlock::Create(context_, "nb.uf_flt", fn);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "nb.uf_merge", fn);

    builder_->CreateCondBr(isInt32, intBB, fltBB);

    // Int32 path: extract and convert to double
    builder_->SetInsertPoint(intBB);
    llvm::Value* lo32 = builder_->CreateTrunc(raw, builder_->getInt32Ty(), "nb.uf_lo32");
    llvm::Value* ext = builder_->CreateSExt(lo32, builder_->getInt64Ty(), "nb.uf_sext");
    llvm::Value* result_int = builder_->CreateSIToFP(ext, builder_->getDoubleTy(), "nb.uf_sitofp");
    builder_->CreateBr(mergeBB);

    // Float path: unbias
    builder_->SetInsertPoint(fltBB);
    llvm::Value* unbiased = builder_->CreateSub(raw,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0002000000000000ULL), "nb.uf_unbiased");
    llvm::Value* result_flt = builder_->CreateBitCast(unbiased, builder_->getDoubleTy(), "nb.uf_dbl");
    builder_->CreateBr(mergeBB);

    // Merge
    builder_->SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder_->CreatePHI(builder_->getDoubleTy(), 2, "nb.unboxed_float");
    phi->addIncoming(result_int, intBB);
    phi->addIncoming(result_flt, fltBB);
    return phi;
}

// BoxBool: i1 → ptr (NaN-boxed)
// TRUE = 0x07, FALSE = 0x06
llvm::Value* HIRToLLVM::emitInlineBoxBool(llvm::Value* val) {
    // If already a ptr (boxed Any TsValue*), pass through. This happens
    // when the source is a runtime call like ts_value_lt that returns a
    // boxed bool, and a downstream BoxBool was emitted by ASTToHIR before
    // SpecializationPass replaced the original i1-producing opcode.
    if (val->getType()->isPointerTy()) {
        return val;
    }
    // Ensure val is i1 - extension functions may return i32 for booleans
    if (val->getType() != builder_->getInt1Ty()) {
        // Use the operand's actual integer type for the zero comparison.
        // ConstantInt::get(ptr, 0) produces i0 0 (zero-width int) which
        // is invalid LLVM — guarded above.
        val = builder_->CreateICmpNE(val,
            llvm::ConstantInt::get(val->getType(), 0), "nb.to_i1");
    }
    llvm::Value* truePtr = builder_->CreateIntToPtr(
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x07), getGCPtrTy());
    llvm::Value* falsePtr = builder_->CreateIntToPtr(
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x06), getGCPtrTy());
    return builder_->CreateSelect(val, truePtr, falsePtr, "nb.boxed_bool");
}

// UnboxBool: ptr (NaN-boxed) → i1
// TRUE = 0x07
llvm::Value* HIRToLLVM::emitInlineUnboxBool(llvm::Value* val) {
    llvm::Value* raw = builder_->CreatePtrToInt(val, builder_->getInt64Ty(), "nb.ub_raw");
    return builder_->CreateICmpEQ(raw,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x07), "nb.unboxed_bool");
}

//==============================================================================
// Boxing Operations
//==============================================================================

void HIRToLLVM::lowerBoxInt(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineBoxInt(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxFloat(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineBoxFloat(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxBool(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineBoxBool(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerBoxString(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    val = gcPtrToRaw(val);  // Strip addrspace(1) for runtime call
    // Pointers are raw in NaN boxing - string pointers pass through
    setValue(inst->result, val);
}

void HIRToLLVM::lowerBoxObject(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    val = gcPtrToRaw(val);  // Strip addrspace(1) for runtime call

    llvm::Value* result;
    if (val->getType()->isPointerTy()) {
        // Pointers are raw in NaN boxing - pass through
        result = val;
    } else if (val->getType()->isIntegerTy(1)) {
        // Boolean - inline box
        result = emitInlineBoxBool(val);
    } else if (val->getType()->isIntegerTy(64)) {
        // Integer - inline box
        result = emitInlineBoxInt(val);
    } else if (val->getType()->isDoubleTy()) {
        // Double - inline box
        result = emitInlineBoxFloat(val);
    } else if (val->getType()->isIntegerTy(32)) {
        // i32 - extend to i64 and inline box
        llvm::Value* extended = builder_->CreateSExt(val, builder_->getInt64Ty(), "i32_ext");
        result = emitInlineBoxInt(extended);
    } else {
        // Fall back to treating it as a pointer (may fail)
        auto fn = getTsValueMakeObject();
        result = builder_->CreateCall(fn, {val});
    }
    setValue(inst->result, result);
}

//==============================================================================
// Unboxing Operations
//==============================================================================

void HIRToLLVM::lowerUnboxInt(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineUnboxInt(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxFloat(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineUnboxFloat(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxBool(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* result = emitInlineUnboxBool(val);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxString(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    // Pointers are raw in NaN boxing - string pointers pass through
    // Still need runtime call for type checking (could be a number or special)
    auto fn = getTsValueGetString();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerUnboxObject(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    // Pointers are raw in NaN boxing, but we still need to filter out
    // numbers and specials. Use runtime call for safety.
    auto fn = getTsValueGetObject();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

//==============================================================================
// Type Checking
//==============================================================================

void HIRToLLVM::lowerTypeCheck(HIRInstruction* inst) {
    // TODO: Implement type checking
    // For now, return true
    llvm::Value* result = llvm::ConstantInt::getTrue(context_);
    setValue(inst->result, result);
}

void HIRToLLVM::lowerTypeOf(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);

    // ts_typeof expects a boxed TsValue*, so we need to box primitives (inline NaN boxing)
    if (val->getType()->isDoubleTy()) {
        val = emitInlineBoxFloat(val);
    } else if (val->getType()->isIntegerTy(64)) {
        val = emitInlineBoxInt(val);
    } else if (val->getType()->isIntegerTy(1)) {
        val = emitInlineBoxBool(val);
    }
    // If val is already a pointer, pass it directly

    auto fn = getTsTypeOf();
    llvm::Value* result = builder_->CreateCall(fn, {val});
    setValue(inst->result, result);
}

void HIRToLLVM::lowerInstanceOf(HIRInstruction* inst) {
    llvm::Value* val = getOperandValue(inst->operands[0]);
    llvm::Value* ctor = getOperandValue(inst->operands[1]);
    auto fn = getTsInstanceOf();
    llvm::Value* result = builder_->CreateCall(fn, {val, ctor});
    // Truncate to i1
    result = builder_->CreateTrunc(result, builder_->getInt1Ty());
    setValue(inst->result, result);
}

//==============================================================================
// GC Operations (custom generational GC; see runtime/src/TsGC.cpp)
//==============================================================================


}  // namespace ts::hir
