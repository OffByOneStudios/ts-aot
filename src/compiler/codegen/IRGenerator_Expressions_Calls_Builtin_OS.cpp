#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace ts {
using namespace ast;

// Static helper to register os module's runtime functions once
static bool osFunctionsRegistered = false;
static void ensureOSFunctionsRegistered(BoxingPolicy& bp) {
    if (osFunctionsRegistered) return;
    osFunctionsRegistered = true;

    // OS info functions (no args, return string)
    bp.registerRuntimeApi("ts_os_platform", {}, false);
    bp.registerRuntimeApi("ts_os_arch", {}, false);
    bp.registerRuntimeApi("ts_os_type", {}, false);
    bp.registerRuntimeApi("ts_os_release", {}, false);
    bp.registerRuntimeApi("ts_os_version", {}, false);
    bp.registerRuntimeApi("ts_os_hostname", {}, false);
    bp.registerRuntimeApi("ts_os_machine", {}, false);

    // Path functions (no args, return string)
    bp.registerRuntimeApi("ts_os_homedir", {}, false);
    bp.registerRuntimeApi("ts_os_tmpdir", {}, false);

    // Memory functions (no args, return int)
    bp.registerRuntimeApi("ts_os_totalmem", {}, false);
    bp.registerRuntimeApi("ts_os_freemem", {}, false);
    bp.registerRuntimeApi("ts_os_availableParallelism", {}, false);

    // Other info functions
    bp.registerRuntimeApi("ts_os_uptime", {}, false);  // returns double
    bp.registerRuntimeApi("ts_os_loadavg", {}, true);  // returns boxed array
    bp.registerRuntimeApi("ts_os_cpus", {}, true);     // returns boxed array
    bp.registerRuntimeApi("ts_os_endianness", {}, false);  // returns string
    bp.registerRuntimeApi("ts_os_networkInterfaces", {}, true);  // returns boxed object
    bp.registerRuntimeApi("ts_os_userInfo", {}, true);  // returns boxed object

    // Priority functions
    bp.registerRuntimeApi("ts_os_getPriority", {false}, false);  // pid -> int
    bp.registerRuntimeApi("ts_os_setPriority", {false, false}, false);  // pid, priority -> void

    // Constants functions
    bp.registerRuntimeApi("ts_os_get_constants", {}, true);
    bp.registerRuntimeApi("ts_os_get_signals", {}, true);
    bp.registerRuntimeApi("ts_os_get_errno", {}, true);
    bp.registerRuntimeApi("ts_os_get_priority_constants", {}, true);
}

bool IRGenerator::tryGenerateOSCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Register boxing info for os functions on first call
    ensureOSFunctionsRegistered(boxingPolicy);

    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "os") return false;

    const std::string& methodName = prop->name;

    // ========================================================================
    // String-returning functions (no args)
    // ========================================================================

    if (methodName == "platform" || methodName == "arch" || methodName == "type" ||
        methodName == "release" || methodName == "version" || methodName == "hostname" ||
        methodName == "machine" || methodName == "homedir" || methodName == "tmpdir" ||
        methodName == "endianness") {

        std::string funcName = "ts_os_" + methodName;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Integer-returning functions (no args)
    // ========================================================================

    if (methodName == "totalmem" || methodName == "freemem" || methodName == "availableParallelism") {
        std::string funcName = "ts_os_" + methodName;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Double-returning functions (no args)
    // ========================================================================

    if (methodName == "uptime") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getDoubleTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_uptime", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Array-returning functions
    // ========================================================================

    if (methodName == "loadavg" || methodName == "cpus") {
        std::string funcName = "ts_os_" + methodName;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // ========================================================================
    // Object-returning functions
    // ========================================================================

    if (methodName == "networkInterfaces" || methodName == "userInfo") {
        std::string funcName = "ts_os_" + methodName;
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(funcName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // ========================================================================
    // Priority functions
    // ========================================================================

    if (methodName == "getPriority") {
        // getPriority(pid?: number): number
        llvm::Value* pid;
        if (node->arguments.empty()) {
            pid = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
        } else {
            visit(node->arguments[0].get());
            pid = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt64Ty(),
            { builder->getInt64Ty() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_getPriority", ft);
        lastValue = createCall(ft, fn.getCallee(), { pid });
        return true;
    }

    if (methodName == "setPriority") {
        // setPriority(pid: number, priority: number): void
        // or setPriority(priority: number): void (pid = 0)
        llvm::Value* pid;
        llvm::Value* priority;

        if (node->arguments.size() == 1) {
            pid = llvm::ConstantInt::get(builder->getInt64Ty(), 0);
            visit(node->arguments[0].get());
            priority = lastValue;
        } else if (node->arguments.size() >= 2) {
            visit(node->arguments[0].get());
            pid = lastValue;
            visit(node->arguments[1].get());
            priority = lastValue;
        } else {
            return false;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getInt64Ty(), builder->getInt64Ty() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_setPriority", ft);
        createCall(ft, fn.getCallee(), { pid, priority });
        lastValue = nullptr;
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateOSPropertyAccess(ast::PropertyAccessExpression* node) {
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "os") return false;

    const std::string& propName = node->name;

    // os.EOL - line ending constant
    if (propName == "EOL") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_eol", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // os.devNull - null device path
    if (propName == "devNull") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_devnull", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    // os.constants - returns the full constants object
    if (propName == "constants") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateOSConstantsPropertyAccess(ast::PropertyAccessExpression* node) {
    // Handle os.constants.signals, os.constants.errno, os.constants.priority
    auto parentProp = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
    if (!parentProp) return false;

    auto id = dynamic_cast<ast::Identifier*>(parentProp->expression.get());
    if (!id || id->name != "os") return false;
    if (parentProp->name != "constants") return false;

    const std::string& propName = node->name;

    if (propName == "signals") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_signals", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    if (propName == "errno") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_errno", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    if (propName == "priority") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_os_get_priority_constants", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

} // namespace ts
