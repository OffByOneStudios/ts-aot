#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register cluster module's runtime functions once
static bool clusterFunctionsRegistered = false;
static void ensureClusterFunctionsRegistered(BoxingPolicy& bp) {
    if (clusterFunctionsRegistered) return;
    clusterFunctionsRegistered = true;

    // Cluster module methods
    bp.registerRuntimeApi("ts_cluster_fork", {true}, true);
    bp.registerRuntimeApi("ts_cluster_setup_primary", {true}, false);
    bp.registerRuntimeApi("ts_cluster_setup_master", {true}, false);
    bp.registerRuntimeApi("ts_cluster_disconnect", {true}, false);

    // Cluster module properties (called as functions for codegen)
    bp.registerRuntimeApi("ts_cluster_is_master", {}, false);
    bp.registerRuntimeApi("ts_cluster_is_primary", {}, false);
    bp.registerRuntimeApi("ts_cluster_is_worker", {}, false);
    bp.registerRuntimeApi("ts_cluster_get_worker", {}, true);
    bp.registerRuntimeApi("ts_cluster_get_workers", {}, true);
    bp.registerRuntimeApi("ts_cluster_get_settings", {}, true);

    // Worker instance methods
    bp.registerRuntimeApi("ts_worker_get_id", {true}, false);
    bp.registerRuntimeApi("ts_worker_get_process", {true}, true);
    bp.registerRuntimeApi("ts_worker_is_dead", {true}, false);
    bp.registerRuntimeApi("ts_worker_exited_after_disconnect", {true}, false);
    bp.registerRuntimeApi("ts_worker_is_connected", {true}, false);
    bp.registerRuntimeApi("ts_worker_send", {true, true, true}, false);
    bp.registerRuntimeApi("ts_worker_disconnect", {true}, false);
    bp.registerRuntimeApi("ts_worker_kill", {true, true}, false);
}

