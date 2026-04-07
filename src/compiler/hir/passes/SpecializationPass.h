#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>

namespace ts::hir {

/// Strategy B: rewrites generic HIR opcodes into type-specific ones
///
/// Phase 1 added generic opcodes (Add, Sub, Mul, Div, Mod, Neg, CmpEq..CmpGe,
/// GetProp, SetProp) that ASTToHIR will emit in Phase 3+ instead of committing
/// to a type at emission time. This pass walks the function after
/// TypePropagationPass has refined operand types and rewrites each generic
/// instruction into the type-specific form (AddI64/AddF64/StringConcat/runtime
/// call) based on what operand types are statically known.
///
/// Decision table for binary arithmetic (`Add`):
///   String + anything             → StringConcat
///   BigInt + BigInt               → Call("ts_bigint_add", ...)
///   Float64 + Numeric             → AddF64
///   Int64 + Int64                 → AddI64
///   Any + anything (or unknown)   → Call("ts_value_add", ...)
///
/// Comparison opcodes (`CmpEq`/`CmpLt`/...) follow similar rules but always
/// return Bool. `GetProp` becomes `GetPropStatic` (if key is a string constant)
/// or `GetPropDynamic` (otherwise), with the analyzer-derived result type
/// preserved as the type annotation operand.
///
/// Until Phase 3 starts emitting generic opcodes, this pass is a no-op
/// — it scans every function but never finds a generic opcode to rewrite.
///
/// Pattern modeled on IntegerOptimizationPass — single forward pass over
/// instructions, in-place replacement via std::unique_ptr swap.
class SpecializationPass : public HIRFunctionPass {
public:
    const char* name() const override { return "Specialization"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Specialize a single instruction. Returns the replacement instruction
    /// (which the caller swaps into the block) or nullptr if the instruction
    /// is not a generic opcode or cannot be specialized with current type info.
    std::unique_ptr<HIRInstruction> specializeInstruction(HIRInstruction* inst);

    /// Specialize an arithmetic op (Add/Sub/Mul/Div/Mod) by picking the right
    /// type-specific opcode based on operand types.
    std::unique_ptr<HIRInstruction> specializeArithmetic(HIRInstruction* inst);

    /// Specialize a comparison op (CmpEq/CmpNe/CmpLt/CmpLe/CmpGt/CmpGe).
    std::unique_ptr<HIRInstruction> specializeComparison(HIRInstruction* inst);

    /// Specialize a unary Neg.
    std::unique_ptr<HIRInstruction> specializeNeg(HIRInstruction* inst);

    /// Specialize a generic GetProp into GetPropStatic or GetPropDynamic.
    std::unique_ptr<HIRInstruction> specializeGetProp(HIRInstruction* inst);

    /// Specialize a generic SetProp into SetPropStatic or SetPropDynamic.
    std::unique_ptr<HIRInstruction> specializeSetProp(HIRInstruction* inst);

    /// Helpers — read operand types from HIRValue::type.
    static std::shared_ptr<HIRType> operandType(const HIROperand& op);
    static bool isString(const HIRType* t);
    static bool isFloat64(const HIRType* t);
    static bool isInt64(const HIRType* t);
    static bool isBigInt(const HIRType* t);
    static bool isAny(const HIRType* t);
    static bool isBool(const HIRType* t);
};

} // namespace ts::hir
