// BlockStatement fix: use "body" instead of "statements" from JSON
#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

namespace ast {

ExprPtr parseExpression(const json& j);
StmtPtr parseStatement(const json& j);
NodePtr parseClassMember(const json& j);
NodePtr parseNode(const json& j);
std::unique_ptr<CallSignature> parseCallSignature(const json& j);
std::unique_ptr<ConstructSignature> parseConstructSignature(const json& j);

void setLocation(Node* node, const json& j) {
    if (!node) return;
    if (!j.is_object()) return;
    
    // std::cerr << "DEBUG: setLocation called for " << j.value("kind", "unknown") << std::endl;

    if (j.contains("line") && !j["line"].is_null()) {
        try {
            if (j["line"].is_number()) {
                node->line = j["line"].get<int>();
            } else if (j["line"].is_string()) {
                node->line = std::stoi(j["line"].get<std::string>());
            }
            // SPDLOG_DEBUG("  Set line={}", node->line);
        } catch (const std::exception& e) {
            SPDLOG_DEBUG("Error parsing line: {} for {}", e.what(), j.dump());
        }
    }
    
    if (j.contains("column") && !j["column"].is_null()) {
        try {
            if (j["column"].is_number()) {
                node->column = j["column"].get<int>();
            } else if (j["column"].is_string()) {
                node->column = std::stoi(j["column"].get<std::string>());
            }
        } catch (...) {}
    }
    
    if (j.contains("sourceFile") && !j["sourceFile"].is_null() && j["sourceFile"].is_string()) {
        node->sourceFile = j["sourceFile"].get<std::string>();
    }
}

std::unique_ptr<TypeParameter> parseTypeParameter(const json& j) {
    auto node = std::make_unique<TypeParameter>();
    setLocation(node.get(), j);
    node->name = j["name"].get<std::string>();
    if (j.contains("constraint") && !j["constraint"].is_null()) {
        node->constraint = j["constraint"].get<std::string>();
    }
    return node;
}

std::unique_ptr<Parameter> parseParameter(const json& j) {
    auto node = std::make_unique<Parameter>();
    setLocation(node.get(), j);
    node->name = parseNode(j["name"]);
    node->type = j["type"].get<std::string>();
    if (j.contains("isOptional")) {
        node->isOptional = j["isOptional"].get<bool>();
    }
    if (j.contains("isRest")) {
        node->isRest = j["isRest"].get<bool>();
    }
    if (j.contains("initializer") && !j["initializer"].is_null()) {
        node->initializer = parseExpression(j["initializer"]);
    }
    if (j.contains("access") && !j["access"].is_null()) {
        std::string access = j["access"];
        if (access == "private") node->access = ts::AccessModifier::Private;
        else if (access == "protected") node->access = ts::AccessModifier::Protected;
        else node->access = ts::AccessModifier::Public;
    }
    if (j.contains("isReadonly")) {
        node->isReadonly = j["isReadonly"].get<bool>();
    }
    if (j.contains("isParameterProperty")) {
        node->isParameterProperty = j["isParameterProperty"].get<bool>();
    }
    return node;
}

std::unique_ptr<CallSignature> parseCallSignature(const json& j) {
    auto sig = std::make_unique<CallSignature>();
    if (j.contains("typeParameters")) {
        for (const auto& tp : j["typeParameters"]) {
            sig->typeParameters.push_back(parseTypeParameter(tp));
        }
    }
    if (j.contains("parameters")) {
        for (const auto& param : j["parameters"]) {
            sig->parameters.push_back(parseParameter(param));
        }
    }
    if (j.contains("returnType") && !j["returnType"].is_null()) {
        sig->returnType = j["returnType"].get<std::string>();
    }
    return sig;
}

std::unique_ptr<ConstructSignature> parseConstructSignature(const json& j) {
    auto sig = std::make_unique<ConstructSignature>();
    if (j.contains("typeParameters")) {
        for (const auto& tp : j["typeParameters"]) {
            sig->typeParameters.push_back(parseTypeParameter(tp));
        }
    }
    if (j.contains("parameters")) {
        for (const auto& param : j["parameters"]) {
            sig->parameters.push_back(parseParameter(param));
        }
    }
    if (j.contains("returnType") && !j["returnType"].is_null()) {
        sig->returnType = j["returnType"].get<std::string>();
    }
    return sig;
}

NodePtr parseNode(const json& j) {
    if (j.is_null()) return nullptr;
    if (j.is_string()) {
        auto node = std::make_unique<Identifier>();
        node->name = j.get<std::string>();
        return node;
    }
    
    std::string kind = j["kind"];
    if (kind == "Identifier") {
        auto node = std::make_unique<Identifier>();
        setLocation(node.get(), j);
        node->name = j["name"].get<std::string>();
        if (j.contains("isPrivate")) {
            node->isPrivate = j["isPrivate"];
        }
        return node;
    } else if (kind == "ObjectBindingPattern") {
        auto node = std::make_unique<ObjectBindingPattern>();
        setLocation(node.get(), j);
        for (const auto& el : j["elements"]) {
            node->elements.push_back(parseNode(el));
        }
        return node;
    } else if (kind == "ArrayBindingPattern") {
        auto node = std::make_unique<ArrayBindingPattern>();
        setLocation(node.get(), j);
        for (const auto& el : j["elements"]) {
            node->elements.push_back(parseNode(el));
        }
        return node;
    } else if (kind == "BindingElement") {
        auto node = std::make_unique<BindingElement>();
        setLocation(node.get(), j);
        node->name = parseNode(j["name"]);
        if (j.contains("propertyName") && !j["propertyName"].is_null()) {
            node->propertyName = j["propertyName"];
        }
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseExpression(j["initializer"]);
        }
        node->isSpread = j.value("isSpread", false);
        return node;
    } else if (kind == "OmittedExpression") {
        auto node = std::make_unique<OmittedExpression>();
        setLocation(node.get(), j);
        return node;
    } else if (kind == "BlockStatement") {
        return parseStatement(j);
    }
    
