#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>
#include <map>
#include <set>

namespace ts::hir {

/// Type propagation pass for refining types through the SSA graph
///
/// This pass performs three main tasks:
/// 1. Forward type propagation - Infer more precise types for values based on
///    operations (constants, arithmetic, boxing/unboxing, etc.)
/// 2. Phi node type merging - Compute the join of types at merge points
/// 3. Type narrowing - After typeof/instanceof guards, narrow types in branches
///
/// Input:
///   %0 = const.i64 42
///   %1 = box.int %0         ; type: Any (was)
///   %2 = call "foo", %1
///
/// Output:
///   %0 = const.i64 42       ; type: Int64 (confirmed)
///   %1 = box.int %0         ; type: Any, innerType: Int64
///   %2 = call "foo", %1
///
/// Type narrowing example:
///   condbr %isNumber, %thenBB, %elseBB
///   %thenBB:
///     ; %x now known to be Int64 or Float64
class TypePropagationPass : public HIRFunctionPass {
public:
    const char* name() const override { return "TypePropagation"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Run a single iteration of type propagation
    /// @return true if any types were changed
    bool propagateTypes(HIRFunction& func);

    /// Infer the result type for an instruction based on operands
    std::shared_ptr<HIRType> inferResultType(HIRInstruction* inst);

    /// Merge types at a phi node
    /// @return The merged type (join of all incoming types)
    std::shared_ptr<HIRType> mergePhiTypes(HIRInstruction* phi);

    /// Check if two types are compatible (can be merged)
    bool typesCompatible(const HIRType* a, const HIRType* b) const;

    /// Compute the join (common supertype) of two types
    std::shared_ptr<HIRType> joinTypes(std::shared_ptr<HIRType> a,
                                        std::shared_ptr<HIRType> b);

    /// Apply type narrowing based on a conditional branch
    /// @param cond The condition value
    /// @param trueBB The true branch target
    /// @param falseBB The false branch target
    void applyTypeNarrowing(HIRInstruction* condBr, HIRBlock* trueBB, HIRBlock* falseBB);

    /// Check if a value has a more specific type after a guard
    struct TypeGuard {
        std::shared_ptr<HIRValue> value;      // The guarded value
        std::shared_ptr<HIRType> narrowedType; // Type after guard is true
        bool isNegation = false;               // True for "!=" comparisons
    };

    /// Extract type guard info from a condition
    std::optional<TypeGuard> extractTypeGuard(HIRInstruction* condInst);

    /// Track refined types in specific blocks
    std::map<HIRBlock*, std::map<std::shared_ptr<HIRValue>, std::shared_ptr<HIRType>>> narrowedTypes_;

    /// Get the effective type of a value in a block (considering narrowing)
    std::shared_ptr<HIRType> getEffectiveType(std::shared_ptr<HIRValue> val, HIRBlock* block);
};

} // namespace ts::hir
