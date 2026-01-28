#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>
#include <vector>
#include <string>

namespace ts::hir {

/// Resolves high-level method calls to specific HIR operations
///
/// This pass transforms CallMethod instructions into more specific opcodes
/// when the receiver type is known. Uses the BuiltinRegistry for centralized
/// method resolution tables.
///
/// Input:
///   %0 = call_method %arr, "push", (%val)    ; arr has Array type
///
/// Output:
///   %0 = array.push %arr, %val
///
/// Transformations performed:
/// - Array methods: push, pop, shift, unshift, length, etc.
/// - String methods: charAt, charCodeAt, substring, etc.
/// - Object methods: keys, values, entries, etc.
///
/// Methods on unknown types or unrecognized methods are left as CallMethod
/// for runtime dispatch.
class MethodResolutionPass : public HIRFunctionPass {
public:
    const char* name() const override { return "MethodResolution"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Try to resolve a CallMethod instruction
    /// @param inst The CallMethod instruction
    /// @param func The containing function (for creating new instructions)
    /// @return The replacement instruction, or nullptr if not resolved
    std::unique_ptr<HIRInstruction> tryResolveMethod(HIRInstruction* inst, HIRFunction& func);

    /// Create an instruction with a specific HIR opcode
    std::unique_ptr<HIRInstruction> createOpcodeInstruction(
        HIRInstruction* original,
        HIROpcode opcode,
        std::shared_ptr<HIRValue> receiver,
        const std::vector<std::shared_ptr<HIRValue>>& args);

    /// Create a Call instruction to a runtime function
    std::unique_ptr<HIRInstruction> createRuntimeCallInstruction(
        HIRInstruction* original,
        const std::string& runtimeFunction,
        std::shared_ptr<HIRValue> receiver,
        const std::vector<std::shared_ptr<HIRValue>>& args,
        std::shared_ptr<HIRType> returnType);
};

} // namespace ts::hir
