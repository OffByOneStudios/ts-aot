#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

using json = nlohmann::json;

namespace ast {

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
        if (j.contains("typeArguments")) {
            for (const auto& ta : j["typeArguments"]) {
                node->typeArguments.push_back(ta.get<std::string>());
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
        if (j.contains("typeArguments")) {
            for (const auto& ta : j["typeArguments"]) {
                node->typeArguments.push_back(ta.get<std::string>());
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
    } else if (kind == "RegularExpressionLiteral") {
        auto node = std::make_unique<RegularExpressionLiteral>();
        node->text = j["text"];
        return node;
    } else if (kind == "NumericLiteral") {
        auto node = std::make_unique<NumericLiteral>();
        node->value = j["value"];
        return node;
    } else if (kind == "BooleanLiteral") {
        auto node = std::make_unique<BooleanLiteral>();
        node->value = j["value"];
        return node;
    } else if (kind == "AwaitExpression") {
        auto node = std::make_unique<AwaitExpression>();
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "PrefixUnaryExpression") {
        auto node = std::make_unique<PrefixUnaryExpression>();
        node->op = j["operator"];
        node->operand = parseExpression(j["operand"]);
        return node;
    } else if (kind == "PostfixUnaryExpression") {
        auto node = std::make_unique<PostfixUnaryExpression>();
        node->op = j["operator"];
        node->operand = parseExpression(j["operand"]);
        return node;
    } else if (kind == "AsExpression") {
        auto node = std::make_unique<AsExpression>();
        node->expression = parseExpression(j["expression"]);
        node->type = j["type"];
        return node;
    } else if (kind == "SpreadElement") {
        auto node = std::make_unique<SpreadElement>();
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "OmittedExpression") {
        return std::make_unique<OmittedExpression>();
    } else if (kind == "ArrowFunction") {
        auto node = std::make_unique<ArrowFunction>();
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
    } else if (kind == "SuperExpression") {
        return std::make_unique<SuperExpression>();
    }
    
    throw std::runtime_error("Unknown expression kind: " + kind);
}

} // namespace ast
