#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    if (node->op == "typeof") {
        visit(node->operand.get());
        llvm::Value* val = lastValue;
        
        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", createFt);

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
            llvm::FunctionCallee typeofFn = module->getOrInsertFunction("ts_typeof", typeofFt);
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
        if (operandV->getType()->isDoubleTy()) {
            operandV = builder->CreateFCmpONE(operandV, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "tobool");
        } else if (operandV->getType()->isIntegerTy() && operandV->getType()->getIntegerBitWidth() > 1) {
             operandV = builder->CreateICmpNE(operandV, llvm::ConstantInt::get(operandV->getType(), 0), "tobool");
        }
        lastValue = builder->CreateNot(operandV, "nottmp");
    }
}

void IRGenerator::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    visit(node->operand.get());
    llvm::Value* val = lastValue;
    
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
                builder->CreateStore(next, variable);
            }
        }
        lastValue = val; // Postfix returns old value
    }
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    lastValue = castValue(lastValue, getLLVMType(node->inferredType));
}

} // namespace ts


