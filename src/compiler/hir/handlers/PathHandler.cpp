#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// PathHandler - Handles path module builtin functions
//
// This handler extracts the path function lowering from the monolithic
// lowerCall function. It handles:
// - ts_path_basename - Extract filename (with optional extension removal)
// - ts_path_dirname - Extract directory
// - ts_path_extname - Extract extension
// - ts_path_join - Join path segments
// - ts_path_normalize - Normalize path
// - ts_path_resolve - Resolve to absolute path
// - ts_path_relative - Compute relative path
// - ts_path_isAbsolute - Check if path is absolute
//==============================================================================
class PathHandler : public BuiltinHandler {
public:
    const char* name() const override { return "PathHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> pathFuncs = {
            "ts_path_basename",
            "ts_path_dirname",
            "ts_path_extname",
            "ts_path_join",
            "ts_path_normalize",
            "ts_path_resolve",
            "ts_path_relative",
            "ts_path_isAbsolute"
        };
        return pathFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        if (funcName == "ts_path_basename") {
            return lowerBasename(inst, lowerer);
        }
        if (funcName == "ts_path_dirname") {
            return lowerSingleArgPath(funcName, inst, lowerer);
        }
        if (funcName == "ts_path_extname") {
            return lowerSingleArgPath(funcName, inst, lowerer);
        }
        if (funcName == "ts_path_join") {
            return lowerJoin(inst, lowerer);
        }
        if (funcName == "ts_path_normalize") {
            return lowerSingleArgPath(funcName, inst, lowerer);
        }
        if (funcName == "ts_path_resolve") {
            return lowerSingleArgPath(funcName, inst, lowerer);
        }
        if (funcName == "ts_path_relative") {
            return lowerRelative(inst, lowerer);
        }
        if (funcName == "ts_path_isAbsolute") {
            return lowerIsAbsolute(inst, lowerer);
        }
        return nullptr;
    }

private:
    //==========================================================================
    // ts_path_basename(path, ext?) -> TsString*
    // ext is optional - pass null if not provided
    //==========================================================================
    llvm::Value* lowerBasename(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* path = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* ext = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            ext = lowerer.getOperandValue(inst->operands[2]);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_path_basename", ft);
        return builder.CreateCall(ft, fn.getCallee(), { path, ext });
    }

    //==========================================================================
    // Single-arg path functions that return TsString*
    // dirname, extname, normalize, resolve
    //==========================================================================
    llvm::Value* lowerSingleArgPath(const std::string& funcName, HIRInstruction* inst,
                                     HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* path = lowerer.getOperandValue(inst->operands[1]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { path });
    }

    //==========================================================================
    // ts_path_join(path1, path2?) -> TsString*
    // path2 is optional
    //==========================================================================
    llvm::Value* lowerJoin(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* path1 = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* path2 = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            path2 = lowerer.getOperandValue(inst->operands[2]);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_path_join", ft);
        return builder.CreateCall(ft, fn.getCallee(), { path1, path2 });
    }

    //==========================================================================
    // ts_path_relative(from, to?) -> TsString*
    // to is optional
    //==========================================================================
    llvm::Value* lowerRelative(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* from = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* to = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 2) {
            to = lowerer.getOperandValue(inst->operands[2]);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_path_relative", ft);
        return builder.CreateCall(ft, fn.getCallee(), { from, to });
    }

    //==========================================================================
    // ts_path_isAbsolute(path) -> bool
    // Runtime returns int32, we convert to bool
    //==========================================================================
    llvm::Value* lowerIsAbsolute(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* path = lowerer.getOperandValue(inst->operands[1]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt32Ty(),
            { builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_path_is_absolute", ft);
        llvm::Value* result32 = builder.CreateCall(ft, fn.getCallee(), { path });

        // Convert to bool
        return builder.CreateICmpNE(result32,
            llvm::ConstantInt::get(builder.getInt32Ty(), 0));
    }
};

} // namespace ts::hir

// Factory function to create PathHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createPathHandler() {
        return std::make_unique<PathHandler>();
    }
}
