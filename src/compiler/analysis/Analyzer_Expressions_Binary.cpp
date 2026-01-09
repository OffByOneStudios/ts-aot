#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

void Analyzer::visitBinaryExpression(ast::BinaryExpression* node) {
    if (node->op == "??") {
        visit(node->left.get());
        auto leftType = lastType;
        visit(node->right.get());
        auto rightType = lastType;
        
        if (leftType && rightType && leftType->kind == rightType->kind) {
            lastType = leftType;
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
        return;
    }
    if (node->op == "instanceof") {
        visit(node->left.get());
        visit(node->right.get());
        lastType = std::make_shared<Type>(TypeKind::Boolean);
        return;
    }
    if (node->op == "in") {
        visit(node->left.get());
        visit(node->right.get());
        lastType = std::make_shared<Type>(TypeKind::Boolean);
        return;
    }
    visit(node->left.get());
    auto leftType = lastType;
    visit(node->right.get());
    auto rightType = lastType;

    // Simple type inference for binary ops
    if (node->op == "==" || node->op == "===" || node->op == "!=" || node->op == "!==" ||
        node->op == "<" || node->op == "<=" || node->op == ">" || node->op == ">=") {
        lastType = std::make_shared<Type>(TypeKind::Boolean);
    } else if (node->op == "&&" || node->op == "||") {
        // JavaScript && and || return the actual value, not a boolean!
        // The type is the union of both operand types, but we simplify to Any
        // to handle the dynamic nature of these operators correctly.
        lastType = std::make_shared<Type>(TypeKind::Any);
    } else if (node->op == "|" || node->op == "&" || node->op == "^" || node->op == "<<" || node->op == ">>" || node->op == ">>>") {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else if (node->op == "/") {
        // Division ALWAYS produces a double in JavaScript/TypeScript
        // even for integer operands (e.g., 4 / 2 = 2.0, 1 / 0 = Infinity)
        lastType = std::make_shared<Type>(TypeKind::Double);
    } else if (leftType && rightType) {
        if (leftType->kind == TypeKind::Int && rightType->kind == TypeKind::Int) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else if (leftType->isNumber() && rightType->isNumber()) {
            lastType = std::make_shared<Type>(TypeKind::Double);
        } else if (leftType->kind == TypeKind::String || rightType->kind == TypeKind::String) {
            lastType = std::make_shared<Type>(TypeKind::String);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitConditionalExpression(ast::ConditionalExpression* node) {
    visit(node->condition.get());
    visit(node->whenTrue.get());
    auto trueType = lastType;
    visit(node->whenFalse.get());
    auto falseType = lastType;

    if (trueType && falseType && trueType->kind == falseType->kind) {
        lastType = trueType;
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitAssignmentExpression(ast::AssignmentExpression* node) {
    // Strict mode check: cannot assign to eval or arguments
    if (strictMode) {
        if (auto id = dynamic_cast<Identifier*>(node->left.get())) {
            if (id->name == "eval" || id->name == "arguments") {
                reportError("Strict mode: Cannot assign to '" + id->name + "'");
            }
        }
    }

    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->left.get())) {
        visit(prop->expression.get());
        auto objType = lastType;
        if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            auto current = cls;
            while (current) {
                if (current->fields.count(prop->name)) {
                    if (current->readonlyFields.count(prop->name)) {
                        // Only allowed in constructor of the same class
                        if (currentMethodName != "constructor" || currentClass != current) {
                            reportError(fmt::format("Cannot assign to '{}' because it is a read-only property.", prop->name));
                        }
                    }
                    break;
                } else if (current->staticFields.count(prop->name)) {
                    if (current->staticReadonlyFields.count(prop->name)) {
                        reportError(fmt::format("Cannot assign to static '{}' because it is a read-only property.", prop->name));
                    }
                    break;
                }
                current = current->baseClass;
            }
        }
    }

    visit(node->left.get());
    visit(node->right.get());
}

} // namespace ts

