#include "HIRPass.h"
#include "../HIR.h"

namespace ts::hir {

PassResult HIRFunctionPass::run(HIRModule& module) {
    PassResult result = PassResult::unchanged();
    currentModule_ = &module;

    for (auto& func : module.functions) {
        auto funcResult = runOnFunction(*func);

        // Propagate errors immediately
        if (!funcResult.success()) {
            currentModule_ = nullptr;
            return funcResult;
        }

        // Track if any function was modified
        result |= funcResult;
    }

    currentModule_ = nullptr;
    return result;
}

} // namespace ts::hir
