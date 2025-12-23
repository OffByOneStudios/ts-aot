#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>

namespace ts {
using namespace ast;
void Analyzer::visitAwaitExpression(ast::AwaitExpression* node) {
    visit(node->expression.get());
    auto type = lastType;
    if (type->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(type);
        if (cls->name == "Promise" && !cls->typeArguments.empty()) {
            lastType = cls->typeArguments[0];
            return;
        }
    }
    // If it's not a promise, await just returns the value (simplified)
    lastType = type;
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

} // namespace ts