    // Fallback to expression for ArrowFunction body and others
    try {
        return parseExpression(j);
    } catch (...) {
        return nullptr;
    }
}

ExprPtr parseExpression(const json& j) {
    if (j.is_null()) throw std::runtime_error("parseExpression called with null");
    if (!j.contains("kind")) {
        SPDLOG_ERROR("JSON missing kind: {}", j.dump());
        throw std::runtime_error("JSON missing kind");
    }
    std::string kind = j["kind"];
    
    if (kind == "BinaryExpression") {
        auto node = std::make_unique<BinaryExpression>();
        setLocation(node.get(), j);
        node->op = j["operator"];
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "AssignmentExpression") {
        auto node = std::make_unique<AssignmentExpression>();
        setLocation(node.get(), j);
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "CallExpression") {
        auto node = std::make_unique<CallExpression>();
        setLocation(node.get(), j);
        node->callee = parseExpression(j["callee"]);
        if (j.contains("arguments")) {
            for (const auto& arg : j["arguments"]) {
                node->arguments.push_back(parseExpression(arg));
            }
        }
        if (j.contains("typeArguments")) {
            for (const auto& ta : j["typeArguments"]) {
                node->typeArguments.push_back(ta.get<std::string>());
            }
        }
        return node;
    } else if (kind == "NewExpression") {
        auto node = std::make_unique<NewExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        if (j.contains("arguments")) {
            for (const auto& arg : j["arguments"]) {
                node->arguments.push_back(parseExpression(arg));
            }
        }
        if (j.contains("typeArguments")) {
            for (const auto& ta : j["typeArguments"]) {
                node->typeArguments.push_back(ta.get<std::string>());
            }
        }
        return node;
    } else if (kind == "ArrayLiteralExpression") {
        auto node = std::make_unique<ArrayLiteralExpression>();
        setLocation(node.get(), j);
        for (const auto& el : j["elements"]) {
            node->elements.push_back(parseExpression(el));
        }
        return node;
    } else if (kind == "ObjectLiteralExpression") {
        auto node = std::make_unique<ObjectLiteralExpression>();
        setLocation(node.get(), j);
        for (const auto& prop : j["properties"]) {
            std::string pKind = prop["kind"];
            if (pKind == "PropertyAssignment") {
                auto p = std::make_unique<PropertyAssignment>();
                setLocation(p.get(), prop);
                p->name = prop["name"];
                p->initializer = parseExpression(prop["initializer"]);
                node->properties.push_back(std::move(p));
            } else if (pKind == "ShorthandPropertyAssignment") {
                auto p = std::make_unique<ShorthandPropertyAssignment>();
                setLocation(p.get(), prop);
                p->name = prop["name"];
                node->properties.push_back(std::move(p));
            } else if (pKind == "MethodDefinition") {
                node->properties.push_back(parseNode(prop));
            }
        }
        return node;
    } else if (kind == "ElementAccessExpression") {
        auto node = std::make_unique<ElementAccessExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        node->argumentExpression = parseExpression(j["argumentExpression"]);
        return node;
    } else if (kind == "PropertyAccessExpression") {
        auto node = std::make_unique<PropertyAccessExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        node->name = j["name"];
        return node;
    } else if (kind == "Identifier") {
        auto node = std::make_unique<Identifier>();
        setLocation(node.get(), j);
        node->name = j["name"];
        if (j.contains("isPrivate")) {
            node->isPrivate = j["isPrivate"];
        }
        return node;
    } else if (kind == "StringLiteral") {
        auto node = std::make_unique<StringLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "NumericLiteral") {
        auto node = std::make_unique<NumericLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "BooleanLiteral") {
        auto node = std::make_unique<BooleanLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "AwaitExpression") {
        auto node = std::make_unique<AwaitExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "PrefixUnaryExpression") {
        auto node = std::make_unique<PrefixUnaryExpression>();
        setLocation(node.get(), j);
        node->op = j["operator"];
        node->operand = parseExpression(j["operand"]);
        return node;
    } else if (kind == "PostfixUnaryExpression") {
        auto node = std::make_unique<PostfixUnaryExpression>();
        setLocation(node.get(), j);
        node->op = j["operator"];
        node->operand = parseExpression(j["operand"]);
        return node;
    } else if (kind == "AsExpression") {
        auto node = std::make_unique<AsExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        node->type = j["type"];
        return node;
    } else if (kind == "SpreadElement") {
        auto node = std::make_unique<SpreadElement>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "OmittedExpression") {
        auto node = std::make_unique<OmittedExpression>();
        setLocation(node.get(), j);
        return node;
    } else if (kind == "ArrowFunction") {
        auto node = std::make_unique<ArrowFunction>();
        setLocation(node.get(), j);
        node->isAsync = j.value("isAsync", false);
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                node->parameters.push_back(parseParameter(param));
            }
        }
        node->body = parseNode(j["body"]);
        return node;
    } else if (kind == "FunctionExpression") {
        auto node = std::make_unique<FunctionExpression>();
        setLocation(node.get(), j);
        if (j.contains("name") && !j["name"].is_null()) {
            node->name = j["name"];
        }
        node->isAsync = j.value("isAsync", false);
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                node->parameters.push_back(parseParameter(param));
            }
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("returnType")) {
            node->returnType = j["returnType"];
        }
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "ClassExpression") {
        auto node = std::make_unique<ClassExpression>();
        setLocation(node.get(), j);
        if (j.contains("name") && !j["name"].is_null() && j["name"].is_string()) {
            node->name = j["name"].get<std::string>();
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("baseClass") && j["baseClass"].is_string()) {
            node->baseClass = j["baseClass"].get<std::string>();
        }
        if (j.contains("implementsInterfaces")) {
            for (const auto& i : j["implementsInterfaces"]) {
                node->implementsInterfaces.push_back(i);
            }
        }
        if (j.contains("members")) {
            for (const auto& member : j["members"]) {
                node->members.push_back(parseClassMember(member));
            }
        }
        return node;
    } else if (kind == "ParenthesizedExpression") {
        auto node = std::make_unique<ParenthesizedExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "TemplateExpression") {
        auto node = std::make_unique<TemplateExpression>();
        setLocation(node.get(), j);
        node->head = j["head"];
        for (const auto& span : j["templateSpans"]) {
            TemplateSpan s;
            s.expression = parseExpression(span["expression"]);
            s.literal = span["literal"];
            node->spans.push_back(std::move(s));
        }
        return node;
    } else if (kind == "SuperExpression") {
        auto node = std::make_unique<SuperExpression>();
        setLocation(node.get(), j);
        return node;
    }
    
    throw std::runtime_error("Unknown expression kind: " + kind);
}

