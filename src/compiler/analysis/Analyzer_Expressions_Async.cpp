#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#include <iostream>

namespace ts {
using namespace ast;
void Analyzer::visitAwaitExpression(ast::AwaitExpression* node) {
    if (functionDepth == 0 && currentModule) {
        currentModule->isAsync = true;
    }
    visit(node->expression.get());
    auto type = lastType;
    if (!type) {
        type = std::make_shared<Type>(TypeKind::Any);
    }
    if (type->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(type);
        if (cls->name.find("Promise") == 0 && !cls->typeArguments.empty()) {
            lastType = cls->typeArguments[0];
            node->inferredType = lastType;
            return;
        }
    }
    // If it's not a promise, await just returns the value (simplified)
    lastType = type;
    node->inferredType = lastType;
}

void Analyzer::visitYieldExpression(ast::YieldExpression* node) {
    if (node->expression) {
        visit(node->expression.get());
    } else {
        lastType = std::make_shared<Type>(TypeKind::Undefined);
    }
    // yield returns any for now (the value passed to next())
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitDynamicImport(ast::DynamicImport* node) {
    // Visit the module specifier (usually a string literal)
    if (node->moduleSpecifier) {
        visit(node->moduleSpecifier.get());
    }

    // For string literal specifiers, resolve and load the module at analysis time
    // so it gets included in the compilation and its init runs before dynamic import
    if (auto* lit = dynamic_cast<ast::StringLiteral*>(node->moduleSpecifier.get())) {
        auto resolved = resolveModule(lit->value);
        if (resolved.isValid() && resolved.type != ModuleType::Builtin) {
            loadModule(lit->value);
        }
    }

    // Dynamic import returns Promise<any> since we don't know the module's type at compile time
    auto promiseType = std::make_shared<ClassType>("Promise");
    promiseType->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));
    lastType = promiseType;
    node->inferredType = lastType;
}

} // namespace ts


