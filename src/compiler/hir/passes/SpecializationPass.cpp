#include "SpecializationPass.h"
#include "../HIR.h"
#include "../HIRBuilder.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

//==============================================================================
// Type helpers
//==============================================================================

std::shared_ptr<HIRType> SpecializationPass::operandType(const HIROperand& op) {
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&op)) {
        if (*valPtr) return (*valPtr)->type;
    }
    return nullptr;
}

bool SpecializationPass::isString(const HIRType* t)  { return t && t->kind == HIRTypeKind::String; }
bool SpecializationPass::isFloat64(const HIRType* t) { return t && t->kind == HIRTypeKind::Float64; }
bool SpecializationPass::isInt64(const HIRType* t)   { return t && t->kind == HIRTypeKind::Int64; }
bool SpecializationPass::isBigInt(const HIRType* t)  { return t && t->kind == HIRTypeKind::BigInt; }
bool SpecializationPass::isAny(const HIRType* t)     { return !t || t->kind == HIRTypeKind::Any; }
bool SpecializationPass::isBool(const HIRType* t)    { return t && t->kind == HIRTypeKind::Bool; }

//==============================================================================
// Pass entry point
//==============================================================================

PassResult SpecializationPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    for (auto& block : func.blocks) {
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto* inst = block->instructions[i].get();
            auto replacement = specializeInstruction(inst);
            if (replacement) {
                SPDLOG_DEBUG("Specialization: {} -> {}",
                             inst->toString(), replacement->toString());
                block->instructions[i] = std::move(replacement);
                changed = true;
            }
        }
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

//==============================================================================
// Top-level dispatch
//==============================================================================

std::unique_ptr<HIRInstruction> SpecializationPass::specializeInstruction(HIRInstruction* inst) {
    switch (inst->opcode) {
        // Generic arithmetic
        case HIROpcode::Add:
        case HIROpcode::Sub:
        case HIROpcode::Mul:
        case HIROpcode::Div:
        case HIROpcode::Mod:
            return specializeArithmetic(inst);

        case HIROpcode::Neg:
            return specializeNeg(inst);

        // Generic comparison
        case HIROpcode::CmpEq:
        case HIROpcode::CmpNe:
        case HIROpcode::CmpLt:
        case HIROpcode::CmpLe:
        case HIROpcode::CmpGt:
        case HIROpcode::CmpGe:
            return specializeComparison(inst);

        // Generic property access
        case HIROpcode::GetProp:
            return specializeGetProp(inst);
        case HIROpcode::SetProp:
            return specializeSetProp(inst);

        // Everything else: not a generic opcode, leave alone
        default:
            return nullptr;
    }
}

//==============================================================================
// Arithmetic specialization
//==============================================================================

// Helper: build a Call instruction (used as the dynamic-dispatch fallback).
static std::unique_ptr<HIRInstruction> makeCall(
    const std::string& name,
    HIRInstruction* originalInst,
    std::shared_ptr<HIRType> returnType)
{
    auto call = std::make_unique<HIRInstruction>(HIROpcode::Call);
    call->result = originalInst->result;
    if (call->result) call->result->type = returnType;
    call->operands.push_back(name);
    // Forward operands. The original generic instruction has lhs/rhs (or just
    // val for unary). Append them after the function name.
    for (auto& op : originalInst->operands) {
        if (std::holds_alternative<std::shared_ptr<HIRValue>>(op)) {
            call->operands.push_back(op);
        }
    }
    call->sourceFileIdx = originalInst->sourceFileIdx;
    call->sourceLine = originalInst->sourceLine;
    call->sourceColumn = originalInst->sourceColumn;
    return call;
}

// Helper: build a type-specific binary instruction with the given opcode
// and result type, copying operands from the generic original.
static std::unique_ptr<HIRInstruction> makeTypedBinary(
    HIROpcode newOpcode,
    HIRInstruction* originalInst,
    std::shared_ptr<HIRType> resultType)
{
    auto inst = std::make_unique<HIRInstruction>(newOpcode);
    inst->result = originalInst->result;
    if (inst->result) inst->result->type = resultType;
    inst->operands = originalInst->operands;
    inst->sourceFileIdx = originalInst->sourceFileIdx;
    inst->sourceLine = originalInst->sourceLine;
    inst->sourceColumn = originalInst->sourceColumn;
    return inst;
}

