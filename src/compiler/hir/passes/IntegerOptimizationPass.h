#pragma once

#include "HIRPass.h"
#include "../HIR.h"
#include <memory>
#include <map>
#include <set>
#include <cstdint>
#include <climits>
#include <cmath>
#include <algorithm>

namespace ts::hir {

/// Integer optimization pass for safe Float64 to Int64 conversion
///
/// This pass analyzes numeric operations and converts Float64 operations to Int64
/// where it can prove the conversion is safe (maintains JavaScript semantics).
///
/// JavaScript Number Semantics:
/// - All numbers are IEEE 754 double-precision (Float64)
/// - Safe integer range: -2^53+1 to 2^53-1 (Number.MAX_SAFE_INTEGER)
/// - Division ALWAYS returns Float64 (even 4/2 = 2.0)
/// - Bitwise operations convert to 32-bit signed integers
/// - Overflow wraps around (no undefined behavior like C++)
///
/// This pass:
/// 1. Detects integer literals in Float64 constants
/// 2. Tracks numeric ranges through operations
/// 3. Converts F64 operations to I64 where proven safe
/// 4. Ensures type consistency across all uses
///
/// Example transformation:
///   %0 = const.f64 5.0
///   %1 = const.f64 3.0
///   %2 = add.f64 %0, %1
/// becomes:
///   %0 = const.i64 5
///   %1 = const.i64 3
///   %2 = add.i64 %0, %1
class IntegerOptimizationPass : public HIRFunctionPass {
public:
    const char* name() const override { return "IntegerOptimization"; }

    PassResult runOnFunction(HIRFunction& func) override;

private:
    /// Represents the possible range and type of a numeric value
    struct NumericRange {
        enum class Kind {
            Unknown,       ///< Cannot determine type (e.g., function parameter)
            Integer,       ///< Known to be exact integer with specific value
            Float,         ///< Known to require floating-point (division result, etc.)
            IntegerRange,  ///< Integer with known min/max bounds
            MayOverflow    ///< Integer operation that could exceed safe range
        };

        Kind kind = Kind::Unknown;
        int64_t minVal = INT64_MIN;  ///< Minimum value (for IntegerRange)
        int64_t maxVal = INT64_MAX;  ///< Maximum value (for IntegerRange)

        /// JavaScript's MAX_SAFE_INTEGER = 2^53 - 1
        static constexpr int64_t MAX_SAFE_INTEGER = 9007199254740991LL;
        static constexpr int64_t MIN_SAFE_INTEGER = -9007199254740991LL;

        /// Check if value is a safe integer that can use I64 operations
        bool isSafeInteger() const {
            if (kind == Kind::Integer) return true;
            if (kind == Kind::IntegerRange) {
                return minVal >= MIN_SAFE_INTEGER && maxVal <= MAX_SAFE_INTEGER;
            }
            return false;
        }

        /// Check if value must be Float64 (e.g., division result)
        bool mustBeFloat() const {
            return kind == Kind::Float;
        }

        /// Check if range is known (not Unknown)
        bool isKnown() const {
            return kind != Kind::Unknown;
        }

        /// Create a range for a specific integer value
        static NumericRange makeInteger(int64_t val) {
            NumericRange r;
            r.kind = Kind::IntegerRange;
            r.minVal = val;
            r.maxVal = val;
            return r;
        }

        /// Create a range for an integer range [min, max]
        static NumericRange makeRange(int64_t min, int64_t max) {
            NumericRange r;
            r.kind = Kind::IntegerRange;
            r.minVal = min;
            r.maxVal = max;
            return r;
        }

        /// Create a range that must be Float64
        static NumericRange makeFloat() {
            NumericRange r;
            r.kind = Kind::Float;
            r.minVal = 0;
            r.maxVal = 0;
            return r;
        }

        /// Create an unknown range
        static NumericRange makeUnknown() {
            return NumericRange{};
        }

        /// Create a range that may overflow
        static NumericRange makeMayOverflow(int64_t min, int64_t max) {
            NumericRange r;
            r.kind = Kind::MayOverflow;
            r.minVal = min;
            r.maxVal = max;
            return r;
        }
    };

    /// Map from HIRValue to its numeric range
    std::map<std::shared_ptr<HIRValue>, NumericRange> ranges_;

    /// Set of values that should be converted from F64 to I64
    std::set<std::shared_ptr<HIRValue>> convertToInt_;

    /// Phase 1: Detect integer literals in constant instructions
    void detectIntegerLiterals(HIRFunction& func);

    /// Phase 2: Propagate ranges through operations
    void propagateRanges(HIRFunction& func);

    /// Phase 3: Convert safe operations from F64 to I64
    bool convertSafeOperations(HIRFunction& func);

    /// Phase 4: Verify type consistency
    bool verifyTypeConsistency(HIRFunction& func);

    /// Get the numeric range of a value
    NumericRange getRange(std::shared_ptr<HIRValue> val);

    /// Get the numeric range of an operand
    NumericRange getRange(const HIROperand& operand);

    /// Compute the range resulting from an operation
    NumericRange computeResultRange(HIROpcode op, const NumericRange& lhs, const NumericRange& rhs);

    /// Combine two integer ranges for addition
    NumericRange combineAdd(const NumericRange& lhs, const NumericRange& rhs);

    /// Combine two integer ranges for subtraction
    NumericRange combineSub(const NumericRange& lhs, const NumericRange& rhs);

    /// Combine two integer ranges for multiplication
    NumericRange combineMul(const NumericRange& lhs, const NumericRange& rhs);

    /// Check if a double value is an exact safe integer
    static bool isExactInteger(double val);

    /// Convert a Float64 instruction to Int64 equivalent
    std::unique_ptr<HIRInstruction> convertToInt64(HIRInstruction* inst, HIRFunction& func);

    /// Get the Int64 equivalent opcode for a Float64 opcode
    static HIROpcode getInt64Opcode(HIROpcode f64Op);
};

} // namespace ts::hir
