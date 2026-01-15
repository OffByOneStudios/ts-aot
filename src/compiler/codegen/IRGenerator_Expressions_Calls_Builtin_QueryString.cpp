#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register querystring module's runtime functions once
static bool querystringFunctionsRegistered = false;
static void ensureQueryStringFunctionsRegistered(BoxingPolicy& bp) {
    if (querystringFunctionsRegistered) return;
    querystringFunctionsRegistered = true;

    // querystring.parse(str, sep?, eq?) -> object
    bp.registerRuntimeApi("ts_querystring_parse", {false, false, false}, true);
    // querystring.stringify(obj, sep?, eq?) -> string
    bp.registerRuntimeApi("ts_querystring_stringify", {true, false, false}, false);
    // querystring.escape(str) -> string
    bp.registerRuntimeApi("ts_querystring_escape", {false}, false);
    // querystring.unescape(str) -> string
    bp.registerRuntimeApi("ts_querystring_unescape", {false}, false);
}

bool IRGenerator::tryGenerateQueryStringCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureQueryStringFunctionsRegistered(boxingPolicy);

    // Check if this is a querystring.xxx call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "querystring") return false;

    SPDLOG_DEBUG("tryGenerateQueryStringCall: querystring.{}", prop->name);

    const std::string& methodName = prop->name;

    // =========================================================================
    // querystring.parse(str, sep?, eq?)
    // querystring.decode(str, sep?, eq?) - alias
    // =========================================================================
    if (methodName == "parse" || methodName == "decode") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get string to parse
        visit(node->arguments[0].get());
        llvm::Value* str = lastValue;

        // Get optional separator (default "&")
        llvm::Value* sep;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            sep = lastValue;
        } else {
            sep = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        // Get optional equals sign (default "=")
        llvm::Value* eq;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            eq = lastValue;
        } else {
            eq = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_querystring_parse", ft);
        lastValue = createCall(ft, fn.getCallee(), { str, sep, eq });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // querystring.stringify(obj, sep?, eq?)
    // querystring.encode(obj, sep?, eq?) - alias
    // =========================================================================
    if (methodName == "stringify" || methodName == "encode") {
        if (node->arguments.empty()) {
            // Empty object returns empty string
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_string_create", ft);
            llvm::Value* emptyStr = builder->CreateGlobalStringPtr("");
            lastValue = createCall(ft, fn.getCallee(), { emptyStr });
            return true;
        }

        // Get object to stringify
        visit(node->arguments[0].get());
        llvm::Value* obj = lastValue;

        // Ensure it's boxed for the runtime call
        if (!boxedValues.count(obj)) {
            obj = boxValue(obj, node->arguments[0]->inferredType);
        }

        // Get optional separator (default "&")
        llvm::Value* sep;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            sep = lastValue;
        } else {
            sep = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        // Get optional equals sign (default "=")
        llvm::Value* eq;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            eq = lastValue;
        } else {
            eq = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_querystring_stringify", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, sep, eq });
        return true;
    }

    // =========================================================================
    // querystring.escape(str)
    // =========================================================================
    if (methodName == "escape") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* str = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_querystring_escape", ft);
        lastValue = createCall(ft, fn.getCallee(), { str });
        return true;
    }

    // =========================================================================
    // querystring.unescape(str)
    // =========================================================================
    if (methodName == "unescape") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* str = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_querystring_unescape", ft);
        lastValue = createCall(ft, fn.getCallee(), { str });
        return true;
    }

    return false;
}

} // namespace ts
