#pragma once

#include "BuiltinHandler.h"
#include <memory>
#include <vector>
#include <string>

namespace ts::hir {

//==============================================================================
// HandlerRegistry - Central registry for builtin function handlers
//
// This registry maintains a list of BuiltinHandler instances and provides
// a unified interface for lowering builtin calls. Handlers are tried in
// registration order until one claims the call.
//
// Usage:
//   // Register handlers (typically at initialization)
//   HandlerRegistry::instance().registerHandler(std::make_unique<MathHandler>());
//   HandlerRegistry::instance().registerHandler(std::make_unique<ConsoleHandler>());
//
//   // Try to lower a call
//   if (auto* result = HandlerRegistry::instance().tryLower(funcName, inst, lowerer)) {
//       // Handler succeeded
//   }
//==============================================================================
class HandlerRegistry {
public:
    /// Get the singleton instance
    static HandlerRegistry& instance();

    /// Register a handler. Handlers are tried in registration order.
    void registerHandler(std::unique_ptr<BuiltinHandler> handler);

    /// Try to lower a call using registered handlers.
    /// @param funcName The function name (e.g., "Math.min", "console.log")
    /// @param inst The HIR call instruction
    /// @param lowerer Reference to the HIRToLLVM lowerer
    /// @return The result value if a handler succeeded, nullptr if no handler matched
    llvm::Value* tryLower(const std::string& funcName,
                          HIRInstruction* inst,
                          HIRToLLVM& lowerer);

    /// Check if any handler can handle the given function
    bool hasHandler(const std::string& funcName, HIRInstruction* inst) const;

    //==========================================================================
    // Method call interface (for lowerCallMethod - e.g., map.set(key, val))
    //==========================================================================

    /// Try to lower a method call using registered handlers.
    /// @param methodName The method name (e.g., "set", "get", "push")
    /// @param className The class/receiver type name (e.g., "Map", "Set", "Array")
    /// @param inst The HIR call instruction
    /// @param lowerer Reference to the HIRToLLVM lowerer
    /// @return The result value if a handler succeeded, nullptr if no handler matched
    llvm::Value* tryLowerMethod(const std::string& methodName,
                                const std::string& className,
                                HIRInstruction* inst,
                                HIRToLLVM& lowerer);

    /// Check if any handler can handle the given method call
    bool hasMethodHandler(const std::string& methodName,
                          const std::string& className,
                          HIRInstruction* inst) const;

    /// Get the number of registered handlers (for debugging/testing)
    size_t handlerCount() const { return handlers_.size(); }

    /// Clear all handlers (for testing)
    void clear() { handlers_.clear(); }

    /// Register all builtin handlers
    /// Called during initialization to populate the registry
    static void registerBuiltinHandlers();

private:
    HandlerRegistry() = default;
    ~HandlerRegistry() = default;

    // Non-copyable
    HandlerRegistry(const HandlerRegistry&) = delete;
    HandlerRegistry& operator=(const HandlerRegistry&) = delete;

    std::vector<std::unique_ptr<BuiltinHandler>> handlers_;
    static bool builtinsRegistered_;
};

} // namespace ts::hir
