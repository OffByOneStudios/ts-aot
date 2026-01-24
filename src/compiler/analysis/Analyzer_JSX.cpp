#include "Analyzer.h"
#include "../ast/AstNodes.h"

namespace ts {
using namespace ast;

// JSX elements are transformed to function calls at codegen time
// For analysis, they produce an 'any' type (or a JSX.Element type if configured)
void Analyzer::visitJsxElement(ast::JsxElement* node) {
    // Visit all attributes
    for (auto& attr : node->attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            if (jsxAttr->initializer) {
                visit(jsxAttr->initializer.get());
            }
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            visit(spreadAttr->expression.get());
        }
    }

    // Visit all children
    for (auto& child : node->children) {
        visit(child.get());
    }

    // JSX elements have type 'any' (could be configured for JSX.Element)
    lastType = std::make_shared<Type>(TypeKind::Any);
    node->inferredType = lastType;
}

void Analyzer::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    // Visit all attributes
    for (auto& attr : node->attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            if (jsxAttr->initializer) {
                visit(jsxAttr->initializer.get());
            }
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            visit(spreadAttr->expression.get());
        }
    }

    // JSX elements have type 'any'
    lastType = std::make_shared<Type>(TypeKind::Any);
    node->inferredType = lastType;
}

void Analyzer::visitJsxFragment(ast::JsxFragment* node) {
    // Visit all children
    for (auto& child : node->children) {
        visit(child.get());
    }

    // JSX fragments have type 'any'
    lastType = std::make_shared<Type>(TypeKind::Any);
    node->inferredType = lastType;
}

void Analyzer::visitJsxExpression(ast::JsxExpression* node) {
    if (node->expression) {
        visit(node->expression.get());
        node->inferredType = lastType;
    } else {
        // Empty expression {} has type undefined
        lastType = std::make_shared<Type>(TypeKind::Undefined);
        node->inferredType = lastType;
    }
}

void Analyzer::visitJsxText(ast::JsxText* node) {
    // JSX text is a string
    lastType = std::make_shared<Type>(TypeKind::String);
    node->inferredType = lastType;
}

} // namespace ts
