#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include "AccessModifier.h"

namespace ts {
struct Type;
}

namespace ast {

// Forward declarations
struct Node;
struct Statement;
struct Expression;
struct Program;
struct Parameter;
struct TypeParameter;
struct FunctionDeclaration;
struct VariableDeclaration;
struct BindingElement;
struct ObjectBindingPattern;
struct ArrayBindingPattern;
struct ExpressionStatement;
struct BlockStatement;
struct ReturnStatement;
struct IfStatement;
struct WhileStatement;
struct ForStatement;
struct ForOfStatement;
struct ForInStatement;
struct BreakStatement;
struct ContinueStatement;
struct CaseClause;
struct DefaultClause;
struct SwitchStatement;
struct TryStatement;
struct CatchClause;
struct ThrowStatement;
struct ImportDeclaration;
struct ExportDeclaration;
struct ExportAssignment;
struct BinaryExpression;
struct AssignmentExpression;
struct CallExpression;
struct NewExpression;
struct SpreadElement;
struct OmittedExpression;
struct ArrayLiteralExpression;
struct ElementAccessExpression;
struct PropertyAccessExpression;
struct PropertyAssignment;
struct ShorthandPropertyAssignment;
struct ComputedPropertyName;
struct ObjectLiteralExpression;
struct Identifier;
struct SuperExpression;
struct StringLiteral;
struct RegularExpressionLiteral;
struct NumericLiteral;
struct BooleanLiteral;
struct NullLiteral;
struct UndefinedLiteral;
struct ArrowFunction;
struct FunctionExpression;
struct TemplateExpression;
struct AsExpression;
struct ConditionalExpression;
struct PrefixUnaryExpression;
struct PostfixUnaryExpression;
struct AwaitExpression;
struct YieldExpression;
struct ClassDeclaration;
struct MethodDefinition;
struct StaticBlock;
struct PropertyDefinition;
struct InterfaceDeclaration;
struct TypeAliasDeclaration;
struct EnumDeclaration;
struct EnumMember;

struct Visitor {
    virtual ~Visitor() = default;
    virtual void visitProgram(Program* node) = 0;
    virtual void visitFunctionDeclaration(FunctionDeclaration* node) = 0;
    virtual void visitVariableDeclaration(VariableDeclaration* node) = 0;
    virtual void visitExpressionStatement(ExpressionStatement* node) = 0;
    virtual void visitBlockStatement(BlockStatement* node) = 0;
    virtual void visitReturnStatement(ReturnStatement* node) = 0;
    virtual void visitIfStatement(IfStatement* node) = 0;
    virtual void visitWhileStatement(WhileStatement* node) = 0;
    virtual void visitForStatement(ForStatement* node) = 0;
    virtual void visitForOfStatement(ForOfStatement* node) = 0;
    virtual void visitForInStatement(ForInStatement* node) = 0;
    virtual void visitBreakStatement(BreakStatement* node) = 0;
    virtual void visitContinueStatement(ContinueStatement* node) = 0;
    virtual void visitSwitchStatement(SwitchStatement* node) = 0;
    virtual void visitTryStatement(TryStatement* node) = 0;
    virtual void visitThrowStatement(ThrowStatement* node) = 0;
    virtual void visitImportDeclaration(ImportDeclaration* node) = 0;
    virtual void visitExportDeclaration(ExportDeclaration* node) = 0;
    virtual void visitExportAssignment(ExportAssignment* node) = 0;
    virtual void visitBinaryExpression(BinaryExpression* node) = 0;
    virtual void visitConditionalExpression(ConditionalExpression* node) = 0;
    virtual void visitAssignmentExpression(AssignmentExpression* node) = 0;
    virtual void visitCallExpression(CallExpression* node) = 0;
    virtual void visitNewExpression(NewExpression* node) = 0;
    virtual void visitArrayLiteralExpression(ArrayLiteralExpression* node) = 0;
    virtual void visitElementAccessExpression(ElementAccessExpression* node) = 0;
    virtual void visitPropertyAccessExpression(PropertyAccessExpression* node) = 0;
    virtual void visitObjectLiteralExpression(ObjectLiteralExpression* node) = 0;
    virtual void visitPropertyAssignment(PropertyAssignment* node) = 0;
    virtual void visitShorthandPropertyAssignment(ShorthandPropertyAssignment* node) = 0;
    virtual void visitComputedPropertyName(ComputedPropertyName* node) = 0;
    virtual void visitMethodDefinition(MethodDefinition* node) = 0;
    virtual void visitStaticBlock(StaticBlock* node) = 0;
    virtual void visitIdentifier(Identifier* node) = 0;
    virtual void visitSuperExpression(SuperExpression* node) = 0;
    virtual void visitStringLiteral(StringLiteral* node) = 0;
    virtual void visitRegularExpressionLiteral(RegularExpressionLiteral* node) = 0;
    virtual void visitNumericLiteral(NumericLiteral* node) = 0;
    virtual void visitBooleanLiteral(BooleanLiteral* node) = 0;
    virtual void visitNullLiteral(NullLiteral* node) = 0;
    virtual void visitUndefinedLiteral(UndefinedLiteral* node) = 0;
    virtual void visitAwaitExpression(AwaitExpression* node) = 0;
    virtual void visitYieldExpression(YieldExpression* node) = 0;
    virtual void visitArrowFunction(ArrowFunction* node) = 0;
    virtual void visitFunctionExpression(FunctionExpression* node) = 0;
    virtual void visitTemplateExpression(TemplateExpression* node) = 0;
    virtual void visitAsExpression(AsExpression* node) = 0;
    virtual void visitPrefixUnaryExpression(PrefixUnaryExpression* node) = 0;
    virtual void visitPostfixUnaryExpression(PostfixUnaryExpression* node) = 0;
    virtual void visitClassDeclaration(ClassDeclaration* node) = 0;
    virtual void visitInterfaceDeclaration(InterfaceDeclaration* node) = 0;
    virtual void visitObjectBindingPattern(ObjectBindingPattern* node) = 0;
    virtual void visitArrayBindingPattern(ArrayBindingPattern* node) = 0;
    virtual void visitBindingElement(BindingElement* node) = 0;
    virtual void visitSpreadElement(SpreadElement* node) = 0;
    virtual void visitOmittedExpression(OmittedExpression* node) = 0;
    virtual void visitTypeAliasDeclaration(TypeAliasDeclaration* node) = 0;
    virtual void visitEnumDeclaration(EnumDeclaration* node) = 0;
};

using NodePtr = std::unique_ptr<Node>;
using StmtPtr = std::unique_ptr<Statement>;
using ExprPtr = std::unique_ptr<Expression>;

struct Node {
    virtual ~Node() = default;
    virtual std::string getKind() const = 0;
    virtual void accept(Visitor* visitor) = 0;
    int line = 0;
    int column = 0;
};

struct Statement : Node {};
struct Expression : Node {
    std::shared_ptr<ts::Type> inferredType;
};

// --- Statements ---

struct Program : Node {
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "Program"; }
    void accept(Visitor* visitor) override { visitor->visitProgram(this); }
};

