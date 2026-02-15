#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <sstream>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;
void Analyzer::visitProgram(ast::Program* node) {
    // Check for "use strict" directive at the beginning of the program
    globalStrictMode = false;
    strictMode = false;
    if (!node->body.empty()) {
        if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node->body[0].get())) {
            if (auto strLit = dynamic_cast<ast::StringLiteral*>(exprStmt->expression.get())) {
                if (strLit->value == "use strict") {
                    globalStrictMode = true;
                    strictMode = true;
                }
            }
        }
    }

    // Pass 1: Hoist declarations to support circular dependencies
    for (auto& stmt : node->body) {
        if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            auto funcType = std::make_shared<FunctionType>();
            funcType->node = func;
            symbols.define(func->name, funcType);
            if (func->isExported && currentModule) {
                currentModule->exports->define(func->name, funcType);
            }
            if (func->isDefaultExport && currentModule) {
                currentModule->exports->define("default", funcType);
            }
        } else if (auto var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            // Hoist variable declarations so JS/TS 'var' semantics work for nested function bodies.
            // We currently don't distinguish var/let/const in the AST, so we hoist all variable
            // names as Any to avoid spurious "undefined variable" errors in common JS patterns.
            declareBindingPattern(var->name.get(), std::make_shared<Type>(TypeKind::Any));
            if (var->isExported && currentModule) {
                if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
                    currentModule->exports->define(id->name, std::make_shared<Type>(TypeKind::Any));
                }
            }
        } else if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
            auto classType = std::make_shared<ClassType>(cls->name);
            classType->node = cls;
            symbols.defineType(cls->name, classType);
            if (cls->isExported && currentModule) {
                currentModule->exports->defineType(cls->name, classType);
            }
            if (cls->isDefaultExport && currentModule) {
                currentModule->exports->defineType("default", classType);
            }
        } else if (auto inter = dynamic_cast<ast::InterfaceDeclaration*>(stmt.get())) {
            auto interfaceType = std::make_shared<InterfaceType>(inter->name);
            symbols.defineType(inter->name, interfaceType);
            if (inter->isExported && currentModule) {
                currentModule->exports->defineType(inter->name, interfaceType);
            }
            if (inter->isDefaultExport && currentModule) {
                currentModule->exports->defineType("default", interfaceType);
            }
        } else if (auto alias = dynamic_cast<ast::TypeAliasDeclaration*>(stmt.get())) {
            // We can't fully resolve the type yet because it might depend on other hoisted types,
            // but we can register the name.
            // For now, let's just let the second pass handle it, or do a partial registration.
            // Actually, type aliases are often used in function signatures, so hoisting is good.
            auto type = parseType(alias->type, symbols);
            symbols.defineType(alias->name, type);
            if (alias->isExported && currentModule) {
                currentModule->exports->defineType(alias->name, type);
            }
        } else if (auto enm = dynamic_cast<ast::EnumDeclaration*>(stmt.get())) {
            auto enumType = std::make_shared<EnumType>(enm->name);
            int nextValue = 0;
            for (const auto& member : enm->members) {
                if (member.initializer) {
                    if (auto str = dynamic_cast<ast::StringLiteral*>(member.initializer.get())) {
                        // String enum member
                        enumType->members[member.name] = str->value;
                        // String members don't affect nextValue
                    } else {
                        // Try to evaluate as a constant expression
                        auto constVal = evaluateConstantExpression(member.initializer.get(), enumType->members);
                        if (constVal) {
                            nextValue = static_cast<int>(*constVal);
                            enumType->members[member.name] = nextValue++;
                        } else {
                            // Fallback to auto-increment if evaluation fails
                            SPDLOG_WARN("Could not evaluate computed enum member '{}' - using auto-increment", member.name);
                            enumType->members[member.name] = nextValue++;
                        }
                    }
                } else {
                    // No initializer - use auto-incremented numeric value
                    enumType->members[member.name] = nextValue++;
                }
            }
            symbols.define(enm->name, enumType);
            symbols.defineType(enm->name, enumType);
            if (enm->isExported && currentModule) {
                currentModule->exports->define(enm->name, enumType);
                currentModule->exports->defineType(enm->name, enumType);
            }
        }
    }

    // Pass 2: Full analysis
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visitImportDeclaration(ast::ImportDeclaration* node) {
    // Type-only imports: still load module for type resolution,
    // but skip runtime symbol definitions for type-only specifiers
    auto module = loadModule(node->moduleSpecifier);
    if (!module) {
        return;
    }

    // Import symbols
    if (!node->defaultImport.empty()) {
        auto sym = module->exports->lookup("default");
        if (sym) {
            symbols.define(node->defaultImport, sym->type);
        } else {
            auto type = module->exports->lookupType("default");
            if (type) {
                symbols.defineType(node->defaultImport, type);
            } else {
                reportError(fmt::format("Module {} does not have a default export", node->moduleSpecifier));
            }
        }
    }

    if (!node->namespaceImport.empty()) {
        auto nsType = std::make_shared<NamespaceType>(module);
        symbols.define(node->namespaceImport, nsType);
    }

    for (const auto& spec : node->namedImports) {
        std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
        auto sym = module->exports->lookup(name);
        if (sym) {
            SPDLOG_DEBUG("Importing symbol {} as {} from {}", name, spec.name, node->moduleSpecifier);
            symbols.define(spec.name, sym->type);
        } else {
            auto type = module->exports->lookupType(name);
            if (type) {
                SPDLOG_DEBUG("Importing type {} as {} from {}", name, spec.name, node->moduleSpecifier);
                symbols.defineType(spec.name, type);
            } else {
                reportError(fmt::format("Module {} does not export {}", node->moduleSpecifier, name));
            }
        }
    }
}

