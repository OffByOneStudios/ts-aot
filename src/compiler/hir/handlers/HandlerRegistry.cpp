#include "HandlerRegistry.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

bool HandlerRegistry::builtinsRegistered_ = false;

HandlerRegistry& HandlerRegistry::instance() {
    static HandlerRegistry instance;

    // Auto-register builtin handlers on first access
    if (!builtinsRegistered_) {
        builtinsRegistered_ = true;
        registerBuiltinHandlers();
    }

    return instance;
}

void HandlerRegistry::registerHandler(std::unique_ptr<BuiltinHandler> handler) {
    if (handler) {
        SPDLOG_DEBUG("HandlerRegistry: registering handler '{}'", handler->name());
        handlers_.push_back(std::move(handler));
    }
}

llvm::Value* HandlerRegistry::tryLower(const std::string& funcName,
                                        HIRInstruction* inst,
                                        HIRToLLVM& lowerer) {
    for (auto& handler : handlers_) {
        if (handler->canHandle(funcName, inst)) {
            SPDLOG_DEBUG("HandlerRegistry: {} handling '{}'", handler->name(), funcName);
            return handler->lower(funcName, inst, lowerer);
        }
    }
    return nullptr;  // No handler matched
}

bool HandlerRegistry::hasHandler(const std::string& funcName, HIRInstruction* inst) const {
    for (const auto& handler : handlers_) {
        if (handler->canHandle(funcName, inst)) {
            return true;
        }
    }
    return false;
}

void HandlerRegistry::registerBuiltinHandlers() {
    // Phase 1: Empty registry - no handlers registered yet
    // Handlers will be added in subsequent phases:
    //   Phase 2: MathHandler
    //   Phase 3: ConsoleHandler
    //   Phase 4: ArrayHandler
    //   etc.

    SPDLOG_DEBUG("HandlerRegistry: builtin handlers registered (count={})",
                 instance().handlerCount());
}

} // namespace ts::hir