struct Parameter : Node {
    NodePtr name;
    std::string type;
    bool isOptional = false;
    bool isRest = false;
    NodePtr initializer = nullptr;
    ts::AccessModifier access = ts::AccessModifier::Public;
    bool isReadonly = false;
    bool isParameterProperty = false;
    std::string getKind() const override { return "Parameter"; }
    void accept(Visitor* visitor) override { /* Not visited directly */ }
};

struct TypeParameter : Node {
    std::string name;
    std::string constraint;
    std::string getKind() const override { return "TypeParameter"; }
    void accept(Visitor* visitor) override { /* Not visited directly */ }
};

struct FunctionDeclaration : Statement {
    std::string name;
    bool isExported = false;
    bool isDefaultExport = false;
    bool isAsync = false;
    bool isGenerator = false;
    std::vector<std::unique_ptr<Parameter>> parameters;
    std::vector<std::unique_ptr<TypeParameter>> typeParameters;
    std::string returnType;
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "FunctionDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitFunctionDeclaration(this); }
};

struct VariableDeclaration : Statement {
    NodePtr name;
    bool isExported = false;
    std::string type;
    ExprPtr initializer;
    std::shared_ptr<ts::Type> resolvedType;
    std::string getKind() const override { return "VariableDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitVariableDeclaration(this); }
};

struct BindingElement : Node {
    NodePtr name;
    std::string propertyName;
    ExprPtr initializer;
    bool isSpread = false;
    std::string getKind() const override { return "BindingElement"; }
    void accept(Visitor* visitor) override { visitor->visitBindingElement(this); }
};

struct ObjectBindingPattern : Node {
    std::vector<NodePtr> elements;
    std::string getKind() const override { return "ObjectBindingPattern"; }
    void accept(Visitor* visitor) override { visitor->visitObjectBindingPattern(this); }
};

struct ArrayBindingPattern : Node {
    std::vector<NodePtr> elements;
    std::string getKind() const override { return "ArrayBindingPattern"; }
    void accept(Visitor* visitor) override { visitor->visitArrayBindingPattern(this); }
};

struct ExpressionStatement : Statement {
    ExprPtr expression;
    std::string getKind() const override { return "ExpressionStatement"; }
    void accept(Visitor* visitor) override { visitor->visitExpressionStatement(this); }
};

struct BlockStatement : Statement {
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "BlockStatement"; }
    void accept(Visitor* visitor) override { visitor->visitBlockStatement(this); }
};

