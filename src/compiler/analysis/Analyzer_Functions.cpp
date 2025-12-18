#include "Analyzer.h"
#include <fmt/core.h>

namespace ts {

using namespace ast;

void Analyzer::visitFunctionDeclaration(FunctionDeclaration* node) {
    auto funcType = std::make_shared<FunctionType>();
    if (!node->returnType.empty()) {
        funcType->returnType = parseType(node->returnType, symbols);
    } else {
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }
    
    for (const auto& param : node->parameters) {
        if (!param->type.empty()) {
            funcType->paramTypes.push_back(parseType(param->type, symbols));
        } else {
            funcType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    }

    symbols.define(node->name, funcType);

    symbols.enterScope();
    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        symbols.define(node->parameters[i]->name, funcType->paramTypes[i]);
    }

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitMethodDefinition(MethodDefinition* node, std::shared_ptr<ClassType> classType) {
    std::string oldMethod = currentMethodName;
    currentMethodName = node->name;

    if (node->name == "constructor" && currentClass) {
        std::vector<StmtPtr> injected;
        for (const auto& param : node->parameters) {
            if (param->isParameterProperty) {
                auto thisExpr = std::make_unique<Identifier>();
                thisExpr->name = "this";
                auto propAccess = std::make_unique<PropertyAccessExpression>();
                propAccess->expression = std::move(thisExpr);
                propAccess->name = param->name;
                
                auto rhs = std::make_unique<Identifier>();
                rhs->name = param->name;
                
                auto assign = std::make_unique<AssignmentExpression>();
                assign->left = std::move(propAccess);
                assign->right = std::move(rhs);
                
                auto stmt = std::make_unique<ExpressionStatement>();
                stmt->expression = std::move(assign);
                injected.push_back(std::move(stmt));
            }
        }
        if (!injected.empty()) {
            node->body.insert(node->body.begin(), 
                std::make_move_iterator(injected.begin()), 
                std::make_move_iterator(injected.end()));
        }
    }

    auto methodType = std::make_shared<FunctionType>();
    if (node->isGetter) {
        methodType->returnType = parseType(node->returnType, symbols);
    } else if (node->isSetter) {
        methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        if (node->parameters.size() > 0) {
            methodType->paramTypes.push_back(parseType(node->parameters[0]->type, symbols));
        } else {
            methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    } else {
        if (!node->returnType.empty()) {
            methodType->returnType = parseType(node->returnType, symbols);
        } else {
            methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        }
        
        for (const auto& param : node->parameters) {
            if (!param->type.empty()) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            } else {
                methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
    }

    symbols.define(node->name, methodType);

    symbols.enterScope();
    // Define 'this' (only for instance methods)
    if (!node->isStatic) {
        symbols.define("this", classType);
    }

    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        symbols.define(node->parameters[i]->name, methodType->paramTypes[i]);
    }

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    symbols.exitScope();
    currentMethodName = oldMethod;
}

std::shared_ptr<Type> Analyzer::analyzeFunctionBody(FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes) {
    // If return type is annotated, use it (unless it's explicit 'any', which we want to refine if possible)
    if (!node->returnType.empty() && node->returnType != "any") {
        return parseType(node->returnType, symbols);
    }

    // Create a new scope for the function body
    symbols.enterScope();
    
    // Bind parameters to the provided types
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        if (i < argTypes.size()) {
            symbols.define(node->parameters[i]->name, argTypes[i]);
        }
    }

    currentReturnType = std::make_shared<Type>(TypeKind::Void); // Default to Void

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    // Debug
    fmt::print("Analyzed function {} return type: {}\n", node->name, currentReturnType->toString());

    symbols.exitScope();
    return currentReturnType;
}

void Analyzer::visitArrowFunction(ast::ArrowFunction* node) {
    // For now, just return a generic function type
    lastType = std::make_shared<FunctionType>();
}

} // namespace ts
