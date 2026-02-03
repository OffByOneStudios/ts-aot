#include "IntegerOptimizationPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace ts::hir {

PassResult IntegerOptimizationPass::runOnFunction(HIRFunction& func) {
    // Clear state from previous functions
    ranges_.clear();
    convertToInt_.clear();

    // Phase 1: Detect integer literals
    detectIntegerLiterals(func);

    // Phase 2: Propagate ranges through operations
    propagateRanges(func);

    // Phase 3: Convert safe operations
    bool changed = convertSafeOperations(func);

    // Phase 4: Verify consistency (for debugging, doesn't modify IR)
    verifyTypeConsistency(func);

    return changed ? PassResult::modified() : PassResult::unchanged();
}

void IntegerOptimizationPass::detectIntegerLiterals(HIRFunction& func) {
    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (!inst->result) continue;

            switch (inst->opcode) {
                case HIROpcode::ConstFloat: {
                    // Check if the float constant is an exact safe integer
                    if (auto* valPtr = std::get_if<double>(&inst->operands[0])) {
                        double val = *valPtr;
                        if (isExactInteger(val)) {
                            int64_t intVal = static_cast<int64_t>(val);
                            ranges_[inst->result] = NumericRange::makeInteger(intVal);
                            SPDLOG_DEBUG("IntegerOpt: {} is integer literal {}",
                                        inst->result->toString(), intVal);
                        } else {
                            ranges_[inst->result] = NumericRange::makeFloat();
                            SPDLOG_DEBUG("IntegerOpt: {} is non-integer float {}",
                                        inst->result->toString(), val);
                        }
                    }
                    break;
                }

                case HIROpcode::ConstInt: {
                    // Integer constants are already known integers
                    if (auto* valPtr = std::get_if<int64_t>(&inst->operands[0])) {
                        ranges_[inst->result] = NumericRange::makeInteger(*valPtr);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

void IntegerOptimizationPass::propagateRanges(HIRFunction& func) {
    // Simple forward pass - more sophisticated would use worklist algorithm
    // For now, we do a single pass which works for straight-line code

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            if (!inst->result) continue;
            if (ranges_.count(inst->result)) continue;  // Already have range

            NumericRange resultRange = NumericRange::makeUnknown();

            switch (inst->opcode) {
                // Float64 arithmetic
                case HIROpcode::AddF64:
                case HIROpcode::SubF64:
                case HIROpcode::MulF64: {
                    if (inst->operands.size() >= 2) {
                        auto lhsRange = getRange(inst->operands[0]);
                        auto rhsRange = getRange(inst->operands[1]);
                        resultRange = computeResultRange(inst->opcode, lhsRange, rhsRange);
                    }
                    break;
                }

                // Division ALWAYS produces Float64
                case HIROpcode::DivF64:
                case HIROpcode::ModF64: {
                    resultRange = NumericRange::makeFloat();
                    SPDLOG_DEBUG("IntegerOpt: {} is division/mod result (always float)",
                                inst->result->toString());
                    break;
                }

                // Int64 arithmetic - preserve integer nature
                case HIROpcode::AddI64:
                case HIROpcode::SubI64:
                case HIROpcode::MulI64: {
                    if (inst->operands.size() >= 2) {
                        auto lhsRange = getRange(inst->operands[0]);
                        auto rhsRange = getRange(inst->operands[1]);
                        // For I64 ops, compute as if they were integers
                        resultRange = computeResultRange(inst->opcode, lhsRange, rhsRange);
                    }
                    break;
                }

                // Bitwise operations produce 32-bit signed integers
                case HIROpcode::AndI64:
                case HIROpcode::OrI64:
                case HIROpcode::XorI64:
                case HIROpcode::ShlI64:
                case HIROpcode::ShrI64:
                case HIROpcode::UShrI64: {
                    // Bitwise ops produce 32-bit signed integers (-2^31 to 2^31-1)
                    // >>> (unsigned shift) produces 32-bit unsigned (0 to 2^32-1)
                    if (inst->opcode == HIROpcode::UShrI64) {
                        resultRange = NumericRange::makeRange(0, UINT32_MAX);
                    } else {
                        resultRange = NumericRange::makeRange(INT32_MIN, INT32_MAX);
                    }
                    break;
                }

                // Comparisons produce booleans (not tracked as numeric)
                case HIROpcode::CmpEqF64:
                case HIROpcode::CmpNeF64:
                case HIROpcode::CmpLtF64:
                case HIROpcode::CmpLeF64:
                case HIROpcode::CmpGtF64:
                case HIROpcode::CmpGeF64:
                case HIROpcode::CmpEqI64:
                case HIROpcode::CmpNeI64:
                case HIROpcode::CmpLtI64:
                case HIROpcode::CmpLeI64:
                case HIROpcode::CmpGtI64:
                case HIROpcode::CmpGeI64:
                    // Boolean result - not a numeric range
                    break;

                // Type conversions
                case HIROpcode::CastI64ToF64: {
                    // Converting I64 to F64 - result is float
                    auto srcRange = getRange(inst->operands[0]);
                    if (srcRange.isSafeInteger()) {
                        // If source is safe integer, result is safe integer (as float)
                        resultRange = srcRange;
                    } else {
                        resultRange = NumericRange::makeFloat();
                    }
                    break;
                }

                case HIROpcode::CastF64ToI64: {
                    // Truncation - result is integer but may lose precision
                    resultRange = NumericRange::makeRange(
                        NumericRange::MIN_SAFE_INTEGER,
                        NumericRange::MAX_SAFE_INTEGER);
                    break;
                }

                // Box/unbox preserve numeric nature
                case HIROpcode::BoxFloat:
                case HIROpcode::UnboxFloat: {
                    if (!inst->operands.empty()) {
                        resultRange = getRange(inst->operands[0]);
                    }
                    break;
                }

                case HIROpcode::BoxInt:
                case HIROpcode::UnboxInt: {
                    if (!inst->operands.empty()) {
                        resultRange = getRange(inst->operands[0]);
                    }
                    break;
                }

                // Negation
                case HIROpcode::NegF64:
                case HIROpcode::NegI64: {
                    if (!inst->operands.empty()) {
                        auto srcRange = getRange(inst->operands[0]);
                        if (srcRange.kind == NumericRange::Kind::IntegerRange) {
                            // Negation swaps and negates bounds
                            resultRange = NumericRange::makeRange(-srcRange.maxVal, -srcRange.minVal);
                        } else if (srcRange.mustBeFloat()) {
                            resultRange = NumericRange::makeFloat();
                        }
                    }
                    break;
                }

                default:
                    // Unknown operation - leave as unknown
                    break;
            }

            if (resultRange.isKnown()) {
                ranges_[inst->result] = resultRange;
            }
        }
    }
}

bool IntegerOptimizationPass::convertSafeOperations(HIRFunction& func) {
    bool changed = false;

    // First pass: Build a map of which values are used in f64 operations
    std::set<std::shared_ptr<HIRValue>> usedInF64Ops;

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Check if this operation requires f64 operands
            bool requiresF64 = false;
            switch (inst->opcode) {
                // F64 comparisons require f64 operands
                case HIROpcode::CmpEqF64:
                case HIROpcode::CmpNeF64:
                case HIROpcode::CmpLtF64:
                case HIROpcode::CmpLeF64:
                case HIROpcode::CmpGtF64:
                case HIROpcode::CmpGeF64:
                // F64 arithmetic requires f64 operands (unless we're converting the whole op)
                case HIROpcode::AddF64:
                case HIROpcode::SubF64:
                case HIROpcode::MulF64:
                // Division always requires and produces f64
                case HIROpcode::DivF64:
                case HIROpcode::ModF64:
                // Negation
                case HIROpcode::NegF64:
                    requiresF64 = true;
                    break;
                case HIROpcode::Store:
                    // If storing to f64 alloca, the value will be used as f64
                    if (inst->operands.size() >= 2) {
                        // Check if target alloca is f64 typed
                        // For now, conservatively assume stores might require f64
                        requiresF64 = true;
                    }
                    break;
                case HIROpcode::Call:
                case HIROpcode::CallIndirect:
                    // Function calls may expect f64 parameters
                    // Conservatively mark all call arguments as requiring f64
                    requiresF64 = true;
                    break;
                case HIROpcode::BoxFloat:
                    // Boxing as float requires f64
                    requiresF64 = true;
                    break;
                default:
                    break;
            }

            if (requiresF64) {
                // Mark all operand values as used in f64 context
                for (auto& op : inst->operands) {
                    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&op)) {
                        if (*valPtr) {
                            usedInF64Ops.insert(*valPtr);
                        }
                    }
                }
            }
        }
    }

    // Second pass: Convert operations, but not constants used in f64 ops
    for (auto& block : func.blocks) {
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto* inst = block->instructions[i].get();

            // Only convert Float64 operations
            switch (inst->opcode) {
                case HIROpcode::AddF64:
                case HIROpcode::SubF64:
                case HIROpcode::MulF64: {
                    // Check if result and both operands are safe integers
                    if (!inst->result) break;

                    auto resultRange = getRange(inst->result);
                    if (!resultRange.isSafeInteger()) break;

                    if (inst->operands.size() < 2) break;
                    auto lhsRange = getRange(inst->operands[0]);
                    auto rhsRange = getRange(inst->operands[1]);

                    if (!lhsRange.isSafeInteger() || !rhsRange.isSafeInteger()) break;

                    // Check if operands are already i64-typed (converted or originally i64)
                    // We can only convert the operation if the operands have been/will be converted
                    bool operandsAreI64 = true;
                    for (auto& op : inst->operands) {
                        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&op)) {
                            if (*valPtr) {
                                // Check if this value is used in any f64 operation (other than this one)
                                // If so, the constant won't be converted, so we can't convert the op
                                if (usedInF64Ops.count(*valPtr)) {
                                    operandsAreI64 = false;
                                    break;
                                }
                            }
                        }
                    }

                    if (!operandsAreI64) {
                        SPDLOG_DEBUG("IntegerOpt: Skipping {} - operands used in f64 ops",
                                    inst->toString());
                        break;
                    }

                    // Safe to convert!
                    auto replacement = convertToInt64(inst, func);
                    if (replacement) {
                        SPDLOG_DEBUG("IntegerOpt: Converting {} to {}",
                                    inst->toString(), replacement->toString());
                        block->instructions[i] = std::move(replacement);
                        changed = true;
                    }
                    break;
                }

                case HIROpcode::ConstFloat: {
                    // Convert integer-valued float constants to int constants
                    // BUT ONLY if not used in operations that require f64
                    if (!inst->result) break;

                    // Check if this value is used in any f64 operation
                    if (usedInF64Ops.count(inst->result)) {
                        SPDLOG_DEBUG("IntegerOpt: Skipping {} - used in f64 operation",
                                    inst->result->toString());
                        break;
                    }

                    auto range = getRange(inst->result);
                    if (!range.isSafeInteger()) break;

                    // Get the integer value
                    if (auto* valPtr = std::get_if<double>(&inst->operands[0])) {
                        double val = *valPtr;
                        if (isExactInteger(val)) {
                            int64_t intVal = static_cast<int64_t>(val);

                            auto replacement = std::make_unique<HIRInstruction>(HIROpcode::ConstInt);
                            replacement->operands.push_back(intVal);
                            replacement->result = inst->result;
                            replacement->result->type = HIRType::makeInt64();

                            SPDLOG_DEBUG("IntegerOpt: Converting const.f64 {} to const.i64 {}",
                                        val, intVal);
                            block->instructions[i] = std::move(replacement);
                            changed = true;
                        }
                    }
                    break;
                }

                // Don't convert division - it ALWAYS returns float in JavaScript
                case HIROpcode::DivF64:
                case HIROpcode::ModF64:
                    // Never convert these
                    break;

                default:
                    break;
            }
        }
    }

    return changed;
}

