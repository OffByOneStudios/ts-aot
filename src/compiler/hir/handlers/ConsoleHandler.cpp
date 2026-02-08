#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// ConsoleHandler - Handles console.* builtin functions
//
// This handler extracts the console function lowering from the monolithic
// lowerCall function. Console functions require type-specific dispatch:
// - console.log(42) -> ts_console_log_int(42)
// - console.log("hi") -> ts_console_log_string("hi")
// - console.log(3.14) -> ts_console_log_double(3.14)
//==============================================================================
class ConsoleHandler : public BuiltinHandler {
public:
    const char* name() const override { return "ConsoleHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> consoleFuncs = {
            "ts_console_log",
            "ts_console_error",
            "ts_console_warn",
            "ts_console_info",
            "ts_console_debug"
        };
        return consoleFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Determine if this is an error/warn stream (goes to stderr)
        bool isError = (funcName == "ts_console_error" || funcName == "ts_console_warn");

        // Handle each argument with type-specific dispatch
        for (size_t i = 1; i < inst->operands.size(); ++i) {
            llvm::Value* arg = lowerer.getOperandValue(inst->operands[i]);
            llvm::Type* argType = arg->getType();

            std::string actualFuncName;
            llvm::Type* paramType = builder.getPtrTy();

            if (argType->isIntegerTy(64)) {
                actualFuncName = isError ? "ts_console_error_int" : "ts_console_log_int";
                paramType = builder.getInt64Ty();
            } else if (argType->isDoubleTy()) {
                actualFuncName = isError ? "ts_console_error_double" : "ts_console_log_double";
                paramType = builder.getDoubleTy();
            } else if (argType->isIntegerTy(1)) {
                actualFuncName = isError ? "ts_console_error_bool" : "ts_console_log_bool";
                paramType = builder.getInt1Ty();
            } else if (argType->isIntegerTy(32)) {
                // i32 from extension getters (e.g., cluster.isMaster) - treat as bool
                actualFuncName = isError ? "ts_console_error_bool" : "ts_console_log_bool";
                paramType = builder.getInt1Ty();
                arg = builder.CreateTrunc(arg, builder.getInt1Ty(), "i32_to_bool");
            } else if (argType->isPointerTy()) {
                actualFuncName = isError ? "ts_console_error_value" : "ts_console_log_value";
            } else {
                SPDLOG_WARN("ConsoleHandler: Unknown argument type for {}", funcName);
                actualFuncName = isError ? "ts_console_error_value" : "ts_console_log_value";
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                builder.getVoidTy(), { paramType }, false);
            llvm::FunctionCallee fn = module.getOrInsertFunction(actualFuncName, ft);
            builder.CreateCall(ft, fn.getCallee(), { arg });
        }

        // console functions return undefined (null pointer)
        return llvm::ConstantPointerNull::get(builder.getPtrTy());
    }
};

} // namespace ts::hir

// Factory function to create ConsoleHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createConsoleHandler() {
        return std::make_unique<ConsoleHandler>();
    }
}
