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
    }
    symbols.define(node->name, type);
}

void Analyzer::visitExpressionStatement(ExpressionStatement* node) {
    visit(node->expression.get());
}

void Analyzer::visitCallExpression(CallExpression* node) {
    visit(node->callee.get());
    for (auto& arg : node->arguments) {
        visit(arg.get());
    }
}

void Analyzer::visitPropertyAccessExpression(PropertyAccessExpression* node) {
    visit(node->expression.get());
}

void Analyzer::visitBinaryExpression(BinaryExpression* node) {
    visit(node->left.get());
    visit(node->right.get());
}

void Analyzer::visitIdentifier(Identifier* node) {
    auto symbol = symbols.lookup(node->name);
    if (!symbol) {
        std::cerr << "Warning: Undefined symbol " << node->name << std::endl;
    }
}

void Analyzer::visitStringLiteral(StringLiteral* node) {
}

void Analyzer::visitNumericLiteral(NumericLiteral* node) {
}

} // namespace ts
