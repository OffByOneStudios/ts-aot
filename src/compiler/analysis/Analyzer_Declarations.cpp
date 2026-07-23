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
                currentModule->reDirectExports.insert(func->name);
            }
            if (func->isDefaultExport && currentModule) {
                currentModule->exports->define("default", funcType);
                currentModule->reDirectExports.insert("default");
            }
        } else if (auto var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            // Hoist variable declarations with their declaration kind so the main pass
            // can detect redeclaration conflicts correctly.
            DeclKind dk = DeclKind::Var;
            if (var->varKind == ast::VarKind::Let) dk = DeclKind::Let;
            else if (var->varKind == ast::VarKind::Const) dk = DeclKind::Const;
            declareBindingPattern(var->name.get(), std::make_shared<Type>(TypeKind::Any), dk);
            if (var->isExported && currentModule) {
                if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
                    currentModule->exports->define(id->name, std::make_shared<Type>(TypeKind::Any));
                    currentModule->reDirectExports.insert(id->name);
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
                currentModule->reDirectExports.insert("default");
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

int Analyzer::resolveModuleExport(Module* m, const std::string& name,
                                  std::set<std::pair<std::string, std::string>>& visited,
                                  std::string* definingPath) {
    if (!m) return 1;
    auto key = std::make_pair(m->path, name);
    if (visited.count(key)) return 2;  // circular indirect export
    visited.insert(key);

    // Named indirect export: `export { srcName as name } from srcPath`.
    auto ni = m->reNamedIndirect.find(name);
    if (ni != m->reNamedIndirect.end()) {
        auto srcIt = modules.find(ni->second.first);
        if (srcIt == modules.end()) return 1;
        return resolveModuleExport(srcIt->second.get(), ni->second.second,
                                   visited, definingPath);
    }
    // Local declaration wins over star-exports (ES 16.2.1.6.3 step 6).
    if (m->reDirectExports.count(name)) {
        if (definingPath) *definingPath = m->path;
        return 0;
    }
    // Star exports: ambiguous when found in 2+ DISTINCT defining modules.
    // ES 16.2.1.6.3 step 6: "default" is NEVER provided by `export *` — a
    // default request that wasn't satisfied locally fails without consulting
    // star exports.
    if (name != "default" && !m->reStarSources.empty()) {
        std::set<std::string> definers;
        for (const auto& srcPath : m->reStarSources) {
            auto srcIt = modules.find(srcPath);
            if (srcIt == modules.end()) continue;
            std::string def;
            // Fresh visited per branch, seeded with the walked prefix, so
            // one dead branch doesn't poison a sibling.
            std::set<std::pair<std::string, std::string>> branch = visited;
            if (resolveModuleExport(srcIt->second.get(), name, branch, &def) == 0) {
                definers.insert(def.empty() ? srcPath : def);
            }
        }
        if (definers.size() > 1) return 3;  // ambiguous
        if (definers.size() == 1) {
            if (definingPath) *definingPath = *definers.begin();
            return 0;
        }
    }
    // Fallback: locally-exported names registered through paths that don't
    // fill reDirectExports (defaults, enums, CJS interop) — be lenient.
    if (m->exports && (m->exports->lookup(name) || m->exports->lookupType(name))) {
        if (definingPath) *definingPath = m->path;
        return 0;
    }
    // In-progress module (circular load): its export entries are not fully
    // recorded yet, so NOT_FOUND is unreliable — give the benefit of the
    // doubt. True cycles are still caught above via the visited set.
    if (!m->analyzed) {
        if (definingPath) *definingPath = m->path;
        return 0;
    }
    return 1;
}

// Compose the spec-worded SyntaxError message for a failed ResolveExport.
static std::string linkErrorMessage(int res, const std::string& name,
                                    const std::string& fromPath) {
    if (res == 2)
        return "SyntaxError: Detected cycle while resolving export '" + name +
               "' in module '" + fromPath + "'";
    if (res == 3)
        return "SyntaxError: The requested module '" + fromPath +
               "' contains conflicting star exports for name '" + name + "'";
    return "SyntaxError: The requested module '" + fromPath +
           "' does not provide an export named '" + name + "'";
}

void Analyzer::visitImportDeclaration(ast::ImportDeclaration* node) {
    // Type-only imports: still load module for type resolution,
    // but skip runtime symbol definitions for type-only specifiers
    auto module = loadModule(node->moduleSpecifier);
    if (!module) {
        return;
    }
    staticImportPaths.insert(module->path);

    // ECMA-262 16.2.1.5 Link: a module whose own graph failed ResolveExport
    // (or that failed to parse under the module goal) poisons every module
    // that STATICALLY imports it — the error propagates up to the entry,
    // where analyze() turns it into a compile-time SyntaxError. Dynamic-only
    // import() chains never pass through here and keep the runtime-reject
    // behavior.
    if (!node->isTypeOnly && !module->linkError.empty() && currentModule &&
        currentModule->linkError.empty()) {
        currentModule->linkError = module->linkError;
    }

    // ES 16.2.1.6.3 ResolveExport for the default import binding: for strict
    // ES modules (ESM by resolver/extension, or CJS-marker-free sources) a
    // missing "default" is a link-time SyntaxError. Note "default" is never
    // satisfiable through `export *` (step 6). CJS-marker modules keep the
    // lenient interop (default = module.exports).
    if (!node->defaultImport.empty() && module->ast && !module->isJsonModule &&
        module->type != ModuleType::Declaration && !node->isTypeOnly &&
        (module->isESM || !module->cjsMarkers)) {
        std::set<std::pair<std::string, std::string>> visited;
        int rres = resolveModuleExport(module.get(), "default", visited);
        if (rres != 0 && currentModule && currentModule->linkError.empty()) {
            currentModule->linkError =
                linkErrorMessage(rres, "default", node->moduleSpecifier);
        }
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
        // Phase 9i Bug 2: explicit `import * as X from 'mod'` must bind X
        // to a NamespaceType even when the analyzer pre-registered X as a
        // global ObjectType (which happens for built-in extension modules
        // like `net`, `url`, `crypto` so untyped JS can use them without
        // an import). Without the override, `net.Socket` resolves against
        // the global ObjectType (which only carries the contract's
        // `objects.*` methods, not its `types.*`) and falls through to
        // "Unknown property Socket" because the namespace property dispatch
        // path is never reached. `define` returns false on collision, so
        // fall back to `update` to replace the existing binding.
        if (!symbols.define(node->namespaceImport, nsType)) {
            symbols.update(node->namespaceImport, nsType);
        }
    }

    for (const auto& spec : node->namedImports) {
        std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
        // ES link-time import binding resolution (16.2.1.6.3 ResolveExport):
        // an import whose ResolveExport is circular or ambiguous is a link
        // error of THIS module (import() of it must reject with SyntaxError).
        // NOT_FOUND is also a link error for strict ES modules (ESM by
        // resolver/extension, or CJS-marker-free sources); marker-bearing CJS
        // modules keep the legacy lenient handling below (dynamic exports).
        if (module->ast && !module->isJsonModule &&
            module->type != ModuleType::Declaration &&
            !node->isTypeOnly && !spec.isTypeOnly) {
            std::set<std::pair<std::string, std::string>> visited;
            int rres = resolveModuleExport(module.get(), name, visited);
            bool strictEsm = module->isESM || !module->cjsMarkers;
            if ((rres == 2 || rres == 3 || (rres == 1 && strictEsm)) &&
                currentModule && currentModule->linkError.empty()) {
                currentModule->linkError =
                    linkErrorMessage(rres, name, node->moduleSpecifier);
            }
        }
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
                // Commit 5: type-only imports (`import type { X } from ...`
                // and `import { type X } from ...`) don't require the
                // exported symbol to exist at runtime. Silently define as
                // Any — TypeScript erases these at compile time.
                bool isTypeOnly = node->isTypeOnly || spec.isTypeOnly;
                // Phase 9k: suppress for CommonJS modules whose exports
                // are dynamic. Also define the symbol as Any so that
                // downstream usage doesn't produce "Undefined variable".
                if (!isTypeOnly && (module->type != ModuleType::UntypedJavaScript || module->isESM)) {
                    reportError(fmt::format("Module {} does not export {}", node->moduleSpecifier, name));
                }
                if (isTypeOnly) {
                    symbols.defineType(spec.name, std::make_shared<Type>(TypeKind::Any));
                } else {
                    symbols.define(spec.name, std::make_shared<Type>(TypeKind::Any));
                }
            }
        }
    }
}

