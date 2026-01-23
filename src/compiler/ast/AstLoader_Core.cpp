#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fmt/format.h>

using json = nlohmann::json;

namespace ast {

void setLocation(Node* node, const json& j) {
    if (!node) return;
    if (!j.is_object()) return;
    
    if (j.contains("line") && !j["line"].is_null()) {
        try {
            if (j["line"].is_number()) {
                node->line = j["line"].get<int>();
            } else if (j["line"].is_string()) {
                node->line = std::stoi(j["line"].get<std::string>());
            }
        } catch (...) {}
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
    if (j.contains("default") && !j["default"].is_null()) {
        node->defaultType = j["default"].get<std::string>();
    }
    return node;
}

void parseDecorators(std::vector<std::string>& decorators, const json& j) {
    if (j.contains("decorators") && j["decorators"].is_array()) {
        for (const auto& d : j["decorators"]) {
            decorators.push_back(d.get<std::string>());
        }
    }
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
    } else if (kind == "ComputedPropertyName") {
        auto node = std::make_unique<ComputedPropertyName>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "MethodDefinition") {
        return parseClassMember(j);
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
    } else if (kind == "ComputedPropertyName") {
        auto node = std::make_unique<ComputedPropertyName>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
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
        node->name = j["name"];
        if (j.contains("nameNode") && !j["nameNode"].is_null()) {
            node->nameNode = parseNode(j["nameNode"]);
        }
        node->isAsync = j.value("isAsync", false);
        node->isGenerator = j.value("isGenerator", false);
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
        parseDecorators(node->decorators, j);
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
    } else if (kind == "IndexSignature") {
        auto node = std::make_unique<IndexSignature>();
        setLocation(node.get(), j);
        node->keyType = j.value("keyType", "string");
        node->valueType = j.value("valueType", "any");
        if (j.contains("isReadonly")) {
            node->isReadonly = j["isReadonly"].get<bool>();
        }
        return node;
    }
    throw std::runtime_error("Unknown class member kind: " + kind);
}

std::unique_ptr<Program> loadAst(const std::string& jsonPath) {
    std::ifstream i(jsonPath);
    if (!i.is_open()) {
        throw std::runtime_error("Could not open file: " + jsonPath);
    }
    
    json j;
    i >> j;
    
    auto program = std::make_unique<Program>();
    auto flattenAndAdd = [&](std::unique_ptr<Statement> stmt) {
        // If the statement is a BlockStatement from MultipleVariableDeclarations expansion,
        // flatten it by adding each statement directly
        if (auto block = dynamic_cast<BlockStatement*>(stmt.get())) {
            for (auto& s : block->statements) {
                program->body.push_back(std::move(s));
            }
        } else {
            program->body.push_back(std::move(stmt));
        }
    };
    
    if (j.contains("body")) {
        for (const auto& stmt : j["body"]) {
            flattenAndAdd(parseStatement(stmt));
        }
    } else if (j.contains("statements")) {
        for (const auto& stmt : j["statements"]) {
            flattenAndAdd(parseStatement(stmt));
        }
    }
    
    return program;
}

} // namespace ast