bool IntegerOptimizationPass::verifyTypeConsistency(HIRFunction& func) {
    // This phase verifies that our conversions are consistent
    // For now, just log warnings for debugging

    for (auto& block : func.blocks) {
        for (auto& inst : block->instructions) {
            // Check for type mismatches between operands and operation type
            switch (inst->opcode) {
                case HIROpcode::AddI64:
                case HIROpcode::SubI64:
                case HIROpcode::MulI64: {
                    // I64 operations should have I64-typed operands
                    for (auto& op : inst->operands) {
                        if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&op)) {
                            if (*valPtr && (*valPtr)->type &&
                                (*valPtr)->type->kind == HIRTypeKind::Float64) {
                                SPDLOG_WARN("IntegerOpt: Type mismatch - I64 op with F64 operand: {}",
                                           inst->toString());
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    return true;
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::getRange(std::shared_ptr<HIRValue> val) {
    auto it = ranges_.find(val);
    if (it != ranges_.end()) {
        return it->second;
    }
    return NumericRange::makeUnknown();
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::getRange(const HIROperand& operand) {
    // Direct constants
    if (auto* intVal = std::get_if<int64_t>(&operand)) {
        return NumericRange::makeInteger(*intVal);
    }
    if (auto* floatVal = std::get_if<double>(&operand)) {
        if (isExactInteger(*floatVal)) {
            return NumericRange::makeInteger(static_cast<int64_t>(*floatVal));
        }
        return NumericRange::makeFloat();
    }

    // HIRValue reference
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
        if (*valPtr) {
            return getRange(*valPtr);
        }
    }

    return NumericRange::makeUnknown();
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::computeResultRange(HIROpcode op, const NumericRange& lhs, const NumericRange& rhs) {
    // If either is unknown, result is unknown
    if (!lhs.isKnown() || !rhs.isKnown()) {
        return NumericRange::makeUnknown();
    }

    // If either must be float, result is float
    if (lhs.mustBeFloat() || rhs.mustBeFloat()) {
        return NumericRange::makeFloat();
    }

    // Map F64 opcodes to range computations
    switch (op) {
        case HIROpcode::AddF64:
        case HIROpcode::AddI64:
            return combineAdd(lhs, rhs);

        case HIROpcode::SubF64:
        case HIROpcode::SubI64:
            return combineSub(lhs, rhs);

        case HIROpcode::MulF64:
        case HIROpcode::MulI64:
            return combineMul(lhs, rhs);

        case HIROpcode::DivF64:
        case HIROpcode::DivI64:
            // Division always produces float in JavaScript
            return NumericRange::makeFloat();

        default:
            return NumericRange::makeUnknown();
    }
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::combineAdd(const NumericRange& lhs, const NumericRange& rhs) {
    if (lhs.kind != NumericRange::Kind::IntegerRange ||
        rhs.kind != NumericRange::Kind::IntegerRange) {
        return NumericRange::makeUnknown();
    }

    // Check for overflow before computing
    // Overflow detection: (b > 0 && a > MAX - b) || (b < 0 && a < MIN - b)
    bool mayOverflowMax = (rhs.maxVal > 0 &&
                          lhs.maxVal > NumericRange::MAX_SAFE_INTEGER - rhs.maxVal);
    bool mayOverflowMin = (rhs.minVal < 0 &&
                          lhs.minVal < NumericRange::MIN_SAFE_INTEGER - rhs.minVal);

    if (mayOverflowMax || mayOverflowMin) {
        // Compute the range anyway for tracking, but mark as may overflow
        int64_t newMin = lhs.minVal + rhs.minVal;
        int64_t newMax = lhs.maxVal + rhs.maxVal;
        return NumericRange::makeMayOverflow(newMin, newMax);
    }

    int64_t newMin = lhs.minVal + rhs.minVal;
    int64_t newMax = lhs.maxVal + rhs.maxVal;

    return NumericRange::makeRange(newMin, newMax);
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::combineSub(const NumericRange& lhs, const NumericRange& rhs) {
    if (lhs.kind != NumericRange::Kind::IntegerRange ||
        rhs.kind != NumericRange::Kind::IntegerRange) {
        return NumericRange::makeUnknown();
    }

    // a - b: min = lhs.min - rhs.max, max = lhs.max - rhs.min
    bool mayOverflowMax = (rhs.minVal < 0 &&
                          lhs.maxVal > NumericRange::MAX_SAFE_INTEGER + rhs.minVal);
    bool mayOverflowMin = (rhs.maxVal > 0 &&
                          lhs.minVal < NumericRange::MIN_SAFE_INTEGER + rhs.maxVal);

    if (mayOverflowMax || mayOverflowMin) {
        int64_t newMin = lhs.minVal - rhs.maxVal;
        int64_t newMax = lhs.maxVal - rhs.minVal;
        return NumericRange::makeMayOverflow(newMin, newMax);
    }

    int64_t newMin = lhs.minVal - rhs.maxVal;
    int64_t newMax = lhs.maxVal - rhs.minVal;

    return NumericRange::makeRange(newMin, newMax);
}

IntegerOptimizationPass::NumericRange
IntegerOptimizationPass::combineMul(const NumericRange& lhs, const NumericRange& rhs) {
    if (lhs.kind != NumericRange::Kind::IntegerRange ||
        rhs.kind != NumericRange::Kind::IntegerRange) {
        return NumericRange::makeUnknown();
    }

    // Compute all four corner products to find min/max
    int64_t products[4] = {
        lhs.minVal * rhs.minVal,
        lhs.minVal * rhs.maxVal,
        lhs.maxVal * rhs.minVal,
        lhs.maxVal * rhs.maxVal
    };

    int64_t newMin = *std::min_element(products, products + 4);
    int64_t newMax = *std::max_element(products, products + 4);

    // Check if result exceeds safe integer range
    if (newMin < NumericRange::MIN_SAFE_INTEGER ||
        newMax > NumericRange::MAX_SAFE_INTEGER) {
        return NumericRange::makeMayOverflow(newMin, newMax);
    }

    return NumericRange::makeRange(newMin, newMax);
}

bool IntegerOptimizationPass::isExactInteger(double val) {
    // Check if the value is:
    // 1. Not NaN or Infinity
    // 2. Equal to its floor (no fractional part)
    // 3. Within safe integer range
    if (!std::isfinite(val)) return false;
    if (std::floor(val) != val) return false;
    if (val < static_cast<double>(NumericRange::MIN_SAFE_INTEGER)) return false;
    if (val > static_cast<double>(NumericRange::MAX_SAFE_INTEGER)) return false;
    return true;
}

std::unique_ptr<HIRInstruction>
IntegerOptimizationPass::convertToInt64(HIRInstruction* inst, HIRFunction& func) {
    HIROpcode newOpcode = getInt64Opcode(inst->opcode);
    if (newOpcode == inst->opcode) {
        // No conversion needed or not convertible
        return nullptr;
    }

    auto newInst = std::make_unique<HIRInstruction>(newOpcode);
    newInst->result = inst->result;
    newInst->operands = inst->operands;
    newInst->sourceLocation = inst->sourceLocation;

    // Update result type
    if (newInst->result) {
        newInst->result->type = HIRType::makeInt64();
    }

    return newInst;
}

HIROpcode IntegerOptimizationPass::getInt64Opcode(HIROpcode f64Op) {
    switch (f64Op) {
        case HIROpcode::AddF64:  return HIROpcode::AddI64;
        case HIROpcode::SubF64:  return HIROpcode::SubI64;
        case HIROpcode::MulF64:  return HIROpcode::MulI64;
        // Don't convert division - JavaScript division always returns float
        case HIROpcode::DivF64:  return HIROpcode::DivF64;
        case HIROpcode::ModF64:  return HIROpcode::ModF64;
        case HIROpcode::NegF64:  return HIROpcode::NegI64;
        case HIROpcode::CmpEqF64:  return HIROpcode::CmpEqI64;
        case HIROpcode::CmpNeF64:  return HIROpcode::CmpNeI64;
        case HIROpcode::CmpLtF64:  return HIROpcode::CmpLtI64;
        case HIROpcode::CmpLeF64:  return HIROpcode::CmpLeI64;
        case HIROpcode::CmpGtF64:  return HIROpcode::CmpGtI64;
        case HIROpcode::CmpGeF64:  return HIROpcode::CmpGeI64;
        default:
            return f64Op;  // No conversion available
    }
}

} // namespace ts::hir
