#pragma once

#include "HIRPass.h"
#include <vector>
#include <memory>
#include <iostream>
#include <functional>

namespace ts::hir {

// Forward declaration
struct HIRModule;

/// Manages and runs a pipeline of HIR passes
///
/// Example:
/// ```cpp
/// PassManager pm;
/// pm.addPass(std::make_unique<TypePropagationPass>());
/// pm.addPass(std::make_unique<MethodResolutionPass>());
/// pm.addPass(std::make_unique<ConstantFoldingPass>());
///
/// pm.setDebugPrinting(true);  // Print HIR between passes
/// auto result = pm.run(module);
/// ```
class PassManager {
public:
    PassManager() = default;

    /// Add a pass to the pipeline
    /// Passes are run in the order they are added
    void addPass(std::unique_ptr<HIRPass> pass);

    /// Run all passes in order
    /// @param module The HIR module to transform
    /// @return Combined result of all passes
    PassResult run(HIRModule& module);

    /// Enable/disable debug output between passes
    void setDebugPrinting(bool enabled) { debugPrinting_ = enabled; }

    /// Set output stream for debug printing (default: stderr)
    void setDebugStream(std::ostream& os) { debugStream_ = &os; }

    /// Set a callback to be invoked after each pass
    /// Useful for custom logging or verification
    using PassCallback = std::function<void(const char* passName, const HIRModule& module, const PassResult& result)>;
    void setAfterPassCallback(PassCallback callback) { afterPassCallback_ = callback; }

    /// Get the number of passes in the pipeline
    size_t size() const { return passes_.size(); }

    /// Check if pipeline is empty
    bool empty() const { return passes_.empty(); }

    /// Clear all passes from the pipeline
    void clear() { passes_.clear(); }

private:
    std::vector<std::unique_ptr<HIRPass>> passes_;
    bool debugPrinting_ = false;
    std::ostream* debugStream_ = &std::cerr;
    PassCallback afterPassCallback_;

    /// Print HIR module for debugging
    void printModule(const HIRModule& module, const char* label);
};

} // namespace ts::hir
