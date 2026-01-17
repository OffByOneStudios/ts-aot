#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

static bool assertFunctionsRegistered = false;
static void ensureAssertFunctionsRegistered(BoxingPolicy& bp) {
    if (assertFunctionsRegistered) return;
    assertFunctionsRegistered = true;

    // All assert functions take boxed values
    bp.registerRuntimeApi("ts_assert", {true, true}, false);                     // value, message
    bp.registerRuntimeApi("ts_assert_ok", {true, true}, false);                  // value, message
    bp.registerRuntimeApi("ts_assert_equal", {true, true, true}, false);         // actual, expected, message
    bp.registerRuntimeApi("ts_assert_not_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_strict_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_not_strict_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_deep_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_not_deep_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_deep_strict_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_not_deep_strict_equal", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_throws", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_does_not_throw", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_rejects", {true, true, true}, true);
    bp.registerRuntimeApi("ts_assert_does_not_reject", {true, true, true}, true);
    bp.registerRuntimeApi("ts_assert_fail", {true}, false);
    bp.registerRuntimeApi("ts_assert_if_error", {true}, false);
    bp.registerRuntimeApi("ts_assert_match", {true, true, true}, false);
    bp.registerRuntimeApi("ts_assert_does_not_match", {true, true, true}, false);
}

bool IRGenerator::tryGenerateAssertCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if this is an assert.* call
    auto id = dynamic_cast<Identifier*>(prop->expression.get());
    if (!id || id->name != "assert") return false;

    ensureAssertFunctionsRegistered(boxingPolicy);
    SPDLOG_DEBUG("tryGenerateAssertCall: assert.{}", prop->name);

    const std::string& methodName = prop->name;

    // Helper to get a boxed argument or null
    auto getBoxedArg = [&](size_t idx) -> llvm::Value* {
        if (idx < node->arguments.size()) {
            visit(node->arguments[idx].get());
            return boxValue(lastValue, node->arguments[idx]->inferredType);
        }
        return llvm::ConstantPointerNull::get(builder->getPtrTy());
    };

    // Generate function calls based on method name
    if (methodName == "ok" || methodName == "default") {
        llvm::Value* value = getBoxedArg(0);
        llvm::Value* message = getBoxedArg(1);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_ok", ft);
        createCall(ft, fn.getCallee(), { value, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "equal") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "notEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_not_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "strictEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_strict_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "notStrictEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_not_strict_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "deepEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_deep_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "notDeepEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_not_deep_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "deepStrictEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_deep_strict_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "notDeepStrictEqual") {
        llvm::Value* actual = getBoxedArg(0);
        llvm::Value* expected = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_not_deep_strict_equal", ft);
        createCall(ft, fn.getCallee(), { actual, expected, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "throws") {
        llvm::Value* fn_arg = getBoxedArg(0);
        llvm::Value* error = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_throws", ft);
        createCall(ft, fn.getCallee(), { fn_arg, error, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "doesNotThrow") {
        llvm::Value* fn_arg = getBoxedArg(0);
        llvm::Value* error = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_does_not_throw", ft);
        createCall(ft, fn.getCallee(), { fn_arg, error, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "rejects") {
        llvm::Value* asyncFn = getBoxedArg(0);
        llvm::Value* error = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_rejects", ft);
        lastValue = createCall(ft, fn.getCallee(), { asyncFn, error, message });
        return true;
    }

    if (methodName == "doesNotReject") {
        llvm::Value* asyncFn = getBoxedArg(0);
        llvm::Value* error = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_does_not_reject", ft);
        lastValue = createCall(ft, fn.getCallee(), { asyncFn, error, message });
        return true;
    }

    if (methodName == "fail") {
        llvm::Value* message = getBoxedArg(0);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_fail", ft);
        createCall(ft, fn.getCallee(), { message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "ifError") {
        llvm::Value* value = getBoxedArg(0);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_if_error", ft);
        createCall(ft, fn.getCallee(), { value });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "match") {
        llvm::Value* str = getBoxedArg(0);
        llvm::Value* regexp = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_match", ft);
        createCall(ft, fn.getCallee(), { str, regexp, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    if (methodName == "doesNotMatch") {
        llvm::Value* str = getBoxedArg(0);
        llvm::Value* regexp = getBoxedArg(1);
        llvm::Value* message = getBoxedArg(2);

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_assert_does_not_match", ft);
        createCall(ft, fn.getCallee(), { str, regexp, message });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    return false;
}

} // namespace ts
