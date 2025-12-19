#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;
void Analyzer::visitProgram(ast::Program* node) {
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
                    if (auto num = dynamic_cast<ast::NumericLiteral*>(member.initializer.get())) {
                        nextValue = (int)num->value;
                    }
                }
                enumType->members[member.name] = nextValue++;
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
    auto module = loadModule(node->moduleSpecifier);
    if (!module) return;

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
            fmt::print("Importing symbol {} as {} from {}\n", name, spec.name, node->moduleSpecifier);
            symbols.define(spec.name, sym->type);
        } else {
            auto type = module->exports->lookupType(name);
            if (type) {
                fmt::print("Importing type {} as {} from {}\n", name, spec.name, node->moduleSpecifier);
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
                fmt::print("Re-exporting symbol {} from {} in {}\n", name, node->moduleSpecifier, currentModule->path);
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

} // namespace ts

