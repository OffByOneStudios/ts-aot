#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <set>
#include <map>
#include <memory>

namespace ts::hir {

/// Dead code elimination pass for HIR optimization
///
/// This pass removes:
/// 1. Unused instructions (no uses and no side effects)
/// 2. Unreachable blocks (no predecessors except entry block)
/// 3. Dead stores (stores to unused allocas)
///
/// The pass uses a conservative liveness analysis:
/// - All terminators are live
/// - All side-effecting instructions are live
/// - Instructions whose results are used by live instructions are live
///
/// Example:
///   Input:
///     %0 = const.i64 3
///     %1 = const.i64 4
///     %2 = add.i64 %0, %1     ; dead - result unused
///     %3 = const.i64 7
///     call @print, %3
///     ret void
///
///   Output:
///     %3 = const.i64 7
///     call @print, %3
///     ret void
///
class DeadCodeEliminationPass : public HIRFunctionPass {
public:
    const char* name() const override { return "DeadCodeElimination"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Map from HIRValue to instructions that use it
    std::map<std::shared_ptr<HIRValue>, std::set<HIRInstruction*>> uses_;

    /// Set of live instructions
    std::set<HIRInstruction*> liveInstructions_;

    /// Set of reachable blocks
    std::set<HIRBlock*> reachableBlocks_;

    /// Build use-def chains for all values in the function
    void buildUseMap(HIRFunction& func);

    /// Mark instructions as live starting from roots
    void markLiveInstructions(HIRFunction& func);

    /// Check if an instruction has side effects (must be kept)
    bool hasSideEffects(HIRInstruction* inst) const;

    /// Check if an instruction is a terminator
    bool isTerminator(HIROpcode opcode) const;

    /// Mark an instruction as live and propagate to operands
    void markLive(HIRInstruction* inst);

    /// Remove dead instructions from all blocks
    /// @return true if any instructions were removed
    bool removeDeadInstructions(HIRFunction& func);

    /// Find and mark reachable blocks
    void findReachableBlocks(HIRFunction& func);

    /// Remove unreachable blocks
    /// @return true if any blocks were removed
    bool removeUnreachableBlocks(HIRFunction& func);

    /// Get the defining instruction for a value (if available)
    HIRInstruction* getDefiningInstruction(std::shared_ptr<HIRValue> val, HIRFunction& func);
};

} // namespace ts::hir
