#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

namespace ast {

ExprPtr parseExpression(const json& j);
StmtPtr parseStatement(const json& j);

ExprPtr parseExpression(const json& j) {
    std::string kind = j["kind"];
    
    if (kind == "BinaryExpression") {
        auto node = std::make_unique<BinaryExpression>();
        node->op = j["operator"];
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "CallExpression") {
        auto node = std::make_unique<CallExpression>();
        node->callee = parseExpression(j["callee"]);
        for (const auto& arg : j["arguments"]) {
            node->arguments.push_back(parseExpression(arg));
        }
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
    }
    
    throw std::runtime_error("Unknown expression kind: " + kind);
}

StmtPtr parseStatement(const json& j) {
    std::string kind = j["kind"];

    if (kind == "FunctionDeclaration") {
        auto node = std::make_unique<FunctionDeclaration>();
        node->name = j["name"];
        if (j.contains("parameters")) {
            for (const auto& param : j["parameters"]) {
                auto p = std::make_unique<Parameter>();
                p->name = param["name"];
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
    } else if (kind == "ReturnStatement") {
        auto node = std::make_unique<ReturnStatement>();
        if (j.contains("expression") && !j["expression"].is_null()) {
            node->expression = parseExpression(j["expression"]);
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
