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
    node->inferredType = lastType;
}

void Analyzer::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Boolean);
    node->inferredType = lastType;
}

void Analyzer::visitNullLiteral(ast::NullLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Null);
    node->inferredType = lastType;
}

void Analyzer::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Undefined);
    node->inferredType = lastType;
}

void Analyzer::visitStringLiteral(ast::StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
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
    node->inferredType = lastType;
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
    node->inferredType = lastType;
}

void Analyzer::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    visit(node->tag.get());
    
    std::vector<std::shared_ptr<Type>> argTypes;
    argTypes.push_back(std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String))); // strings

    if (auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get())) {
        for (auto& span : templateExpr->spans) {
            visit(span.expression.get());
            argTypes.push_back(span.expression->inferredType);
        }
    } else {
        visit(node->templateExpr.get());
        argTypes.push_back(node->templateExpr->inferredType);
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->tag.get())) {
        functionUsages[id->name].push_back({argTypes, {}});
    }

    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
}

} // namespace ts


