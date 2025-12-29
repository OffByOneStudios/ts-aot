#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;

void Analyzer::dumpTypes(ast::Program* program) {
    SPDLOG_DEBUG("Analyzer::dumpTypes starting with {} expressions", expressions.size());
    struct TypeEntry {
        int line;
        int column;
        std::string kind;
        std::string type;

        bool operator<(const TypeEntry& other) const {
            if (line != other.line) return line < other.line;
            if (column != other.column) return column < other.column;
            if (kind != other.kind) return kind < other.kind;
            return type < other.type;
        }
    };

    std::vector<TypeEntry> entries;
    for (auto expr : expressions) {
        if (expr->inferredType) {
            entries.push_back({
                expr->line,
                expr->column,
                expr->getKind(),
                expr->inferredType->toString()
            });
        } else {
            // SPDLOG_DEBUG("Expression {} at L{}:C{} has no inferred type", expr->getKind(), expr->line, expr->column);
        }
    }
    SPDLOG_DEBUG("Collected {} type entries", entries.size());

    std::sort(entries.begin(), entries.end());

    for (const auto& entry : entries) {
        fmt::print("L{}:C{} {} -> {}\n", entry.line, entry.column, entry.kind, entry.type);
    }
}

void Analyzer::visitOmittedExpression(ast::OmittedExpression* node) {
    lastType = std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<Type> Analyzer::parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName.empty()) return std::make_shared<Type>(TypeKind::Any);

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

    if (typeName.ends_with("[]")) {
        auto baseName = typeName.substr(0, typeName.size() - 2);
        auto baseType = parseType(baseName, symbols);
        return std::make_shared<ArrayType>(baseType);
    }

    if (typeName.starts_with("[") && typeName.ends_with("]")) {
        std::vector<std::shared_ptr<Type>> elementTypes;
        std::string inner = typeName.substr(1, typeName.size() - 2);
        std::stringstream ss(inner);
        std::string segment;
        while (std::getline(ss, segment, ',')) {
            // Trim whitespace
            segment.erase(0, segment.find_first_not_of(" "));
            segment.erase(segment.find_last_not_of(" ") + 1);
            elementTypes.push_back(parseType(segment, symbols));
        }
        return std::make_shared<TupleType>(elementTypes);
    }

    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    if (typeName == "null") return std::make_shared<Type>(TypeKind::Null);
    if (typeName == "undefined") return std::make_shared<Type>(TypeKind::Undefined);
    
    if (typeName.starts_with("Promise<") && typeName.ends_with(">")) {
        auto innerName = typeName.substr(8, typeName.size() - 9);
        auto innerType = parseType(innerName, symbols);
        auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
        auto wrapped = std::make_shared<ClassType>("Promise");
        if (promiseClass) {
            wrapped->methods = promiseClass->methods;
            wrapped->typeParameters = promiseClass->typeParameters;
        }
        wrapped->typeArguments = { innerType };
        return wrapped;
    }

    auto type = symbols.lookupType(typeName);
    if (type) return type;
    
    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<FunctionType> Analyzer::resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes) {
    if (overloads.empty()) return nullptr;
    
    for (const auto& overload : overloads) {
        size_t minArgs = 0;
        for (size_t i = 0; i < overload->paramTypes.size(); ++i) {
            bool optional = i < overload->isOptional.size() && overload->isOptional[i];
            bool rest = (i == overload->paramTypes.size() - 1) && overload->hasRest;
            if (!optional && !rest) {
                minArgs++;
            }
        }

        size_t maxArgs = overload->hasRest ? std::numeric_limits<size_t>::max() : overload->paramTypes.size();

        if (argTypes.size() >= minArgs && argTypes.size() <= maxArgs) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                std::shared_ptr<Type> expectedType;
                if (i < overload->paramTypes.size()) {
                    expectedType = overload->paramTypes[i];
                    if (i == overload->paramTypes.size() - 1 && overload->hasRest) {
                        if (expectedType->kind == TypeKind::Array) {
                            expectedType = std::static_pointer_cast<ArrayType>(expectedType)->elementType;
                        }
                    }
                } else if (overload->hasRest) {
                    expectedType = overload->paramTypes.back();
                    if (expectedType->kind == TypeKind::Array) {
                        expectedType = std::static_pointer_cast<ArrayType>(expectedType)->elementType;
                    }
                }

                if (expectedType && !argTypes[i]->isAssignableTo(expectedType)) {
                    match = false;
                    break;
                }
            }
            if (match) return overload;
        }
    }
    
    return overloads[0]; // Fallback
}