struct ReturnStatement : Statement {
    ExprPtr expression;
    std::string getKind() const override { return "ReturnStatement"; }
    void accept(Visitor* visitor) override { visitor->visitReturnStatement(this); }
};

struct IfStatement : Statement {
    ExprPtr condition;
    StmtPtr thenStatement;
    StmtPtr elseStatement;
    std::string getKind() const override { return "IfStatement"; }
    void accept(Visitor* visitor) override { visitor->visitIfStatement(this); }
};

struct WhileStatement : Statement {
    ExprPtr condition;
    StmtPtr body;
    std::string getKind() const override { return "WhileStatement"; }
    void accept(Visitor* visitor) override { visitor->visitWhileStatement(this); }
};

struct ForStatement : Statement {
    StmtPtr initializer;
    ExprPtr condition;
    ExprPtr incrementor;
    StmtPtr body;
    std::string getKind() const override { return "ForStatement"; }
    void accept(Visitor* visitor) override { visitor->visitForStatement(this); }
};

struct BreakStatement : Statement {
    std::string getKind() const override { return "BreakStatement"; }
    void accept(Visitor* visitor) override { visitor->visitBreakStatement(this); }
};

struct ContinueStatement : Statement {
    std::string getKind() const override { return "ContinueStatement"; }
    void accept(Visitor* visitor) override { visitor->visitContinueStatement(this); }
};

struct CaseClause : Node {
    ExprPtr expression;
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "CaseClause"; }
    void accept(Visitor* visitor) override { /* Visited via SwitchStatement */ }
};

struct DefaultClause : Node {
    std::vector<StmtPtr> statements;
    std::string getKind() const override { return "DefaultClause"; }
    void accept(Visitor* visitor) override { /* Visited via SwitchStatement */ }
};

struct SwitchStatement : Statement {
    ExprPtr expression;
    std::vector<NodePtr> clauses;
    std::string getKind() const override { return "SwitchStatement"; }
    void accept(Visitor* visitor) override { visitor->visitSwitchStatement(this); }
};

struct CatchClause : Node {
    NodePtr variable;
    std::vector<StmtPtr> block;
    std::string getKind() const override { return "CatchClause"; }
    void accept(Visitor* visitor) override { /* Visited via TryStatement */ }
};

struct TryStatement : Statement {
    std::vector<StmtPtr> tryBlock;
    std::unique_ptr<CatchClause> catchClause;
    std::vector<StmtPtr> finallyBlock;
    std::string getKind() const override { return "TryStatement"; }
    void accept(Visitor* visitor) override { visitor->visitTryStatement(this); }
};

