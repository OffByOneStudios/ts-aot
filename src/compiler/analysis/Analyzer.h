#pragma once

#include <memory>
#include <map>
#include <vector>
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

    // Get usage information (Function Name -> List of Argument Type Lists)
    const std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>>& getFunctionUsages() const { return functionUsages; }

    // Analyze a function body with specific argument types to determine return type
    std::shared_ptr<Type> analyzeFunctionBody(ast::FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes);

private:
    SymbolTable symbols;
    std::shared_ptr<Type> lastType; // Result of the last visited expression
    std::shared_ptr<Type> currentReturnType; // Inferred return type of the current function
    std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>> functionUsages;

    void visit(ast::Node* node);
    void visitProgram(ast::Program* node);
    void visitFunctionDeclaration(ast::FunctionDeclaration* node);
    void visitVariableDeclaration(ast::VariableDeclaration* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);
    void visitReturnStatement(ast::ReturnStatement* node);
    void visitCallExpression(ast::CallExpression* node);
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node);
    void visitElementAccessExpression(ast::ElementAccessExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitAssignmentExpression(ast::AssignmentExpression* node);
    void visitIfStatement(ast::IfStatement* node);
    void visitWhileStatement(ast::WhileStatement* node);
    void visitForStatement(ast::ForStatement* node);
    void visitBreakStatement(ast::BreakStatement* node);
    void visitContinueStatement(ast::ContinueStatement* node);
    void visitBlockStatement(ast::BlockStatement* node);
    void visitIdentifier(ast::Identifier* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
};

} // namespace ts
