#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

// Static helper to register Process module's runtime functions once (28 functions)
static bool processFunctionsRegistered = false;
static void ensureProcessFunctionsRegistered(BoxingPolicy& bp) {
    if (processFunctionsRegistered) return;
    processFunctionsRegistered = true;
    
    // Process lifecycle
    bp.registerRuntimeApi("ts_process_exit", {false}, false);  // code
    bp.registerRuntimeApi("ts_process_abort", {}, false);
    
    // Process info
    bp.registerRuntimeApi("ts_process_cwd", {}, false);  // -> string
    bp.registerRuntimeApi("ts_process_chdir", {false}, false);  // dir
    bp.registerRuntimeApi("ts_process_get_platform", {}, false);  // -> string
    bp.registerRuntimeApi("ts_process_uptime", {}, false);  // -> double
    bp.registerRuntimeApi("ts_process_umask", {false}, false);  // mask -> old mask
    
    // Memory
    bp.registerRuntimeApi("ts_process_memory_usage", {}, true);  // -> boxed object
    bp.registerRuntimeApi("ts_process_available_memory", {}, false);  // -> number
    bp.registerRuntimeApi("ts_process_constrained_memory", {}, false);
    bp.registerRuntimeApi("ts_process_resource_usage", {}, true);  // -> boxed object
    
    // CPU
    bp.registerRuntimeApi("ts_process_cpu_usage", {true}, true);  // previousValue -> boxed
    bp.registerRuntimeApi("ts_process_hrtime", {true}, true);  // time -> boxed array
    
    // Handles/Requests
    bp.registerRuntimeApi("ts_process_get_active_handles", {}, true);  // -> array
    bp.registerRuntimeApi("ts_process_get_active_requests", {}, true);
    bp.registerRuntimeApi("ts_process_get_active_resources_info", {}, true);
    
    // Event handling
    bp.registerRuntimeApi("ts_process_on", {false, true}, true);  // event, listener
    bp.registerRuntimeApi("ts_process_once", {false, true}, true);
    bp.registerRuntimeApi("ts_process_remove_listener", {false, true}, true);
    bp.registerRuntimeApi("ts_process_remove_all_listeners", {false}, true);
    bp.registerRuntimeApi("ts_process_emit_warning", {false, false, false, false}, false);  // msg, name, code, ctor
    
    // Scheduling
    bp.registerRuntimeApi("ts_process_next_tick", {true}, false);  // callback
    bp.registerRuntimeApi("ts_process_tick_callback", {}, false);
    bp.registerRuntimeApi("ts_process_ref", {}, false);
    bp.registerRuntimeApi("ts_process_unref", {}, false);
    
    // Signals/Kill
    bp.registerRuntimeApi("ts_process_kill", {false, false}, false);  // pid, signal
    
    // Exception handling
    bp.registerRuntimeApi("ts_process_set_uncaught_exception_capture_callback", {true}, false);
    bp.registerRuntimeApi("ts_process_has_uncaught_exception_capture_callback", {}, false);  // -> bool
}