bool IRGenerator::tryGenerateClusterCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureClusterFunctionsRegistered(boxingPolicy);

    // Check if this is a cluster.xxx call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "cluster") return false;

    SPDLOG_DEBUG("tryGenerateClusterCall: cluster.{}", prop->name);

    const std::string& methodName = prop->name;

    // =========================================================================
    // cluster.fork(env?)
    // =========================================================================
    if (methodName == "fork") {
        llvm::Value* env;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            env = lastValue;
            if (!boxedValues.count(env)) {
                env = boxValue(env, node->arguments[0]->inferredType);
            }
        } else {
            env = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_cluster_fork", ft);
        lastValue = createCall(ft, fn.getCallee(), { env });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cluster.setupPrimary(settings?) / cluster.setupMaster(settings?)
    // =========================================================================
    if (methodName == "setupPrimary" || methodName == "setupMaster") {
        llvm::Value* settings;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            settings = lastValue;
            if (!boxedValues.count(settings)) {
                settings = boxValue(settings, node->arguments[0]->inferredType);
            }
        } else {
            settings = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        const char* funcName = (methodName == "setupPrimary")
            ? "ts_cluster_setup_primary"
            : "ts_cluster_setup_master";

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction(funcName, ft);
        lastValue = createCall(ft, fn.getCallee(), { settings });
        return true;
    }

    // =========================================================================
    // cluster.disconnect(callback?)
    // =========================================================================
    if (methodName == "disconnect") {
        llvm::Value* callback;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            callback = lastValue;
            if (!boxedValues.count(callback)) {
                callback = boxValue(callback, node->arguments[0]->inferredType);
            }
        } else {
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_cluster_disconnect", ft);
        lastValue = createCall(ft, fn.getCallee(), { callback });
        return true;
    }

    // =========================================================================
    // cluster.on(event, listener) / cluster.once(event, listener)
    // These use the EventEmitter pattern
    // =========================================================================
    if (methodName == "on" || methodName == "once") {
        // Get the cluster singleton first
        llvm::FunctionType* getInstanceFt = llvm::FunctionType::get(
            builder->getPtrTy(), {}, false
        );
        llvm::FunctionCallee getInstanceFn = getRuntimeFunction("ts_cluster_get_instance", getInstanceFt);
        llvm::Value* cluster = createCall(getInstanceFt, getInstanceFn.getCallee(), {});

        if (node->arguments.size() < 2) {
            lastValue = cluster;
            return true;
        }

        // Get event name
        visit(node->arguments[0].get());
        llvm::Value* eventName = lastValue;
        if (!boxedValues.count(eventName)) {
            eventName = boxValue(eventName, node->arguments[0]->inferredType);
        }

        // Get listener callback
        visit(node->arguments[1].get());
        llvm::Value* listener = lastValue;
        if (!boxedValues.count(listener)) {
            listener = boxValue(listener, node->arguments[1]->inferredType);
        }

        // Use EventEmitter on/once
        const char* emitterFunc = (methodName == "on")
            ? "ts_event_emitter_on"
            : "ts_event_emitter_once";

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction(emitterFunc, ft);
        lastValue = createCall(ft, fn.getCallee(), { cluster, eventName, listener });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateWorkerCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureClusterFunctionsRegistered(boxingPolicy);

    // Get the expression type to see if it's a Worker
    auto exprType = prop->expression->inferredType;
    if (!exprType) return false;

    // Check if the type is Worker (ClassType with name "Worker")
    auto classType = std::dynamic_pointer_cast<ClassType>(exprType);
    if (!classType || classType->name != "Worker") return false;

    SPDLOG_DEBUG("tryGenerateWorkerCall: Worker.{}", prop->name);

    const std::string& methodName = prop->name;

    // Get the worker object
    visit(prop->expression.get());
    llvm::Value* worker = lastValue;
    if (!boxedValues.count(worker)) {
        worker = boxValue(worker, exprType);
    }

    // =========================================================================
    // worker.send(message, sendHandle?)
    // =========================================================================
    if (methodName == "send") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantInt::get(builder->getInt1Ty(), 0);
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* message = lastValue;
        if (!boxedValues.count(message)) {
            message = boxValue(message, node->arguments[0]->inferredType);
        }

        llvm::Value* sendHandle;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            sendHandle = lastValue;
            if (!boxedValues.count(sendHandle)) {
                sendHandle = boxValue(sendHandle, node->arguments[1]->inferredType);
            }
        } else {
            sendHandle = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_worker_send", ft);
        lastValue = createCall(ft, fn.getCallee(), { worker, message, sendHandle });
        return true;
    }

    // =========================================================================
    // worker.disconnect()
    // =========================================================================
    if (methodName == "disconnect") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_worker_disconnect", ft);
        lastValue = createCall(ft, fn.getCallee(), { worker });
        return true;
    }

    // =========================================================================
    // worker.kill(signal?)
    // =========================================================================
    if (methodName == "kill") {
        llvm::Value* signal;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            signal = lastValue;
            if (!boxedValues.count(signal)) {
                signal = boxValue(signal, node->arguments[0]->inferredType);
            }
        } else {
            signal = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_worker_kill", ft);
        lastValue = createCall(ft, fn.getCallee(), { worker, signal });
        return true;
    }

    // =========================================================================
    // worker.isConnected()
    // =========================================================================
    if (methodName == "isConnected") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_worker_is_connected", ft);
        lastValue = createCall(ft, fn.getCallee(), { worker });
        return true;
    }

    // =========================================================================
    // worker.on(event, listener) / worker.once(event, listener)
    // =========================================================================
    if (methodName == "on" || methodName == "once") {
        if (node->arguments.size() < 2) {
            lastValue = worker;
            return true;
        }

        // Get event name
        visit(node->arguments[0].get());
        llvm::Value* eventName = lastValue;
        if (!boxedValues.count(eventName)) {
            eventName = boxValue(eventName, node->arguments[0]->inferredType);
        }

        // Get listener callback
        visit(node->arguments[1].get());
        llvm::Value* listener = lastValue;
        if (!boxedValues.count(listener)) {
            listener = boxValue(listener, node->arguments[1]->inferredType);
        }

        const char* emitterFunc = (methodName == "on")
            ? "ts_event_emitter_on"
            : "ts_event_emitter_once";

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction(emitterFunc, ft);
        lastValue = createCall(ft, fn.getCallee(), { worker, eventName, listener });
        return true;
    }

    return false;
}

} // namespace ts
