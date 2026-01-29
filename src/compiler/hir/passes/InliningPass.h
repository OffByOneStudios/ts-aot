#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <set>
#include <map>
#include <memory>
#include <string>

namespace ts::hir {

/// Inlining pass for HIR optimization
///
/// This pass inlines small functions and lambda callbacks at call sites.
/// Inlining enables further optimizations by:
/// - Eliminating call overhead
/// - Enabling constant propagation across call boundaries
/// - Allowing dead code elimination of unused parameters
///
/// The pass uses a cost model to decide whether to inline:
/// - Instruction count threshold (default: 20)
/// - Call site bonus for single-use functions
/// - Penalty for recursive functions (never inline)
///
/// Example:
///   Before:
///     func @double(%x: i64) -> i64:
///       %1 = mul.i64 %x, 2
///       ret %1
///
///     func @user_main:
///       %a = const.i64 5
///       %b = call @double, %a
///       call @print, %b
///
///   After:
///     func @user_main:
///       %a = const.i64 5
///       %1 = mul.i64 %a, 2    ; inlined
///       call @print, %1
///
class InliningPass : public HIRPass {
public:
    /// Configuration for inlining decisions
    struct Config {
        /// Maximum instruction count for a function to be inlinable
        size_t maxInlineSize = 20;

        /// Bonus for functions called only once (added to threshold)
        size_t singleCallSiteBonus = 10;

        /// Whether to inline lambda/arrow function callbacks
        bool inlineLambdas = true;

        /// Whether to inline across basic blocks (more complex)
        bool allowMultiBlock = false;

        /// Maximum inline depth (prevent exponential growth)
        size_t maxInlineDepth = 3;
    };

    explicit InliningPass(Config config = Config{});

    const char* name() const override { return "Inlining"; }

    PassResult run(HIRModule& module) override;

private:
    Config config_;

    /// Map from function name to call count
    std::map<std::string, size_t> callCounts_;

    /// Set of functions that are recursive (directly or indirectly)
    std::set<std::string> recursiveFunctions_;

    /// Current inline depth for preventing infinite recursion
    size_t currentInlineDepth_ = 0;

    /// Analyze module to gather call statistics
    void analyzeCallGraph(HIRModule& module);

    /// Count calls to each function
    void countCalls(HIRFunction& func);

    /// Detect recursive functions
    void detectRecursion(HIRModule& module);

    /// Check if a function should be inlined at a call site
    bool shouldInline(const HIRFunction& caller, const HIRFunction& callee);

    /// Get the instruction count of a function (approximate size)
    size_t getFunctionSize(const HIRFunction& func) const;

    /// Check if a function is a simple lambda (single block, no control flow)
    bool isSimpleLambda(const HIRFunction& func) const;

    /// Inline a function call
    /// @param caller The function containing the call
    /// @param callInst The Call instruction to inline
    /// @param callee The function being called
    /// @return true if inlining succeeded
    bool inlineCall(HIRFunction& caller, HIRInstruction* callInst,
                    const HIRFunction& callee);

    /// Clone and remap a function's instructions for inlining
    /// @param callee The function to clone
    /// @param args The arguments passed to the call
    /// @param valueMap Map from original values to cloned values
    /// @param caller The calling function (for generating new value IDs)
    /// @return The cloned instructions
    std::vector<std::unique_ptr<HIRInstruction>> cloneInstructions(
        const HIRFunction& callee,
        const std::vector<std::shared_ptr<HIRValue>>& args,
        std::map<std::shared_ptr<HIRValue>, std::shared_ptr<HIRValue>>& valueMap,
        HIRFunction& caller);

    /// Find the return value from inlined instructions
    std::shared_ptr<HIRValue> findReturnValue(
        const std::vector<std::unique_ptr<HIRInstruction>>& instructions);

    /// Replace uses of the call result with the inlined return value
    void replaceCallResult(HIRFunction& func, HIRInstruction* callInst,
                           std::shared_ptr<HIRValue> returnValue);
};

} // namespace ts::hir