void Analyzer::visitExportDeclaration(ast::ExportDeclaration* node) {
    if (!node->moduleSpecifier.empty()) {
        auto module = loadModule(node->moduleSpecifier);
        if (!module) return;

        // ES2020: export * as ns from "module"
        if (!node->namespaceExport.empty()) {
            auto nsType = std::make_shared<NamespaceType>(module);
            currentModule->exports->define(node->namespaceExport, nsType);
            SPDLOG_DEBUG("Namespace re-export {} from {} in {}", node->namespaceExport, node->moduleSpecifier, currentModule->path);
            return;
        }

        if (node->isStarExport) {
            // Re-export all from module
            for (auto& [name, sym] : module->exports->getGlobalSymbols()) {
                currentModule->exports->define(name, sym->type);
            }
            for (auto& [name, type] : module->exports->getGlobalTypes()) {
                currentModule->exports->defineType(name, type);
            }
            return;
        }

        for (const auto& spec : node->namedExports) {
            std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
            auto sym = module->exports->lookup(name);
            if (sym) {
                SPDLOG_DEBUG("Re-exporting symbol {} from {} in {}", name, node->moduleSpecifier, currentModule->path);
                currentModule->exports->define(spec.name, sym->type);
            } else {
                auto type = module->exports->lookupType(name);
                if (type) {
                    currentModule->exports->defineType(spec.name, type);
                } else {
                    reportError(fmt::format("Module {} does not export {}", node->moduleSpecifier, name));
                }
            }
        }
        return;
    }

    for (const auto& spec : node->namedExports) {
        std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
        auto sym = symbols.lookup(name);
        if (sym) {
            currentModule->exports->define(spec.name, sym->type);
        } else {
            auto type = symbols.lookupType(name);
            if (type) {
                currentModule->exports->defineType(spec.name, type);
            } else {
                reportError(fmt::format("Symbol {} not found for export", name));
            }
        }
    }
}

void Analyzer::visitExportAssignment(ast::ExportAssignment* node) {
    visit(node->expression.get());
    if (currentModule) {
        currentModule->exports->define("default", lastType);
    }
}

