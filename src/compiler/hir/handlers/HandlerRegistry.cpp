#include "HandlerRegistry.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

// Factory functions for handlers (defined in their respective .cpp files)
std::unique_ptr<BuiltinHandler> createMathHandler();
std::unique_ptr<BuiltinHandler> createConsoleHandler();
std::unique_ptr<BuiltinHandler> createArrayHandler();

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
    auto& reg = instance();

    // Phase 2: MathHandler - Math.* and Number.* functions
    reg.registerHandler(createMathHandler());

    // Phase 3: ConsoleHandler - console.* functions with type dispatch
    reg.registerHandler(createConsoleHandler());

    // Phase 4: ArrayHandler - Array.* functions
    reg.registerHandler(createArrayHandler());

    // Future phases:
    //   Phase 5: MapSetHandler, PathHandler, TimerHandler, etc.

    SPDLOG_DEBUG("HandlerRegistry: builtin handlers registered (count={})",
                 reg.handlerCount());
}

} // namespace ts::hir