void Analyzer::reportError(const std::string& message) {
    fmt::print(stderr, "Error: {}\n", message);
    errorCount++;
}

std::shared_ptr<Type> Analyzer::substitute(std::shared_ptr<Type> type, const std::map<std::string, std::shared_ptr<Type>>& env) {
    if (!type) return nullptr;

    if (type->kind == TypeKind::TypeParameter) {
        auto tp = std::static_pointer_cast<TypeParameterType>(type);
        if (env.count(tp->name)) {
            return env.at(tp->name);
        }
        return type;
    }

    if (type->kind == TypeKind::Array) {
        auto arr = std::static_pointer_cast<ArrayType>(type);
        return std::make_shared<ArrayType>(substitute(arr->elementType, env));
    }

    if (type->kind == TypeKind::Function) {
        auto func = std::static_pointer_cast<FunctionType>(type);
        auto result = std::make_shared<FunctionType>();
        for (const auto& p : func->paramTypes) {
            result->paramTypes.push_back(substitute(p, env));
        }
        result->returnType = substitute(func->returnType, env);
        return result;
    }

    if (type->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(type);
        if (cls->typeArguments.empty()) return type;
        
        auto result = std::make_shared<ClassType>(cls->name);
        result->node = cls->node;
        result->baseClass = cls->baseClass;
        result->fields = cls->fields;
        result->methods = cls->methods;
        result->staticMethods = cls->staticMethods;
        result->staticFields = cls->staticFields;
        result->typeParameters = cls->typeParameters;
        for (const auto& arg : cls->typeArguments) {
            result->typeArguments.push_back(substitute(arg, env));
        }
        return result;
    }

    if (type->kind == TypeKind::Interface) {
        auto inter = std::static_pointer_cast<InterfaceType>(type);
        if (inter->typeArguments.empty()) return type;

        auto result = std::make_shared<InterfaceType>(inter->name);
        result->node = inter->node;
        result->fields = inter->fields;
        result->methods = inter->methods;
        result->typeParameters = inter->typeParameters;
        for (const auto& arg : inter->typeArguments) {
            result->typeArguments.push_back(substitute(arg, env));
        }
        return result;
    }

    return type;
}

std::shared_ptr<Module> Analyzer::loadModule(const std::string& specifier) {
    std::string name = specifier;
    if (name.starts_with("node:")) {
        name = name.substr(5);
    }

    if (name == "fs" || name == "path" || name == "crypto" || name == "os" || name == "http" || name == "https" || name == "events" || name == "net" || name == "stream") {
        if (modules.count("builtin:" + name)) {
            return modules["builtin:" + name];
        }
        auto module = std::make_shared<Module>();
        module->path = "builtin:" + name;
        module->analyzed = true;
        
        auto sym = symbols.lookup(name);
        if (sym && sym->type->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(sym->type);
            for (auto& [fieldName, type] : objType->fields) {
                module->exports->define(fieldName, type);
            }
            
            // Special case for fs.promises
            if (name == "fs" && objType->fields.count("promises")) {
                module->exports->define("promises", objType->fields["promises"]);
            }
        }
        
        modules["builtin:" + name] = module;
        return module;
    }

    std::string resolvedPath = resolveModulePath(specifier);
    if (resolvedPath.empty()) {
        reportError("Could not resolve module: " + specifier);
        return nullptr;
    }

    if (modules.count(resolvedPath)) {
        return modules[resolvedPath];
    }

    fmt::print("Loading module: {} from {}\n", specifier, resolvedPath);
    auto module = std::make_shared<Module>();
    module->path = resolvedPath;
    modules[resolvedPath] = module;

    try {
        if (resolvedPath.ends_with(".ts")) {
            std::string jsonPath = resolvedPath + ".json";
            std::string command = "node scripts/dump_ast.js \"" + resolvedPath + "\" \"" + jsonPath + "\"";
            if (system(command.c_str()) != 0) {
                reportError("Failed to run dump_ast.js for " + resolvedPath);
                return nullptr;
            }
            module->ast = std::shared_ptr<ast::Program>(ast::loadAst(jsonPath).release());
        } else {
            module->ast = std::shared_ptr<ast::Program>(ast::loadAst(resolvedPath).release());
        }
        analyzeModule(module);
    } catch (const std::exception& e) {
        reportError("Failed to load module " + resolvedPath + ": " + e.what());
        return nullptr;
    }

    return module;
}

} // namespace ts

