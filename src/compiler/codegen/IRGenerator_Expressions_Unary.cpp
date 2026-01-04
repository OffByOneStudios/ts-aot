#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

void IRGenerator::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    visit(node->expression.get());
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    if (node->op == "typeof") {
        visit(node->operand.get());
        llvm::Value* val = lastValue;
        
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createFn = getRuntimeFunction("ts_string_create", createFt);

        if (val->getType()->isDoubleTy() || val->getType()->isIntegerTy(64)) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("number");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }
        
        if (val->getType()->isIntegerTy(1)) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("boolean");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }

        // If statically typed as Function, return "function" directly
        if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::Function) {
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("function");
            lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
            return;
        }

        if (val->getType()->isPointerTy()) {
            // If it's 'any' type, use the slow path ts_value_typeof
            if (node->operand->inferredType && node->operand->inferredType->kind == TypeKind::Any) {
                llvm::FunctionType* typeofFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee typeofFn = getRuntimeFunction("ts_value_typeof", typeofFt);
                lastValue = createCall(typeofFt, typeofFn.getCallee(), {val});
                return;
            }

            llvm::FunctionType* typeofFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee typeofFn = getRuntimeFunction("ts_typeof", typeofFt);
            lastValue = createCall(typeofFt, typeofFn.getCallee(), {val});
            return;
        }

        llvm::Value* strPtr = builder->CreateGlobalStringPtr("unknown");
        lastValue = createCall(createFt, createFn.getCallee(), {strPtr});
        return;
    }

    visit(node->operand.get());
    llvm::Value* operandV = lastValue;

    if (node->op == "++" || node->op == "--") {
        // Prefix increment/decrement: increment first, then return new value
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }
            if (variable) {
                llvm::Value* newVal = nullptr;
                if (operandV->getType()->isPointerTy()) {
                    // Boxed 'any' / runtime value: use runtime arithmetic
                    llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
                    llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
                    llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

                    const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
                    llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
                    newVal = createCall(arithFt, arithFn.getCallee(), { operandV, oneBoxed });
                } else if (operandV->getType()->isDoubleTy()) {
                    llvm::Value* one = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), 1.0);
                    newVal = (node->op == "++") ? builder->CreateFAdd(operandV, one) : builder->CreateFSub(operandV, one);
                } else if (operandV->getType()->isIntegerTy()) {
                    llvm::Value* one = llvm::ConstantInt::get(operandV->getType(), 1);
                    newVal = (node->op == "++") ? builder->CreateAdd(operandV, one) : builder->CreateSub(operandV, one);
                }

                if (newVal) {
                    // Cast for store if needed
                    llvm::Type* varType = nullptr;
                    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                        varType = alloca->getAllocatedType();
                    } else if (auto gv = llvm::dyn_cast<llvm::GlobalVariable>(variable)) {
                        varType = gv->getValueType();
                    }
                    if (varType && newVal->getType() != varType) {
                        newVal = castValue(newVal, varType);
                    }
                    builder->CreateStore(newVal, variable);
                    lastValue = newVal;  // Prefix: return new value
                    return;
                }
            }
        }
        return;
    }

    if (node->op == "-") {
        if (operandV->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operandV, "negtmp");
        } else if (operandV->getType()->isIntegerTy()) {
            lastValue = builder->CreateNeg(operandV, "negtmp");
        }
    } else if (node->op == "!") {
        operandV = emitToBoolean(operandV, node->operand->inferredType);
        lastValue = builder->CreateNot(operandV, "nottmp");
    } else if (node->op == "~") {
        operandV = toInt32(operandV);
        lastValue = builder->CreateNot(operandV, "bitnot");
        if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
            lastValue = builder->CreateIntCast(lastValue, builder->getInt64Ty(), true);
        } else {
            lastValue = castValue(lastValue, builder->getDoubleTy());
        }
    }
}

