#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>

namespace ts {
using namespace ast;

void Analyzer::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    visit(node->expression.get());
    node->inferredType = lastType;
}

void Analyzer::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    visit(node->operand.get());
    auto operandType = lastType;
    if (node->op == "!") {
        lastType = std::make_shared<Type>(TypeKind::Boolean);
    } else if (node->op == "~") {
        // Bitwise NOT: BigInt stays BigInt, otherwise Int
        if (operandType && operandType->kind == TypeKind::BigInt) {
            lastType = std::make_shared<Type>(TypeKind::BigInt);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Int);
        }
    } else if (node->op == "typeof") {
        lastType = std::make_shared<Type>(TypeKind::String);
    } else if (node->op == "-" || node->op == "+") {
        // Unary minus/plus: preserve BigInt, otherwise follow numeric rules
        if (operandType && operandType->kind == TypeKind::BigInt) {
            lastType = std::make_shared<Type>(TypeKind::BigInt);
        } else if (operandType && operandType->kind == TypeKind::Int) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Double);
        }
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
    node->inferredType = lastType;
}

void Analyzer::visitDeleteExpression(ast::DeleteExpression* node) {
    visit(node->expression.get());
    lastType = std::make_shared<Type>(TypeKind::Boolean);
}

void Analyzer::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    visit(node->operand.get());
    if (lastType && lastType->kind == TypeKind::Int) {
        // Keep as Int
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
}

void Analyzer::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    lastType = parseType(node->type, symbols);
    node->inferredType = lastType;
}

} // namespace ts
