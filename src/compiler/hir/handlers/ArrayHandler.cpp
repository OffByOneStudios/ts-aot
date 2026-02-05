#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// ArrayHandler - Handles Array.* builtin functions
//
// This handler extracts the array function lowering from the monolithic
// lowerCall function. It handles:
// - ts_array_create: Create empty array
// - ts_array_concat: Concatenate arrays (single or chained)
// - ts_array_push: Push value onto array (with boxing)
// - ts_array_find/findLast: Find element by predicate
// - ts_array_findIndex/findLastIndex: Find index by predicate
// - ts_array_some/every: Test all/any elements
// - ts_array_slice: Extract sub-array
//==============================================================================
class ArrayHandler : public BuiltinHandler {
public:
    const char* name() const override { return "ArrayHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        static const std::unordered_set<std::string> arrayFuncs = {
            "ts_array_create",
            "ts_array_concat",
            "ts_array_push",
            "ts_array_find",
            "ts_array_findLast",
            "ts_array_findIndex",
            "ts_array_findLastIndex",
            "ts_array_some",
            "ts_array_every",
            "ts_array_slice"
        };
        return arrayFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        if (funcName == "ts_array_create") {
            return lowerArrayCreate(inst, lowerer);
        }
        if (funcName == "ts_array_concat") {
            return lowerArrayConcat(inst, lowerer);
        }
        if (funcName == "ts_array_push") {
            return lowerArrayPush(inst, lowerer);
        }
        if (funcName == "ts_array_find" || funcName == "ts_array_findLast") {
            return lowerArrayFind(funcName, inst, lowerer);
        }
        if (funcName == "ts_array_findIndex" || funcName == "ts_array_findLastIndex") {
            return lowerArrayFindIndex(funcName, inst, lowerer);
        }
        if (funcName == "ts_array_some" || funcName == "ts_array_every") {
            return lowerArraySomeEvery(funcName, inst, lowerer);
        }
        if (funcName == "ts_array_slice") {
            return lowerArraySlice(inst, lowerer);
        }
        return nullptr;
    }

private:
    // ts_array_create() - returns ptr, no args
    llvm::Value* lowerArrayCreate(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::FunctionType* ft = llvm::FunctionType::get(builder.getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_create", ft);
        return builder.CreateCall(ft, fn.getCallee(), {});
    }

    // ts_array_concat - chains concat calls for multiple arguments
    // JavaScript concat() can take multiple arguments: arr.concat(a, b, c)
    // We chain calls: concat(concat(concat(arr, a), b), c)
    llvm::Value* lowerArrayConcat(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_concat", ft);

        // Start with the source array (operand[1])
        llvm::Value* result = lowerer.getOperandValue(inst->operands[1]);

        // Chain concat calls for each argument (operands[2], operands[3], ...)
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* other = lowerer.getOperandValue(inst->operands[i]);
            result = builder.CreateCall(ft, fn.getCallee(), { result, other });
        }

        return result;
    }

    // ts_array_push(void* arr, void* value) - returns int64_t (new length)
    // Boxes the value before pushing if it's a primitive
    llvm::Value* lowerArrayPush(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* val = lowerer.getOperandValue(inst->operands[2]);

        // Box the value if needed
        val = boxValue(val, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_push", ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, val });
    }

    // ts_array_find and ts_array_findLast - return ptr (TsValue*)
    // Takes (array, callback, thisArg?)
    llvm::Value* lowerArrayFind(const std::string& funcName, HIRInstruction* inst,
                                 HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* callback = lowerer.getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder.getPtrTy());

        // Pass the raw closure pointer - runtime will check ts_is_closure and handle it
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });
    }

    // ts_array_findIndex and ts_array_findLastIndex - return int64_t directly
    llvm::Value* lowerArrayFindIndex(const std::string& funcName, HIRInstruction* inst,
                                      HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* callback = lowerer.getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder.getPtrTy());

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(),
            { builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });
    }

    // ts_array_some and ts_array_every - return bool (i1)
    llvm::Value* lowerArraySomeEvery(const std::string& funcName, HIRInstruction* inst,
                                      HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* callback = lowerer.getOperandValue(inst->operands[2]);
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.getOperandValue(inst->operands[3])
            : llvm::ConstantPointerNull::get(builder.getPtrTy());

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt1Ty(),
            { builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, callback, thisArg });
    }

    // ts_array_slice - takes (ptr, i64, i64), returns ptr
    // HIR may pass f64 for the indices but runtime expects i64
    llvm::Value* lowerArraySlice(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* startVal = lowerer.getOperandValue(inst->operands[2]);
        llvm::Value* endVal = lowerer.getOperandValue(inst->operands[3]);

        // Convert f64 indices to i64 if needed
        if (startVal->getType()->isDoubleTy()) {
            startVal = builder.CreateFPToSI(startVal, builder.getInt64Ty());
        }
        if (endVal->getType()->isDoubleTy()) {
            endVal = builder.CreateFPToSI(endVal, builder.getInt64Ty());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getInt64Ty(), builder.getInt64Ty() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_slice", ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, startVal, endVal });
    }

    // Helper to box a value if needed for array operations
    llvm::Value* boxValue(llvm::Value* val, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();

        if (!val->getType()->isPointerTy()) {
            if (val->getType()->isIntegerTy(64)) {
                auto boxFn = lowerer.getTsValueMakeInt();
                return builder.CreateCall(boxFn, {val});
            } else if (val->getType()->isDoubleTy()) {
                auto boxFn = lowerer.getTsValueMakeDouble();
                return builder.CreateCall(boxFn, {val});
            } else if (val->getType()->isIntegerTy(1)) {
                auto boxFn = lowerer.getTsValueMakeBool();
                llvm::Value* extended = builder.CreateZExt(val, builder.getInt32Ty());
                return builder.CreateCall(boxFn, {extended});
            }
        }
        return val; // Already a pointer, return as-is
    }
};

} // namespace ts::hir

// Factory function to create ArrayHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createArrayHandler() {
        return std::make_unique<ArrayHandler>();
    }
}
