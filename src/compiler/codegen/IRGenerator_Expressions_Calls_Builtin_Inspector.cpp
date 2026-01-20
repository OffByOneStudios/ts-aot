#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register inspector module's runtime functions once
static bool inspectorFunctionsRegistered = false;
static void ensureInspectorFunctionsRegistered(BoxingPolicy& bp) {
    if (inspectorFunctionsRegistered) return;
    inspectorFunctionsRegistered = true;

    // inspector module stubs - all functions are no-ops
    bp.registerRuntimeApi("ts_inspector_open", {true, true, true}, false);
    bp.registerRuntimeApi("ts_inspector_close", {}, false);
    bp.registerRuntimeApi("ts_inspector_url", {}, false);
    bp.registerRuntimeApi("ts_inspector_wait_for_debugger", {}, false);
    bp.registerRuntimeApi("ts_inspector_console", {}, false);

    // inspector.Session methods
    bp.registerRuntimeApi("ts_inspector_session_create", {}, false);
    bp.registerRuntimeApi("ts_inspector_session_connect", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_connect_to_main_thread", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_disconnect", {true}, false);
    bp.registerRuntimeApi("ts_inspector_session_post", {true, true, true, true}, false);
}

bool IRGenerator::tryGenerateInspectorCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureInspectorFunctionsRegistered(boxingPolicy);

    // Check if this is an inspector.* call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "inspector") return false;

    SPDLOG_DEBUG("tryGenerateInspectorCall: inspector.{}", prop->name);

    if (prop->name == "open") {
        // inspector.open(port?: number, host?: string, wait?: boolean): void
        llvm::Value* port = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* host = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* wait = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            port = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            host = boxValue(lastValue, node->arguments[1]->inferredType);
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            wait = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_open", ft);
        createCall(ft, fn.getCallee(), { port, host, wait });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "close") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_close", ft);
        createCall(ft, fn.getCallee(), {});
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "url") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_url", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    if (prop->name == "waitForDebugger") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), {}, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_wait_for_debugger", ft);
        createCall(ft, fn.getCallee(), {});
        lastValue = getUndefinedValue();
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateInspectorSessionNew(ast::NewExpression* node) {
    ensureInspectorFunctionsRegistered(boxingPolicy);

    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id) return false;

    // Check if the type is InspectorSession
    if (node->inferredType) {
        auto classType = std::dynamic_pointer_cast<ClassType>(node->inferredType);
        if (classType && classType->name == "InspectorSession") {
            SPDLOG_DEBUG("tryGenerateInspectorSessionNew: creating new inspector.Session");

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_create", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        }
    }

    // Also check for direct Session reference from inspector module
    if (id->name == "Session") {
        // Check if this is from the inspector module
        auto parent = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
        if (!parent) {
            // Could be a direct import - check the inferred type
            SPDLOG_DEBUG("tryGenerateInspectorSessionNew: creating new Session");

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_create", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        }
    }

    return false;
}

bool IRGenerator::tryGenerateInspectorSessionCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureInspectorFunctionsRegistered(boxingPolicy);

    // Get the object being called on
    auto objectType = prop->expression->inferredType;
    if (!objectType) return false;

    auto classType = std::dynamic_pointer_cast<ClassType>(objectType);
    if (!classType || classType->name != "InspectorSession") return false;

    SPDLOG_DEBUG("tryGenerateInspectorSessionCall: session.{}", prop->name);

    // Get the session object
    visit(prop->expression.get());
    llvm::Value* session = lastValue;

    if (prop->name == "connect") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_connect", ft);
        createCall(ft, fn.getCallee(), { session });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "connectToMainThread") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_connect_to_main_thread", ft);
        createCall(ft, fn.getCallee(), { session });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "disconnect") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_disconnect", ft);
        createCall(ft, fn.getCallee(), { session });
        lastValue = getUndefinedValue();
        return true;
    }

    if (prop->name == "post") {
        llvm::Value* method = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* params = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            method = lastValue;
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            params = boxValue(lastValue, node->arguments[1]->inferredType);
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_inspector_session_post", ft);
        createCall(ft, fn.getCallee(), { session, method, params, callback });
        lastValue = getUndefinedValue();
        return true;
    }

    return false;
}

} // namespace ts
