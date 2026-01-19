#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register async_hooks module's runtime functions once
static bool asyncHooksFunctionsRegistered = false;
static void ensureAsyncHooksFunctionsRegistered(BoxingPolicy& bp) {
    if (asyncHooksFunctionsRegistered) return;
    asyncHooksFunctionsRegistered = true;

    // Module-level functions
    bp.registerRuntimeApi("ts_async_hooks_executionAsyncId", {}, false);
    bp.registerRuntimeApi("ts_async_hooks_triggerAsyncId", {}, false);
    bp.registerRuntimeApi("ts_async_hooks_executionAsyncResource", {}, true);
    bp.registerRuntimeApi("ts_async_hooks_createHook", {true}, true);

    // AsyncLocalStorage
    bp.registerRuntimeApi("ts_async_local_storage_create", {}, true);
    bp.registerRuntimeApi("ts_async_local_storage_getStore", {true}, true);
    bp.registerRuntimeApi("ts_async_local_storage_run", {true, true, true}, true);
    bp.registerRuntimeApi("ts_async_local_storage_exit", {true, true}, true);
    bp.registerRuntimeApi("ts_async_local_storage_enterWith", {true, true}, false);
    bp.registerRuntimeApi("ts_async_local_storage_disable", {true}, false);

    // AsyncResource
    bp.registerRuntimeApi("ts_async_resource_create", {false}, true);
    bp.registerRuntimeApi("ts_async_resource_asyncId", {true}, false);
    bp.registerRuntimeApi("ts_async_resource_triggerAsyncId", {true}, false);
    bp.registerRuntimeApi("ts_async_resource_runInAsyncScope", {true, true, true}, true);
    bp.registerRuntimeApi("ts_async_resource_bind", {true, true}, true);
    bp.registerRuntimeApi("ts_async_resource_emitDestroy", {true}, true);

    // AsyncHook
    bp.registerRuntimeApi("ts_async_hook_enable", {true}, true);
    bp.registerRuntimeApi("ts_async_hook_disable", {true}, true);
}

