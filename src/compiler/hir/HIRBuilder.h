#pragma once

#include "HIR.h"
#include <functional>

namespace ts::hir {

/**
 * HIRBuilder - Convenient API for constructing HIR
 *
 * Usage:
 *   HIRBuilder builder(module);
 *   auto fn = builder.createFunction("foo", HIRType::makeInt64());
 *   builder.setInsertPoint(fn->getEntryBlock());
 *
 *   auto x = builder.createConstInt(42);
 *   auto y = builder.createConstInt(10);
 *   auto sum = builder.createAdd(x, y);
 *   builder.createReturn(sum);
 */
class HIRBuilder {
public:
    HIRBuilder() : module_(nullptr) {}
    explicit HIRBuilder(HIRModule* module) : module_(module) {}

    //==========================================================================
    // Module-level operations
    //==========================================================================

    HIRFunction* createFunction(const std::string& name, std::shared_ptr<HIRType> returnType) {
        auto fn = module_->createFunction(name);
        fn->returnType = returnType;
        return fn;
    }

    HIRFunction* getOrCreateFunction(const std::string& name, std::shared_ptr<HIRType> returnType) {
        if (auto existing = module_->getFunction(name)) {
            return existing;
        }
        return createFunction(name, returnType);
    }

    void addFunctionParam(HIRFunction* fn, const std::string& name, std::shared_ptr<HIRType> type) {
        fn->params.push_back({name, type});
    }

    HIRClass* createClass(const std::string& name) {
        return module_->createClass(name);
    }

    HIRShape* createShape(const std::string& className) {
        return module_->createShape(className);
    }

    //==========================================================================
    // Insert point management
    //==========================================================================

    void setFunction(HIRFunction* fn) {
        currentFunction_ = fn;
        currentBlock_ = fn ? fn->getEntryBlock() : nullptr;
    }

    void setInsertPoint(HIRBlock* block) {
        currentBlock_ = block;
        if (block) {
            currentFunction_ = block->parent;
        }
    }

    HIRBlock* getInsertBlock() const { return currentBlock_; }
    HIRFunction* getCurrentFunction() const { return currentFunction_; }

    HIRBlock* createBlock(const std::string& label) {
        if (!currentFunction_) return nullptr;
        return currentFunction_->createBlock(label);
    }

    //==========================================================================
    // Value creation
    //==========================================================================

    std::shared_ptr<HIRValue> createValue(std::shared_ptr<HIRType> type, const std::string& name = "") {
        if (!currentFunction_) return nullptr;
        return currentFunction_->createValue(type, name);
    }

    //==========================================================================
    // Constants
    //==========================================================================

    std::shared_ptr<HIRValue> createConstInt(int64_t value) {
        auto result = createValue(HIRType::makeInt64());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstInt);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createConstFloat(double value) {
        auto result = createValue(HIRType::makeFloat64());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstFloat);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createConstBool(bool value) {
        auto result = createValue(HIRType::makeBool());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstBool);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createConstString(const std::string& value) {
        auto result = createValue(HIRType::makeString());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstString);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createConstNull() {
        auto result = createValue(HIRType::makePtr());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstNull);
        inst->result = result;
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createConstUndefined() {
        auto result = createValue(HIRType::makeAny());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstUndefined);
        inst->result = result;
        emit(std::move(inst));
        return result;
    }

