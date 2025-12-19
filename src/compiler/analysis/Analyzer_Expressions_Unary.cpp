#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>

namespace ts {
using namespace ast;
void Analyzer::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    visit(node->operand.get());
}

void Analyzer::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    visit(node->operand.get());
}

void Analyzer::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    lastType = parseType(node->type, symbols);
    node->inferredType = lastType;
}

std::vector<std::shared_ptr<Type>> Analyzer::inferTypeArguments(
    const std::vector<std::shared_ptr<TypeParameterType>>& typeParams,
    const std::vector<std::shared_ptr<Type>>& paramTypes,
    const std::vector<std::shared_ptr<Type>>& argTypes) {
    
    std::map<std::string, std::shared_ptr<Type>> inferred;
    
    auto inferFromTypes = [&](auto self, std::shared_ptr<Type> paramType, std::shared_ptr<Type> argType) -> void {
        if (!paramType || !argType) return;

        // printf("Inferring from param: %s and arg: %s\n", paramType->toString().c_str(), argType->toString().c_str());

        if (paramType->kind == TypeKind::TypeParameter) {
            auto tp = std::static_pointer_cast<TypeParameterType>(paramType);
            bool target = false;
            for (auto& p : typeParams) {
                if (p->name == tp->name) { target = true; break; }
            }
            if (target) {
                if (inferred.find(tp->name) == inferred.end()) {
                    inferred[tp->name] = argType;
                }
            }
        } else if (paramType->kind == TypeKind::Array && argType->kind == TypeKind::Array) {
            auto pa = std::static_pointer_cast<ArrayType>(paramType);
            auto aa = std::static_pointer_cast<ArrayType>(argType);
            self(self, pa->elementType, aa->elementType);
        }
    };

    size_t count = std::min(paramTypes.size(), argTypes.size());
    for (size_t i = 0; i < count; ++i) {
        inferFromTypes(inferFromTypes, paramTypes[i], argTypes[i]);
    }

    std::vector<std::shared_ptr<Type>> result;
    for (auto& tp : typeParams) {
        if (inferred.count(tp->name)) {
            result.push_back(inferred[tp->name]);
        } else {
            // Fallback to Any or constraint if not inferred
            result.push_back(tp->constraint ? tp->constraint : std::make_shared<Type>(TypeKind::Any));
        }
    }
    return result;
}

} // namespace ts


