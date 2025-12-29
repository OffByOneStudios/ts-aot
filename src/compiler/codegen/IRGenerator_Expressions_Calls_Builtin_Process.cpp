#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
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

    return false;
}

} // namespace ts