struct ThrowStatement : Statement {
    ExprPtr expression;
    std::string getKind() const override { return "ThrowStatement"; }
    void accept(Visitor* visitor) override { visitor->visitThrowStatement(this); }
};

struct ForOfStatement : Statement {
    StmtPtr initializer; // VariableDeclaration
    ExprPtr expression;  // Iterable
    StmtPtr body;
    bool isAwait = false;
    std::string getKind() const override { return "ForOfStatement"; }
    void accept(Visitor* visitor) override { visitor->visitForOfStatement(this); }
};

struct ForInStatement : Statement {
    StmtPtr initializer; // VariableDeclaration
    ExprPtr expression;  // Object
    StmtPtr body;
    std::string getKind() const override { return "ForInStatement"; }
    void accept(Visitor* visitor) override { visitor->visitForInStatement(this); }
};

struct PropertyDefinition : Node {
    std::string name;
    std::string type;
    ExprPtr initializer;
    ts::AccessModifier access = ts::AccessModifier::Public;
    bool isStatic = false;
    bool isReadonly = false;
    std::string getKind() const override { return "PropertyDefinition"; }
    void accept(Visitor* visitor) override { /* Visited via ClassDeclaration */ }
};

struct MethodDefinition : Node {
    std::string name;
    NodePtr nameNode;
    std::shared_ptr<ts::Type> inferredType;
    bool isAsync = false;
    bool isGenerator = false;
    std::vector<std::unique_ptr<Parameter>> parameters;
    std::vector<std::unique_ptr<TypeParameter>> typeParameters;
    std::string returnType;
    std::vector<StmtPtr> body;
    ts::AccessModifier access = ts::AccessModifier::Public;
    bool isStatic = false;
    bool isAbstract = false;
    bool isGetter = false;
    bool isSetter = false;
    bool hasBody = true;
    std::string getKind() const override { return "MethodDefinition"; }
    void accept(Visitor* visitor) override { visitor->visitMethodDefinition(this); }
};

struct StaticBlock : Node {
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "StaticBlock"; }
    void accept(Visitor* visitor) override { visitor->visitStaticBlock(this); }
};

struct ClassDeclaration : Statement {
    std::string name;
    bool isExported = false;
    bool isDefaultExport = false;
    std::string baseClass;
    std::vector<std::string> implementsInterfaces;
    std::vector<std::unique_ptr<TypeParameter>> typeParameters;
    std::vector<NodePtr> members; // PropertyDefinition or MethodDefinition
    bool isAbstract = false;
    bool isStruct = false;
    std::string getKind() const override { return "ClassDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitClassDeclaration(this); }
};

struct InterfaceDeclaration : Statement {
    std::string name;
    bool isExported = false;
    bool isDefaultExport = false;
    std::vector<std::string> baseInterfaces;
    std::vector<std::unique_ptr<TypeParameter>> typeParameters;
    std::vector<NodePtr> members; // PropertyDefinition or MethodDefinition
    std::string getKind() const override { return "InterfaceDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitInterfaceDeclaration(this); }
};

struct ImportSpecifier {
    std::string name;
    std::string propertyName;
};

struct ImportDeclaration : Statement {
    std::string moduleSpecifier;
    std::string defaultImport;
    std::vector<ImportSpecifier> namedImports;
    std::string namespaceImport;
    std::string getKind() const override { return "ImportDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitImportDeclaration(this); }
};

struct ExportSpecifier {
    std::string name;
    std::string propertyName;
};

struct ExportDeclaration : Statement {
    std::string moduleSpecifier;
    std::vector<ExportSpecifier> namedExports;
    bool isStarExport = false;
    std::string getKind() const override { return "ExportDeclaration"; }
    void accept(Visitor* visitor) override { visitor->visitExportDeclaration(this); }
};

struct ExportAssignment : Statement {
    ExprPtr expression;
    bool isExportEquals = false;
    std::string getKind() const override { return "ExportAssignment"; }
    void accept(Visitor* visitor) override { visitor->visitExportAssignment(this); }
};

struct TypeAliasDeclaration : Statement {
    std::string name;
    std::string type;
    std::vector<TypeParameter> typeParameters;
    bool isExported = false;

