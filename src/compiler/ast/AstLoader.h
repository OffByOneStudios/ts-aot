#pragma once
#include "AstNodes.h"
#include <string>
#include <memory>

#include <nlohmann/json.hpp>

namespace ast {
    std::unique_ptr<Program> loadAst(const std::string& jsonPath);

    // Internal parsing functions used across split files
    ExprPtr parseExpression(const nlohmann::json& j);
    StmtPtr parseStatement(const nlohmann::json& j);
    NodePtr parseClassMember(const nlohmann::json& j);
    NodePtr parseNode(const nlohmann::json& j);
    std::unique_ptr<Parameter> parseParameter(const nlohmann::json& j);
    std::unique_ptr<TypeParameter> parseTypeParameter(const nlohmann::json& j);
    void parseDecorators(std::vector<std::string>& decorators, const nlohmann::json& j);
    void setLocation(Node* node, const nlohmann::json& j);

    void printAst(const Node* node, int indent = 0);
}