ts::AccessModifier parseAccessModifier(const json& j) {
    if (j.is_null()) return ts::AccessModifier::Public;
    std::string access = j.get<std::string>();
    if (access == "private") return ts::AccessModifier::Private;
    if (access == "protected") return ts::AccessModifier::Protected;
    return ts::AccessModifier::Public;
}

NodePtr parseClassMember(const json& j) {
    std::string kind = j["kind"];
    if (kind == "PropertyDefinition") {
        auto node = std::make_unique<PropertyDefinition>();
        setLocation(node.get(), j);
        node->name = j["name"].get<std::string>();
        node->type = j["type"];
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseExpression(j["initializer"]);
        }
        if (j.contains("access")) {
            node->access = parseAccessModifier(j["access"]);
        }
        if (j.contains("isStatic")) {
            node->isStatic = j["isStatic"];
        }
        if (j.contains("isReadonly")) {
            node->isReadonly = j["isReadonly"];
        }
        parseDecorators(node->decorators, j);
        return node;
    } else if (kind == "MethodDefinition") {
        auto node = std::make_unique<MethodDefinition>();
        setLocation(node.get(), j);
        node->name = j["name"];
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                node->parameters.push_back(parseParameter(param));
            }
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("returnType")) {
            node->returnType = j["returnType"];
        }
        if (j.contains("access")) {
            node->access = parseAccessModifier(j["access"]);
        }
        if (j.contains("isStatic")) {
            node->isStatic = j["isStatic"];
        }
        if (j.contains("isAbstract")) {
            node->isAbstract = j["isAbstract"];
        }
        if (j.contains("isGetter")) {
            node->isGetter = j["isGetter"];
        }
        if (j.contains("isSetter")) {
            node->isSetter = j["isSetter"];
        }
        if (j.contains("hasBody")) {
            node->hasBody = j["hasBody"];
        }
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "StaticBlock") {
        auto node = std::make_unique<StaticBlock>();
        setLocation(node.get(), j);
        if (j.contains("body") && !j["body"].is_null()) {
            for (const auto& stmt : j["body"]) {
                node->body.push_back(parseStatement(stmt));
            }
        }
        return node;
    }
    throw std::runtime_error("Unknown class member kind: " + kind);
}

