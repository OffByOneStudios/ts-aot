#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace ast {

// Forward declarations
struct Node;
struct Statement;
struct Expression;

using NodePtr = std::unique_ptr<Node>;
using StmtPtr = std::unique_ptr<Statement>;
using ExprPtr = std::unique_ptr<Expression>;

} // namespace ast

namespace ts { struct Type; }

namespace ast {

struct Node {
    virtual ~Node() = default;
    virtual std::string getKind() const = 0;
};

struct Statement : Node {};
struct Expression : Node {
    std::shared_ptr<ts::Type> inferredType;
};

// --- Statements ---

struct Program : Node {
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "Program"; }
};

struct Parameter : Node {
    std::string name;
    std::string type;
    std::string getKind() const override { return "Parameter"; }
};

struct FunctionDeclaration : Statement {
    std::string name;
    std::vector<std::unique_ptr<Parameter>> parameters;
    std::string returnType;
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "FunctionDeclaration"; }
};

struct VariableDeclaration : Statement {
    std::string name;
    ExprPtr initializer;
    std::string getKind() const override { return "VariableDeclaration"; }
};

struct ExpressionStatement : Statement {
    ExprPtr expression;
    std::string getKind() const override { return "ExpressionStatement"; }
};

struct BlockStatement : Statement {
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "BlockStatement"; }
};

struct ReturnStatement : Statement {
    ExprPtr expression;
    std::string getKind() const override { return "ReturnStatement"; }
};

struct IfStatement : Statement {
    ExprPtr condition;
    StmtPtr thenStatement;
    StmtPtr elseStatement;
    std::string getKind() const override { return "IfStatement"; }
};

struct WhileStatement : Statement {
    ExprPtr condition;
    StmtPtr body;
    std::string getKind() const override { return "WhileStatement"; }
};

struct ForStatement : Statement {
    StmtPtr initializer;
    ExprPtr condition;
    ExprPtr incrementor;
    StmtPtr body;
    std::string getKind() const override { return "ForStatement"; }
};

struct BreakStatement : Statement {
    std::string getKind() const override { return "BreakStatement"; }
};

struct ContinueStatement : Statement {
    std::string getKind() const override { return "ContinueStatement"; }
};

struct CaseClause : Node {
    ExprPtr expression;
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "CaseClause"; }
};

struct DefaultClause : Node {
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "DefaultClause"; }
};

struct SwitchStatement : Statement {
    ExprPtr expression;
    std::vector<NodePtr> clauses;
    std::string getKind() const override { return "SwitchStatement"; }
};

struct ForOfStatement : Statement {
    StmtPtr initializer; // VariableDeclaration
    ExprPtr expression;  // Iterable
    StmtPtr body;
    std::string getKind() const override { return "ForOfStatement"; }
};

// --- Expressions ---

struct BinaryExpression : Expression {
    std::string op;
    ExprPtr left;
    ExprPtr right;
    std::string getKind() const override { return "BinaryExpression"; }
};

struct PrefixUnaryExpression : Expression {
    std::string op;
    ExprPtr operand;
    std::string getKind() const override { return "PrefixUnaryExpression"; }
};

struct AssignmentExpression : Expression {
    ExprPtr left; // Usually Identifier
    ExprPtr right;
    std::string getKind() const override { return "AssignmentExpression"; }
};

struct CallExpression : Expression {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
    std::string getKind() const override { return "CallExpression"; }
};

struct NewExpression : Expression {
    ExprPtr expression;
    std::vector<ExprPtr> arguments;
    std::string getKind() const override { return "NewExpression"; }
};

struct ArrayLiteralExpression : Expression {
    std::vector<ExprPtr> elements;
    std::string getKind() const override { return "ArrayLiteralExpression"; }
};

struct ElementAccessExpression : Expression {
    ExprPtr expression;
    ExprPtr argumentExpression;
    std::string getKind() const override { return "ElementAccessExpression"; }
};

struct PropertyAccessExpression : Expression {
    ExprPtr expression;
    std::string name;
    std::string getKind() const override { return "PropertyAccessExpression"; }
};

struct PropertyAssignment : Node {
    std::string name;
    ExprPtr initializer;
    std::string getKind() const override { return "PropertyAssignment"; }
};

struct ObjectLiteralExpression : Expression {
    std::vector<std::unique_ptr<PropertyAssignment>> properties;
    std::string getKind() const override { return "ObjectLiteralExpression"; }
};

struct Identifier : Expression {
    std::string name;
    std::string getKind() const override { return "Identifier"; }
};

struct StringLiteral : Expression {
    std::string value;
    std::string getKind() const override { return "StringLiteral"; }
};

struct NumericLiteral : Expression {
    double value;
    std::string getKind() const override { return "NumericLiteral"; }
};

struct BooleanLiteral : Expression {
    bool value;
    std::string getKind() const override { return "BooleanLiteral"; }
};

struct ArrowFunction : Expression {
    std::vector<std::unique_ptr<Parameter>> parameters;
    NodePtr body; // BlockStatement or Expression
    std::string getKind() const override { return "ArrowFunction"; }
};

struct TemplateSpan {
    ExprPtr expression;
    std::string literal;
};

struct TemplateExpression : Expression {
    std::string head;
    std::vector<TemplateSpan> spans;
    std::string getKind() const override { return "TemplateExpression"; }
};

} // namespace ast
