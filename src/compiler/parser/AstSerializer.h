#pragma once

#include "../ast/AstNodes.h"
#include <nlohmann/json.hpp>
#include <string>

namespace ts::parser {

/// Serializes ast::* nodes to JSON matching the exact schema produced by dump_ast.js.
/// This enables comparison testing between the native parser and the Node.js parser.
class AstSerializer {
public:
    nlohmann::json serialize(const ast::Program& program);

private:
    // Statement serialization
    nlohmann::json serializeStatement(const ast::Statement& stmt);
    nlohmann::json serializeFunctionDecl(const ast::FunctionDeclaration& fn);
    nlohmann::json serializeClassDecl(const ast::ClassDeclaration& cls);
    nlohmann::json serializeVariableDecl(const ast::VariableDeclaration& decl);
    nlohmann::json serializeIfStmt(const ast::IfStatement& stmt);
    nlohmann::json serializeWhileStmt(const ast::WhileStatement& stmt);
    nlohmann::json serializeForStmt(const ast::ForStatement& stmt);
    nlohmann::json serializeForOfStmt(const ast::ForOfStatement& stmt);
    nlohmann::json serializeForInStmt(const ast::ForInStatement& stmt);
    nlohmann::json serializeSwitchStmt(const ast::SwitchStatement& stmt);
    nlohmann::json serializeTryStmt(const ast::TryStatement& stmt);
    nlohmann::json serializeReturnStmt(const ast::ReturnStatement& stmt);
    nlohmann::json serializeThrowStmt(const ast::ThrowStatement& stmt);
    nlohmann::json serializeBlockStmt(const ast::BlockStatement& stmt);
    nlohmann::json serializeExprStmt(const ast::ExpressionStatement& stmt);
    nlohmann::json serializeImportDecl(const ast::ImportDeclaration& decl);
    nlohmann::json serializeExportDecl(const ast::ExportDeclaration& decl);
    nlohmann::json serializeExportAssignment(const ast::ExportAssignment& stmt);
    nlohmann::json serializeLabeledStmt(const ast::LabeledStatement& stmt);
    nlohmann::json serializeBreakStmt(const ast::BreakStatement& stmt);
    nlohmann::json serializeContinueStmt(const ast::ContinueStatement& stmt);
    nlohmann::json serializeInterfaceDecl(const ast::InterfaceDeclaration& decl);
    nlohmann::json serializeTypeAliasDecl(const ast::TypeAliasDeclaration& decl);
    nlohmann::json serializeEnumDecl(const ast::EnumDeclaration& decl);

    // Expression serialization
    nlohmann::json serializeExpression(const ast::Expression& expr);
    nlohmann::json serializeIdentifier(const ast::Identifier& id);
    nlohmann::json serializeCallExpr(const ast::CallExpression& call);
    nlohmann::json serializePropertyAccess(const ast::PropertyAccessExpression& pa);
    nlohmann::json serializeElementAccess(const ast::ElementAccessExpression& ea);
    nlohmann::json serializeBinaryExpr(const ast::BinaryExpression& bin);
    nlohmann::json serializeAssignmentExpr(const ast::AssignmentExpression& assign);
    nlohmann::json serializeConditionalExpr(const ast::ConditionalExpression& cond);
    nlohmann::json serializeArrowFunction(const ast::ArrowFunction& fn);
    nlohmann::json serializeFunctionExpr(const ast::FunctionExpression& fn);
    nlohmann::json serializeClassExpr(const ast::ClassExpression& cls);
    nlohmann::json serializeNewExpr(const ast::NewExpression& expr);
    nlohmann::json serializeTemplateExpr(const ast::TemplateExpression& tmpl);
    nlohmann::json serializeTaggedTemplate(const ast::TaggedTemplateExpression& tt);
    nlohmann::json serializeObjectLiteral(const ast::ObjectLiteralExpression& obj);
    nlohmann::json serializeArrayLiteral(const ast::ArrayLiteralExpression& arr);

    // Node/member serialization
    nlohmann::json serializeNode(const ast::Node& node);
    nlohmann::json serializeMethodDef(const ast::MethodDefinition& method);
    nlohmann::json serializePropertyDef(const ast::PropertyDefinition& prop);
    nlohmann::json serializeParameter(const ast::Parameter& param);
    nlohmann::json serializeTypeParameter(const ast::TypeParameter& tp);
    nlohmann::json serializeDecorators(const std::vector<ast::Decorator>& decorators);

    // Helpers
    void addLocation(nlohmann::json& j, const ast::Node& node);
    nlohmann::json serializeNodePtr(const ast::NodePtr& ptr);
    nlohmann::json serializeExprPtr(const ast::ExprPtr& ptr);
    nlohmann::json serializeStmtPtr(const ast::StmtPtr& ptr);
    std::string accessModifierToString(ts::AccessModifier mod);
};

} // namespace ts::parser
