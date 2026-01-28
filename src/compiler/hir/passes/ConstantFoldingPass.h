#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>
#include <map>
#include <variant>

namespace ts::hir {

/// Constant folding pass for optimizing compile-time expressions
///
/// This pass evaluates constant expressions at compile time:
/// - Arithmetic: 3 + 4 → 7, 2.0 * 3.0 → 6.0
/// - String concatenation: "hello" + "world" → "helloworld"
/// - Boolean operations: true && false → false
/// - Comparisons: 5 > 3 → true
///
/// Input:
///   %0 = const.i64 3
///   %1 = const.i64 4
///   %2 = add.i64 %0, %1
///   call @print, %2
///
/// Output:
///   %0 = const.i64 7
///   call @print, %0
///
/// The pass tracks constant values through the SSA graph and replaces
/// operations on constants with their computed results.
class ConstantFoldingPass : public HIRFunctionPass {
public:
    const char* name() const override { return "ConstantFolding"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Represents a compile-time constant value
    struct ConstValue {
        enum class Kind { Int, Float, Bool, String, Null, Undefined, Unknown };
        Kind kind = Kind::Unknown;
        int64_t intVal = 0;
        double floatVal = 0.0;
        bool boolVal = false;
        std::string stringVal;

        static ConstValue makeInt(int64_t v) {
            ConstValue cv; cv.kind = Kind::Int; cv.intVal = v; return cv;
        }
        static ConstValue makeFloat(double v) {
            ConstValue cv; cv.kind = Kind::Float; cv.floatVal = v; return cv;
        }
        static ConstValue makeBool(bool v) {
            ConstValue cv; cv.kind = Kind::Bool; cv.boolVal = v; return cv;
        }
        static ConstValue makeString(const std::string& v) {
            ConstValue cv; cv.kind = Kind::String; cv.stringVal = v; return cv;
        }
        static ConstValue makeNull() {
            ConstValue cv; cv.kind = Kind::Null; return cv;
        }
        static ConstValue makeUndefined() {
            ConstValue cv; cv.kind = Kind::Undefined; return cv;
        }
        static ConstValue unknown() {
            return ConstValue{};
        }

        bool isKnown() const { return kind != Kind::Unknown; }
        bool isInt() const { return kind == Kind::Int; }
        bool isFloat() const { return kind == Kind::Float; }
        bool isBool() const { return kind == Kind::Bool; }
        bool isString() const { return kind == Kind::String; }
    };

    /// Map from HIRValue to its constant value (if known)
    std::map<std::shared_ptr<HIRValue>, ConstValue> constants_;

    /// Try to fold a single instruction
    /// @return The replacement instruction, or nullptr if not foldable
    std::unique_ptr<HIRInstruction> tryFold(HIRInstruction* inst, HIRFunction& func);

    /// Get the constant value of an operand (if known)
    ConstValue getConstant(const HIROperand& operand);

    /// Get the constant value of a HIRValue (if known)
    ConstValue getConstant(std::shared_ptr<HIRValue> val);

    /// Create a constant instruction from a ConstValue
    std::unique_ptr<HIRInstruction> createConstInst(
        const ConstValue& val,
        std::shared_ptr<HIRValue> result,
        HIRFunction& func);

    /// Fold integer arithmetic
    ConstValue foldIntArithmetic(HIROpcode op, int64_t a, int64_t b);

    /// Fold float arithmetic
    ConstValue foldFloatArithmetic(HIROpcode op, double a, double b);

    /// Fold integer comparisons
    ConstValue foldIntComparison(HIROpcode op, int64_t a, int64_t b);

    /// Fold float comparisons
    ConstValue foldFloatComparison(HIROpcode op, double a, double b);

    /// Fold boolean operations
    ConstValue foldBooleanOp(HIROpcode op, bool a, bool b);

    /// Fold string concatenation
    ConstValue foldStringConcat(const std::string& a, const std::string& b);
};

} // namespace ts::hir
