#include "BuiltinHandler.h"
#include "../HIRToLLVM.h"
#include <llvm/IR/Intrinsics.h>
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace ts::hir {

//==============================================================================
// MathHandler - Handles Math.* and Number.* builtin functions
//
// This handler extracts the Math and Number function lowering from the
// monolithic lowerCall function, making the codebase more maintainable.
//==============================================================================
class MathHandler : public BuiltinHandler {
public:
    const char* name() const override { return "MathHandler"; }

    bool canHandle(const std::string& funcName, HIRInstruction* inst) const override {
        // Single-arg double functions
        static const std::unordered_set<std::string> singleArgDoubleFuncs = {
            "ts_math_floor", "ts_math_ceil", "ts_math_round", "ts_math_trunc",
            "ts_math_abs", "ts_math_sqrt", "ts_math_sin", "ts_math_cos",
            "ts_math_tan", "ts_math_log", "ts_math_exp", "ts_math_sign",
            "ts_math_fround", "ts_math_cbrt", "ts_math_sinh", "ts_math_cosh",
            "ts_math_tanh", "ts_math_asinh", "ts_math_acosh", "ts_math_atanh",
            "ts_math_asin", "ts_math_acos", "ts_math_atan", "ts_math_expm1",
            "ts_math_log10", "ts_math_log2", "ts_math_log1p"
        };

        // Two-arg double functions
        static const std::unordered_set<std::string> twoArgDoubleFuncs = {
            "ts_math_pow", "ts_math_atan2", "ts_math_hypot",
            "ts_math_min", "ts_math_max"
        };

        // Number.* functions
        static const std::unordered_set<std::string> numberFuncs = {
            "ts_number_isFinite", "ts_number_isNaN",
            "ts_number_isInteger", "ts_number_isSafeInteger"
        };

        return singleArgDoubleFuncs.count(funcName) > 0 ||
               twoArgDoubleFuncs.count(funcName) > 0 ||
               funcName == "ts_math_random" ||
               funcName == "ts_math_clz32" ||
               funcName == "ts_math_imul" ||
               numberFuncs.count(funcName) > 0;
    }

    llvm::Value* lower(const std::string& funcName, HIRInstruction* inst,
                       HIRToLLVM& lowerer) override {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        // Single-arg double functions
        if (isSingleArgDoubleFunc(funcName)) {
            return lowerSingleArgDouble(funcName, inst, lowerer);
        }

        // Two-arg double functions (pow, atan2, hypot, min, max)
        if (funcName == "ts_math_pow" || funcName == "ts_math_atan2" ||
            funcName == "ts_math_hypot" || funcName == "ts_math_min" ||
            funcName == "ts_math_max") {
            return lowerTwoArgDouble(funcName, inst, lowerer);
        }

        // Math.random - no args
        if (funcName == "ts_math_random") {
            return lowerMathRandom(inst, lowerer);
        }

        // Math.clz32 - i64 arg, returns i64
        if (funcName == "ts_math_clz32") {
            return lowerMathClz32(inst, lowerer);
        }

        // Math.imul - two i64 args, returns i64
        if (funcName == "ts_math_imul") {
            return lowerMathImul(inst, lowerer);
        }

        // Number.isFinite
        if (funcName == "ts_number_isFinite") {
            return lowerNumberIsFinite(inst, lowerer);
        }

        // Number.isNaN
        if (funcName == "ts_number_isNaN") {
            return lowerNumberIsNaN(inst, lowerer);
        }

        // Number.isInteger
        if (funcName == "ts_number_isInteger") {
            return lowerNumberIsInteger(inst, lowerer);
        }

        // Number.isSafeInteger
        if (funcName == "ts_number_isSafeInteger") {
            return lowerNumberIsSafeInteger(inst, lowerer);
        }

        return nullptr;
    }

private:
    bool isSingleArgDoubleFunc(const std::string& funcName) const {
        static const std::unordered_set<std::string> funcs = {
            "ts_math_floor", "ts_math_ceil", "ts_math_round", "ts_math_trunc",
            "ts_math_abs", "ts_math_sqrt", "ts_math_sin", "ts_math_cos",
            "ts_math_tan", "ts_math_log", "ts_math_exp", "ts_math_sign",
            "ts_math_fround", "ts_math_cbrt", "ts_math_sinh", "ts_math_cosh",
            "ts_math_tanh", "ts_math_asinh", "ts_math_acosh", "ts_math_atanh",
            "ts_math_asin", "ts_math_acos", "ts_math_atan", "ts_math_expm1",
            "ts_math_log10", "ts_math_log2", "ts_math_log1p"
        };
        return funcs.count(funcName) > 0;
    }

    // Convert value to double if needed
    llvm::Value* ensureDouble(llvm::Value* val, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        if (val->getType()->isIntegerTy()) {
            return builder.CreateSIToFP(val, builder.getDoubleTy(), "i64_to_f64");
        }
        return val;
    }

    // Convert value to i64 if needed
    llvm::Value* ensureI64(llvm::Value* val, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        if (val->getType()->isDoubleTy()) {
            return builder.CreateFPToSI(val, builder.getInt64Ty(), "f64_to_i64");
        }
        return val;
    }

    llvm::Value* lowerSingleArgDouble(const std::string& funcName,
                                       HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arg = lowerer.getOperandValue(inst->operands[1]);
        arg = ensureDouble(arg, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getDoubleTy(), { builder.getDoubleTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { arg });
    }

    llvm::Value* lowerTwoArgDouble(const std::string& funcName,
                                    HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* a = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* b = lowerer.getOperandValue(inst->operands[2]);
        a = ensureDouble(a, lowerer);
        b = ensureDouble(b, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getDoubleTy(), { builder.getDoubleTy(), builder.getDoubleTy() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction(funcName, ft);
        return builder.CreateCall(ft, fn.getCallee(), { a, b });
    }

    llvm::Value* lowerMathRandom(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getDoubleTy(), {}, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_math_random", ft);
        return builder.CreateCall(ft, fn.getCallee(), {});
    }

    llvm::Value* lowerMathClz32(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* arg = lowerer.getOperandValue(inst->operands[1]);
        arg = ensureI64(arg, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(), { builder.getInt64Ty() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_math_clz32", ft);
        return builder.CreateCall(ft, fn.getCallee(), { arg });
    }

    llvm::Value* lowerMathImul(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* a = lowerer.getOperandValue(inst->operands[1]);
        llvm::Value* b = lowerer.getOperandValue(inst->operands[2]);
        a = ensureI64(a, lowerer);
        b = ensureI64(b, lowerer);

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder.getInt64Ty(), { builder.getInt64Ty(), builder.getInt64Ty() }, false);
        llvm::FunctionCallee fn = module.getOrInsertFunction("ts_math_imul", ft);
        return builder.CreateCall(ft, fn.getCallee(), { a, b });
    }

    llvm::Value* lowerNumberIsFinite(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();

        llvm::Value* val = lowerer.getOperandValue(inst->operands[1]);

        // Handle different input types
        if (val->getType()->isIntegerTy(64)) {
            val = builder.CreateSIToFP(val, builder.getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            val = builder.CreateUIToFP(val, builder.getDoubleTy(), "bool_to_dbl");
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false (strict Number.isFinite doesn't do type coercion)
            return builder.getInt1(false);
        }

        // A number is finite if it's not NaN and not infinite
        llvm::Value* isNotNaN = builder.CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), true);
        llvm::Value* notPosInf = builder.CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder.CreateFCmpONE(val, negInf);
        return builder.CreateAnd(isNotNaN, builder.CreateAnd(notPosInf, notNegInf));
    }

    llvm::Value* lowerNumberIsNaN(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();

        llvm::Value* val = lowerer.getOperandValue(inst->operands[1]);

        // Handle different input types
        if (val->getType()->isIntegerTy(64)) {
            val = builder.CreateSIToFP(val, builder.getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            val = builder.CreateUIToFP(val, builder.getDoubleTy(), "bool_to_dbl");
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false (strict Number.isNaN doesn't do type coercion)
            return builder.getInt1(false);
        }

        // NaN is the only value that is not equal to itself
        return builder.CreateFCmpUNO(val, val);
    }

    llvm::Value* lowerNumberIsInteger(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* val = lowerer.getOperandValue(inst->operands[1]);

        // Handle different input types
        if (val->getType()->isIntegerTy(64)) {
            // Integers are always integers
            return builder.getInt1(true);
        } else if (val->getType()->isIntegerTy(1)) {
            // Booleans converted to 0/1 are integers
            return builder.getInt1(true);
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false
            return builder.getInt1(false);
        }

        // For doubles: isInteger = isFinite && floor(value) == value
        llvm::Value* isNotNaN = builder.CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), true);
        llvm::Value* notPosInf = builder.CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder.CreateFCmpONE(val, negInf);
        llvm::Value* isFinite = builder.CreateAnd(isNotNaN, builder.CreateAnd(notPosInf, notNegInf));

        // Check floor(val) == val using intrinsic
        llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::floor, {builder.getDoubleTy()});
        llvm::Value* floorVal = builder.CreateCall(floorFn, {val});
        llvm::Value* isWholeNumber = builder.CreateFCmpOEQ(val, floorVal);

        return builder.CreateAnd(isFinite, isWholeNumber);
    }

    llvm::Value* lowerNumberIsSafeInteger(HIRInstruction* inst, HIRToLLVM& lowerer) {
        auto& builder = lowerer.builder();
        auto& module = lowerer.module();

        llvm::Value* val = lowerer.getOperandValue(inst->operands[1]);

        // Handle different input types
        if (val->getType()->isIntegerTy(64)) {
            // i64 can hold values outside safe range, need to check
            val = builder.CreateSIToFP(val, builder.getDoubleTy(), "to_dbl");
        } else if (val->getType()->isIntegerTy(1)) {
            // Booleans (0/1) are always safe integers
            return builder.getInt1(true);
        } else if (val->getType()->isPointerTy()) {
            // Boxed value - return false
            return builder.getInt1(false);
        }

        // isSafeInteger = isInteger && abs(value) <= MAX_SAFE_INTEGER
        llvm::Value* isNotNaN = builder.CreateFCmpOEQ(val, val);
        llvm::Value* posInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), false);
        llvm::Value* negInf = llvm::ConstantFP::getInfinity(builder.getDoubleTy(), true);
        llvm::Value* notPosInf = builder.CreateFCmpONE(val, posInf);
        llvm::Value* notNegInf = builder.CreateFCmpONE(val, negInf);
        llvm::Value* isFinite = builder.CreateAnd(isNotNaN, builder.CreateAnd(notPosInf, notNegInf));

        llvm::Function* floorFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::floor, {builder.getDoubleTy()});
        llvm::Value* floorVal = builder.CreateCall(floorFn, {val});
        llvm::Value* isWholeNumber = builder.CreateFCmpOEQ(val, floorVal);
        llvm::Value* isInteger = builder.CreateAnd(isFinite, isWholeNumber);

        // MAX_SAFE_INTEGER = 2^53 - 1 = 9007199254740991
        llvm::Value* maxSafe = llvm::ConstantFP::get(builder.getDoubleTy(), 9007199254740991.0);
        llvm::Value* minSafe = llvm::ConstantFP::get(builder.getDoubleTy(), -9007199254740991.0);
        llvm::Value* notTooLarge = builder.CreateFCmpOLE(val, maxSafe);
        llvm::Value* notTooSmall = builder.CreateFCmpOGE(val, minSafe);
        llvm::Value* inRange = builder.CreateAnd(notTooLarge, notTooSmall);

        return builder.CreateAnd(isInteger, inRange);
    }
};

// Register the MathHandler
static bool mathHandlerRegistered = []() {
    // This will be called during static initialization
    // However, we defer actual registration to registerBuiltinHandlers()
    return true;
}();

} // namespace ts::hir

// Factory function to create MathHandler - called from HandlerRegistry
namespace ts::hir {
    std::unique_ptr<BuiltinHandler> createMathHandler() {
        return std::make_unique<MathHandler>();
    }
}