std::unique_ptr<HIRInstruction> SpecializationPass::specializeArithmetic(HIRInstruction* inst) {
    if (inst->operands.size() < 2) return nullptr;

    auto lhsType = operandType(inst->operands[0]);
    auto rhsType = operandType(inst->operands[1]);
    auto* lhs = lhsType.get();
    auto* rhs = rhsType.get();

    // The RESULT type set by ASTToHIR is the analyzer's best verdict — it
    // includes information from the AST inferredType that the HIRValue::type
    // operands may have lost (e.g., GetPropStatic always produces Any but
    // the analyzer knows the property's underlying type). Use it as the
    // primary signal; fall back to operand-type analysis only when the
    // result type is itself Any.
    auto* resType = (inst->result && inst->result->type) ? inst->result->type.get() : nullptr;

    // Combine result-type and operand-type evidence. If EITHER source says
    // "this is type X", treat it as X.
    bool useString = isString(resType) || isString(lhs) || isString(rhs);
    bool useBigInt = isBigInt(resType) || isBigInt(lhs) || isBigInt(rhs);
    bool useFloat  = isFloat64(resType) || isFloat64(lhs) || isFloat64(rhs);
    bool useInt    = isInt64(resType) || (isInt64(lhs) && isInt64(rhs));
    bool resultIsAny = isAny(resType);
    bool eitherAny = (isAny(lhs) || isAny(rhs)) && resultIsAny;

    // Add is the only operator that can mean string concatenation.
    if (inst->opcode == HIROpcode::Add && useString) {
        // String concat — ASTToHIR has its own builder for this with a
        // simpler operand layout, but mirroring it here keeps the rewrite
        // self-contained.
        return makeTypedBinary(HIROpcode::StringConcat, inst, HIRType::makeString());
    }

    // BigInt → runtime call. The runtime helper name depends on the operator.
    if (useBigInt) {
        const char* fn = nullptr;
        switch (inst->opcode) {
            case HIROpcode::Add: fn = "ts_bigint_add"; break;
            case HIROpcode::Sub: fn = "ts_bigint_sub"; break;
            case HIROpcode::Mul: fn = "ts_bigint_mul"; break;
            case HIROpcode::Div: fn = "ts_bigint_div"; break;
            case HIROpcode::Mod: fn = "ts_bigint_mod"; break;
            default: return nullptr;
        }
        return makeCall(fn, inst, HIRType::makeBigInt());
    }

    // Any operand: dynamic dispatch via runtime helper.
    if (eitherAny) {
        const char* fn = nullptr;
        switch (inst->opcode) {
            case HIROpcode::Add: fn = "ts_value_add"; break;
            case HIROpcode::Sub: fn = "ts_value_sub"; break;
            case HIROpcode::Mul: fn = "ts_value_mul"; break;
            case HIROpcode::Div: fn = "ts_value_div"; break;
            case HIROpcode::Mod: fn = "ts_value_mod"; break;
            default: return nullptr;
        }
        return makeCall(fn, inst, HIRType::makeAny());
    }

    // Float64: emit AddF64 family. Division and mod are always Float64
    // per JS semantics (4/2 === 2.0, not 2).
    if (useFloat || inst->opcode == HIROpcode::Div || inst->opcode == HIROpcode::Mod) {
        HIROpcode newOp;
        switch (inst->opcode) {
            case HIROpcode::Add: newOp = HIROpcode::AddF64; break;
            case HIROpcode::Sub: newOp = HIROpcode::SubF64; break;
            case HIROpcode::Mul: newOp = HIROpcode::MulF64; break;
            case HIROpcode::Div: newOp = HIROpcode::DivF64; break;
            case HIROpcode::Mod: newOp = HIROpcode::ModF64; break;
            default: return nullptr;
        }
        return makeTypedBinary(newOp, inst, HIRType::makeFloat64());
    }

    // Both Int64 — pick the I64 family. (Div/Mod handled above as Float.)
    if (useInt) {
        HIROpcode newOp;
        switch (inst->opcode) {
            case HIROpcode::Add: newOp = HIROpcode::AddI64; break;
            case HIROpcode::Sub: newOp = HIROpcode::SubI64; break;
            case HIROpcode::Mul: newOp = HIROpcode::MulI64; break;
            default: return nullptr;
        }
        return makeTypedBinary(newOp, inst, HIRType::makeInt64());
    }

    // Couldn't classify — leave the generic instruction in place. HIRToLLVM
    // will produce a loud error if it actually reaches lowering this way.
    SPDLOG_WARN("SpecializationPass: could not specialize {} (lhs={}, rhs={})",
                inst->toString(),
                lhs ? lhs->toString() : "null",
                rhs ? rhs->toString() : "null");
    return nullptr;
}

