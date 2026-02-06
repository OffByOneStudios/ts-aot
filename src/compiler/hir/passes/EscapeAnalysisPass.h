#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <set>
#include <map>
#include <memory>

namespace ts::hir {

/// Escape analysis pass for HIR optimization
///
/// Determines which object allocations (NewObject, NewObjectDynamic,
/// NewArrayBoxed) can be safely stack-allocated instead of heap-allocated.
///
/// An allocation escapes if:
/// - It is returned from the function
/// - It is passed as an argument to a call (conservative)
/// - It is stored into another object's property or array element
/// - It is used by a Phi with escaping incoming values
/// - The function is async or a generator (stack doesn't persist)
///
/// Non-escaping allocations get inst->escapes = false, which HIRToLLVM
/// uses to emit alloca instead of ts_alloc.
///
class EscapeAnalysisPass : public HIRFunctionPass {
public:
    const char* name() const override { return "EscapeAnalysis"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Map from HIRValue to the instruction that defines it
    std::map<HIRValue*, HIRInstruction*> definingInst_;

    /// Map from HIRValue to all instructions that use it
    std::map<HIRValue*, std::set<HIRInstruction*>> useMap_;

    /// Set of values that escape
    std::set<HIRValue*> escapingValues_;

    /// Build def-use chains for all values in the function
    void buildDefUseChains(HIRFunction& func);

    /// Check if an opcode is an allocation we can optimize
    bool isAllocationOpcode(HIROpcode op) const;

    /// Check if a use causes the value to escape
    bool useEscapes(HIRInstruction* use, HIRValue* allocValue) const;

    /// Propagate escape through Phi nodes
    void propagateEscapeThroughPhis(HIRFunction& func);
};

} // namespace ts::hir
