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
        setLocation(node.get(), j);
        node->op = j["operator"];
        node->left = parseExpression(j["left"]);
        node->right = parseExpression(j["right"]);
        return node;
    } else if (kind == "ConditionalExpression") {
        auto node = std::make_unique<ConditionalExpression>();
        setLocation(node.get(), j);
        node->condition = parseExpression(j["condition"]);
        node->whenTrue = parseExpression(j["whenTrue"]);
        node->whenFalse = parseExpression(j["whenFalse"]);
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
            auto p = std::make_unique<PropertyAssignment>();
            p->name = prop["name"];
            p->initializer = parseExpression(prop["initializer"]);
            node->properties.push_back(std::move(p));
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
        return node;
    } else if (kind == "StringLiteral") {
        auto node = std::make_unique<StringLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "RegularExpressionLiteral") {
        auto node = std::make_unique<RegularExpressionLiteral>();
        setLocation(node.get(), j);
        node->text = j["text"];
        return node;
    } else if (kind == "NumericLiteral") {
        auto node = std::make_unique<NumericLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "ParenthesizedExpression") {
        return parseExpression(j["expression"]);
    } else if (kind == "BooleanLiteral") {
        auto node = std::make_unique<BooleanLiteral>();
        setLocation(node.get(), j);
        node->value = j["value"];
        return node;
    } else if (kind == "NullLiteral") {
        auto node = std::make_unique<NullLiteral>();
        setLocation(node.get(), j);
        return node;
    } else if (kind == "UndefinedLiteral") {
        auto node = std::make_unique<UndefinedLiteral>();
        setLocation(node.get(), j);
        return node;
    } else if (kind == "AwaitExpression") {
        auto node = std::make_unique<AwaitExpression>();
        setLocation(node.get(), j);
        node->expression = parseExpression(j["expression"]);
        return node;
    } else if (kind == "YieldExpression") {
        auto node = std::make_unique<YieldExpression>();
        setLocation(node.get(), j);
        if (j.contains("expression") && !j["expression"].is_null()) {
            node->expression = parseExpression(j["expression"]);
        }
        if (j.contains("isAsterisk")) {
            node->isAsterisk = j["isAsterisk"];
        }
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
        for (const auto& stmt : j["body"]) {
            node->body.push_back(parseStatement(stmt));
        }
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

} // namespace ast
