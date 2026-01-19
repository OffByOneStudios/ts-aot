#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register child_process module's runtime functions once
static bool childProcessFunctionsRegistered = false;
static void ensureChildProcessFunctionsRegistered(BoxingPolicy& bp) {
    if (childProcessFunctionsRegistered) return;
    childProcessFunctionsRegistered = true;

    // Module-level functions
    bp.registerRuntimeApi("ts_child_process_spawn", {true, true, true}, true);
    bp.registerRuntimeApi("ts_child_process_exec", {true, true, true}, true);
    bp.registerRuntimeApi("ts_child_process_exec_sync", {true, true}, true);
    bp.registerRuntimeApi("ts_child_process_exec_file", {true, true, true, true}, true);
    bp.registerRuntimeApi("ts_child_process_exec_file_sync", {true, true, true}, true);
    bp.registerRuntimeApi("ts_child_process_fork", {true, true, true}, true);
    bp.registerRuntimeApi("ts_child_process_spawn_sync", {true, true, true}, true);

    // ChildProcess instance methods
    bp.registerRuntimeApi("ts_child_process_kill", {true, true}, false);
    bp.registerRuntimeApi("ts_child_process_get_pid", {true}, false);
    bp.registerRuntimeApi("ts_child_process_get_connected", {true}, false);
    bp.registerRuntimeApi("ts_child_process_get_killed", {true}, false);
    bp.registerRuntimeApi("ts_child_process_get_exit_code", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_signal_code", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_spawnfile", {true}, false);  // Returns raw TsString*
    bp.registerRuntimeApi("ts_child_process_get_spawnargs", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_stdin", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_stdout", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_stderr", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_channel", {true}, true);
    bp.registerRuntimeApi("ts_child_process_get_stdio", {true}, true);
    bp.registerRuntimeApi("ts_child_process_send", {true, true, true}, false);
    bp.registerRuntimeApi("ts_child_process_disconnect", {true}, false);
    bp.registerRuntimeApi("ts_child_process_ref", {true}, true);
    bp.registerRuntimeApi("ts_child_process_unref", {true}, true);
}

