#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>

namespace ts {
using namespace ast;
void Analyzer::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
}

void Analyzer::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Boolean);
}

void Analyzer::visitNullLiteral(ast::NullLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Null);
}

void Analyzer::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Undefined);
}

void Analyzer::visitStringLiteral(ast::StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    lastType = symbols.lookupType("RegExp");
    if (!lastType) {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    std::vector<std::shared_ptr<Type>> elementTypes;
    for (auto& el : node->elements) {
        visit(el.get());
        elementTypes.push_back(lastType ? lastType : std::make_shared<Type>(TypeKind::Any));
    }
    lastType = std::make_shared<TupleType>(elementTypes);
}

void Analyzer::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    auto objType = std::make_shared<ObjectType>();
    for (auto& prop : node->properties) {
        visit(prop.get());
        if (auto pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
            objType->fields[pa->name] = lastType;
        } else if (auto spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
            objType->fields[spa->name] = lastType;
        } else if (auto md = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            objType->fields[md->name] = lastType;
        }
    }
    lastType = objType;
    node->inferredType = objType;
}

void Analyzer::visitPropertyAssignment(ast::PropertyAssignment* node) {
    if (node->nameNode) {
        if (dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
            visit(node->nameNode.get());
        }
    }
    visit(node->initializer.get());
}

void Analyzer::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    auto sym = symbols.lookup(node->name);
    lastType = sym ? sym->type : std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    visit(node->expression.get());
}

void Analyzer::visitTemplateExpression(ast::TemplateExpression* node) {
    for (auto& span : node->spans) {
        visit(span.expression.get());
    }
    lastType = std::make_shared<Type>(TypeKind::String);
}

} // namespace ts


