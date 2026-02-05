#pragma once

#include "../HIR.h"
#include <llvm/IR/Value.h>
#include <string>

namespace ts::hir {

// Forward declaration
class HIRToLLVM;

//==============================================================================
// BuiltinHandler - Interface for handling builtin function lowering
//
// Each handler is responsible for a category of builtin functions (e.g., Math,
// Console, Array). Handlers are registered with the HandlerRegistry and tried
// in order until one claims the call.
//
// This pattern replaces the giant switch statements in lowerCall/lowerCallMethod,
// making the codebase more maintainable and testable.
//==============================================================================
class BuiltinHandler {
public:
    virtual ~BuiltinHandler() = default;

    /// Get a human-readable name for this handler (for debugging)
    virtual const char* name() const = 0;

    /// Check if this handler can handle the given function call.
    /// @param funcName The name of the function being called (e.g., "Math.min", "console.log")
    /// @param inst The HIR call instruction
    /// @return true if this handler will handle the call
    virtual bool canHandle(const std::string& funcName,
                          HIRInstruction* inst) const = 0;

    /// Lower the call to LLVM IR.
    /// Only called if canHandle() returned true.
    /// @param funcName The name of the function being called
    /// @param inst The HIR call instruction
    /// @param lowerer Reference to the HIRToLLVM lowerer for helper access
    /// @return The LLVM Value representing the result, or nullptr for void
    virtual llvm::Value* lower(const std::string& funcName,
                               HIRInstruction* inst,
                               HIRToLLVM& lowerer) = 0;
};

} // namespace ts::hir
