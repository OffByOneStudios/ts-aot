#include "ConstantFoldingPass.h"
#include "../HIR.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace ts::hir {

PassResult ConstantFoldingPass::runOnFunction(HIRFunction& func) {
    bool changed = false;

    // Clear constant tracking from previous functions
    constants_.clear();

    for (auto& block : func.blocks) {
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto* inst = block->instructions[i].get();

            // First, track constants from constant instructions
            if (inst->result) {
                ConstValue cv;
                switch (inst->opcode) {
                    case HIROpcode::ConstInt:
                        if (auto* val = std::get_if<int64_t>(&inst->operands[0])) {
                            cv = ConstValue::makeInt(*val);
                        }
                        break;
                    case HIROpcode::ConstFloat:
                        if (auto* val = std::get_if<double>(&inst->operands[0])) {
                            cv = ConstValue::makeFloat(*val);
                        }
                        break;
                    case HIROpcode::ConstBool:
                        if (auto* val = std::get_if<bool>(&inst->operands[0])) {
                            cv = ConstValue::makeBool(*val);
                        }
                        break;
                    case HIROpcode::ConstString:
                        if (auto* val = std::get_if<std::string>(&inst->operands[0])) {
                            cv = ConstValue::makeString(*val);
                        }
                        break;
                    case HIROpcode::ConstNull:
                        cv = ConstValue::makeNull();
                        break;
                    case HIROpcode::ConstUndefined:
                        cv = ConstValue::makeUndefined();
                        break;
                    default:
                        break;
                }

                if (cv.isKnown()) {
                    constants_[inst->result] = cv;
                }
            }

            // Try to fold the instruction
            auto replacement = tryFold(inst, func);
            if (replacement) {
                SPDLOG_DEBUG("ConstantFolding: Folded {} to {}",
                             inst->toString(), replacement->toString());

                // Track the new constant value
                if (replacement->result) {
                    ConstValue cv;
                    switch (replacement->opcode) {
                        case HIROpcode::ConstInt:
                            if (auto* val = std::get_if<int64_t>(&replacement->operands[0])) {
                                cv = ConstValue::makeInt(*val);
                            }
                            break;
                        case HIROpcode::ConstFloat:
                            if (auto* val = std::get_if<double>(&replacement->operands[0])) {
                                cv = ConstValue::makeFloat(*val);
                            }
                            break;
                        case HIROpcode::ConstBool:
                            if (auto* val = std::get_if<bool>(&replacement->operands[0])) {
                                cv = ConstValue::makeBool(*val);
                            }
                            break;
                        case HIROpcode::ConstString:
                            if (auto* val = std::get_if<std::string>(&replacement->operands[0])) {
                                cv = ConstValue::makeString(*val);
                            }
                            break;
                        default:
                            break;
                    }
                    if (cv.isKnown()) {
                        constants_[replacement->result] = cv;
                    }
                }

                block->instructions[i] = std::move(replacement);
                changed = true;
            }
        }
    }

    return changed ? PassResult::modified() : PassResult::unchanged();
}