    std::string getKind() const override { return "TypeAliasDeclaration"; }
    void accept(Visitor* v) override { v->visitTypeAliasDeclaration(this); }
};

struct EnumMember {
    std::string name;
    std::unique_ptr<Expression> initializer;
};

struct EnumDeclaration : Statement {
    std::string name;
    std::vector<EnumMember> members;
    bool isExported = false;

    std::string getKind() const override { return "EnumDeclaration"; }
    void accept(Visitor* v) override { v->visitEnumDeclaration(this); }
};

// --- Expressions ---

struct BinaryExpression : Expression {
    std::string op;
    ExprPtr left;
    ExprPtr right;
    std::string getKind() const override { return "BinaryExpression"; }
    void accept(Visitor* visitor) override { visitor->visitBinaryExpression(this); }
};

struct ConditionalExpression : Expression {
    ExprPtr condition;
    ExprPtr whenTrue;
    ExprPtr whenFalse;
    std::string getKind() const override { return "ConditionalExpression"; }
    void accept(Visitor* visitor) override { visitor->visitConditionalExpression(this); }
};

struct PrefixUnaryExpression : Expression {
    std::string op;
    ExprPtr operand;
    std::string getKind() const override { return "PrefixUnaryExpression"; }
    void accept(Visitor* visitor) override { visitor->visitPrefixUnaryExpression(this); }
};

struct PostfixUnaryExpression : Expression {
    std::string op;
    ExprPtr operand;
    std::string getKind() const override { return "PostfixUnaryExpression"; }
    void accept(Visitor* visitor) override { visitor->visitPostfixUnaryExpression(this); }
};

struct AssignmentExpression : Expression {
    ExprPtr left; // Usually Identifier
    ExprPtr right;
    std::string getKind() const override { return "AssignmentExpression"; }
    void accept(Visitor* visitor) override { visitor->visitAssignmentExpression(this); }
};

struct CallExpression : Expression {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
    std::vector<std::string> typeArguments;
    std::vector<std::shared_ptr<ts::Type>> resolvedTypeArguments;
    std::string getKind() const override { return "CallExpression"; }
    void accept(Visitor* visitor) override { 
        // printf("CallExpression::accept\n");
        visitor->visitCallExpression(this); 
    }
};

struct NewExpression : Expression {
    ExprPtr expression;
    std::vector<ExprPtr> arguments;
    std::vector<std::string> typeArguments;
    std::vector<std::shared_ptr<ts::Type>> resolvedTypeArguments;
    bool escapes = true;
    std::string getKind() const override { return "NewExpression"; }
    void accept(Visitor* visitor) override { visitor->visitNewExpression(this); }
};

struct SpreadElement : Expression {
    ExprPtr expression;
    std::string getKind() const override { return "SpreadElement"; }
    void accept(Visitor* visitor) override { visitor->visitSpreadElement(this); }
};

struct OmittedExpression : Expression {
    std::string getKind() const override { return "OmittedExpression"; }
    void accept(Visitor* visitor) override { visitor->visitOmittedExpression(this); }
};

struct ArrayLiteralExpression : Expression {
    std::vector<ExprPtr> elements;
    std::string getKind() const override { return "ArrayLiteralExpression"; }
    void accept(Visitor* visitor) override { visitor->visitArrayLiteralExpression(this); }
};

struct ElementAccessExpression : Expression {
    ExprPtr expression;
    ExprPtr argumentExpression;
    std::string getKind() const override { return "ElementAccessExpression"; }
    void accept(Visitor* visitor) override { visitor->visitElementAccessExpression(this); }
};

struct PropertyAccessExpression : Expression {
    ExprPtr expression;
    std::string name;
    std::string getKind() const override { return "PropertyAccessExpression"; }
    void accept(Visitor* visitor) override { visitor->visitPropertyAccessExpression(this); }
};

struct ComputedPropertyName : Node {
    ExprPtr expression;
    std::string getKind() const override { return "ComputedPropertyName"; }
    void accept(Visitor* visitor) override { visitor->visitComputedPropertyName(this); }
};

struct PropertyAssignment : Node {
    std::string name;
    NodePtr nameNode;
    ExprPtr initializer;
    std::string getKind() const override { return "PropertyAssignment"; }
    void accept(Visitor* visitor) override { visitor->visitPropertyAssignment(this); }
};

struct ShorthandPropertyAssignment : Node {
    std::string name;
    NodePtr nameNode;
    std::string getKind() const override { return "ShorthandPropertyAssignment"; }
    void accept(Visitor* visitor) override { visitor->visitShorthandPropertyAssignment(this); }
};

struct ObjectLiteralExpression : Expression {
    std::vector<NodePtr> properties; // PropertyAssignment, ShorthandPropertyAssignment, or MethodDefinition
    std::string getKind() const override { return "ObjectLiteralExpression"; }
    void accept(Visitor* visitor) override { visitor->visitObjectLiteralExpression(this); }
};

struct Identifier : Expression {
    std::string name;
    bool isPrivate = false;
    std::string getKind() const override { return "Identifier"; }
    void accept(Visitor* visitor) override { visitor->visitIdentifier(this); }
};

struct SuperExpression : Expression {
    std::string getKind() const override { return "SuperExpression"; }
    void accept(Visitor* visitor) override { visitor->visitSuperExpression(this); }
};

struct StringLiteral : Expression {
    std::string value;
    std::string getKind() const override { return "StringLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitStringLiteral(this); }
};

struct RegularExpressionLiteral : Expression {
    std::string text;
    std::string getKind() const override { return "RegularExpressionLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitRegularExpressionLiteral(this); }
};

struct NumericLiteral : Expression {
    double value;
    std::string getKind() const override { return "NumericLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitNumericLiteral(this); }
};

struct BooleanLiteral : Expression {
    bool value;
    std::string getKind() const override { return "BooleanLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitBooleanLiteral(this); }
};

struct NullLiteral : Expression {
    std::string getKind() const override { return "NullLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitNullLiteral(this); }
};

struct UndefinedLiteral : Expression {
    std::string getKind() const override { return "UndefinedLiteral"; }
    void accept(Visitor* visitor) override { visitor->visitUndefinedLiteral(this); }
};

struct AwaitExpression : Expression {
    ExprPtr expression;
    std::string getKind() const override { return "AwaitExpression"; }
    void accept(Visitor* visitor) override { visitor->visitAwaitExpression(this); }
};

struct YieldExpression : Expression {
    ExprPtr expression;
    bool isAsterisk = false;
    std::string getKind() const override { return "YieldExpression"; }
    void accept(Visitor* visitor) override { visitor->visitYieldExpression(this); }
};

struct ArrowFunction : Expression {
    bool isAsync = false;
    std::vector<std::unique_ptr<Parameter>> parameters;
    NodePtr body; // BlockStatement or Expression
    std::string getKind() const override { return "ArrowFunction"; }
    void accept(Visitor* visitor) override { visitor->visitArrowFunction(this); }
};

struct FunctionExpression : Expression {
    std::string name;
    bool isAsync = false;
    bool isGenerator = false;
    std::vector<std::unique_ptr<Parameter>> parameters;
    std::vector<std::unique_ptr<TypeParameter>> typeParameters;
    std::string returnType;
    std::vector<StmtPtr> body;
    std::string getKind() const override { return "FunctionExpression"; }
    void accept(Visitor* visitor) override { visitor->visitFunctionExpression(this); }
};

struct TemplateSpan {
    ExprPtr expression;
    std::string literal;
};

struct TemplateExpression : Expression {
    std::string head;
    std::vector<TemplateSpan> spans;
    std::string getKind() const override { return "TemplateExpression"; }
    void accept(Visitor* visitor) override { visitor->visitTemplateExpression(this); }
};

struct AsExpression : Expression {
    ExprPtr expression;
    std::string type;
    std::string getKind() const override { return "AsExpression"; }
    void accept(Visitor* visitor) override { visitor->visitAsExpression(this); }
};

} // namespace ast
