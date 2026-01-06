#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register Promise runtime functions
static bool promiseFunctionsRegistered = false;
static void ensurePromiseFunctionsRegistered(BoxingPolicy& bp) {
    if (promiseFunctionsRegistered) return;
    promiseFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_promise_then", {true, true, true}, true);  // promise, onFulfilled, onRejected
    bp.registerRuntimeApi("ts_promise_catch", {true, true}, true);  // promise, onRejected
}

bool IRGenerator::tryGeneratePromiseCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensurePromiseFunctionsRegistered(boxingPolicy);

    SPDLOG_DEBUG("tryGeneratePromiseCall: checking method '{}'", prop->name);

    // Handle promise.then(onFulfilled, onRejected)
    if (prop->name == "then") {
        visit(prop->expression.get());
        llvm::Value* promise = lastValue;

        if (node->arguments.empty()) return true;

        visit(node->arguments[0].get());
        llvm::Value* onFulfilled = lastValue;

        llvm::Value* onRejected = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            onRejected = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_promise_then", ft);
        lastValue = createCall(ft, fn.getCallee(), { promise, onFulfilled, onRejected });
        return true;
    }

    // Handle promise.catch(onRejected)
    if (prop->name == "catch") {
        visit(prop->expression.get());
        llvm::Value* promise = lastValue;

        if (node->arguments.empty()) return true;

        visit(node->arguments[0].get());
        llvm::Value* onRejected = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_promise_catch", ft);
        lastValue = createCall(ft, fn.getCallee(), { promise, onRejected });
        return true;
    }

    return false;
}

} // namespace ts
