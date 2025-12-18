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

    int getErrorCount() const { return errorCount; }

    // Analyze a function body with specific argument types to determine return type
    std::shared_ptr<Type> analyzeFunctionBody(ast::FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes);

    void reportError(const std::string& message);

    std::shared_ptr<Type> parseType(const std::string& typeName, SymbolTable& symbols);
    std::shared_ptr<Type> resolveType(const std::string& typeName);

private:
    SymbolTable symbols;
    std::shared_ptr<Type> lastType; // Result of the last visited expression
    std::shared_ptr<Type> currentReturnType; // Inferred return type of the current function
    std::map<std::string, std::vector<std::vector<std::shared_ptr<Type>>>> functionUsages;
    int errorCount = 0;

    void visit(ast::Node* node);
    void visitProgram(ast::Program* node);
    void visitClassDeclaration(ast::ClassDeclaration* node);
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node);
    void visitFunctionDeclaration(ast::FunctionDeclaration* node);
    void visitVariableDeclaration(ast::VariableDeclaration* node);
    void visitExpressionStatement(ast::ExpressionStatement* node);
    void visitReturnStatement(ast::ReturnStatement* node);
    void visitCallExpression(ast::CallExpression* node);
    void visitNewExpression(ast::NewExpression* node);
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node);
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node);
    void visitElementAccessExpression(ast::ElementAccessExpression* node);
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node);
    void visitAsExpression(ast::AsExpression* node);
    void visitBinaryExpression(ast::BinaryExpression* node);
    void visitAssignmentExpression(ast::AssignmentExpression* node);
    void visitIfStatement(ast::IfStatement* node);
    void visitWhileStatement(ast::WhileStatement* node);
    void visitForStatement(ast::ForStatement* node);
    void visitForOfStatement(ast::ForOfStatement* node);
    void visitSwitchStatement(ast::SwitchStatement* node);
    void visitTryStatement(ast::TryStatement* node);
    void visitThrowStatement(ast::ThrowStatement* node);
    void visitBreakStatement(ast::BreakStatement* node);
    void visitContinueStatement(ast::ContinueStatement* node);
    void visitTemplateExpression(ast::TemplateExpression* node);
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node);
    void visitSuperExpression(ast::SuperExpression* node);
    void visitBlockStatement(ast::BlockStatement* node);
    void visitIdentifier(ast::Identifier* node);
    void visitStringLiteral(ast::StringLiteral* node);
    void visitNumericLiteral(ast::NumericLiteral* node);
    void visitBooleanLiteral(ast::BooleanLiteral* node);
    void visitArrowFunction(ast::ArrowFunction* node);
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node);
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node);
    void visitBindingElement(ast::BindingElement* node);
    void visitSpreadElement(ast::SpreadElement* node);
    void visitOmittedExpression(ast::OmittedExpression* node);

    void declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type);

    std::shared_ptr<FunctionType> resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes);

    void visitMethodDefinition(ast::MethodDefinition* node, std::shared_ptr<ClassType> classType);
    void visitPropertyDefinition(ast::PropertyDefinition* node, std::shared_ptr<ClassType> classType);

    void checkInterfaceImplementation(std::shared_ptr<ClassType> classType, std::shared_ptr<InterfaceType> interfaceType);

    std::shared_ptr<ClassType> currentClass;
    std::string currentMethodName;
};

} // namespace ts