bool IRGenerator::tryGenerateProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureProcessFunctionsRegistered(boxingPolicy);
    
    auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!obj || obj->name != "process") return false;

    if (prop->name == "exit") {
        llvm::Value* code = nullptr;
        if (node->arguments.empty()) {
            code = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        } else {
            visit(node->arguments[0].get());
            code = lastValue;
        }
        
        llvm::FunctionType* exitFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee exitFn = module->getOrInsertFunction("ts_process_exit", exitFt);
        
        createCall(exitFt, exitFn.getCallee(), { code });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "cwd") {
        llvm::FunctionType* cwdFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee cwdFn = module->getOrInsertFunction("ts_process_cwd", cwdFt);
        
        lastValue = unboxValue(createCall(cwdFt, cwdFn.getCallee(), {}), node->inferredType);
        return true;
    }

    if (prop->name == "chdir") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* dir = lastValue;

        llvm::FunctionType* chdirFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee chdirFn = module->getOrInsertFunction("ts_process_chdir", chdirFt);

        createCall(chdirFt, chdirFn.getCallee(), { dir });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "nextTick") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* nextTickFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee nextTickFn = module->getOrInsertFunction("ts_process_next_tick", nextTickFt);

        createCall(nextTickFt, nextTickFn.getCallee(), { callback });
        lastValue = nullptr;
        return true;
    }

    // ========================================================================
    // Milestone 102.6: High-Resolution Time & Resource Usage
    // ========================================================================

    if (prop->name == "hrtime") {
        llvm::Value* prevTime = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            prevTime = lastValue;
        } else {
            prevTime = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* hrtimeFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee hrtimeFn = module->getOrInsertFunction("ts_process_hrtime", hrtimeFt);
        
        lastValue = createCall(hrtimeFt, hrtimeFn.getCallee(), { prevTime });
        return true;
    }

    if (prop->name == "uptime") {
        llvm::FunctionType* uptimeFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false);
        llvm::FunctionCallee uptimeFn = module->getOrInsertFunction("ts_process_uptime", uptimeFt);
        
        lastValue = createCall(uptimeFt, uptimeFn.getCallee(), {});
        return true;
    }

    if (prop->name == "memoryUsage") {
        llvm::FunctionType* memUsageFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee memUsageFn = module->getOrInsertFunction("ts_process_memory_usage", memUsageFt);
        
        lastValue = createCall(memUsageFt, memUsageFn.getCallee(), {});
        return true;
    }

    if (prop->name == "cpuUsage") {
        llvm::Value* prevUsage = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            prevUsage = lastValue;
        } else {
            prevUsage = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* cpuUsageFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee cpuUsageFn = module->getOrInsertFunction("ts_process_cpu_usage", cpuUsageFt);
        
        lastValue = createCall(cpuUsageFt, cpuUsageFn.getCallee(), { prevUsage });
        return true;
    }

    if (prop->name == "resourceUsage") {
        llvm::FunctionType* resUsageFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee resUsageFn = module->getOrInsertFunction("ts_process_resource_usage", resUsageFt);
        
        lastValue = createCall(resUsageFt, resUsageFn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Milestone 102.7: Process Control
    // ========================================================================

    if (prop->name == "kill") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* pid = lastValue;
        
        llvm::Value* signal = nullptr;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            signal = lastValue;
        } else {
            signal = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 15); // SIGTERM
        }
        
        llvm::FunctionType* killFt = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context),
            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt32Ty(*context) },
            false
        );
        llvm::FunctionCallee killFn = module->getOrInsertFunction("ts_process_kill", killFt);
        
        llvm::Value* result = createCall(killFt, killFn.getCallee(), { pid, signal });
        // Return true if result == 0
        lastValue = builder->CreateICmpEQ(result, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
        return true;
    }

    if (prop->name == "abort") {
        llvm::FunctionType* abortFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
        llvm::FunctionCallee abortFn = module->getOrInsertFunction("ts_process_abort", abortFt);
        
        createCall(abortFt, abortFn.getCallee(), {});
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "umask") {
        llvm::Value* mask = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            mask = builder->CreateTrunc(lastValue, llvm::Type::getInt32Ty(*context));
        } else {
            mask = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), -1);
        }
        
        llvm::FunctionType* umaskFt = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context),
            { llvm::Type::getInt32Ty(*context) },
            false
        );
        llvm::FunctionCallee umaskFn = module->getOrInsertFunction("ts_process_umask", umaskFt);
        
        llvm::Value* result = createCall(umaskFt, umaskFn.getCallee(), { mask });
        lastValue = builder->CreateSExt(result, llvm::Type::getInt64Ty(*context));
        return true;
    }

    if (prop->name == "emitWarning") {
        if (node->arguments.empty()) return true;
        
        visit(node->arguments[0].get());
        llvm::Value* warning = lastValue;
        
        llvm::FunctionType* emitWarningFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee emitWarningFn = module->getOrInsertFunction("ts_process_emit_warning", emitWarningFt);
        
        createCall(emitWarningFt, emitWarningFn.getCallee(), { warning });
        lastValue = nullptr;
        return true;
    }

    // ========================================================================
    // Milestone 102.8: Process Events
    // ========================================================================

    if (prop->name == "on" || prop->name == "once") {
        if (node->arguments.size() < 2) return true;
        
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;
        
        visit(node->arguments[1].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[1]->inferredType);
        
        const char* funcName = (prop->name == "on") ? "ts_process_on" : "ts_process_once";
        llvm::FunctionType* onFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee onFn = module->getOrInsertFunction(funcName, onFt);
        
        createCall(onFt, onFn.getCallee(), { event, callback });
        
        // Return process for chaining
        llvm::FunctionType* getPtrFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee getPlatformFn = module->getOrInsertFunction("ts_process_get_platform", getPtrFt);
        lastValue = createCall(getPtrFt, getPlatformFn.getCallee(), {});
        return true;
    }

    if (prop->name == "removeListener") {
        if (node->arguments.size() < 2) return true;
        
        visit(node->arguments[0].get());
        llvm::Value* event = lastValue;
        
        visit(node->arguments[1].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[1]->inferredType);
        
        llvm::FunctionType* removeFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee removeFn = module->getOrInsertFunction("ts_process_remove_listener", removeFt);
        
        createCall(removeFt, removeFn.getCallee(), { event, callback });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "removeAllListeners") {
        llvm::Value* event = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            event = lastValue;
        } else {
            event = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* removeFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee removeFn = module->getOrInsertFunction("ts_process_remove_all_listeners", removeFt);
        
        createCall(removeFt, removeFn.getCallee(), { event });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "setUncaughtExceptionCaptureCallback") {
        if (node->arguments.empty()) return true;
        
        visit(node->arguments[0].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);
        
        llvm::FunctionType* setFt = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_process_set_uncaught_exception_capture_callback", setFt);
        
        createCall(setFt, setFn.getCallee(), { callback });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "hasUncaughtExceptionCaptureCallback") {
        llvm::FunctionType* hasFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), {}, false);
        llvm::FunctionCallee hasFn = module->getOrInsertFunction("ts_process_has_uncaught_exception_capture_callback", hasFt);
        
        lastValue = createCall(hasFt, hasFn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Milestone 102.9: Event Loop Handles
    // ========================================================================

    if (prop->name == "ref") {
        llvm::FunctionType* refFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
        llvm::FunctionCallee refFn = module->getOrInsertFunction("ts_process_ref", refFt);
        
        createCall(refFt, refFn.getCallee(), {});
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "unref") {
        llvm::FunctionType* unrefFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
        llvm::FunctionCallee unrefFn = module->getOrInsertFunction("ts_process_unref", unrefFt);
        
        createCall(unrefFt, unrefFn.getCallee(), {});
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "getActiveResourcesInfo") {
        llvm::FunctionType* getInfoFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee getInfoFn = module->getOrInsertFunction("ts_process_get_active_resources_info", getInfoFt);
        
        lastValue = createCall(getInfoFt, getInfoFn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Milestone 102.12: Memory Info
    // ========================================================================

    if (prop->name == "constrainedMemory") {
        llvm::FunctionType* constrainedFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee constrainedFn = module->getOrInsertFunction("ts_process_constrained_memory", constrainedFt);
        
        lastValue = createCall(constrainedFt, constrainedFn.getCallee(), {});
        return true;
    }

    if (prop->name == "availableMemory") {
        llvm::FunctionType* availableFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee availableFn = module->getOrInsertFunction("ts_process_available_memory", availableFt);
        
        lastValue = createCall(availableFt, availableFn.getCallee(), {});
        return true;
    }

    // ========================================================================
    // Milestone 102.13: Internal/Debug APIs
    // ========================================================================

    if (prop->name == "_getActiveHandles") {
        llvm::FunctionType* getHandlesFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee getHandlesFn = module->getOrInsertFunction("ts_process_get_active_handles", getHandlesFt);
        
        lastValue = createCall(getHandlesFt, getHandlesFn.getCallee(), {});
        return true;
    }

    if (prop->name == "_getActiveRequests") {
        llvm::FunctionType* getRequestsFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee getRequestsFn = module->getOrInsertFunction("ts_process_get_active_requests", getRequestsFt);
        
        lastValue = createCall(getRequestsFt, getRequestsFn.getCallee(), {});
        return true;
    }

    if (prop->name == "_tickCallback") {
        llvm::FunctionType* tickFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
        llvm::FunctionCallee tickFn = module->getOrInsertFunction("ts_process_tick_callback", tickFt);
        
        createCall(tickFt, tickFn.getCallee(), {});
        lastValue = nullptr;
        return true;
    }

    return false;
}

} // namespace ts
