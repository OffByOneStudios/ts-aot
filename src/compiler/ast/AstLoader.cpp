#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

namespace ast {

ExprPtr parseExpression(const json& j);
StmtPtr parseStatement(const json& j);
NodePtr parseClassMember(const json& j);

ExprPtr parseExpression(const json& j) {
    if (j.is_null()) throw std::runtime_error("parseExpression called with null");
    if (!j.contains("kind")) {
        std::cerr << "JSON missing kind: " << j.dump() << std::endl;
        throw std::runtime_error("JSON missing kind");
    }
    std::string kind = j["kind"];
    
    if (kind == "BinaryExpression") {
        auto node = std::make_unique<BinaryExpression>();
        node->op = j["operator"];
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "AssignmentExpression") {
        auto node = std::make_unique<AssignmentExpression>();
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "CallExpression") {
        auto node = std::make_unique<CallExpression>();
        node->callee = parseExpression(j["callee"]);
        if (j.contains("arguments")) {
            for (const auto& arg : j["arguments"]) {
                node->arguments.push_back(parseExpression(arg));
            }
        }
        return node;
    } else if (kind == "NewExpression") {
        auto node = std::make_unique<NewExpression>();
        node->expression = parseExpression(j["expression"]);
        if (j.contains("arguments")) {
            for (const auto& arg : j["arguments"]) {
                node->arguments.push_back(parseExpression(arg));
            }
        }
        return node;
    } else if (kind == "ArrayLiteralExpression") {
        auto node = std::make_unique<ArrayLiteralExpression>();
        for (const auto& el : j["elements"]) {
            node->elements.push_back(parseExpression(el));
        }
        return node;
    } else if (kind == "ObjectLiteralExpression") {
        auto node = std::make_unique<ObjectLiteralExpression>();
        for (const auto& prop : j["properties"]) {
            auto p = std::make_unique<PropertyAssignment>();
            p->name = prop["name"];
            p->initializer = parseExpression(prop["initializer"]);
            node->properties.push_back(std::move(p));
        }
        return node;
    } else if (kind == "ElementAccessExpression") {
        auto node = std::make_unique<ElementAccessExpression>();
        node->expression = parseExpression(j["expression"]);
        node->argumentExpression = parseExpression(j["argumentExpression"]);
        return node;
    } else if (kind == "PropertyAccessExpression") {
        auto node = std::make_unique<PropertyAccessExpression>();
        node->expression = parseExpression(j["expression"]);
        node->name = j["name"];
        return node;
    } else if (kind == "Identifier") {
        auto node = std::make_unique<Identifier>();
        node->name = j["name"];
        return node;
    } else if (kind == "StringLiteral") {
        auto node = std::make_unique<StringLiteral>();
        node->value = j["value"];
        return node;
    } else if (kind == "NumericLiteral") {
        auto node = std::make_unique<NumericLiteral>();
        node->value = j["value"];
        return node;
    } else if (kind == "BooleanLiteral") {
        auto node = std::make_unique<BooleanLiteral>();
        node->value = j["value"];
        return node;
    } else if (kind == "PrefixUnaryExpression") {
        auto node = std::make_unique<PrefixUnaryExpression>();
        node->op = j["operator"];
        node->operand = parseExpression(j["operand"]);
        return node;
    } else if (kind == "ArrowFunction") {
        auto node = std::make_unique<ArrowFunction>();
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                auto p = std::make_unique<Parameter>();
                p->name = param["name"];
                if (param.contains("type")) p->type = param["type"];
                node->parameters.push_back(std::move(p));
            }
        }
        std::string bodyKind = j["body"]["kind"];
        if (bodyKind == "BlockStatement") {
            node->body = parseStatement(j["body"]);
        } else {
            node->body = parseExpression(j["body"]);
        }
        return node;
    } else if (kind == "TemplateExpression") {
        auto node = std::make_unique<TemplateExpression>();
        node->head = j["head"];
        for (const auto& span : j["templateSpans"]) {
            TemplateSpan s;
            s.expression = parseExpression(span["expression"]);
            s.literal = span["literal"];
            node->spans.push_back(std::move(s));
        }
        return node;
    }
    
    throw std::runtime_error("Unknown expression kind: " + kind);
}