bool IRGenerator::tryGenerateChildProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureChildProcessFunctionsRegistered(boxingPolicy);

    // Check if this is a child_process.xxx call
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "child_process") return false;

    SPDLOG_DEBUG("tryGenerateChildProcessCall: child_process.{}", prop->name);

    const std::string& methodName = prop->name;

    // =========================================================================
    // child_process.spawn(command, args?, options?)
    // =========================================================================
    if (methodName == "spawn") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get command string
        visit(node->arguments[0].get());
        llvm::Value* command = lastValue;
        if (!boxedValues.count(command)) {
            command = boxValue(command, node->arguments[0]->inferredType);
        }

        // Get optional args array
        llvm::Value* args;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            args = lastValue;
            if (!boxedValues.count(args)) {
                args = boxValue(args, node->arguments[1]->inferredType);
            }
        } else {
            args = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        // Get optional options object
        llvm::Value* options;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[2]->inferredType);
            }
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_spawn", ft);
        lastValue = createCall(ft, fn.getCallee(), { command, args, options });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.exec(command, options?, callback?)
    // =========================================================================
    if (methodName == "exec") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get command string
        visit(node->arguments[0].get());
        llvm::Value* command = lastValue;
        if (!boxedValues.count(command)) {
            command = boxValue(command, node->arguments[0]->inferredType);
        }

        // Get optional options
        llvm::Value* options;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[1]->inferredType);
            }
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        // Get optional callback
        llvm::Value* callback;
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            callback = lastValue;
            if (!boxedValues.count(callback)) {
                callback = boxValue(callback, node->arguments[2]->inferredType);
            }
        } else {
            callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_exec", ft);
        lastValue = createCall(ft, fn.getCallee(), { command, options, callback });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.execSync(command, options?)
    // =========================================================================
    if (methodName == "execSync") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get command string
        visit(node->arguments[0].get());
        llvm::Value* command = lastValue;
        if (!boxedValues.count(command)) {
            command = boxValue(command, node->arguments[0]->inferredType);
        }

        // Get optional options
        llvm::Value* options;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[1]->inferredType);
            }
        } else {
            options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_exec_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { command, options });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.execFile(file, args?, options?, callback?)
    // =========================================================================
    if (methodName == "execFile") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get file string
        visit(node->arguments[0].get());
        llvm::Value* file = lastValue;
        if (!boxedValues.count(file)) {
            file = boxValue(file, node->arguments[0]->inferredType);
        }

        // Get optional args
        llvm::Value* args = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            args = lastValue;
            if (!boxedValues.count(args)) {
                args = boxValue(args, node->arguments[1]->inferredType);
            }
        }

        // Get optional options
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[2]->inferredType);
            }
        }

        // Get optional callback
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 3) {
            visit(node->arguments[3].get());
            callback = lastValue;
            if (!boxedValues.count(callback)) {
                callback = boxValue(callback, node->arguments[3]->inferredType);
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_exec_file", ft);
        lastValue = createCall(ft, fn.getCallee(), { file, args, options, callback });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.execFileSync(file, args?, options?)
    // =========================================================================
    if (methodName == "execFileSync") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get file string
        visit(node->arguments[0].get());
        llvm::Value* file = lastValue;
        if (!boxedValues.count(file)) {
            file = boxValue(file, node->arguments[0]->inferredType);
        }

        // Get optional args
        llvm::Value* args = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            args = lastValue;
            if (!boxedValues.count(args)) {
                args = boxValue(args, node->arguments[1]->inferredType);
            }
        }

        // Get optional options
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[2]->inferredType);
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_exec_file_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { file, args, options });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.fork(modulePath, args?, options?)
    // =========================================================================
    if (methodName == "fork") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get modulePath string
        visit(node->arguments[0].get());
        llvm::Value* modulePath = lastValue;
        if (!boxedValues.count(modulePath)) {
            modulePath = boxValue(modulePath, node->arguments[0]->inferredType);
        }

        // Get optional args
        llvm::Value* args = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            args = lastValue;
            if (!boxedValues.count(args)) {
                args = boxValue(args, node->arguments[1]->inferredType);
            }
        }

        // Get optional options
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[2]->inferredType);
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_fork", ft);
        lastValue = createCall(ft, fn.getCallee(), { modulePath, args, options });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // child_process.spawnSync(command, args?, options?)
    // =========================================================================
    if (methodName == "spawnSync") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get command string
        visit(node->arguments[0].get());
        llvm::Value* command = lastValue;
        if (!boxedValues.count(command)) {
            command = boxValue(command, node->arguments[0]->inferredType);
        }

        // Get optional args
        llvm::Value* args = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            args = lastValue;
            if (!boxedValues.count(args)) {
                args = boxValue(args, node->arguments[1]->inferredType);
            }
        }

        // Get optional options
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            options = lastValue;
            if (!boxedValues.count(options)) {
                options = boxValue(options, node->arguments[2]->inferredType);
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_spawn_sync", ft);
        lastValue = createCall(ft, fn.getCallee(), { command, args, options });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateChildProcessMethodCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check for ChildProcess method calls
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "ChildProcess") {
        return false;
    }

    ensureChildProcessFunctionsRegistered(boxingPolicy);

    // Get the object we're calling the method on
    visit(prop->expression.get());
    llvm::Value* cpObj = lastValue;
    // Note: Don't box here - the runtime functions handle unboxing via ts_value_get_object

    const std::string& methodName = prop->name;

    SPDLOG_DEBUG("tryGenerateChildProcessMethodCall: cp.{}", methodName);

    // =========================================================================
    // cp.kill(signal?)
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
            builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_kill", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj, signal });
        return true;
    }

    // =========================================================================
    // cp.send(message, sendHandle?)
    // =========================================================================
    if (methodName == "send") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantInt::getFalse(*context);
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* message = lastValue;
        if (!boxedValues.count(message)) {
            message = boxValue(message, node->arguments[0]->inferredType);
        }

        llvm::Value* sendHandle = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            sendHandle = lastValue;
            if (!boxedValues.count(sendHandle)) {
                sendHandle = boxValue(sendHandle, node->arguments[1]->inferredType);
            }
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_send", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj, message, sendHandle });
        return true;
    }

    // =========================================================================
    // cp.disconnect()
    // =========================================================================
    if (methodName == "disconnect") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_disconnect", ft);
        createCall(ft, fn.getCallee(), { cpObj });
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    // =========================================================================
    // cp.ref()
    // =========================================================================
    if (methodName == "ref") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_ref", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.unref()
    // =========================================================================
    if (methodName == "unref") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_unref", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateChildProcessPropertyAccess(ast::PropertyAccessExpression* prop) {
    // Check for ChildProcess property access
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    if (classType->name != "ChildProcess") {
        return false;
    }

    ensureChildProcessFunctionsRegistered(boxingPolicy);

    // Get the object
    visit(prop->expression.get());
    llvm::Value* cpObj = lastValue;
    // Note: Don't box here - the runtime functions handle unboxing via ts_value_get_object

    const std::string& propName = prop->name;

    SPDLOG_DEBUG("tryGenerateChildProcessPropertyAccess: cp.{}", propName);

    // =========================================================================
    // cp.pid
    // =========================================================================
    if (propName == "pid") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_pid", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        return true;
    }

    // =========================================================================
    // cp.connected
    // =========================================================================
    if (propName == "connected") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_connected", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        return true;
    }

    // =========================================================================
    // cp.killed
    // =========================================================================
    if (propName == "killed") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_killed", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        return true;
    }

    // =========================================================================
    // cp.exitCode
    // =========================================================================
    if (propName == "exitCode") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_exit_code", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.signalCode
    // =========================================================================
    if (propName == "signalCode") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_signal_code", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.spawnfile
    // =========================================================================
    if (propName == "spawnfile") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_spawnfile", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        // Returns raw TsString*, not boxed - don't add to boxedValues
        return true;
    }

    // =========================================================================
    // cp.spawnargs
    // =========================================================================
    if (propName == "spawnargs") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_spawnargs", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.stdin
    // =========================================================================
    if (propName == "stdin") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_stdin", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.stdout
    // =========================================================================
    if (propName == "stdout") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_stdout", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.stderr
    // =========================================================================
    if (propName == "stderr") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_stderr", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.channel
    // =========================================================================
    if (propName == "channel") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_channel", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    // =========================================================================
    // cp.stdio
    // =========================================================================
    if (propName == "stdio") {
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_child_process_get_stdio", ft);
        lastValue = createCall(ft, fn.getCallee(), { cpObj });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

} // namespace ts