    // Overloads that take a pre-created result value
    void createConstInt(std::shared_ptr<HIRValue> result, int64_t value) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstInt);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
    }

    void createConstFloat(std::shared_ptr<HIRValue> result, double value) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstFloat);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
    }

    void createConstBool(std::shared_ptr<HIRValue> result, bool value) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstBool);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
    }

    void createConstString(std::shared_ptr<HIRValue> result, const std::string& value) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstString);
        inst->result = result;
        inst->operands.push_back(value);
        emit(std::move(inst));
    }

    void createConstNull(std::shared_ptr<HIRValue> result) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstNull);
        inst->result = result;
        emit(std::move(inst));
    }

    void createConstUndefined(std::shared_ptr<HIRValue> result) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ConstUndefined);
        inst->result = result;
        emit(std::move(inst));
    }

    //==========================================================================
    // Integer arithmetic
    //==========================================================================

    std::shared_ptr<HIRValue> createAddI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::AddI64, lhs, rhs, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createSubI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::SubI64, lhs, rhs, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createMulI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::MulI64, lhs, rhs, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createDivI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::DivI64, lhs, rhs, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createModI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::ModI64, lhs, rhs, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createNegI64(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::NegI64, val, HIRType::makeInt64());
    }

    // Overloads with pre-created result
    void createAddI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::AddI64, result, lhs, rhs);
    }
    void createSubI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::SubI64, result, lhs, rhs);
    }
    void createMulI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::MulI64, result, lhs, rhs);
    }
    void createDivI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::DivI64, result, lhs, rhs);
    }
    void createModI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::ModI64, result, lhs, rhs);
    }
    void createNegI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> val) {
        createUnaryOpWithResult(HIROpcode::NegI64, result, val);
    }

    //==========================================================================
    // Bitwise operations
    //==========================================================================

    std::shared_ptr<HIRValue> createAndI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::AndI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createOrI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::OrI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createXorI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::XorI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createShlI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::ShlI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createShrI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::ShrI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createUShrI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::UShrI64, lhs, rhs, HIRType::makeInt64());
    }
    std::shared_ptr<HIRValue> createNotI64(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::NotI64, val, HIRType::makeInt64());
    }

    // Overloads with pre-created result
    void createAndI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::AndI64, result, lhs, rhs);
    }
    void createOrI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::OrI64, result, lhs, rhs);
    }
    void createXorI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::XorI64, result, lhs, rhs);
    }
    void createShlI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::ShlI64, result, lhs, rhs);
    }
    void createShrI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::ShrI64, result, lhs, rhs);
    }
    void createUShrI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::UShrI64, result, lhs, rhs);
    }
    void createNotI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> val) {
        createUnaryOpWithResult(HIROpcode::NotI64, result, val);
    }

    //==========================================================================
    // Float arithmetic
    //==========================================================================

    std::shared_ptr<HIRValue> createAddF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::AddF64, lhs, rhs, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createSubF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::SubF64, lhs, rhs, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createMulF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::MulF64, lhs, rhs, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createDivF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::DivF64, lhs, rhs, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createModF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::ModF64, lhs, rhs, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createNegF64(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::NegF64, val, HIRType::makeFloat64());
    }

    // Overloads with pre-created result
    void createAddF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::AddF64, result, lhs, rhs);
    }
    void createSubF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::SubF64, result, lhs, rhs);
    }
    void createMulF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::MulF64, result, lhs, rhs);
    }
    void createDivF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::DivF64, result, lhs, rhs);
    }
    void createModF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::ModF64, result, lhs, rhs);
    }
    void createNegF64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> val) {
        createUnaryOpWithResult(HIROpcode::NegF64, result, val);
    }

    //==========================================================================
    // String operations
    //==========================================================================

    std::shared_ptr<HIRValue> createStringConcat(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::StringConcat, lhs, rhs, HIRType::makeString());
    }

    //==========================================================================
    // Checked arithmetic (overflow → branch)
    //==========================================================================

    std::shared_ptr<HIRValue> createAddI64Checked(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs,
                                                   HIRBlock* overflowBlock) {
        auto result = createValue(HIRType::makeInt64());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::AddI64Checked);
        inst->result = result;
        inst->operands.push_back(lhs);
        inst->operands.push_back(rhs);
        inst->operands.push_back(overflowBlock);
        emit(std::move(inst));
        return result;
    }

    //==========================================================================
    // Comparisons
    //==========================================================================

    std::shared_ptr<HIRValue> createCmpEqI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpEqI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpNeI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpNeI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpLtI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpLtI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpLeI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpLeI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpGtI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpGtI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpGeI64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpGeI64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpEqF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpEqF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpNeF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpNeF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpLtF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpLtF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpLeF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpLeF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpGtF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpGtF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpGeF64(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpGeF64, lhs, rhs, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createCmpEqPtr(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::CmpEqPtr, lhs, rhs, HIRType::makeBool());
    }

    // Overloads with pre-created result
    void createCmpEqI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpEqI64, result, lhs, rhs);
    }
    void createCmpNeI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpNeI64, result, lhs, rhs);
    }
    void createCmpLtI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpLtI64, result, lhs, rhs);
    }
    void createCmpLeI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpLeI64, result, lhs, rhs);
    }
    void createCmpGtI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpGtI64, result, lhs, rhs);
    }
    void createCmpGeI64(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::CmpGeI64, result, lhs, rhs);
    }

    //==========================================================================
    // Logical operations
    //==========================================================================

    std::shared_ptr<HIRValue> createLogicalAnd(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::LogicalAnd, lhs, rhs, HIRType::makeBool());
    }
    std::shared_ptr<HIRValue> createLogicalOr(std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        return createBinaryOp(HIROpcode::LogicalOr, lhs, rhs, HIRType::makeBool());
    }
    std::shared_ptr<HIRValue> createLogicalNot(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::LogicalNot, val, HIRType::makeBool());
    }

    // Overloads with pre-created result
    void createLogicalAnd(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::LogicalAnd, result, lhs, rhs);
    }
    void createLogicalOr(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        createBinaryOpWithResult(HIROpcode::LogicalOr, result, lhs, rhs);
    }
    void createLogicalNot(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> val) {
        createUnaryOpWithResult(HIROpcode::LogicalNot, result, val);
    }

    //==========================================================================
    // Type operations
    //==========================================================================

    std::shared_ptr<HIRValue> createTypeOf(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::TypeOf, val, HIRType::makeString());
    }

    void createTypeOf(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> val) {
        createUnaryOpWithResult(HIROpcode::TypeOf, result, val);
    }

    //==========================================================================
    // Type conversions
    //==========================================================================

    std::shared_ptr<HIRValue> createCastI64ToF64(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::CastI64ToF64, val, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createCastF64ToI64(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::CastF64ToI64, val, HIRType::makeInt64());
    }

    //==========================================================================
    // Boxing/Unboxing
    //==========================================================================

    std::shared_ptr<HIRValue> createBoxInt(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::BoxInt, val, HIRType::makeAny());
    }

    std::shared_ptr<HIRValue> createBoxFloat(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::BoxFloat, val, HIRType::makeAny());
    }

    std::shared_ptr<HIRValue> createBoxBool(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::BoxBool, val, HIRType::makeAny());
    }

    std::shared_ptr<HIRValue> createBoxString(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::BoxString, val, HIRType::makeAny());
    }

    std::shared_ptr<HIRValue> createBoxObject(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::BoxObject, val, HIRType::makeAny());
    }

    std::shared_ptr<HIRValue> createUnboxInt(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::UnboxInt, val, HIRType::makeInt64());
    }

    std::shared_ptr<HIRValue> createUnboxFloat(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::UnboxFloat, val, HIRType::makeFloat64());
    }

    std::shared_ptr<HIRValue> createUnboxBool(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::UnboxBool, val, HIRType::makeBool());
    }

    std::shared_ptr<HIRValue> createUnboxString(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::UnboxString, val, HIRType::makeString());
    }

    std::shared_ptr<HIRValue> createUnboxObject(std::shared_ptr<HIRValue> val) {
        return createUnaryOp(HIROpcode::UnboxObject, val, HIRType::makeObject());
    }

    //==========================================================================
    // GC Operations
    //==========================================================================

    std::shared_ptr<HIRValue> createGCAlloc(std::shared_ptr<HIRType> type, std::shared_ptr<HIRValue> size) {
        auto result = createValue(HIRType::makePtr());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GCAlloc);
        inst->result = result;
        inst->operands.push_back(type);
        inst->operands.push_back(size);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createGCAllocArray(std::shared_ptr<HIRType> elemType, std::shared_ptr<HIRValue> len) {
        auto result = createValue(HIRType::makeArray(elemType));
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GCAllocArray);
        inst->result = result;
        inst->operands.push_back(elemType);
        inst->operands.push_back(len);
        emit(std::move(inst));
        return result;
    }

    void createGCStore(std::shared_ptr<HIRValue> ptr, std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GCStore);
        inst->operands.push_back(ptr);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    std::shared_ptr<HIRValue> createGCLoad(std::shared_ptr<HIRType> type, std::shared_ptr<HIRValue> ptr) {
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GCLoad);
        inst->result = result;
        inst->operands.push_back(type);
        inst->operands.push_back(ptr);
        emit(std::move(inst));
        return result;
    }

    void createSafepoint() {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Safepoint);
        emit(std::move(inst));
    }

    void createSafepointPoll() {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::SafepointPoll);
        emit(std::move(inst));
    }

    //==========================================================================
    // Memory Operations (for local variables)
    //==========================================================================

    std::shared_ptr<HIRValue> createAlloca(std::shared_ptr<HIRType> type, const std::string& name = "") {
        auto result = createValue(HIRType::makePtr(), name);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Alloca);
        inst->result = result;
        inst->operands.push_back(type);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createLoad(std::shared_ptr<HIRType> type, std::shared_ptr<HIRValue> ptr) {
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Load);
        inst->result = result;
        inst->operands.push_back(type);
        inst->operands.push_back(ptr);
        emit(std::move(inst));
        return result;
    }

    void createStore(std::shared_ptr<HIRValue> val, std::shared_ptr<HIRValue> ptr,
                     std::shared_ptr<HIRType> elementType = nullptr) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Store);
        inst->operands.push_back(val);
        inst->operands.push_back(ptr);
        // Store element type for type coercion during LLVM lowering
        if (elementType) {
            inst->operands.push_back(elementType);
        }
        emit(std::move(inst));
    }

    //==========================================================================
    // Objects
    //==========================================================================

    std::shared_ptr<HIRValue> createNewObject(HIRShape* shape) {
        auto result = createValue(HIRType::makeClass(shape->className, shape->id));
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewObject);
        inst->result = result;
        inst->operands.push_back(shape->className);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createNewObjectDynamic() {
        auto result = createValue(HIRType::makeObject());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewObjectDynamic);
        inst->result = result;
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createGetPropStatic(std::shared_ptr<HIRValue> obj, const std::string& name,
                                                   std::shared_ptr<HIRType> type) {
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GetPropStatic);
        inst->result = result;
        inst->operands.push_back(obj);
        inst->operands.push_back(name);
        inst->operands.push_back(type);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createGetPropDynamic(std::shared_ptr<HIRValue> obj, std::shared_ptr<HIRValue> key) {
        auto result = createValue(HIRType::makeAny());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GetPropDynamic);
        inst->result = result;
        inst->operands.push_back(obj);
        inst->operands.push_back(key);
        emit(std::move(inst));
        return result;
    }

    void createSetPropStatic(std::shared_ptr<HIRValue> obj, const std::string& name, std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::SetPropStatic);
        inst->operands.push_back(obj);
        inst->operands.push_back(name);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    void createSetPropDynamic(std::shared_ptr<HIRValue> obj, std::shared_ptr<HIRValue> key, std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::SetPropDynamic);
        inst->operands.push_back(obj);
        inst->operands.push_back(key);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    // Overloads with pre-created result or simpler interface
    void createNewObject(std::shared_ptr<HIRValue> result, const std::string& className) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewObject);
        inst->result = result;
        inst->operands.push_back(className);
        emit(std::move(inst));
    }

    void createGetPropStatic(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> obj, const std::string& name) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GetPropStatic);
        inst->result = result;
        inst->operands.push_back(obj);
        inst->operands.push_back(name);
        emit(std::move(inst));
    }

    //==========================================================================
    // Arrays
    //==========================================================================

    std::shared_ptr<HIRValue> createNewArrayBoxed(std::shared_ptr<HIRValue> len,
                                                    std::shared_ptr<HIRType> elemType = nullptr) {
        if (!elemType) elemType = HIRType::makeAny();
        auto result = createValue(HIRType::makeArray(elemType, false));
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewArrayBoxed);
        inst->result = result;
        inst->operands.push_back(len);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createNewArrayTyped(std::shared_ptr<HIRType> elemType, std::shared_ptr<HIRValue> len) {
        auto result = createValue(HIRType::makeArray(elemType, true));
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewArrayTyped);
        inst->result = result;
        inst->operands.push_back(elemType);
        inst->operands.push_back(len);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createGetElem(std::shared_ptr<HIRValue> arr, std::shared_ptr<HIRValue> idx,
                                             std::shared_ptr<HIRType> elemType = nullptr) {
        // If elemType not specified, try to infer from array type
        if (!elemType) {
            if (arr->type && arr->type->kind == HIRTypeKind::Array && arr->type->elementType) {
                elemType = arr->type->elementType;
            } else {
                elemType = HIRType::makeAny();
            }
        }
        auto result = createValue(elemType);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GetElem);
        inst->result = result;
        inst->operands.push_back(arr);
        inst->operands.push_back(idx);
        emit(std::move(inst));
        return result;
    }

    void createSetElem(std::shared_ptr<HIRValue> arr, std::shared_ptr<HIRValue> idx, std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::SetElem);
        inst->operands.push_back(arr);
        inst->operands.push_back(idx);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    std::shared_ptr<HIRValue> createArrayLength(std::shared_ptr<HIRValue> arr) {
        auto result = createValue(HIRType::makeInt64());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ArrayLength);
        inst->result = result;
        inst->operands.push_back(arr);
        emit(std::move(inst));
        return result;
    }

    // Overloads with pre-created result
    void createNewArrayBoxed(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> len) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::NewArrayBoxed);
        inst->result = result;
        inst->operands.push_back(len);
        emit(std::move(inst));
    }

    void createGetElem(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> arr, std::shared_ptr<HIRValue> idx) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::GetElem);
        inst->result = result;
        inst->operands.push_back(arr);
        inst->operands.push_back(idx);
        emit(std::move(inst));
    }

    //==========================================================================
    // Calls
    //==========================================================================

    std::shared_ptr<HIRValue> createCall(const std::string& funcName,
                                          const std::vector<std::shared_ptr<HIRValue>>& args,
                                          std::shared_ptr<HIRType> returnType) {
        std::shared_ptr<HIRValue> result = nullptr;
        if (returnType->kind != HIRTypeKind::Void) {
            result = createValue(returnType);
        }
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Call);
        inst->result = result;
        inst->operands.push_back(funcName);
        for (auto& arg : args) {
            inst->operands.push_back(arg);
        }
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createCallMethod(std::shared_ptr<HIRValue> obj,
                                                const std::string& methodName,
                                                const std::vector<std::shared_ptr<HIRValue>>& args,
                                                std::shared_ptr<HIRType> returnType) {
        std::shared_ptr<HIRValue> result = nullptr;
        if (returnType->kind != HIRTypeKind::Void) {
            result = createValue(returnType);
        }
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::CallMethod);
        inst->result = result;
        inst->operands.push_back(obj);
        inst->operands.push_back(methodName);
        for (auto& arg : args) {
            inst->operands.push_back(arg);
        }
        emit(std::move(inst));
        return result;
    }

    // Overloads with pre-created result
    void createCall(std::shared_ptr<HIRValue> result, const std::string& funcName,
                    const std::vector<std::shared_ptr<HIRValue>>& args) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Call);
        inst->result = result;
        inst->operands.push_back(funcName);
        for (auto& arg : args) {
            inst->operands.push_back(arg);
        }
        emit(std::move(inst));
    }

    void createCallMethod(std::shared_ptr<HIRValue> result, std::shared_ptr<HIRValue> obj,
                          const std::string& methodName,
                          const std::vector<std::shared_ptr<HIRValue>>& args) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::CallMethod);
        inst->result = result;
        inst->operands.push_back(obj);
        inst->operands.push_back(methodName);
        for (auto& arg : args) {
            inst->operands.push_back(arg);
        }
        emit(std::move(inst));
    }

    // Indirect call through function pointer (for closures and callbacks)
    std::shared_ptr<HIRValue> createCallIndirect(std::shared_ptr<HIRValue> funcPtr,
                                                  const std::vector<std::shared_ptr<HIRValue>>& args,
                                                  std::shared_ptr<HIRType> returnType) {
        std::shared_ptr<HIRValue> result = nullptr;
        if (returnType->kind != HIRTypeKind::Void) {
            result = createValue(returnType);
        }
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::CallIndirect);
        inst->result = result;
        inst->operands.push_back(funcPtr);
        for (auto& arg : args) {
            inst->operands.push_back(arg);
        }
        emit(std::move(inst));
        return result;
    }

    //==========================================================================
    // Globals
    //==========================================================================

    // Load a global object (console, Math, JSON, Object, Array, etc.)
    std::shared_ptr<HIRValue> createLoadGlobal(const std::string& name) {
        auto result = createValue(HIRType::makeObject());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::LoadGlobal);
        inst->result = result;
        inst->operands.push_back(name);
        emit(std::move(inst));
        return result;
    }

    // Load a function pointer by name
    std::shared_ptr<HIRValue> createLoadFunction(const std::string& name,
                                                   std::shared_ptr<HIRType> funcType = nullptr) {
        auto type = funcType ? funcType : HIRType::makePtr();
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::LoadFunction);
        inst->result = result;
        inst->operands.push_back(name);
        emit(std::move(inst));
        return result;
    }

    //==========================================================================
    // Closures
    //==========================================================================

    // Create a closure object from a function and captured values
    // Returns a closure object pointer that can be called indirectly
    // funcType: The function type (with return type info) - used for CallIndirect type inference
    std::shared_ptr<HIRValue> createMakeClosure(const std::string& funcName,
                                                 const std::vector<std::shared_ptr<HIRValue>>& captures,
                                                 std::shared_ptr<HIRType> funcType = nullptr) {
        // Use function type if provided, otherwise fall back to generic pointer
        auto result = createValue(funcType ? funcType : HIRType::makePtr());
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::MakeClosure);
        inst->result = result;
        inst->operands.push_back(funcName);
        for (auto& cap : captures) {
            inst->operands.push_back(cap);
        }
        emit(std::move(inst));
        return result;
    }

    // Load a captured variable from the closure environment
    // The variable name must match one of the function's captures
    std::shared_ptr<HIRValue> createLoadCapture(const std::string& varName,
                                                  std::shared_ptr<HIRType> type) {
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::LoadCapture);
        inst->result = result;
        inst->operands.push_back(varName);
        emit(std::move(inst));
        return result;
    }

    // Store to a captured variable in the closure environment
    void createStoreCapture(const std::string& varName, std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::StoreCapture);
        inst->operands.push_back(varName);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    //==========================================================================
    // Control flow (terminators)
    //==========================================================================

    void createBranch(HIRBlock* target) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Branch);
        inst->operands.push_back(target);
        emit(std::move(inst));
        addEdge(currentBlock_, target);
    }

    void createCondBranch(std::shared_ptr<HIRValue> cond, HIRBlock* thenBlock, HIRBlock* elseBlock) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::CondBranch);
        inst->operands.push_back(cond);
        inst->operands.push_back(thenBlock);
        inst->operands.push_back(elseBlock);
        emit(std::move(inst));
        addEdge(currentBlock_, thenBlock);
        addEdge(currentBlock_, elseBlock);
    }

    void createReturn(std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Return);
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    void createReturnVoid() {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::ReturnVoid);
        emit(std::move(inst));
    }

    void createUnreachable() {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Unreachable);
        emit(std::move(inst));
    }

    void createSwitch(std::shared_ptr<HIRValue> val, HIRBlock* defaultBlock,
                      const std::vector<std::pair<int64_t, HIRBlock*>>& cases) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Switch);
        inst->operands.push_back(val);
        inst->switchDefault = defaultBlock;
        inst->switchCases = cases;
        emit(std::move(inst));
        addEdge(currentBlock_, defaultBlock);
        for (auto& [_, block] : cases) {
            addEdge(currentBlock_, block);
        }
    }

    //==========================================================================
    // Phi nodes
    //==========================================================================

    std::shared_ptr<HIRValue> createPhi(std::shared_ptr<HIRType> type,
                                         const std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>>& incoming) {
        auto result = createValue(type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Phi);
        inst->result = result;
        inst->phiIncoming = incoming;
        emit(std::move(inst));
        return result;
    }

    // Overload with pre-created result
    void createPhi(std::shared_ptr<HIRValue> result,
                   const std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>>& incoming) {
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Phi);
        inst->result = result;
        inst->phiIncoming = incoming;
        emit(std::move(inst));
    }

    //==========================================================================
    // Select (ternary)
    //==========================================================================

    std::shared_ptr<HIRValue> createSelect(std::shared_ptr<HIRValue> cond,
                                            std::shared_ptr<HIRValue> trueVal,
                                            std::shared_ptr<HIRValue> falseVal) {
        auto result = createValue(trueVal->type);
        auto inst = std::make_unique<HIRInstruction>(HIROpcode::Select);
        inst->result = result;
        inst->operands.push_back(cond);
        inst->operands.push_back(trueVal);
        inst->operands.push_back(falseVal);
        emit(std::move(inst));
        return result;
    }

