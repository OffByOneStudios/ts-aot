#include "PassManager.h"
#include "../HIR.h"
#include "../HIRPrinter.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

void PassManager::addPass(std::unique_ptr<HIRPass> pass) {
    passes_.push_back(std::move(pass));
}

PassResult PassManager::run(HIRModule& module) {
    PassResult result = PassResult::unchanged();

    if (debugPrinting_) {
        printModule(module, "Before passes");
    }

    for (auto& pass : passes_) {
        SPDLOG_DEBUG("Running HIR pass: {}", pass->name());

        auto passResult = pass->run(module);

        // Propagate errors immediately
        if (!passResult.success()) {
            SPDLOG_ERROR("HIR pass '{}' failed: {}", pass->name(), passResult.error);
            return passResult;
        }

        if (passResult.changed) {
            SPDLOG_DEBUG("HIR pass '{}' modified the module", pass->name());
        }

        // Invoke callback if set
        if (afterPassCallback_) {
            afterPassCallback_(pass->name(), module, passResult);
        }

        // Debug print after each pass that made changes
        if (debugPrinting_ && passResult.changed) {
            std::string label = std::string("After ") + pass->name();
            printModule(module, label.c_str());
        }

        result |= passResult;
    }

    return result;
}

void PassManager::printModule(const HIRModule& module, const char* label) {
    *debugStream_ << "\n=== HIR: " << label << " ===\n";
    HIRPrinter printer(*debugStream_);
    printer.print(module);
    *debugStream_ << "\n";
}

} // namespace ts::hir
