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
            "ts_array_unshift",
            "ts_array_splice",
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
        if (funcName == "ts_array_unshift") {
            return lowerArrayUnshift(inst, lowerer);
        }
        if (funcName == "ts_array_splice") {
            return lowerArraySplice(inst, lowerer);
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
        // Each argument may be a primitive (needs boxing) or a TsArray* (passed
        // directly). The runtime ts_array_concat distinguishes via magic.
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* other = lowerer.getOperandValue(inst->operands[i]);
            other = boxValue(other, lowerer, &inst->operands[i]);
            result = builder.CreateCall(ft, fn.getCallee(), { result, other });
        }

        return result;
    }

    // ts_array_push(void* arr, void* value) - returns int64_t (new length)
    // Variadic: arr.push(a, b, c) emits N sequential calls, returning the
    // length from the final call as the method result.
    llvm::Value* lowerArrayPush(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_push", ft);

        llvm::Value* result = nullptr;
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* val = lowerer.getOperandValue(inst->operands[i]);
            val = boxValue(val, lowerer, &inst->operands[i]);
            result = builder.CreateCall(ft, fn.getCallee(), { arr, val });
        }
        // Zero-arg push returns current length — produce it via a fallback call
        // path. In practice callers always pass at least one arg.
        if (!result) {
            // Fallback: just return 0 (no push happened). Shouldn't occur.
            result = llvm::ConstantInt::get(builder.getInt64Ty(), 0);
        }
        return result;
    }

    // ts_array_unshift(void* arr, void* value) - returns int64_t
    // Variadic: arr.unshift(a, b, c) prepends so that a ends up at index 0.
    // Iterate in reverse so the LAST call places arg0 at index 0.
    llvm::Value* lowerArrayUnshift(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_unshift", ft);

        llvm::Value* result = nullptr;
        for (size_t i = inst->operands.size(); i > 2; --i) {
            llvm::Value* val = lowerer.getOperandValue(inst->operands[i - 1]);
            val = boxValue(val, lowerer, &inst->operands[i - 1]);
            result = builder.CreateCall(ft, fn.getCallee(), { arr, val });
        }
        if (!result) {
            result = llvm::ConstantInt::get(builder.getInt64Ty(), 0);
        }
        return result;
    }

    // ts_array_splice(arr, start, deleteCount, items) -> ptr (deleted elements)
    // Variadic items: pack operands[4..] into a temp TsArray, pass as items.
    llvm::Value* lowerArraySplice(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);

        auto toI64 = [&](llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isIntegerTy(64)) return v;
            if (v->getType()->isDoubleTy()) return builder.CreateFPToSI(v, builder.getInt64Ty());
            if (v->getType()->isPointerTy()) {
                auto ft = llvm::FunctionType::get(
                    builder.getInt64Ty(), { builder.getPtrTy() }, false);
                auto fn = module.getOrInsertFunction("ts_value_get_int", ft);
                return builder.CreateCall(ft, fn.getCallee(), { v });
            }
            return builder.CreateSExtOrTrunc(v, builder.getInt64Ty());
        };
        llvm::Value* startV = inst->operands.size() > 2
            ? toI64(lowerer.getOperandValue(inst->operands[2]))
            : llvm::ConstantInt::get(builder.getInt64Ty(), 0);
        llvm::Value* delCnt = inst->operands.size() > 3
            ? toI64(lowerer.getOperandValue(inst->operands[3]))
            : llvm::ConstantInt::get(builder.getInt64Ty(), 0x7fffffffffffffffLL);

        // Pack items (operands[4..]) into a temp TsArray.
        llvm::Value* itemsArr = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (inst->operands.size() > 4) {
            auto createFt = llvm::FunctionType::get(builder.getPtrTy(), {}, false);
            auto createFn = module.getOrInsertFunction("ts_array_create", createFt);
            itemsArr = builder.CreateCall(createFt, createFn.getCallee(), {});
            auto pushFt = llvm::FunctionType::get(
                builder.getInt64Ty(), { builder.getPtrTy(), builder.getPtrTy() }, false);
            auto pushFn = module.getOrInsertFunction("ts_array_push", pushFt);
            for (size_t i = 4; i < inst->operands.size(); ++i) {
                llvm::Value* item = lowerer.getOperandValue(inst->operands[i]);
                item = boxValue(item, lowerer, &inst->operands[i]);
                builder.CreateCall(pushFt, pushFn.getCallee(), { itemsArr, item });
            }
        }

        auto spliceFt = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getInt64Ty(),
              builder.getInt64Ty(), builder.getPtrTy() },
            false);
        auto spliceFn = module.getOrInsertFunction("ts_array_splice", spliceFt);
        return builder.CreateCall(spliceFt, spliceFn.getCallee(),
            { arr, startV, delCnt, itemsArr });
    }

    // ts_array_find and ts_array_findLast - return ptr (TsValue*)
    // Takes (array, callback, thisArg?)
    llvm::Value* lowerArrayFind(const std::string& funcName, HIRInstruction* inst,
                                 HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* callback = lowerer.boxPrimitiveToPtr(
            lowerer.getOperandValue(inst->operands[2]));
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.boxPrimitiveToPtr(lowerer.getOperandValue(inst->operands[3]))
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
        llvm::Value* callback = lowerer.boxPrimitiveToPtr(
            lowerer.getOperandValue(inst->operands[2]));
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.boxPrimitiveToPtr(lowerer.getOperandValue(inst->operands[3]))
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
        llvm::Value* callback = lowerer.boxPrimitiveToPtr(
            lowerer.getOperandValue(inst->operands[2]));
        llvm::Value* thisArg = (inst->operands.size() > 3)
            ? lowerer.boxPrimitiveToPtr(lowerer.getOperandValue(inst->operands[3]))
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
    // Called as: .slice() (0 args), .slice(start) (1 arg), .slice(start, end) (2 args)
    llvm::Value* lowerArraySlice(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arr = lowerer.getOperandValue(inst->operands[1]);

        // Start value is optional - if not provided, use 0
        llvm::Value* startVal;
        if (inst->operands.size() > 2) {
            startVal = lowerer.getOperandValue(inst->operands[2]);
        } else {
            startVal = llvm::ConstantInt::get(builder.getInt64Ty(), 0);
        }

        // End value is optional - if not provided, use INT64_MAX (runtime clamps to length)
        llvm::Value* endVal;
        if (inst->operands.size() > 3) {
            endVal = lowerer.getOperandValue(inst->operands[3]);
        } else {
            endVal = llvm::ConstantInt::get(builder.getInt64Ty(), INT64_MAX);
        }

        // Convert f64 indices to i64 if needed.
        // ptr (boxed) values come from `undefined`/`null` literals or from
        // any-typed expressions: route through ts_value_get_int. Undefined
        // unboxes to 0 by default, but the spec wants ToLength(undefined)
        // to behave as "use length"; detect the undefined sentinel
        // (NaN-box value 10) and substitute INT64_MAX so the runtime's
        // length clamp produces the right slice.
        auto coerceToI64 = [&](llvm::Value* v, bool endIsLength) -> llvm::Value* {
            if (v->getType()->isDoubleTy()) {
                return builder.CreateFPToSI(v, builder.getInt64Ty());
            }
            if (v->getType()->isPointerTy()) {
                // Check for compile-time undefined sentinel
                if (auto* ce = llvm::dyn_cast<llvm::ConstantExpr>(v)) {
                    if (ce->getOpcode() == llvm::Instruction::IntToPtr) {
                        if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(ce->getOperand(0))) {
                            uint64_t raw = ci->getZExtValue();
                            if (raw == 10) {
                                return llvm::ConstantInt::get(
                                    builder.getInt64Ty(),
                                    endIsLength ? INT64_MAX : 0);
                            }
                        }
                    }
                }
                // Generic boxed-int unbox
                llvm::FunctionType* ftU = llvm::FunctionType::get(
                    builder.getInt64Ty(), { builder.getPtrTy() }, false);
                llvm::FunctionCallee fnU = module.getOrInsertFunction(
                    "ts_value_get_int", ftU);
                return builder.CreateCall(ftU, fnU.getCallee(), { v });
            }
            return v;
        };
        startVal = coerceToI64(startVal, /*endIsLength=*/false);
        endVal = coerceToI64(endVal, /*endIsLength=*/true);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getPtrTy(),
            { builder.getPtrTy(), builder.getInt64Ty(), builder.getInt64Ty() },
            false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_array_slice", ft);
        return builder.CreateCall(ft, fn.getCallee(), { arr, startVal, endVal });
    }

    // Helper to box a value if needed for array operations
    // Box a value for a ptr-typed runtime call. Delegates to the
    // canonical coerceArgToType so that primitive boxing AND
    // HIR-type-aware string/object boxing match every other ptr-typed
    // runtime call site. Falls back to boxPrimitiveToPtr when the
    // operand is unavailable (we only have the LLVM value, no HIR
    // operand for it).
    llvm::Value* boxValue(llvm::Value* val, HIRToLLVM& lowerer,
                          const HIROperand* operand = nullptr) {
        if (operand) {
            return lowerer.coerceArgToType(
                val, lowerer.builder().getPtrTy(), *operand,
                /*calleeParamType=*/nullptr);
        }
        return lowerer.boxPrimitiveToPtr(val);
    }
};

} // namespace ts::hir

// Factory function to create ArrayHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createArrayHandler() {
        return std::make_unique<ArrayHandler>();
    }
}