private:
    HIRModule* module_;
    HIRFunction* currentFunction_ = nullptr;
    HIRBlock* currentBlock_ = nullptr;

    void emit(std::unique_ptr<HIRInstruction> inst) {
        if (currentBlock_) {
            currentBlock_->addInstruction(std::move(inst));
        }
    }

    std::shared_ptr<HIRValue> createBinaryOp(HIROpcode op, std::shared_ptr<HIRValue> lhs,
                                              std::shared_ptr<HIRValue> rhs, std::shared_ptr<HIRType> resultType) {
        auto result = createValue(resultType);
        auto inst = std::make_unique<HIRInstruction>(op);
        inst->result = result;
        inst->operands.push_back(lhs);
        inst->operands.push_back(rhs);
        emit(std::move(inst));
        return result;
    }

    std::shared_ptr<HIRValue> createUnaryOp(HIROpcode op, std::shared_ptr<HIRValue> val,
                                             std::shared_ptr<HIRType> resultType) {
        auto result = createValue(resultType);
        auto inst = std::make_unique<HIRInstruction>(op);
        inst->result = result;
        inst->operands.push_back(val);
        emit(std::move(inst));
        return result;
    }

    void createBinaryOpWithResult(HIROpcode op, std::shared_ptr<HIRValue> result,
                                   std::shared_ptr<HIRValue> lhs, std::shared_ptr<HIRValue> rhs) {
        auto inst = std::make_unique<HIRInstruction>(op);
        inst->result = result;
        inst->operands.push_back(lhs);
        inst->operands.push_back(rhs);
        emit(std::move(inst));
    }

    void createUnaryOpWithResult(HIROpcode op, std::shared_ptr<HIRValue> result,
                                  std::shared_ptr<HIRValue> val) {
        auto inst = std::make_unique<HIRInstruction>(op);
        inst->result = result;
        inst->operands.push_back(val);
        emit(std::move(inst));
    }

    void addEdge(HIRBlock* from, HIRBlock* to) {
        if (from && to) {
            from->successors.push_back(to);
            to->predecessors.push_back(from);
        }
    }
};

} // namespace ts::hir
