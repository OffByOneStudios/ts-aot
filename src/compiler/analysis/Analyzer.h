#pragma once

#include <memory>
#include "../ast/AstNodes.h"
#include "SymbolTable.h"

namespace ts {

class Analyzer {
public:
    Analyzer();

    // Run the analysis pass on the program
    void analyze(ast::Program* program);

    // Get the symbol table (for later passes)
    const SymbolTable& getSymbolTable() const { return symbols; }

private:
    SymbolTable symbols;

    void visit(ast::Node* node);
    void visitProgram(ast::Program* node);
    void visitFunctionDeclaration(ast::FunctionDeclaration* node);
    void visitVariableDeclaration(ast::VariableDeclaration* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);
    void visitCallExpression(ast::CallExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitIdentifier(ast::Identifier* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
};

} // namespace ts