std::unique_ptr<HIRInstruction> ConstantFoldingPass::tryFold(
    HIRInstruction* inst, HIRFunction& func) {

    switch (inst->opcode) {
        // Integer arithmetic
        case HIROpcode::AddI64:
        case HIROpcode::SubI64:
        case HIROpcode::MulI64:
        case HIROpcode::DivI64:
        case HIROpcode::ModI64:
        case HIROpcode::AndI64:
        case HIROpcode::OrI64:
        case HIROpcode::XorI64:
        case HIROpcode::ShlI64:
        case HIROpcode::ShrI64:
        case HIROpcode::UShrI64: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isInt() && b.isInt()) {
                auto result = foldIntArithmetic(inst->opcode, a.intVal, b.intVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        // Unary integer operations
        case HIROpcode::NegI64:
        case HIROpcode::NotI64: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isInt()) {
                int64_t result;
                if (inst->opcode == HIROpcode::NegI64) {
                    result = -a.intVal;
                } else {
                    result = ~a.intVal;
                }
                return createConstInst(ConstValue::makeInt(result), inst->result, func);
            }
            break;
        }

        // Float arithmetic
        case HIROpcode::AddF64:
        case HIROpcode::SubF64:
        case HIROpcode::MulF64:
        case HIROpcode::DivF64:
        case HIROpcode::ModF64: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isFloat() && b.isFloat()) {
                auto result = foldFloatArithmetic(inst->opcode, a.floatVal, b.floatVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        // Unary float operations
        case HIROpcode::NegF64: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isFloat()) {
                return createConstInst(ConstValue::makeFloat(-a.floatVal), inst->result, func);
            }
            break;
        }

        // Integer comparisons
        case HIROpcode::CmpEqI64:
        case HIROpcode::CmpNeI64:
        case HIROpcode::CmpLtI64:
        case HIROpcode::CmpLeI64:
        case HIROpcode::CmpGtI64:
        case HIROpcode::CmpGeI64: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isInt() && b.isInt()) {
                auto result = foldIntComparison(inst->opcode, a.intVal, b.intVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        // Float comparisons
        case HIROpcode::CmpEqF64:
        case HIROpcode::CmpNeF64:
        case HIROpcode::CmpLtF64:
        case HIROpcode::CmpLeF64:
        case HIROpcode::CmpGtF64:
        case HIROpcode::CmpGeF64: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isFloat() && b.isFloat()) {
                auto result = foldFloatComparison(inst->opcode, a.floatVal, b.floatVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        // Boolean operations
        case HIROpcode::LogicalAnd:
        case HIROpcode::LogicalOr: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isBool() && b.isBool()) {
                auto result = foldBooleanOp(inst->opcode, a.boolVal, b.boolVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        case HIROpcode::LogicalNot: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isBool()) {
                return createConstInst(ConstValue::makeBool(!a.boolVal), inst->result, func);
            }
            break;
        }

        // String concatenation
        case HIROpcode::StringConcat: {
            if (inst->operands.size() < 2) return nullptr;
            auto a = getConstant(inst->operands[0]);
            auto b = getConstant(inst->operands[1]);
            if (a.isString() && b.isString()) {
                auto result = foldStringConcat(a.stringVal, b.stringVal);
                if (result.isKnown()) {
                    return createConstInst(result, inst->result, func);
                }
            }
            break;
        }

        // Type conversions
        case HIROpcode::CastI64ToF64: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isInt()) {
                return createConstInst(
                    ConstValue::makeFloat(static_cast<double>(a.intVal)),
                    inst->result, func);
            }
            break;
        }

        case HIROpcode::CastF64ToI64: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isFloat()) {
                return createConstInst(
                    ConstValue::makeInt(static_cast<int64_t>(a.floatVal)),
                    inst->result, func);
            }
            break;
        }

        case HIROpcode::CastBoolToI64: {
            if (inst->operands.empty()) return nullptr;
            auto a = getConstant(inst->operands[0]);
            if (a.isBool()) {
                return createConstInst(
                    ConstValue::makeInt(a.boolVal ? 1 : 0),
                    inst->result, func);
            }
            break;
        }

        default:
            break;
    }

    return nullptr;
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::getConstant(const HIROperand& operand) {
    // Check if it's a direct constant
    if (auto* intVal = std::get_if<int64_t>(&operand)) {
        return ConstValue::makeInt(*intVal);
    }
    if (auto* floatVal = std::get_if<double>(&operand)) {
        return ConstValue::makeFloat(*floatVal);
    }
    if (auto* boolVal = std::get_if<bool>(&operand)) {
        return ConstValue::makeBool(*boolVal);
    }
    if (auto* strVal = std::get_if<std::string>(&operand)) {
        return ConstValue::makeString(*strVal);
    }

    // Check if it's a HIRValue with a known constant
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&operand)) {
        if (*valPtr) {
            return getConstant(*valPtr);
        }
    }

    return ConstValue::unknown();
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::getConstant(std::shared_ptr<HIRValue> val) {
    auto it = constants_.find(val);
    if (it != constants_.end()) {
        return it->second;
    }
    return ConstValue::unknown();
}