std::unique_ptr<HIRInstruction> SpecializationPass::specializeNeg(HIRInstruction* inst) {
    if (inst->operands.empty()) return nullptr;

    auto valType = operandType(inst->operands[0]);
    auto* t = valType.get();

    // Use the result type as the primary signal (set by ASTToHIR using AST
    // fallback) and fall back to operand type only when result is Any.
    auto* resType = (inst->result && inst->result->type) ? inst->result->type.get() : nullptr;

    if (isBigInt(resType) || isBigInt(t)) {
        return makeCall("ts_bigint_neg", inst, HIRType::makeBigInt());
    }
    if (isFloat64(resType) || isFloat64(t)) {
        return makeTypedBinary(HIROpcode::NegF64, inst, HIRType::makeFloat64());
    }
    if (isInt64(resType) || isInt64(t)) {
        return makeTypedBinary(HIROpcode::NegI64, inst, HIRType::makeInt64());
    }
    if (isAny(resType) && isAny(t)) {
        return makeCall("ts_value_neg", inst, HIRType::makeAny());
    }

    SPDLOG_WARN("SpecializationPass: could not specialize neg (result={}, operand={})",
                resType ? resType->toString() : "null",
                t ? t->toString() : "null");
    return nullptr;
}

//==============================================================================
// Comparison specialization
//==============================================================================

std::unique_ptr<HIRInstruction> SpecializationPass::specializeComparison(HIRInstruction* inst) {
    if (inst->operands.size() < 2) return nullptr;

    auto lhsType = operandType(inst->operands[0]);
    auto rhsType = operandType(inst->operands[1]);
    auto* lhs = lhsType.get();
    auto* rhs = rhsType.get();

    // Comparisons always return Bool, so the result type isn't a useful
    // signal for picking the typed form. We can only use operand types.
    // If one operand is concretely-typed and the other is Any, pick the
    // concrete type's family — that matches the AST-fallback behavior of
    // the legacy ASTToHIR helpers (`isFloat64(lhs) || isFloat64(rhs)`).
    bool useBigInt = isBigInt(lhs) || isBigInt(rhs);
    bool useFloat  = isFloat64(lhs) || isFloat64(rhs);
    bool useInt    = (isInt64(lhs) || isInt64(rhs)) && !useFloat && !useBigInt;
    bool eitherAny = (isAny(lhs) && isAny(rhs));

    if (useBigInt) {
        const char* fn = nullptr;
        switch (inst->opcode) {
            case HIROpcode::CmpEq: fn = "ts_bigint_eq"; break;
            case HIROpcode::CmpNe: fn = "ts_bigint_ne"; break;
            case HIROpcode::CmpLt: fn = "ts_bigint_lt"; break;
            case HIROpcode::CmpLe: fn = "ts_bigint_le"; break;
            case HIROpcode::CmpGt: fn = "ts_bigint_gt"; break;
            case HIROpcode::CmpGe: fn = "ts_bigint_ge"; break;
            default: return nullptr;
        }
        return makeCall(fn, inst, HIRType::makeBool());
    }

    if (eitherAny) {
        const char* fn = nullptr;
        switch (inst->opcode) {
            case HIROpcode::CmpEq: fn = "ts_value_eq"; break;
            case HIROpcode::CmpNe: fn = "ts_value_ne"; break;
            case HIROpcode::CmpLt: fn = "ts_value_lt"; break;
            case HIROpcode::CmpLe: fn = "ts_value_lte"; break;
            case HIROpcode::CmpGt: fn = "ts_value_gt"; break;
            case HIROpcode::CmpGe: fn = "ts_value_gte"; break;
            default: return nullptr;
        }
        return makeCall(fn, inst, HIRType::makeBool());
    }

    if (useFloat) {
        HIROpcode newOp;
        switch (inst->opcode) {
            case HIROpcode::CmpEq: newOp = HIROpcode::CmpEqF64; break;
            case HIROpcode::CmpNe: newOp = HIROpcode::CmpNeF64; break;
            case HIROpcode::CmpLt: newOp = HIROpcode::CmpLtF64; break;
            case HIROpcode::CmpLe: newOp = HIROpcode::CmpLeF64; break;
            case HIROpcode::CmpGt: newOp = HIROpcode::CmpGtF64; break;
            case HIROpcode::CmpGe: newOp = HIROpcode::CmpGeF64; break;
            default: return nullptr;
        }
        return makeTypedBinary(newOp, inst, HIRType::makeBool());
    }

    if (useInt) {
        HIROpcode newOp;
        switch (inst->opcode) {
            case HIROpcode::CmpEq: newOp = HIROpcode::CmpEqI64; break;
            case HIROpcode::CmpNe: newOp = HIROpcode::CmpNeI64; break;
            case HIROpcode::CmpLt: newOp = HIROpcode::CmpLtI64; break;
            case HIROpcode::CmpLe: newOp = HIROpcode::CmpLeI64; break;
            case HIROpcode::CmpGt: newOp = HIROpcode::CmpGtI64; break;
            case HIROpcode::CmpGe: newOp = HIROpcode::CmpGeI64; break;
            default: return nullptr;
        }
        return makeTypedBinary(newOp, inst, HIRType::makeBool());
    }

    SPDLOG_WARN("SpecializationPass: could not specialize comparison {} (lhs={}, rhs={})",
                inst->toString(),
                lhs ? lhs->toString() : "null",
                rhs ? rhs->toString() : "null");
    return nullptr;
}

