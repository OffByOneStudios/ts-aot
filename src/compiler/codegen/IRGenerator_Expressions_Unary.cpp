#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;
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

        if (val->getType()->isPointerTy()) {
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
        if (val->getType()->isDoubleTy()) {
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
    lastValue = castValue(lastValue, getLLVMType(node->inferredType));
}

} // namespace ts


