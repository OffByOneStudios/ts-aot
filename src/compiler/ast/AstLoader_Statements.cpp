#include "AstLoader.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace ast {

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
        if (j.contains("isStruct")) {
            node->isStruct = j["isStruct"];
        }
        parseDecorators(node->decorators, j);
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
        if (j.contains("returnType")) node->returnType = j["returnType"];
        parseDecorators(node->decorators, j);
        for (const auto& stmt : j["body"]) {
            auto parsed = parseStatement(stmt);
            // Flatten BlockStatements from MultipleVariableDeclarations expansion
            if (auto block = dynamic_cast<BlockStatement*>(parsed.get())) {
                for (auto& s : block->statements) {
                    node->body.push_back(std::move(s));
                }
            } else {
                node->body.push_back(std::move(parsed));
            }
        }
        return node;
    } else if (kind == "MultipleVariableDeclarations") {
        // Special case: multiple declarators in a single var statement
        // Expand into separate VariableDeclaration statements
        auto block = std::make_unique<BlockStatement>();
        setLocation(block.get(), j);
        size_t declCount = j["declarations"].size();
        for (const auto& declJson : j["declarations"]) {
            auto node = std::make_unique<VariableDeclaration>();
            setLocation(node.get(), declJson);
            node->name = parseNode(declJson["name"]);
            if (declJson.contains("isExported")) {
                node->isExported = declJson["isExported"];
            }
            if (declJson.contains("type")) node->type = declJson["type"];
            if (declJson.contains("initializer") && !declJson["initializer"].is_null()) {
                node->initializer = parseExpression(declJson["initializer"]);
            }
            block->statements.push_back(std::move(node));
        }
        // Unwrap if there's only one statement (shouldn't happen but be safe)
        if (block->statements.size() == 1) {
            return std::move(block->statements[0]);
        }
        return block;
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
        for (const auto& stmt : j["body"]) {
            auto parsed = parseStatement(stmt);
            // Flatten BlockStatements from MultipleVariableDeclarations expansion
            if (auto block = dynamic_cast<BlockStatement*>(parsed.get())) {
                for (auto& s : block->statements) {
                    node->statements.push_back(std::move(s));
                }
            } else {
                node->statements.push_back(std::move(parsed));
            }
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
        node->isAwait = j.value("isAwait", false);
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
                cc->expression = parseExpression(clause["expression"]);
                for (const auto& stmt : clause["statements"]) {
                    cc->statements.push_back(parseStatement(stmt));
                }
                node->clauses.push_back(std::move(cc));
            } else {
                auto dc = std::make_unique<DefaultClause>();
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
        return node;
    } else if (kind == "ContinueStatement") {
        auto node = std::make_unique<ContinueStatement>();
        setLocation(node.get(), j);
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
        if (j.contains("namespaceExport") && !j["namespaceExport"].is_null()) {
            node->namespaceExport = j["namespaceExport"].get<std::string>();
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

} // namespace ast
