#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// BigIntHandler - Handles BigInt builtin functions
//
// This handler extracts the BigInt function lowering from the monolithic
// lowerCall function. It handles:
// - ts_bigint_create_str - Create from string representation
// - ts_bigint_add, ts_bigint_sub, ts_bigint_mul, ts_bigint_div, ts_bigint_mod - Arithmetic
// - ts_bigint_lt, ts_bigint_le, ts_bigint_gt, ts_bigint_ge, ts_bigint_eq, ts_bigint_ne - Comparison
//==============================================================================
class BigIntHandler : public BuiltinHandler {
public:
    const char* name() const override { return "BigIntHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> bigintFuncs = {
            "ts_bigint_create_str",
            "ts_bigint_add", "ts_bigint_sub", "ts_bigint_mul", "ts_bigint_div", "ts_bigint_mod",
            "ts_bigint_lt", "ts_bigint_le", "ts_bigint_gt", "ts_bigint_ge",
            "ts_bigint_eq", "ts_bigint_ne"
        };
        return bigintFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        if (funcName == "ts_bigint_create_str") {
            return lowerBigIntCreate(inst, lowerer);
        }
        if (isArithmeticOp(funcName)) {
            return lowerBigIntArithmetic(funcName, inst, lowerer);
        }
        if (isComparisonOp(funcName)) {
            return lowerBigIntComparison(funcName, inst, lowerer);
        }
        return nullptr;
    }

private:
    //==========================================================================
    // Operation categorization
    //==========================================================================

    static bool isArithmeticOp(const std::string& funcName) {
        return funcName == "ts_bigint_add" || funcName == "ts_bigint_sub" ||
               funcName == "ts_bigint_mul" || funcName == "ts_bigint_div" ||
               funcName == "ts_bigint_mod";
    }

    static bool isComparisonOp(const std::string& funcName) {
        return funcName == "ts_bigint_lt" || funcName == "ts_bigint_le" ||
               funcName == "ts_bigint_gt" || funcName == "ts_bigint_ge" ||
               funcName == "ts_bigint_eq" || funcName == "ts_bigint_ne";
    }

    //==========================================================================
    // ts_bigint_create_str(strPtr, radix) -> ptr
    // Creates a BigInt from a string representation
    //==========================================================================
    llvm::Value* lowerBigIntCreate(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* strArg = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* radixArg = lowerer.getOperandValue(inst->operands[2]);

        // Convert radix from i64 to i32 if needed
        if (radixArg->getType()->isIntegerTy(64)) {
            radixArg = builder.CreateTrunc(radixArg, builder.getInt32Ty());
        } else if (radixArg->getType()->isDoubleTy()) {
            // Convert f64 to i32
            radixArg = builder.CreateFPToSI(radixArg, builder.getInt32Ty());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy(), builder.getInt32Ty() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_bigint_create_str", ft);
        return builder.CreateCall(ft, fn.getCallee(), { strArg, radixArg });
    }

    //==========================================================================
    // BigInt arithmetic operations: (ptr, ptr) -> ptr
    // add, sub, mul, div, mod
    //==========================================================================
    llvm::Value* lowerBigIntArithmetic(const std::string& funcName, HIRInstruction* inst,
                                        HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* a = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* b = lowerer.getOperandValue(inst->operands[2]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { a, b });
    }

    //==========================================================================
    // BigInt comparison operations: (ptr, ptr) -> bool (i1)
    // lt, le, gt, ge, eq, ne
    //==========================================================================
    llvm::Value* lowerBigIntComparison(const std::string& funcName, HIRInstruction* inst,
                                        HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* a = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* b = lowerer.getOperandValue(inst->operands[2]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt1Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { a, b });
    }
};

} // namespace ts::hir

// Factory function to create BigIntHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createBigIntHandler() {
        return std::make_unique<BigIntHandler>();
    }
}