bool IRGenerator::tryGenerateAsyncHooksCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Check if this is an async_hooks.xxx call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "async_hooks") return false;

    SPDLOG_DEBUG("tryGenerateAsyncHooksCall: async_hooks.{}", prop->name);

    const std::string& methodName = prop->name;

    // =========================================================================
    // async_hooks.executionAsyncId()
    // =========================================================================
    if (methodName == "executionAsyncId") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hooks_executionAsyncId", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // =========================================================================
    // async_hooks.triggerAsyncId()
    // =========================================================================
    if (methodName == "triggerAsyncId") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hooks_triggerAsyncId", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // =========================================================================
    // async_hooks.executionAsyncResource()
    // =========================================================================
    if (methodName == "executionAsyncResource") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            {},
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hooks_executionAsyncResource", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // async_hooks.createHook(callbacks)
    // =========================================================================
    if (methodName == "createHook") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* callbacks = lastValue;

        // Ensure boxed
        if (!boxedValues.count(callbacks)) {
            callbacks = boxValue(callbacks, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hooks_createHook", ft);
        lastValue = createCall(ft, fn.getCallee(), { callbacks });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateAsyncLocalStorageCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for AsyncLocalStorage method calls
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "AsyncLocalStorage") {
        return false;
    }

    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Get the object we're calling the method on
    visit(prop->expression.get());
    llvm::Value* alsObj = lastValue;

    // Ensure it's boxed
    if (!boxedValues.count(alsObj)) {
        alsObj = boxValue(alsObj, prop->expression->inferredType);
    }

    const std::string& methodName = prop->name;

    SPDLOG_DEBUG("tryGenerateAsyncLocalStorageCall: als.{}", methodName);

    // =========================================================================
    // als.getStore()
    // =========================================================================
    if (methodName == "getStore") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_getStore", ft);
        lastValue = createCall(ft, fn.getCallee(), { alsObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // als.run(store, callback, ...args)
    // =========================================================================
    if (methodName == "run") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get store value
        visit(node->arguments[0].get());
        llvm::Value* store = lastValue;
        if (!boxedValues.count(store)) {
            store = boxValue(store, node->arguments[0]->inferredType);
        }

        // Get callback
        visit(node->arguments[1].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[1]->inferredType);
        }

        // For simplicity, pass argc=0 and args=nullptr
        // A more complete implementation would pack additional args
        llvm::Value* argc = llvm::ConstantInt::get(builder->getInt32Ty(), 0);
        llvm::Value* argsNull = llvm::ConstantPointerNull::get(builder->getPtrTy());

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_run", ft);
        lastValue = createCall(ft, fn.getCallee(), { alsObj, store, callback, argc, argsNull });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // als.exit(callback, ...args)
    // =========================================================================
    if (methodName == "exit") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get callback
        visit(node->arguments[0].get());
        llvm::Value* callback = lastValue;
        if (!boxedValues.count(callback)) {
            callback = boxValue(callback, node->arguments[0]->inferredType);
        }

        llvm::Value* argc = llvm::ConstantInt::get(builder->getInt32Ty(), 0);
        llvm::Value* argsNull = llvm::ConstantPointerNull::get(builder->getPtrTy());

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_exit", ft);
        lastValue = createCall(ft, fn.getCallee(), { alsObj, callback, argc, argsNull });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // als.enterWith(store)
    // =========================================================================
    if (methodName == "enterWith") {
        if (node->arguments.empty()) {
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* store = lastValue;
        if (!boxedValues.count(store)) {
            store = boxValue(store, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_enterWith", ft);
        createCall(ft, fn.getCallee(), { alsObj, store });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    // =========================================================================
    // als.disable()
    // =========================================================================
    if (methodName == "disable") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_disable", ft);
        createCall(ft, fn.getCallee(), { alsObj });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateAsyncResourceCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for AsyncResource method calls
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "AsyncResource") {
        return false;
    }

    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Get the object we're calling the method on
    visit(prop->expression.get());
    llvm::Value* resourceObj = lastValue;

    // Ensure it's boxed
    if (!boxedValues.count(resourceObj)) {
        resourceObj = boxValue(resourceObj, prop->expression->inferredType);
    }

    const std::string& methodName = prop->name;

    SPDLOG_DEBUG("tryGenerateAsyncResourceCall: resource.{}", methodName);

    // =========================================================================
    // resource.asyncId()
    // =========================================================================
    if (methodName == "asyncId") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_asyncId", ft);
        lastValue = createCall(ft, fn.getCallee(), { resourceObj });
        return true;
    }

    // =========================================================================
    // resource.triggerAsyncId()
    // =========================================================================
    if (methodName == "triggerAsyncId") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_triggerAsyncId", ft);
        lastValue = createCall(ft, fn.getCallee(), { resourceObj });
        return true;
    }

    // =========================================================================
    // resource.runInAsyncScope(fn, thisArg?, ...args)
    // =========================================================================
    if (methodName == "runInAsyncScope") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get function
        visit(node->arguments[0].get());
        llvm::Value* fn = lastValue;
        if (!boxedValues.count(fn)) {
            fn = boxValue(fn, node->arguments[0]->inferredType);
        }

        // Get optional thisArg
        llvm::Value* thisArg;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            thisArg = lastValue;
            if (!boxedValues.count(thisArg)) {
                thisArg = boxValue(thisArg, node->arguments[1]->inferredType);
            }
        } else {
            thisArg = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::Value* argc = llvm::ConstantInt::get(builder->getInt32Ty(), 0);
        llvm::Value* argsNull = llvm::ConstantPointerNull::get(builder->getPtrTy());

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fnCallee = getRuntimeFunction("ts_async_resource_runInAsyncScope", ft);
        lastValue = createCall(ft, fnCallee.getCallee(), { resourceObj, fn, thisArg, argc, argsNull });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // resource.bind(fn)
    // =========================================================================
    if (methodName == "bind") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* fn = lastValue;
        if (!boxedValues.count(fn)) {
            fn = boxValue(fn, node->arguments[0]->inferredType);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fnCallee = getRuntimeFunction("ts_async_resource_bind", ft);
        lastValue = createCall(ft, fnCallee.getCallee(), { resourceObj, fn });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // resource.emitDestroy()
    // =========================================================================
    if (methodName == "emitDestroy") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_emitDestroy", ft);
        lastValue = createCall(ft, fn.getCallee(), { resourceObj });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateAsyncHookMethodCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for AsyncHook method calls
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "AsyncHook") {
        return false;
    }

    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Get the object we're calling the method on
    visit(prop->expression.get());
    llvm::Value* hookObj = lastValue;

    // Ensure it's boxed
    if (!boxedValues.count(hookObj)) {
        hookObj = boxValue(hookObj, prop->expression->inferredType);
    }

    const std::string& methodName = prop->name;

    SPDLOG_DEBUG("tryGenerateAsyncHookMethodCall: hook.{}", methodName);

    // =========================================================================
    // hook.enable()
    // =========================================================================
    if (methodName == "enable") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hook_enable", ft);
        lastValue = createCall(ft, fn.getCallee(), { hookObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // hook.disable()
    // =========================================================================
    if (methodName == "disable") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_hook_disable", ft);
        lastValue = createCall(ft, fn.getCallee(), { hookObj });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateAsyncLocalStorageNew(ast::NewExpression* node) {
    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Check if this is new AsyncLocalStorage()
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "AsyncLocalStorage") return false;

    SPDLOG_DEBUG("tryGenerateAsyncLocalStorageNew: new AsyncLocalStorage()");

    llvm::FunctionType* ft = llvm::FunctionType::get(
        builder->getPtrTy(),
        {},
        false
    );
    llvm::FunctionCallee fn = getRuntimeFunction("ts_async_local_storage_create", ft);
    lastValue = createCall(ft, fn.getCallee(), {});
    boxedValues.insert(lastValue);
    return true;
}

bool IRGenerator::tryGenerateAsyncResourceNew(ast::NewExpression* node) {
    ensureAsyncHooksFunctionsRegistered(boxingPolicy);

    // Check if this is new AsyncResource(type, triggerAsyncId?)
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "AsyncResource") return false;

    SPDLOG_DEBUG("tryGenerateAsyncResourceNew: new AsyncResource()");

    if (node->arguments.empty()) {
        // Type is required - create with "UNKNOWN"
        llvm::Constant* unknownCStr = builder->CreateGlobalStringPtr("UNKNOWN");
        llvm::FunctionType* strFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee strFn = getRuntimeFunction("ts_string_create", strFt);
        llvm::Value* typeStr = createCall(strFt, strFn.getCallee(), { unknownCStr });

        llvm::Value* triggerId = llvm::ConstantInt::get(builder->getInt64Ty(), -1);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getInt64Ty() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_create", ft);
        lastValue = createCall(ft, fn.getCallee(), { typeStr, triggerId });
        boxedValues.insert(lastValue);
        return true;
    }

    // Get type string
    visit(node->arguments[0].get());
    llvm::Value* typeStr = lastValue;

    // Get optional triggerAsyncId
    llvm::Value* triggerId;
    if (node->arguments.size() > 1) {
        visit(node->arguments[1].get());
        triggerId = lastValue;
        // Ensure it's an i64
        if (triggerId->getType() != builder->getInt64Ty()) {
            if (triggerId->getType()->isDoubleTy()) {
                triggerId = builder->CreateFPToSI(triggerId, builder->getInt64Ty());
            }
        }
    } else {
        triggerId = llvm::ConstantInt::get(builder->getInt64Ty(), -1);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(
        builder->getPtrTy(),
        { builder->getPtrTy(), builder->getInt64Ty() },
        false
    );
    llvm::FunctionCallee fn = getRuntimeFunction("ts_async_resource_create", ft);
    lastValue = createCall(ft, fn.getCallee(), { typeStr, triggerId });
    boxedValues.insert(lastValue);
    return true;
}

} // namespace ts