std::unique_ptr<HIRInstruction> ConstantFoldingPass::createConstInst(
    const ConstValue& val,
    std::shared_ptr<HIRValue> result,
    HIRFunction& func) {

    std::unique_ptr<HIRInstruction> inst;

    switch (val.kind) {
        case ConstValue::Kind::Int:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstInt);
            inst->operands.push_back(val.intVal);
            if (result) {
                result->type = HIRType::makeInt64();
            }
            break;

        case ConstValue::Kind::Float:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstFloat);
            inst->operands.push_back(val.floatVal);
            if (result) {
                result->type = HIRType::makeFloat64();
            }
            break;

        case ConstValue::Kind::Bool:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstBool);
            inst->operands.push_back(val.boolVal);
            if (result) {
                result->type = HIRType::makeBool();
            }
            break;

        case ConstValue::Kind::String:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstString);
            inst->operands.push_back(val.stringVal);
            if (result) {
                result->type = HIRType::makeString();
            }
            break;

        case ConstValue::Kind::Null:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstNull);
            if (result) {
                result->type = HIRType::makeAny();
            }
            break;

        case ConstValue::Kind::Undefined:
            inst = std::make_unique<HIRInstruction>(HIROpcode::ConstUndefined);
            if (result) {
                result->type = HIRType::makeAny();
            }
            break;

        default:
            return nullptr;
    }

    inst->result = result;
    return inst;
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldIntArithmetic(
    HIROpcode op, int64_t a, int64_t b) {

    switch (op) {
        case HIROpcode::AddI64:
            return ConstValue::makeInt(a + b);
        case HIROpcode::SubI64:
            return ConstValue::makeInt(a - b);
        case HIROpcode::MulI64:
            return ConstValue::makeInt(a * b);
        case HIROpcode::DivI64:
            if (b == 0) return ConstValue::unknown();  // Avoid division by zero
            return ConstValue::makeInt(a / b);
        case HIROpcode::ModI64:
            if (b == 0) return ConstValue::unknown();  // Avoid modulo by zero
            return ConstValue::makeInt(a % b);
        case HIROpcode::AndI64:
            return ConstValue::makeInt(a & b);
        case HIROpcode::OrI64:
            return ConstValue::makeInt(a | b);
        case HIROpcode::XorI64:
            return ConstValue::makeInt(a ^ b);
        case HIROpcode::ShlI64:
            return ConstValue::makeInt(a << (b & 63));  // Mask shift amount
        case HIROpcode::ShrI64:
            return ConstValue::makeInt(a >> (b & 63));  // Arithmetic shift
        case HIROpcode::UShrI64:
            return ConstValue::makeInt(static_cast<int64_t>(
                static_cast<uint64_t>(a) >> (b & 63)));  // Logical shift
        default:
            return ConstValue::unknown();
    }
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldFloatArithmetic(
    HIROpcode op, double a, double b) {

    switch (op) {
        case HIROpcode::AddF64:
            return ConstValue::makeFloat(a + b);
        case HIROpcode::SubF64:
            return ConstValue::makeFloat(a - b);
        case HIROpcode::MulF64:
            return ConstValue::makeFloat(a * b);
        case HIROpcode::DivF64:
            // JavaScript allows division by zero (returns Infinity or NaN)
            return ConstValue::makeFloat(a / b);
        case HIROpcode::ModF64:
            return ConstValue::makeFloat(std::fmod(a, b));
        default:
            return ConstValue::unknown();
    }
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldIntComparison(
    HIROpcode op, int64_t a, int64_t b) {

    switch (op) {
        case HIROpcode::CmpEqI64:
            return ConstValue::makeBool(a == b);
        case HIROpcode::CmpNeI64:
            return ConstValue::makeBool(a != b);
        case HIROpcode::CmpLtI64:
            return ConstValue::makeBool(a < b);
        case HIROpcode::CmpLeI64:
            return ConstValue::makeBool(a <= b);
        case HIROpcode::CmpGtI64:
            return ConstValue::makeBool(a > b);
        case HIROpcode::CmpGeI64:
            return ConstValue::makeBool(a >= b);
        default:
            return ConstValue::unknown();
    }
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldFloatComparison(
    HIROpcode op, double a, double b) {

    switch (op) {
        case HIROpcode::CmpEqF64:
            return ConstValue::makeBool(a == b);
        case HIROpcode::CmpNeF64:
            return ConstValue::makeBool(a != b);
        case HIROpcode::CmpLtF64:
            return ConstValue::makeBool(a < b);
        case HIROpcode::CmpLeF64:
            return ConstValue::makeBool(a <= b);
        case HIROpcode::CmpGtF64:
            return ConstValue::makeBool(a > b);
        case HIROpcode::CmpGeF64:
            return ConstValue::makeBool(a >= b);
        default:
            return ConstValue::unknown();
    }
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldBooleanOp(
    HIROpcode op, bool a, bool b) {

    switch (op) {
        case HIROpcode::LogicalAnd:
            return ConstValue::makeBool(a && b);
        case HIROpcode::LogicalOr:
            return ConstValue::makeBool(a || b);
        default:
            return ConstValue::unknown();
    }
}

ConstantFoldingPass::ConstValue ConstantFoldingPass::foldStringConcat(
    const std::string& a, const std::string& b) {
    return ConstValue::makeString(a + b);
}

} // namespace ts::hir
