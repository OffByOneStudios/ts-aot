#include "Analyzer.h"
#include <iostream>

namespace ts {

using namespace ast;

Analyzer::Analyzer() {}

void Analyzer::analyze(Program* program) {
    visitProgram(program);
}

void Analyzer::visit(Node* node) {
    if (!node) return;

    if (auto p = dynamic_cast<Program*>(node)) visitProgram(p);
    else if (auto f = dynamic_cast<FunctionDeclaration*>(node)) visitFunctionDeclaration(f);
    else if (auto v = dynamic_cast<VariableDeclaration*>(node)) visitVariableDeclaration(v);
    else if (auto e = dynamic_cast<ExpressionStatement*>(node)) visitExpressionStatement(e);
    else if (auto c = dynamic_cast<CallExpression*>(node)) visitCallExpression(c);
    else if (auto pa = dynamic_cast<PropertyAccessExpression*>(node)) visitPropertyAccessExpression(pa);
    else if (auto bin = dynamic_cast<BinaryExpression*>(node)) visitBinaryExpression(bin);
    else if (auto id = dynamic_cast<Identifier*>(node)) visitIdentifier(id);
    else if (auto sl = dynamic_cast<StringLiteral*>(node)) visitStringLiteral(sl);
    else if (auto nl = dynamic_cast<NumericLiteral*>(node)) visitNumericLiteral(nl);
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visitFunctionDeclaration(FunctionDeclaration* node) {
    auto funcType = std::make_shared<FunctionType>();
    funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    
    symbols.define(node->name, funcType);

    symbols.enterScope();
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitVariableDeclaration(VariableDeclaration* node) {
    auto type = std::make_shared<Type>(TypeKind::Any);
    if (node->initializer) {
        visit(node->initializer.get());
        if (lastType) {
            type = lastType;
        }
    }
    symbols.define(node->name, type);
}

void Analyzer::visitExpressionStatement(ExpressionStatement* node) {
    visit(node->expression.get());
}

void Analyzer::visitCallExpression(CallExpression* node) {
    visit(node->callee.get());
    
    std::string calleeName;
    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        calleeName = id->name;
    }

    std::vector<std::shared_ptr<Type>> argTypes;
    for (auto& arg : node->arguments) {
        visit(arg.get());
        if (lastType) {
            argTypes.push_back(lastType);
        } else {
            argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    }

    if (!calleeName.empty()) {
        functionUsages[calleeName].push_back(argTypes);
    }
    
    // For now, assume calls return Any unless we know better
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitPropertyAccessExpression(PropertyAccessExpression* node) {
    visit(node->expression.get());
    // TODO: Resolve property type
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitBinaryExpression(BinaryExpression* node) {
    visit(node->left.get());
    auto leftType = lastType;
    visit(node->right.get());
    auto rightType = lastType;

    // Simple type inference for binary ops
    if (leftType && rightType) {
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

void Analyzer::visitIdentifier(Identifier* node) {
    auto symbol = symbols.lookup(node->name);
    if (!symbol) {
        std::cerr << "Warning: Undefined symbol " << node->name << std::endl;
        lastType = std::make_shared<Type>(TypeKind::Any);
    } else {
        lastType = symbol->type;
    }
}

void Analyzer::visitStringLiteral(StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitNumericLiteral(NumericLiteral* node) {
    // Heuristic: if it has a decimal point, it's a double.
    // But for now, let's just say everything is Double unless we add IntLiteral support in AST
    // Actually, let's check if it's an integer
    if (node->value == (int64_t)node->value) {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
}

} // namespace ts
