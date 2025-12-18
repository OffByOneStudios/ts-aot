#include "Analyzer.h"
#include <iostream>
#include <fmt/core.h>
#include <sstream>

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
    else if (auto r = dynamic_cast<ReturnStatement*>(node)) visitReturnStatement(r);
    else if (auto c = dynamic_cast<CallExpression*>(node)) visitCallExpression(c);
    else if (auto n = dynamic_cast<NewExpression*>(node)) visitNewExpression(n);
    else if (auto obj = dynamic_cast<ObjectLiteralExpression*>(node)) visitObjectLiteralExpression(obj);
    else if (auto arr = dynamic_cast<ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto elem = dynamic_cast<ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto pa = dynamic_cast<PropertyAccessExpression*>(node)) visitPropertyAccessExpression(pa);
    else if (auto as = dynamic_cast<AsExpression*>(node)) visitAsExpression(as);
    else if (auto be = dynamic_cast<BinaryExpression*>(node)) visitBinaryExpression(be);
    else if (auto assign = dynamic_cast<AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto ifStmt = dynamic_cast<IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto forOfStmt = dynamic_cast<ForOfStatement*>(node)) visitForOfStatement(forOfStmt);
    else if (auto sw = dynamic_cast<SwitchStatement*>(node)) visitSwitchStatement(sw);
    else if (auto tryStmt = dynamic_cast<TryStatement*>(node)) visitTryStatement(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStatement*>(node)) visitThrowStatement(throwStmt);
    else if (auto br = dynamic_cast<BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto id = dynamic_cast<Identifier*>(node)) visitIdentifier(id);
    else if (auto sl = dynamic_cast<StringLiteral*>(node)) visitStringLiteral(sl);
    else if (auto nl = dynamic_cast<NumericLiteral*>(node)) visitNumericLiteral(nl);
    else if (auto bl = dynamic_cast<BooleanLiteral*>(node)) visitBooleanLiteral(bl);
    else if (auto arrow = dynamic_cast<ArrowFunction*>(node)) visitArrowFunction(arrow);
    else if (auto tmpl = dynamic_cast<TemplateExpression*>(node)) visitTemplateExpression(tmpl);
    else if (auto pre = dynamic_cast<PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto sup = dynamic_cast<SuperExpression*>(node)) visitSuperExpression(sup);
    else if (auto cls = dynamic_cast<ClassDeclaration*>(node)) visitClassDeclaration(cls);
    else if (auto inter = dynamic_cast<InterfaceDeclaration*>(node)) visitInterfaceDeclaration(inter);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
    }
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

std::shared_ptr<Type> Analyzer::parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName.find('|') != std::string::npos) {
        std::vector<std::shared_ptr<Type>> types;
        std::stringstream ss(typeName);
        std::string part;
        while (std::getline(ss, part, '|')) {
            part.erase(0, part.find_first_not_of(" "));
            part.erase(part.find_last_not_of(" ") + 1);
            types.push_back(parseType(part, symbols));
        }
        return std::make_shared<UnionType>(types);
    }

    if (typeName.find('&') != std::string::npos) {
        std::vector<std::shared_ptr<Type>> types;
        std::stringstream ss(typeName);
        std::string part;
        while (std::getline(ss, part, '&')) {
            part.erase(0, part.find_first_not_of(" "));
            part.erase(part.find_last_not_of(" ") + 1);
            types.push_back(parseType(part, symbols));
        }
        return std::make_shared<IntersectionType>(types);
    }

    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    
    // Look up in symbol table for classes
    auto type = symbols.lookupType(typeName);
    if (type) return type;

    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<FunctionType> Analyzer::resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes) {
    if (overloads.empty()) return nullptr;
    
    for (const auto& overload : overloads) {
        if (overload->paramTypes.size() != argTypes.size()) continue;
        
        bool match = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]->isAssignableTo(overload->paramTypes[i])) {
                match = false;
                break;
            }
        }
        if (match) return overload;
    }
    
    return overloads[0]; // Fallback
}

void Analyzer::reportError(const std::string& message) {
    fmt::print(stderr, "Error: {}\n", message);
    errorCount++;
}

} // namespace ts