void Analyzer::visitNamespaceDeclaration(ast::NamespaceDeclaration* node) {
    if (node->isAmbientModule) {
        // declare module 'string-literal' — ambient module declaration
        // Create a synthetic module for the ambient module name
        auto ambientModule = std::make_shared<Module>();
        ambientModule->path = "ambient:" + node->name;
        ambientModule->type = ModuleType::Declaration;
        ambientModule->analyzed = true;

        auto savedModule = currentModule;
        currentModule = ambientModule;

        // Visit body — members marked isExported will add to ambientModule->exports
        for (auto& stmt : node->body) {
            visit(stmt.get());
        }

        currentModule = savedModule;

        // Register by bare name so loadModule('name') can find it
        modules[node->name] = ambientModule;

        // Also copy exports to the file-level module (common single-module case:
        // @types/lodash/index.d.ts contains declare module 'lodash' { ... })
        if (savedModule) {
            for (const auto& [name, sym] : ambientModule->exports->getCurrentScopeSymbols()) {
                savedModule->exports->define(name, sym->type);
            }
            for (const auto& [name, type] : ambientModule->exports->getCurrentScopeTypes()) {
                savedModule->exports->defineType(name, type);
            }
        }
        return;
    }

    // For .d.ts files, namespace bodies contain type aliases, interfaces, etc.
    // Visit them to register types in the current scope.
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visitImportEqualsDeclaration(ast::ImportEqualsDeclaration* node) {
    // import X = require('module') — CommonJS-style import
    auto mod = loadModule(node->moduleSpecifier);
    if (!mod) return;
    // Try default export first (for `export = X` or `export default X`)
    auto defaultExport = mod->exports->lookup("default");
    if (defaultExport) {
        symbols.define(node->name, defaultExport->type);
    } else {
        // Treat as namespace import (like import * as X from 'module')
        auto nsType = std::make_shared<NamespaceType>(mod);
        symbols.define(node->name, nsType);
    }
}

void Analyzer::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {
    auto type = parseType(node->type, symbols);
    symbols.defineType(node->name, type);
    if (node->isExported && currentModule) {
        currentModule->exports->defineType(node->name, type);
    }
}

void Analyzer::visitEnumDeclaration(ast::EnumDeclaration* node) {
    // Already handled in hoisting pass
}

std::optional<int64_t> Analyzer::evaluateConstantExpression(
    ast::Expression* expr,
    const std::map<std::string, std::variant<int, std::string>>& enumMembers) {

    if (auto num = dynamic_cast<ast::NumericLiteral*>(expr)) {
        return static_cast<int64_t>(num->value);
    }

    if (auto ident = dynamic_cast<ast::Identifier*>(expr)) {
        // Check if this is a reference to another enum member
        auto it = enumMembers.find(ident->name);
        if (it != enumMembers.end() && std::holds_alternative<int>(it->second)) {
            return static_cast<int64_t>(std::get<int>(it->second));
        }
        return std::nullopt;
    }

    if (auto binary = dynamic_cast<ast::BinaryExpression*>(expr)) {
        auto left = evaluateConstantExpression(binary->left.get(), enumMembers);
        auto right = evaluateConstantExpression(binary->right.get(), enumMembers);
        if (!left || !right) return std::nullopt;

        if (binary->op == "+") return *left + *right;
        if (binary->op == "-") return *left - *right;
        if (binary->op == "*") return *left * *right;
        if (binary->op == "/") {
            if (*right == 0) return std::nullopt;
            return *left / *right;
        }
        if (binary->op == "%") {
            if (*right == 0) return std::nullopt;
            return *left % *right;
        }
        if (binary->op == "<<") return *left << *right;
        if (binary->op == ">>") return *left >> *right;
        if (binary->op == "&") return *left & *right;
        if (binary->op == "|") return *left | *right;
        if (binary->op == "^") return *left ^ *right;
        return std::nullopt;
    }

    if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(expr)) {
        // Handle string.length
        if (prop->name == "length") {
            if (auto str = dynamic_cast<ast::StringLiteral*>(prop->expression.get())) {
                return static_cast<int64_t>(str->value.length());
            }
        }
        return std::nullopt;
    }

    if (auto prefix = dynamic_cast<ast::PrefixUnaryExpression*>(expr)) {
        auto operand = evaluateConstantExpression(prefix->operand.get(), enumMembers);
        if (!operand) return std::nullopt;

        if (prefix->op == "-") return -(*operand);
        if (prefix->op == "+") return *operand;
        if (prefix->op == "~") return ~(*operand);
        return std::nullopt;
    }

    if (auto call = dynamic_cast<ast::CallExpression*>(expr)) {
        // Handle Math.floor, Math.ceil, Math.round, etc.
        if (auto propAccess = dynamic_cast<ast::PropertyAccessExpression*>(call->callee.get())) {
            if (auto mathIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
                if (mathIdent->name == "Math" && call->arguments.size() == 1) {
                    auto arg = evaluateConstantExpression(call->arguments[0].get(), enumMembers);
                    if (!arg) return std::nullopt;
                    double val = static_cast<double>(*arg);

                    if (propAccess->name == "floor") return static_cast<int64_t>(std::floor(val));
                    if (propAccess->name == "ceil") return static_cast<int64_t>(std::ceil(val));
                    if (propAccess->name == "round") return static_cast<int64_t>(std::round(val));
                    if (propAccess->name == "trunc") return static_cast<int64_t>(std::trunc(val));
                    if (propAccess->name == "abs") return static_cast<int64_t>(std::abs(val));
                }
            }
        }
        return std::nullopt;
    }

    if (auto paren = dynamic_cast<ast::ParenthesizedExpression*>(expr)) {
        return evaluateConstantExpression(paren->expression.get(), enumMembers);
    }

    return std::nullopt;
}

} // namespace ts

