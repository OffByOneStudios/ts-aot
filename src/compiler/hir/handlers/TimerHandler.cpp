#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>

namespace ts::hir {

//==============================================================================
// TimerHandler - Handles timer builtin functions
//
// This handler extracts the timer function lowering from the monolithic
// lowerCall function. It handles:
// - setTimeout* variants - Delay callback execution
// - setInterval* variants - Repeat callback execution
// - setImmediate* variants - Next tick execution
// - clearTimeout/clearInterval/clearImmediate - Cancel timers
//
// Note: These functions use prefix matching since they have mangled names
// like setTimeout_any_dbl, setInterval_func_int, etc.
//==============================================================================
class TimerHandler : public BuiltinHandler {
public:
    const char* name() const override { return "TimerHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        // Use prefix matching for timer functions (they have mangled suffixes)
        return isSetTimeout(funcName) ||
               isSetInterval(funcName) ||
               isSetImmediate(funcName) ||
               isClearTimeout(funcName) ||
               isClearInterval(funcName) ||
               isClearImmediate(funcName);
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        if (isSetTimeout(funcName)) {
            return lowerSetTimeout(inst, lowerer);
        }
        if (isSetInterval(funcName)) {
            return lowerSetInterval(inst, lowerer);
        }
        if (isSetImmediate(funcName)) {
            return lowerSetImmediate(inst, lowerer);
        }
        if (isClearTimeout(funcName) || isClearInterval(funcName) || isClearImmediate(funcName)) {
            return lowerClearTimer(inst, lowerer);
        }
        return nullptr;
    }

private:
    //==========================================================================
    // Prefix matchers - timer functions have mangled names
    //==========================================================================

    static bool isSetTimeout(const std::string& funcName) {
        return funcName.rfind("setTimeout", 0) == 0 &&
               funcName.rfind("clearTimeout", 0) != 0;
    }

    static bool isSetInterval(const std::string& funcName) {
        return funcName.rfind("setInterval", 0) == 0 &&
               funcName.rfind("clearInterval", 0) != 0;
    }

    static bool isSetImmediate(const std::string& funcName) {
        return funcName.rfind("setImmediate", 0) == 0 &&
               funcName.rfind("clearImmediate", 0) != 0;
    }

    static bool isClearTimeout(const std::string& funcName) {
        return funcName.rfind("clearTimeout", 0) == 0;
    }

    static bool isClearInterval(const std::string& funcName) {
        return funcName.rfind("clearInterval", 0) == 0;
    }

    static bool isClearImmediate(const std::string& funcName) {
        return funcName.rfind("clearImmediate", 0) == 0;
    }

    //==========================================================================
    // setTimeout(callback, delay) -> TsValue* (timer handle)
    // Runtime function: ts_set_timeout(TsValue* callback, int64_t delay)
    //==========================================================================
    llvm::Value* lowerSetTimeout(HIRInstruction* inst, HIRToLLVM& lowerer) {
        return lowerSetTimeoutOrInterval(inst, lowerer, "ts_set_timeout");
    }

    //==========================================================================
    // setInterval(callback, delay) -> TsValue* (timer handle)
    // Runtime function: ts_set_interval(TsValue* callback, int64_t delay)
    //==========================================================================
    llvm::Value* lowerSetInterval(HIRInstruction* inst, HIRToLLVM& lowerer) {
        return lowerSetTimeoutOrInterval(inst, lowerer, "ts_set_interval");
    }

    //==========================================================================
    // Common lowering for setTimeout and setInterval
    //==========================================================================
    llvm::Value* lowerSetTimeoutOrInterval(HIRInstruction* inst, HIRToLLVM& lowerer,
                                            const char* runtimeFunc) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* callback = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* delay = lowerer.getOperandValue(inst->operands[2]);

        // Box callback if needed
        callback = boxCallback(callback, lowerer);

        // Convert delay to i64 if needed
        if (delay->getType()->isDoubleTy()) {
            delay = builder.CreateFPToSI(delay, builder.getInt64Ty(), "delay_to_i64");
        } else if (delay->getType()->isPointerTy()) {
            // Unbox ptr (NaN-boxed TsValue*) to i64
            auto unboxFt = llvm::FunctionType::get(builder.getInt64Ty(), { builder.getPtrTy() }, false);
            auto unboxFn = module.getOrInsertFunction("ts_value_get_int", unboxFt);
            delay = builder.CreateCall(unboxFt, unboxFn.getCallee(), { delay }, "unbox_delay");
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy(), builder.getInt64Ty() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(runtimeFunc, ft);
        return builder.CreateCall(ft, fn.getCallee(), { callback, delay });
    }

    //==========================================================================
    // setImmediate(callback) -> TsValue* (timer handle)
    // Runtime function: ts_set_immediate(TsValue* callback)
    //==========================================================================
    llvm::Value* lowerSetImmediate(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* callback = lowerer.getOperandValue(inst->operands[1]);

        // Box callback if needed
        callback = boxCallback(callback, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_set_immediate", ft);
        return builder.CreateCall(ft, fn.getCallee(), { callback });
    }

    //==========================================================================
    // clearTimeout/clearInterval/clearImmediate(timerId) -> void
    // Runtime function: ts_clear_timer(TsValue* timerId)
    //==========================================================================
    llvm::Value* lowerClearTimer(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* timerId = lowerer.getOperandValue(inst->operands[1]);

        // Box timerId if needed - it should be a TsValue* (timer handle)
        if (timerId->getType()->isDoubleTy()) {
            // Convert double to i64 first, then box
            timerId = builder.CreateFPToSI(timerId, builder.getInt64Ty(), "timer_to_i64");
            auto boxFn = lowerer.getTsValueMakeInt();
            timerId = builder.CreateCall(boxFn, { timerId });
        } else if (timerId->getType()->isIntegerTy(64)) {
            auto boxFn = lowerer.getTsValueMakeInt();
            timerId = builder.CreateCall(boxFn, { timerId });
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getVoidTy(), { builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_clear_timer", ft);
        builder.CreateCall(ft, fn.getCallee(), { timerId });

        // Return null for void functions
        return llvm::ConstantPointerNull::get(builder.getPtrTy());
    }

    //==========================================================================
    // Helper: Box callback if needed
    //==========================================================================
    llvm::Value* boxCallback(llvm::Value* callback, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();

        if (!callback->getType()->isPointerTy()) {
            if (callback->getType()->isIntegerTy(64)) {
                auto boxFn = lowerer.getTsValueMakeInt();
                callback = builder.CreateCall(boxFn, { callback });
            }
        }
        return callback;
    }
};

} // namespace ts::hir

// Factory function to create TimerHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createTimerHandler() {
        return std::make_unique<TimerHandler>();
    }
}
