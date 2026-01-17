#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register V8 module's runtime functions once
static bool v8FunctionsRegistered = false;
static void ensureV8FunctionsRegistered(BoxingPolicy& bp) {
    if (v8FunctionsRegistered) return;
    v8FunctionsRegistered = true;

    // v8 module stubs return placeholder data instead of throwing
    bp.registerRuntimeApi("ts_v8_get_heap_statistics", {}, true);
    bp.registerRuntimeApi("ts_v8_get_heap_space_statistics", {}, true);
    bp.registerRuntimeApi("ts_v8_get_heap_code_statistics", {}, true);
    bp.registerRuntimeApi("ts_v8_set_flags_from_string", {true}, false);
    bp.registerRuntimeApi("ts_v8_write_heap_snapshot", {true}, false);
    bp.registerRuntimeApi("ts_v8_serialize", {true}, true);
    bp.registerRuntimeApi("ts_v8_deserialize", {true}, true);
    bp.registerRuntimeApi("ts_v8_cached_data_version_tag", {}, false);
}

bool IRGenerator::tryGenerateV8Call(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureV8FunctionsRegistered(boxingPolicy);

    // Check if this is a v8.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "v8") return false;

    SPDLOG_DEBUG("tryGenerateV8Call: v8.{}", prop->name);

    if (prop->name == "getHeapStatistics") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_get_heap_statistics", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    if (prop->name == "getHeapSpaceStatistics") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_get_heap_space_statistics", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    if (prop->name == "getHeapCodeStatistics") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_get_heap_code_statistics", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    if (prop->name == "setFlagsFromString") {
        llvm::Value* flags = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            flags = lastValue;
        } else {
            flags = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_set_flags_from_string", ft);
        createCall(ft, fn.getCallee(), { flags });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "writeHeapSnapshot") {
        llvm::Value* filename = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            filename = lastValue;
        } else {
            filename = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_write_heap_snapshot", ft);
        lastValue = createCall(ft, fn.getCallee(), { filename });
        return true;
    }

    if (prop->name == "serialize") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* value = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_serialize", ft);
        lastValue = createCall(ft, fn.getCallee(), { value });
        return true;
    }

    if (prop->name == "deserialize") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* buffer = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_deserialize", ft);
        lastValue = createCall(ft, fn.getCallee(), { buffer });
        return true;
    }

    if (prop->name == "cachedDataVersionTag") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_v8_cached_data_version_tag", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

} // namespace ts