NodePtr parseClassMember(const json& j) {
    std::string kind = j["kind"];
    if (kind == "PropertyDefinition") {
        auto node = std::make_unique<PropertyDefinition>();
        node->name = j["name"];
        node->type = j["type"];
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseExpression(j["initializer"]);
        }
        return node;
    } else if (kind == "MethodDefinition") {
        auto node = std::make_unique<MethodDefinition>();
        node->name = j["name"];
        node->returnType = j["returnType"];
        for (const auto& p : j["parameters"]) {
            auto param = std::make_unique<Parameter>();
            param->name = p["name"];
            param->type = p["type"];
            node->parameters.push_back(std::move(param));
        }
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
        return node;
    }
    throw std::runtime_error("Unknown class member kind: " + kind);
}

StmtPtr parseStatement(const json& j) {
    std::string kind = j["kind"];

    if (kind == "ClassDeclaration") {
        auto node = std::make_unique<ClassDeclaration>();
        node->name = j["name"];
        for (const auto& member : j["members"]) {
            node->members.push_back(parseClassMember(member));
        }
        return node;
    } else if (kind == "FunctionDeclaration") {
        auto node = std::make_unique<FunctionDeclaration>();
        node->name = j["name"];
        if (j.contains("returnType")) node->returnType = j["returnType"];
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                auto p = std::make_unique<Parameter>();
                p->name = param["name"];
                if (param.contains("type")) p->type = param["type"];
                node->parameters.push_back(std::move(p));
            }
        }
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "VariableDeclaration") {
        auto node = std::make_unique<VariableDeclaration>();
        node->name = j["name"];
        if (j.contains("initializer") && !j["initializer"].is_null()) {
            node->initializer = parseExpression(j["initializer"]);
        }
        return node;
    } else if (kind == "ExpressionStatement") {
        auto node = std::make_unique<ExpressionStatement>();
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "BlockStatement") {
        auto node = std::make_unique<BlockStatement>();
        for (const auto& stmt : j["statements"]) {
            node->statements.push_back(parseStatement(stmt));
        }
        return node;
    } else if (kind == "ReturnStatement") {
        auto node = std::make_unique<ReturnStatement>();
        if (j.contains("expression") && !j["expression"].is_null()) {
            node->expression = parseExpression(j["expression"]);
        }
        return node;
    } else if (kind == "IfStatement") {
        auto node = std::make_unique<IfStatement>();
        node->condition = parseExpression(j["condition"]);
        node->thenStatement = parseStatement(j["thenStatement"]);
        if (j.contains("elseStatement") && !j["elseStatement"].is_null()) {
            node->elseStatement = parseStatement(j["elseStatement"]);
        }
        return node;
    } else if (kind == "WhileStatement") {
        auto node = std::make_unique<WhileStatement>();
        node->condition = parseExpression(j["condition"]);
        node->body = parseStatement(j["statement"]);
        return node;
    } else if (kind == "ForStatement") {
        auto node = std::make_unique<ForStatement>();
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
        node->initializer = parseStatement(j["initializer"]);
        node->expression = parseExpression(j["expression"]);
        node->body = parseStatement(j["body"]);
        return node;
    } else if (kind == "BreakStatement") {
        return std::make_unique<BreakStatement>();
    } else if (kind == "ContinueStatement") {
        return std::make_unique<ContinueStatement>();
    } else if (kind == "SwitchStatement") {
        auto node = std::make_unique<SwitchStatement>();
        node->expression = parseExpression(j["expression"]);
        for (const auto& clause : j["clauses"]) {
            std::string clauseKind = clause["kind"];
            if (clauseKind == "CaseClause") {
                auto c = std::make_unique<CaseClause>();
                c->expression = parseExpression(clause["expression"]);
                for (const auto& stmt : clause["statements"]) {
                    c->statements.push_back(parseStatement(stmt));
                }
                node->clauses.push_back(std::move(c));
            } else if (clauseKind == "DefaultClause") {
                auto c = std::make_unique<DefaultClause>();
                for (const auto& stmt : clause["statements"]) {
                    c->statements.push_back(parseStatement(stmt));
                }
                node->clauses.push_back(std::move(c));
            }
        }
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
    for (const auto& stmt : j["body"]) {
        program->body.push_back(parseStatement(stmt));
    }
    return program;
}

}