StmtPtr parseStatement(const json& j) {
    std::string kind = j["kind"];

    if (kind == "ClassDeclaration") {
        auto node = std::make_unique<ClassDeclaration>();
        setLocation(node.get(), j);
        node->name = j["name"].get<std::string>();
        if (j.contains("isExported")) {
            node->isExported = j["isExported"];
        }
        if (j.contains("isDefaultExport")) {
            node->isDefaultExport = j["isDefaultExport"];
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("baseClass")) {
            node->baseClass = j["baseClass"];
        }
        if (j.contains("implementsInterfaces")) {
            for (const auto& i : j["implementsInterfaces"]) {
                node->implementsInterfaces.push_back(i);
            }
        }
        for (const auto& member : j["members"]) {
            node->members.push_back(parseClassMember(member));
        }
        if (j.contains("isAbstract")) {
            node->isAbstract = j["isAbstract"];
        }
        return node;
    } else if (kind == "InterfaceDeclaration") {
        auto node = std::make_unique<InterfaceDeclaration>();
        setLocation(node.get(), j);
        node->name = j["name"].get<std::string>();
        if (j.contains("isExported")) {
            node->isExported = j["isExported"];
        }
        if (j.contains("isDefaultExport")) {
            node->isDefaultExport = j["isDefaultExport"];
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("baseInterfaces")) {
            for (const auto& i : j["baseInterfaces"]) {
                node->baseInterfaces.push_back(i);
            }
        }
        for (const auto& member : j["members"]) {
            std::string memberKind = member["kind"].get<std::string>();
            if (memberKind == "CallSignature") {
                node->callSignatures.push_back(parseCallSignature(member));
            } else if (memberKind == "ConstructSignature") {
                node->constructSignatures.push_back(parseConstructSignature(member));
            } else {
                node->members.push_back(parseClassMember(member));
            }
        }
        return node;
    } else if (kind == "FunctionDeclaration") {
        auto node = std::make_unique<FunctionDeclaration>();
        setLocation(node.get(), j);
        node->name = j["name"];
        node->isExported = j.value("isExported", false);
        node->isDefaultExport = j.value("isDefaultExport", false);
        node->isAsync = j.value("isAsync", false);
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                node->parameters.push_back(parseParameter(param));
            }
        }
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(parseTypeParameter(tp));
            }
        }
        if (j.contains("returnType")) node->returnType = j["returnType"];
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "VariableDeclaration") {
        auto node = std::make_unique<VariableDeclaration>();
        setLocation(node.get(), j);
        node->name = parseNode(j["name"]);
        if (j.contains("isExported")) {
            node->isExported = j["isExported"];
        }
        if (j.contains("type")) node->type = j["type"];
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseExpression(j["initializer"]);
        }
        return node;
    } else if (kind == "ExpressionStatement") {
        auto node = std::make_unique<ExpressionStatement>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "BlockStatement") {
        auto node = std::make_unique<BlockStatement>();
        setLocation(node.get(), j);
        // The JSON from dump_ast.js uses "body" for BlockStatement contents
        const auto& body = j.contains("body") ? j["body"] : j["statements"];
        for (const auto& stmt : body) {
            node->statements.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "ReturnStatement") {
        auto node = std::make_unique<ReturnStatement>();
        setLocation(node.get(), j);
        if (j.contains("expression") && !j["expression"].is_null()) {
            node->expression = parseExpression(j["expression"]);
        }
        return node;
    } else if (kind == "IfStatement") {
        auto node = std::make_unique<IfStatement>();
        setLocation(node.get(), j);
        node->condition = parseExpression(j["condition"]);
        node->thenStatement = parseStatement(j["thenStatement"]);
        if (j.contains("elseStatement") && !j["elseStatement"].is_null()) {
            node->elseStatement = parseStatement(j["elseStatement"]);
        }
        return node;
    } else if (kind == "WhileStatement") {
        auto node = std::make_unique<WhileStatement>();
        setLocation(node.get(), j);
        node->condition = parseExpression(j["condition"]);
        node->body = parseStatement(j["statement"]);
        return node;
    } else if (kind == "ForStatement") {
        auto node = std::make_unique<ForStatement>();
        setLocation(node.get(), j);
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseStatement(j["initializer"]);
        }
        if (j.contains("condition") && !j["condition"].is_null()) {
            node->condition = parseExpression(j["condition"]);
        }
        if (j.contains("incrementor") && !j["incrementor"].is_null()) {
            node->incrementor = parseExpression(j["incrementor"]);
        }
        node->body = parseStatement(j["body"]);
        return node;
    } else if (kind == "ForOfStatement") {
        auto node = std::make_unique<ForOfStatement>();
        setLocation(node.get(), j);
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseStatement(j["initializer"]);
        }
        node->expression = parseExpression(j["expression"]);
        node->body = parseStatement(j["body"]);
        return node;
    } else if (kind == "ForInStatement") {
        auto node = std::make_unique<ForInStatement>();
        setLocation(node.get(), j);
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseStatement(j["initializer"]);
        }
        node->expression = parseExpression(j["expression"]);
        node->body = parseStatement(j["body"]);
        return node;
    } else if (kind == "SwitchStatement") {
        auto node = std::make_unique<SwitchStatement>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        for (const auto& clause : j["clauses"]) {
            if (clause["kind"] == "CaseClause") {
                auto cc = std::make_unique<CaseClause>();
                setLocation(cc.get(), clause);
                cc->expression = parseExpression(clause["expression"]);
                for (const auto& stmt : clause["statements"]) {
                    cc->statements.push_back(parseStatement(stmt));
                }
                node->clauses.push_back(std::move(cc));
            } else {
                auto dc = std::make_unique<DefaultClause>();
                setLocation(dc.get(), clause);
                for (const auto& stmt : clause["statements"]) {
                    dc->statements.push_back(parseStatement(stmt));
                }
                node->clauses.push_back(std::move(dc));
            }
        }
        return node;
    } else if (kind == "TryStatement") {
        auto node = std::make_unique<TryStatement>();
        setLocation(node.get(), j);
        for (const auto& stmt : j["tryBlock"]) {
            node->tryBlock.push_back(parseStatement(stmt));
        }
        if (j.contains("catchClause") && !j["catchClause"].is_null()) {
            node->catchClause = std::make_unique<CatchClause>();
            setLocation(node->catchClause.get(), j["catchClause"]);
            if (j["catchClause"].contains("variable") && !j["catchClause"]["variable"].is_null()) {
                node->catchClause->variable = parseNode(j["catchClause"]["variable"]);
            }
            for (const auto& stmt : j["catchClause"]["block"]) {
                node->catchClause->block.push_back(parseStatement(stmt));
            }
        }
        if (j.contains("finallyBlock") && !j["finallyBlock"].is_null()) {
            for (const auto& stmt : j["finallyBlock"]) {
                node->finallyBlock.push_back(parseStatement(stmt));
            }
        }
        return node;
    } else if (kind == "ThrowStatement") {
        auto node = std::make_unique<ThrowStatement>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "BreakStatement") {
        auto node = std::make_unique<BreakStatement>();
        setLocation(node.get(), j);
        if (j.contains("label") && !j["label"].is_null()) {
            node->label = j["label"].get<std::string>();
        }
        return node;
    } else if (kind == "ContinueStatement") {
        auto node = std::make_unique<ContinueStatement>();
        setLocation(node.get(), j);
        if (j.contains("label") && !j["label"].is_null()) {
            node->label = j["label"].get<std::string>();
        }
        return node;
    } else if (kind == "LabeledStatement") {
        auto node = std::make_unique<LabeledStatement>();
        setLocation(node.get(), j);
        node->label = j["label"].get<std::string>();
        node->statement = parseStatement(j["statement"]);
        return node;
    } else if (kind == "ImportDeclaration") {
        auto node = std::make_unique<ImportDeclaration>();
        setLocation(node.get(), j);
        node->moduleSpecifier = j["moduleSpecifier"].get<std::string>();
        
        if (j.contains("importClause") && !j["importClause"].is_null()) {
            const auto& clause = j["importClause"];
            if (clause.contains("name") && !clause["name"].is_null()) {
                node->defaultImport = clause["name"].get<std::string>();
            }
            
            if (clause.contains("namedBindings") && !clause["namedBindings"].is_null()) {
                const auto& bindings = clause["namedBindings"];
                if (bindings["kind"] == "NamespaceImport") {
                    node->namespaceImport = bindings["name"].get<std::string>();
                } else if (bindings["kind"] == "NamedImports") {
                    for (const auto& element : bindings["elements"]) {
                        ImportSpecifier spec;
                        spec.name = element["name"].get<std::string>();
                        if (element.contains("propertyName") && !element["propertyName"].is_null()) {
                            spec.propertyName = element["propertyName"].get<std::string>();
                        }
                        node->namedImports.push_back(spec);
                    }
                }
            }
        }
        return node;
    } else if (kind == "ExportDeclaration") {
        auto node = std::make_unique<ExportDeclaration>();
        setLocation(node.get(), j);
        if (j.contains("moduleSpecifier") && !j["moduleSpecifier"].is_null()) {
            node->moduleSpecifier = j["moduleSpecifier"].get<std::string>();
        }
        if (j.contains("isStarExport")) {
            node->isStarExport = j["isStarExport"];
        }
        if (j.contains("namedExports")) {
            for (const auto& spec : j["namedExports"]) {
                ExportSpecifier es;
                es.name = spec["name"].get<std::string>();
                if (spec.contains("propertyName") && !spec["propertyName"].is_null()) {
                    es.propertyName = spec["propertyName"].get<std::string>();
                }
                node->namedExports.push_back(es);
            }
        }
        return node;
    } else if (kind == "ExportAssignment") {
        auto node = std::make_unique<ExportAssignment>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        node->isExportEquals = j.value("isExportEquals", false);
        return node;
    } else if (kind == "TypeAliasDeclaration") {
        auto node = std::make_unique<TypeAliasDeclaration>();
        setLocation(node.get(), j);
        node->name = j["name"];
        node->type = j["type"];
        if (j.contains("typeParameters")) {
            for (const auto& tp : j["typeParameters"]) {
                node->typeParameters.push_back(*parseTypeParameter(tp));
            }
        }
        node->isExported = j.value("isExported", false);
        return node;
    } else if (kind == "EnumDeclaration") {
        auto node = std::make_unique<EnumDeclaration>();
        setLocation(node.get(), j);
        node->name = j["name"];
        for (const auto& m : j["members"]) {
            EnumMember member;
            member.name = m["name"];
            if (m.contains("initializer") && !m["initializer"].is_null()) {
                member.initializer = parseExpression(m["initializer"]);
            }
            node->members.push_back(std::move(member));
        }
        node->isExported = j.value("isExported", false);
        node->isDeclare = j.value("isDeclare", false);
        return node;
    }

    throw std::runtime_error("Unknown statement kind: " + kind);
}

std::unique_ptr<Program> loadAst(const std::string& jsonPath) {
    std::ifstream i(jsonPath);
    if (!i.is_open()) {
        throw std::runtime_error("Could not open file: " + jsonPath);
    }
    
    json j;
    i >> j;
    
    if (j["kind"] != "Program") {
        throw std::runtime_error("Root node is not a Program");
    }

    auto program = std::make_unique<Program>();
    setLocation(program.get(), j);
    for (const auto& stmt : j["body"]) {
        program->body.push_back(parseStatement(stmt));
    }
    return program;
}

}