//==============================================================================
// Property access specialization
//==============================================================================

std::unique_ptr<HIRInstruction> SpecializationPass::specializeGetProp(HIRInstruction* inst) {
    // Operand layout for GetProp:
    //   [0] = receiver (HIRValue*)
    //   [1] = key (HIRValue* — may be a string constant or a runtime value)
    //   [2] = optional result type annotation (HIRType*)
    if (inst->operands.size() < 2) return nullptr;

    // Check if the key is a string-typed value (i.e., a constant key) by
    // looking at its HIRValue::type. If it is, we can rewrite to GetPropStatic.
    // Otherwise, GetPropDynamic.
    //
    // NOTE: Detecting an *actual* string-constant key (where the underlying
    // ConstString instruction's value is known at compile time) requires
    // following the def-use chain. For Phase 2 we conservatively use the
    // value's type — refine if Phase 3 needs the def-use lookup.
    auto keyType = operandType(inst->operands[1]);
    bool keyIsString = isString(keyType.get());

    if (keyIsString) {
        // Rewrite to GetPropStatic. The static form takes the property NAME
        // (a std::string), not the HIRValue. Without the const-folding step
        // we can't extract the literal here, so for now we route string-keyed
        // GetProp through GetPropDynamic. Phase 4 will add the const-key
        // optimization.
        auto inst2 = std::make_unique<HIRInstruction>(HIROpcode::GetPropDynamic);
        inst2->result = inst->result;
        inst2->operands.push_back(inst->operands[0]);  // receiver
        inst2->operands.push_back(inst->operands[1]);  // key
        inst2->sourceFileIdx = inst->sourceFileIdx;
        inst2->sourceLine = inst->sourceLine;
        inst2->sourceColumn = inst->sourceColumn;
        return inst2;
    }

    // Non-string key → GetPropDynamic.
    auto inst2 = std::make_unique<HIRInstruction>(HIROpcode::GetPropDynamic);
    inst2->result = inst->result;
    inst2->operands.push_back(inst->operands[0]);
    inst2->operands.push_back(inst->operands[1]);
    inst2->sourceFileIdx = inst->sourceFileIdx;
    inst2->sourceLine = inst->sourceLine;
    inst2->sourceColumn = inst->sourceColumn;
    return inst2;
}

std::unique_ptr<HIRInstruction> SpecializationPass::specializeSetProp(HIRInstruction* inst) {
    // Operand layout for SetProp: [0]=receiver, [1]=key, [2]=value
    if (inst->operands.size() < 3) return nullptr;

    // Same Phase 2 simplification: route everything through SetPropDynamic.
    // Phase 4 adds const-key extraction for SetPropStatic.
    auto inst2 = std::make_unique<HIRInstruction>(HIROpcode::SetPropDynamic);
    inst2->operands.push_back(inst->operands[0]);
    inst2->operands.push_back(inst->operands[1]);
    inst2->operands.push_back(inst->operands[2]);
    inst2->sourceFileIdx = inst->sourceFileIdx;
    inst2->sourceLine = inst->sourceLine;
    inst2->sourceColumn = inst->sourceColumn;
    return inst2;
}

} // namespace ts::hir