void Analyzer::visitExportDeclaration(ast::ExportDeclaration* node) {
    if (!node->moduleSpecifier.empty()) {
        auto module = loadModule(node->moduleSpecifier);
        if (!module) return;
        staticImportPaths.insert(module->path);

        // ECMA-262 16.2.1.5 Link: a statically re-exported module whose own
        // graph failed ResolveExport (or failed to parse under the module
        // goal) poisons this module too; the error bubbles to the entry.
        if (!module->linkError.empty() && currentModule &&
            currentModule->linkError.empty()) {
            currentModule->linkError = module->linkError;
        }

        // ES2020: export * as ns from "module"
        if (!node->namespaceExport.empty()) {
            auto nsType = std::make_shared<NamespaceType>(module);
            currentModule->reDirectExports.insert(node->namespaceExport);
            currentModule->exports->define(node->namespaceExport, nsType);
            SPDLOG_DEBUG("Namespace re-export {} from {} in {}", node->namespaceExport, node->moduleSpecifier, currentModule->path);
            return;
        }

        if (node->isStarExport) {
            currentModule->reStarSources.push_back(module->path);
            // Re-export all from module. ES 16.2.1.6.2 ExportedNames:
            // `export *` NEVER re-exports "default".
            for (auto& [name, sym] : module->exports->getGlobalSymbols()) {
                if (name == "default") continue;
                currentModule->exports->define(name, sym->type);
            }
            for (auto& [name, type] : module->exports->getGlobalTypes()) {
                if (name == "default") continue;
                currentModule->exports->defineType(name, type);
            }
            return;
        }

        for (const auto& spec : node->namedExports) {
            std::string name = spec.propertyName.empty() ? spec.name : spec.propertyName;
            // ES 16.2.1: an indirect export entry must RESOLVE — circular
            // chains and ambiguous star-exports are link errors. Record the
            // entry first (so a later back-edge sees it), then resolve.
            currentModule->reNamedIndirect[spec.name] = {module->path, name};
            if (module->ast && !module->isJsonModule) {
                std::set<std::pair<std::string, std::string>> visited;
                int rres = resolveModuleExport(module.get(), name, visited);
                // CIRCULAR/AMBIGUOUS are definitive link errors; NOT_FOUND
                // stays lenient ONLY for CJS interop (marker-bearing modules
                // with dynamic exports). Marker-free sources are ES modules:
                // an unresolvable indirect export entry is a SyntaxError
                // (ECMA-262 16.2.1.5.1 step 9 InitializeEnvironment).
                if (rres == 1 && !module->isESM && module->cjsMarkers &&
                    module->type == ModuleType::UntypedJavaScript) {
                    rres = 0;
                }
                if (rres != 0) {
                    if (currentModule->linkError.empty()) {
                        currentModule->linkError =
                            linkErrorMessage(rres, name, node->moduleSpecifier);
                    }
                    continue;  // no binding; init stub will throw
                }
            }
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
        currentModule->reDirectExports.insert(spec.name);
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
        currentModule->reDirectExports.insert("default");
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
    staticImportPaths.insert(mod->path);
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

