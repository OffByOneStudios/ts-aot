#include "HIRPass.h"
#include "../HIR.h"

namespace ts::hir {

PassResult HIRFunctionPass::run(HIRModule& module) {
    PassResult result = PassResult::unchanged();

    for (auto& func : module.functions) {
        auto funcResult = runOnFunction(*func);

        // Propagate errors immediately
        if (!funcResult.success()) {
            return funcResult;
        }

        // Track if any function was modified
        result |= funcResult;
    }

    return result;
}

} // namespace ts::hir