void IRGenerator::visitDeleteExpression(ast::DeleteExpression* node) {
    if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        visit(elem->expression.get());
        llvm::Value* obj = boxValue(lastValue, elem->expression->inferredType);
        visit(elem->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, elem->argumentExpression->inferredType);
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_object_delete_prop", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, key });
        return;
    } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        visit(prop->expression.get());
        llvm::Value* obj = boxValue(lastValue, prop->expression->inferredType);
        
        llvm::Value* keyStr = builder->CreateGlobalStringPtr(prop->name);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_object_delete_prop", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, key });
        return;
    }
    lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 1);
}

void IRGenerator::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    SPDLOG_DEBUG("visitPostfixUnaryExpression: {}", node->op);
    visit(node->operand.get());
    llvm::Value* val = lastValue;
    
    if (!val) {
        SPDLOG_ERROR("visitPostfixUnaryExpression: operand did not produce a value");
        return;
    }

    if (node->op == "++" || node->op == "--") {
        llvm::Value* next;
        if (val->getType()->isPointerTy()) {
            // Boxed runtime value: increment/decrement via runtime helpers.
            llvm::FunctionType* makeIntFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getInt64Ty() }, false);
            llvm::FunctionCallee makeIntFn = getRuntimeFunction("ts_value_make_int", makeIntFt);
            llvm::Value* oneBoxed = createCall(makeIntFt, makeIntFn.getCallee(), { llvm::ConstantInt::get(builder->getInt64Ty(), 1) });

            const char* opFnName = (node->op == "++") ? "ts_value_add" : "ts_value_sub";
            llvm::FunctionType* arithFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee arithFn = getRuntimeFunction(opFnName, arithFt);
            next = createCall(arithFt, arithFn.getCallee(), { val, oneBoxed });
        } else if (val->getType()->isDoubleTy()) {
            double delta = (node->op == "++") ? 1.0 : -1.0;
            next = builder->CreateFAdd(val, llvm::ConstantFP::get(*context, llvm::APFloat(delta)), "incdec");
        } else {
            int64_t delta = (node->op == "++") ? 1 : -1;
            next = builder->CreateAdd(val, llvm::ConstantInt::get(val->getType(), delta), "incdec");
        }
        
        // Store back
        if (auto id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
            llvm::Value* variable = nullptr;
            if (namedValues.count(id->name)) {
                variable = namedValues[id->name];
            } else {
                variable = module->getGlobalVariable(id->name);
            }

            if (variable) {
                if (verbose) {
                std::string varTypeStr, nextTypeStr;
                llvm::raw_string_ostream varOs(varTypeStr), nextOs(nextTypeStr);
                variable->getType()->print(varOs);
                next->getType()->print(nextOs);
                SPDLOG_DEBUG("  Storing back to {} type: {} value type: {}", id->name, varTypeStr, nextTypeStr);
            }

                // Ensure types match for store
                llvm::Type* varType = nullptr;
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                    varType = alloca->getAllocatedType();
                } else if (auto gv = llvm::dyn_cast<llvm::GlobalVariable>(variable)) {
                    varType = gv->getValueType();
                }

                if (varType && next->getType() != varType) {
                    std::string nextTypeStr, varTypeStr;
                    llvm::raw_string_ostream nextOs(nextTypeStr), varOs(varTypeStr);
                    next->getType()->print(nextOs);
                    varType->print(varOs);
                    SPDLOG_DEBUG("  Casting for store: {} -> {}", nextTypeStr, varTypeStr);
                    next = castValue(next, varType);
                }
                builder->CreateStore(next, variable);
            }
        }
        lastValue = val; // Postfix returns old value
    }
    SPDLOG_DEBUG("visitPostfixUnaryExpression done");
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    
    if (node->inferredType && node->inferredType->kind == TypeKind::Any) {
        // If casting to Any, we must box the value to preserve its type info
        // especially for null/undefined which are both nullptr in IR
        lastValue = boxValue(lastValue, node->expression->inferredType);
    } else {
        lastValue = castValue(lastValue, getLLVMType(node->inferredType));
    }
}

} // namespace ts


