#pragma once

#include <string>
#include <memory>

namespace ts::hir {

// Forward declarations
struct HIRModule;
struct HIRFunction;

/// Result of running a pass
struct PassResult {
    bool changed;           ///< Did the pass modify the IR?
    std::string error;      ///< Empty if successful

    static PassResult unchanged() { return {false, ""}; }
    static PassResult modified() { return {true, ""}; }
    static PassResult failed(const std::string& msg) { return {false, msg}; }

    /// Check if pass succeeded (no error)
    bool success() const { return error.empty(); }

    /// Combine two results (changed if either changed, first error wins)
    PassResult& operator|=(const PassResult& other) {
        if (error.empty() && !other.error.empty()) {
            error = other.error;
        }
        changed = changed || other.changed;
        return *this;
    }
};

/// Base class for all HIR passes
///
/// Passes transform or analyze HIR modules. They can operate at the module
/// level (ModulePass) or function level (FunctionPass).
///
/// Example:
/// ```cpp
/// class MyPass : public HIRPass {
/// public:
///     const char* name() const override { return "MyPass"; }
///     PassResult run(HIRModule& module) override {
///         // Transform module...
///         return PassResult::modified();
///     }
/// };
/// ```
class HIRPass {
public:
    virtual ~HIRPass() = default;

    /// Human-readable name for debugging and logging
    virtual const char* name() const = 0;

    /// Run the pass on an entire module
    /// @param module The HIR module to transform
    /// @return Result indicating if changes were made or if an error occurred
    virtual PassResult run(HIRModule& module) = 0;
};

/// Pass that operates on one function at a time
///
/// Subclass this for passes that don't need cross-function information.
/// The base run() method iterates over all functions and calls runOnFunction().
///
/// Function passes that need to look up module-level information (other
/// functions, classes, shapes) can access the current module via
/// `currentModule_`, which is set by the base run() before each iteration.
class HIRFunctionPass : public HIRPass {
public:
    /// Runs runOnFunction() on each function in the module
    PassResult run(HIRModule& module) override;

    /// Override this to implement per-function transformation
    /// @param func The function to transform
    /// @return Result indicating if changes were made
    virtual PassResult runOnFunction(HIRFunction& func) = 0;

protected:
    /// The HIR module currently being processed. Set by the base run() before
    /// each runOnFunction() call. Subclasses can use this to look up callee
    /// return types, class shapes, and other module-level information.
    /// Will be nullptr outside of a run() invocation.
    HIRModule* currentModule_ = nullptr;
};

} // namespace ts::hir
