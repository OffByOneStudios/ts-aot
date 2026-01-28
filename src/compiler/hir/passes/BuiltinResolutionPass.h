#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>
#include <vector>
#include <string>

namespace ts::hir {

/// Resolves calls to global builtins to runtime function calls
///
/// This pass transforms CallMethod instructions on global builtin objects
/// (console, Math, JSON, etc.) into direct Call instructions.
///
/// Input:
///   %0 = load_global "console"
///   %1 = call_method %0, "log", (%msg)
///
/// Output:
///   %1 = call @ts_console_log(%msg)
///
/// Alternatively, for direct property access patterns:
///   %0 = call @console.log(%msg)
///
/// Note: This pass runs AFTER MethodResolutionPass since method resolution
/// handles typed receivers (arrays, strings) while this handles global builtins.
class BuiltinResolutionPass : public HIRFunctionPass {
public:
    const char* name() const override { return "BuiltinResolution"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Try to resolve a CallMethod on a global builtin
    /// @param inst The CallMethod instruction
    /// @param func The containing function
    /// @return The replacement instruction, or nullptr if not resolved
    std::unique_ptr<HIRInstruction> tryResolveBuiltin(HIRInstruction* inst, HIRFunction& func);

    /// Check if a value is a LoadGlobal instruction loading a builtin
    /// @param value The value to check
    /// @param func The function to search for the defining instruction
    /// @return The global name if it's a builtin, empty string otherwise
    std::string getBuiltinGlobalName(std::shared_ptr<HIRValue> value, HIRFunction& func);

    /// Find the instruction that defines a value
    HIRInstruction* findDefiningInstruction(std::shared_ptr<HIRValue> value, HIRFunction& func);
};

} // namespace ts::hir
