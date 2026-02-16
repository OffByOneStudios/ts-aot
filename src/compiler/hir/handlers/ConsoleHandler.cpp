#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// ConsoleHandler - Handles console.* builtin functions
//
// Single-arg calls use type-specific dispatch for optimal codegen:
// - console.log(42) -> ts_console_log_int(42)
// - console.log("hi") -> ts_console_log_string("hi")
//
// Multi-arg calls use ts_console_log_args which handles util.format-style
// %s/%d/%f substitution (matching Node.js console.log behavior):
// - console.log('hello %s', 'world') -> "hello world"
// - console.log(1, 2, 3) -> "1 2 3"
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

        size_t argCount = inst->operands.size() - 1; // exclude callee operand

        // For single-argument calls, use type-specific dispatch (existing fast path)
        if (argCount == 1) {
            llvm::Value* arg = lowerer.getOperandValue(inst->operands[1]);
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

            return llvm::ConstantPointerNull::get(builder.getPtrTy());
        }

        // For multi-argument calls, NaN-box all args into a stack array and call
        // ts_console_log_args / ts_console_error_args which handles format substitution.

        // Allocate stack array of ptr for the args
        llvm::Type* ptrTy = builder.getPtrTy();
        llvm::ArrayType* arrTy = llvm::ArrayType::get(ptrTy, argCount);
        llvm::Value* argsArray = builder.CreateAlloca(arrTy, nullptr, "console_args");

        for (size_t i = 0; i < argCount; ++i) {
            llvm::Value* arg = lowerer.getOperandValue(inst->operands[i + 1]);
            llvm::Type* argType = arg->getType();

            // NaN-box the argument so the runtime gets a uniform ptr array
            llvm::Value* boxed = arg;
            if (argType->isIntegerTy(64)) {
                llvm::FunctionType* bft = llvm::FunctionType::get(ptrTy, { builder.getInt64Ty() }, false);
                llvm::FunctionCallee bfn = module.getOrInsertFunction("ts_value_make_int", bft);
                boxed = builder.CreateCall(bft, bfn.getCallee(), { arg });
            } else if (argType->isDoubleTy()) {
                llvm::FunctionType* bft = llvm::FunctionType::get(ptrTy, { builder.getDoubleTy() }, false);
                llvm::FunctionCallee bfn = module.getOrInsertFunction("ts_value_make_double", bft);
                boxed = builder.CreateCall(bft, bfn.getCallee(), { arg });
            } else if (argType->isIntegerTy(1)) {
                llvm::FunctionType* bft = llvm::FunctionType::get(ptrTy, { builder.getInt1Ty() }, false);
                llvm::FunctionCallee bfn = module.getOrInsertFunction("ts_value_make_bool", bft);
                boxed = builder.CreateCall(bft, bfn.getCallee(), { arg });
            } else if (argType->isIntegerTy(32)) {
                llvm::Value* ext = builder.CreateSExt(arg, builder.getInt64Ty());
                llvm::FunctionType* bft = llvm::FunctionType::get(ptrTy, { builder.getInt64Ty() }, false);
                llvm::FunctionCallee bfn = module.getOrInsertFunction("ts_value_make_int", bft);
                boxed = builder.CreateCall(bft, bfn.getCallee(), { ext });
            }
            // ptr types (strings, objects, etc.) are already NaN-boxed

            llvm::Value* slot = builder.CreateInBoundsGEP(arrTy, argsArray,
                { builder.getInt32(0), builder.getInt32((uint32_t)i) }, "arg_slot");
            builder.CreateStore(boxed, slot);
        }

        // Cast array to ptr for the function call
        llvm::Value* argsPtr = builder.CreateInBoundsGEP(arrTy, argsArray,
            { builder.getInt32(0), builder.getInt32(0) }, "args_ptr");

        std::string runtimeFunc = isError ? "ts_console_error_args" : "ts_console_log_args";
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getVoidTy(), { ptrTy, builder.getInt32Ty() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(runtimeFunc, ft);
        builder.CreateCall(ft, fn.getCallee(),
            { argsPtr, builder.getInt32((uint32_t)argCount) });

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
