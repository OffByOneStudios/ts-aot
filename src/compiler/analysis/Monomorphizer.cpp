#include "Monomorphizer.h"
#include "Analyzer.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <set>
#include <iostream>
#include <filesystem>

namespace ts {

// File-static analyzer pointer, set during rewriteRequire pass
static Analyzer* s_rewriteAnalyzer = nullptr;

// Convert a JSON value to an AST expression (for inlining require('./foo.json'))
static std::unique_ptr<ast::Expression> jsonToAstExpr(const nlohmann::json& j) {
    if (j.is_null()) {
        return std::make_unique<ast::NullLiteral>();
    }
    if (j.is_boolean()) {
        auto lit = std::make_unique<ast::BooleanLiteral>();
        lit->value = j.get<bool>();
        return lit;
    }
    if (j.is_number()) {
        auto lit = std::make_unique<ast::NumericLiteral>();
        lit->value = j.get<double>();
        return lit;
    }
    if (j.is_string()) {
        auto lit = std::make_unique<ast::StringLiteral>();
        lit->value = j.get<std::string>();
        return lit;
    }
    if (j.is_array()) {
        auto arr = std::make_unique<ast::ArrayLiteralExpression>();
        for (const auto& elem : j) {
            arr->elements.push_back(jsonToAstExpr(elem));
        }
        return arr;
    }
    if (j.is_object()) {
        auto obj = std::make_unique<ast::ObjectLiteralExpression>();
        for (auto& [key, val] : j.items()) {
            auto prop = std::make_unique<ast::PropertyAssignment>();
            prop->name = key;
            prop->initializer = jsonToAstExpr(val);
            obj->properties.push_back(std::move(prop));
        }
        return obj;
    }
    // Fallback: null
    return std::make_unique<ast::NullLiteral>();
}

static ast::Expression* unwrapParens(ast::Expression* expr) {
    while (expr) {
        auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(expr);
        if (!paren || !paren->expression) break;
        expr = paren->expression.get();
    }
    return expr;
}

static std::unique_ptr<ast::Statement> makeConsoleLogStatement(const std::string& msg) {
    auto logCall = std::make_unique<ast::CallExpression>();
    auto logAccess = std::make_unique<ast::PropertyAccessExpression>();
    auto consoleId = std::make_unique<ast::Identifier>();
    consoleId->name = "console";
    logAccess->expression = std::move(consoleId);
    logAccess->name = "log";
    logCall->callee = std::move(logAccess);

    auto msgLit = std::make_unique<ast::StringLiteral>();
    msgLit->value = msg;
    logCall->arguments.push_back(std::move(msgLit));

    auto logStmt = std::make_unique<ast::ExpressionStatement>();
    logStmt->expression = std::move(logCall);
    return logStmt;
}

// Forward declarations for require() rewriting
static void rewriteRequireInExpr(ast::Expression* expr,
    ModuleResolver& resolver, const std::string& fromPath);
static void rewriteRequireInExprOwned(std::unique_ptr<ast::Expression>& expr,
    ModuleResolver& resolver, const std::string& fromPath);
static void rewriteRequireInStmt(ast::Statement* stmt,
    ModuleResolver& resolver, const std::string& fromPath);

// Walk a single expression, rewriting require('literal') → ts_module_get_cached('absolutePath')
static void rewriteRequireInExpr(ast::Expression* expr,
    ModuleResolver& resolver, const std::string& fromPath) {
    if (!expr) return;

    // Match: require('literal')
    if (auto* call = dynamic_cast<ast::CallExpression*>(expr)) {
        auto* id = dynamic_cast<ast::Identifier*>(call->callee.get());
        if (id && id->name == "require" && call->arguments.size() >= 1) {
            auto* lit = dynamic_cast<ast::StringLiteral*>(call->arguments[0].get());
            if (lit) {
                auto resolved = resolver.resolve(lit->value, fs::path(fromPath));
                if (resolved.isValid() && resolved.type != ModuleType::Builtin) {
                    id->name = "ts_module_get_cached";
                    lit->value = resolved.path;
                    // Trim to exactly 1 argument
                    if (call->arguments.size() > 1) {
                        call->arguments.resize(1);
                    }
                    call->inferredType = std::make_shared<Type>(TypeKind::Any);
                    SPDLOG_DEBUG("Rewrote require('{}') -> ts_module_get_cached('{}') in {}",
                        lit->value, resolved.path, fromPath);
                }
            }
        }
        // Recurse into callee and arguments regardless (for nested requires)
        rewriteRequireInExprOwned(call->callee, resolver, fromPath);
        for (auto& arg : call->arguments) {
            rewriteRequireInExprOwned(arg, resolver, fromPath);
        }
        return;
    }

    if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(expr)) {
        rewriteRequireInExpr(paren->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* bin = dynamic_cast<ast::BinaryExpression*>(expr)) {
        rewriteRequireInExpr(bin->left.get(), resolver, fromPath);
        rewriteRequireInExpr(bin->right.get(), resolver, fromPath);
        return;
    }

    if (auto* assign = dynamic_cast<ast::AssignmentExpression*>(expr)) {
        rewriteRequireInExpr(assign->left.get(), resolver, fromPath);
        rewriteRequireInExpr(assign->right.get(), resolver, fromPath);
        return;
    }

    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(expr)) {
        rewriteRequireInExpr(cond->condition.get(), resolver, fromPath);
        rewriteRequireInExpr(cond->whenTrue.get(), resolver, fromPath);
        rewriteRequireInExpr(cond->whenFalse.get(), resolver, fromPath);
        return;
    }

    if (auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(expr)) {
        rewriteRequireInExprOwned(prop->expression, resolver, fromPath);
        return;
    }

    if (auto* elem = dynamic_cast<ast::ElementAccessExpression*>(expr)) {
        rewriteRequireInExpr(elem->expression.get(), resolver, fromPath);
        rewriteRequireInExpr(elem->argumentExpression.get(), resolver, fromPath);
        return;
    }

    if (auto* newExpr = dynamic_cast<ast::NewExpression*>(expr)) {
        rewriteRequireInExpr(newExpr->expression.get(), resolver, fromPath);
        for (auto& arg : newExpr->arguments) {
            rewriteRequireInExpr(arg.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(expr)) {
        for (auto& el : arr->elements) {
            rewriteRequireInExpr(el.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(expr)) {
        for (auto& propNode : obj->properties) {
            if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(propNode.get())) {
                rewriteRequireInExpr(pa->initializer.get(), resolver, fromPath);
            }
        }
        return;
    }

    if (auto* tmpl = dynamic_cast<ast::TemplateExpression*>(expr)) {
        for (auto& span : tmpl->spans) {
            rewriteRequireInExpr(span.expression.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* tagged = dynamic_cast<ast::TaggedTemplateExpression*>(expr)) {
        rewriteRequireInExpr(tagged->tag.get(), resolver, fromPath);
        rewriteRequireInExpr(tagged->templateExpr.get(), resolver, fromPath);
        return;
    }

    if (auto* spread = dynamic_cast<ast::SpreadElement*>(expr)) {
        rewriteRequireInExpr(spread->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* awaitExpr = dynamic_cast<ast::AwaitExpression*>(expr)) {
        rewriteRequireInExpr(awaitExpr->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* yieldExpr = dynamic_cast<ast::YieldExpression*>(expr)) {
        rewriteRequireInExpr(yieldExpr->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* prefix = dynamic_cast<ast::PrefixUnaryExpression*>(expr)) {
        rewriteRequireInExprOwned(prefix->operand, resolver, fromPath);
        return;
    }

    if (auto* postfix = dynamic_cast<ast::PostfixUnaryExpression*>(expr)) {
        rewriteRequireInExpr(postfix->operand.get(), resolver, fromPath);
        return;
    }

    if (auto* asExpr = dynamic_cast<ast::AsExpression*>(expr)) {
        rewriteRequireInExpr(asExpr->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* nonNull = dynamic_cast<ast::NonNullExpression*>(expr)) {
        rewriteRequireInExpr(nonNull->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* delExpr = dynamic_cast<ast::DeleteExpression*>(expr)) {
        rewriteRequireInExpr(delExpr->expression.get(), resolver, fromPath);
        return;
    }

    // Recurse into function bodies (for IIFEs and factory functions)
    if (auto* funcExpr = dynamic_cast<ast::FunctionExpression*>(expr)) {
        for (auto& bodyStmt : funcExpr->body) {
            rewriteRequireInStmt(bodyStmt.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* arrow = dynamic_cast<ast::ArrowFunction*>(expr)) {
        // Body can be BlockStatement or an expression
        if (auto* block = dynamic_cast<ast::BlockStatement*>(arrow->body.get())) {
            for (auto& bodyStmt : block->statements) {
                rewriteRequireInStmt(bodyStmt.get(), resolver, fromPath);
            }
        } else if (auto* bodyExpr = dynamic_cast<ast::Expression*>(arrow->body.get())) {
            rewriteRequireInExpr(bodyExpr, resolver, fromPath);
        }
        return;
    }
}

// Owned-pointer variant: can replace the expression entirely (e.g., require.resolve → StringLiteral)
static void rewriteRequireInExprOwned(std::unique_ptr<ast::Expression>& expr,
    ModuleResolver& resolver, const std::string& fromPath) {
    if (!expr) return;

    // Match: require.resolve('literal') → StringLiteral(resolvedPath)
    if (auto* call = dynamic_cast<ast::CallExpression*>(expr.get())) {
        if (auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(call->callee.get())) {
            if (auto* id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (id->name == "require" && prop->name == "resolve"
                    && call->arguments.size() >= 1) {
                    if (auto* lit = dynamic_cast<ast::StringLiteral*>(call->arguments[0].get())) {
                        auto resolved = resolver.resolve(lit->value, fs::path(fromPath));
                        if (resolved.isValid()) {
                            auto replacement = std::make_unique<ast::StringLiteral>();
                            replacement->value = resolved.path;
                            replacement->inferredType = std::make_shared<Type>(TypeKind::String);
                            SPDLOG_DEBUG("Rewrote require.resolve('{}') -> '{}' in {}",
                                lit->value, resolved.path, fromPath);
                            expr = std::move(replacement);
                            return;
                        }
                    }
                }
            }
        }
    }

    // Match: import('literal') → ts_dynamic_import(resolvedPath) for string literal specifiers
    if (auto* dynImport = dynamic_cast<ast::DynamicImport*>(expr.get())) {
        if (auto* lit = dynamic_cast<ast::StringLiteral*>(dynImport->moduleSpecifier.get())) {
            auto resolved = resolver.resolve(lit->value, fs::path(fromPath));
            if (resolved.isValid() && resolved.type != ModuleType::Builtin) {
                // Rewrite to: ts_dynamic_import(resolvedAbsPath)
                auto replacement = std::make_unique<ast::CallExpression>();
                auto calleeId = std::make_unique<ast::Identifier>();
                calleeId->name = "ts_dynamic_import";
                replacement->callee = std::move(calleeId);
                auto pathLit = std::make_unique<ast::StringLiteral>();
                pathLit->value = resolved.path;
                replacement->arguments.push_back(std::move(pathLit));
                replacement->inferredType = dynImport->inferredType;
                SPDLOG_DEBUG("Rewrote import('{}') -> ts_dynamic_import('{}') in {}",
                    lit->value, resolved.path, fromPath);
                expr = std::move(replacement);
                return;
            }
        }
    }

    // Match: require('file.json') → inline JSON object literal
    if (s_rewriteAnalyzer) {
        if (auto* call = dynamic_cast<ast::CallExpression*>(expr.get())) {
            auto* id = dynamic_cast<ast::Identifier*>(call->callee.get());
            if (id && id->name == "require" && call->arguments.size() >= 1) {
                auto* lit = dynamic_cast<ast::StringLiteral*>(call->arguments[0].get());
                if (lit) {
                    auto resolved = resolver.resolve(lit->value, fs::path(fromPath));
                    if (resolved.isValid() && resolved.type == ModuleType::JSON) {
                        auto modIt = s_rewriteAnalyzer->modules.find(resolved.path);
                        if (modIt != s_rewriteAnalyzer->modules.end() &&
                            modIt->second->isJsonModule && modIt->second->jsonContent.has_value()) {
                            auto replacement = jsonToAstExpr(modIt->second->jsonContent.value());
                            replacement->inferredType = std::make_shared<Type>(TypeKind::Any);
                            SPDLOG_DEBUG("Inlined JSON require('{}') in {}", lit->value, fromPath);
                            expr = std::move(replacement);
                            return;
                        }
                    }
                }
            }
        }
    }

    // Recurse into wrapper expressions that hold unique_ptrs,
    // so nested DynamicImport/require.resolve can be replaced
    if (auto* awaitExpr = dynamic_cast<ast::AwaitExpression*>(expr.get())) {
        rewriteRequireInExprOwned(awaitExpr->expression, resolver, fromPath);
        return;
    }

    if (auto* parenExpr = dynamic_cast<ast::ParenthesizedExpression*>(expr.get())) {
        rewriteRequireInExprOwned(parenExpr->expression, resolver, fromPath);
        return;
    }

    // Match: import.meta → Identifier("__import_meta")
    // Also recurse into PropertyAccessExpression.expression for import.meta.url etc.
    if (auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(expr.get())) {
        if (auto* id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (id->name == "import" && prop->name == "meta") {
                auto replacement = std::make_unique<ast::Identifier>();
                replacement->name = "__import_meta";
                replacement->inferredType = std::make_shared<Type>(TypeKind::Any);
                expr = std::move(replacement);
                return;
            }
        }
        // Recurse into the owned expression so nested import.meta can be replaced
        rewriteRequireInExprOwned(prop->expression, resolver, fromPath);
        return;
    }

    // Fall through to raw-pointer walker for everything else
    rewriteRequireInExpr(expr.get(), resolver, fromPath);
}

// Walk a single statement, recursing into contained expressions and sub-statements
static void rewriteRequireInStmt(ast::Statement* stmt,
    ModuleResolver& resolver, const std::string& fromPath) {
    if (!stmt) return;

    if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmt)) {
        rewriteRequireInExprOwned(exprStmt->expression, resolver, fromPath);
        return;
    }

    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt)) {
        rewriteRequireInExprOwned(varDecl->initializer, resolver, fromPath);
        return;
    }

    if (auto* retStmt = dynamic_cast<ast::ReturnStatement*>(stmt)) {
        rewriteRequireInExprOwned(retStmt->expression, resolver, fromPath);
        return;
    }

    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(stmt)) {
        rewriteRequireInExpr(ifStmt->condition.get(), resolver, fromPath);
        rewriteRequireInStmt(ifStmt->thenStatement.get(), resolver, fromPath);
        rewriteRequireInStmt(ifStmt->elseStatement.get(), resolver, fromPath);
        return;
    }

    if (auto* block = dynamic_cast<ast::BlockStatement*>(stmt)) {
        for (auto& s : block->statements) {
            rewriteRequireInStmt(s.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(stmt)) {
        rewriteRequireInExpr(whileStmt->condition.get(), resolver, fromPath);
        rewriteRequireInStmt(whileStmt->body.get(), resolver, fromPath);
        return;
    }

    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(stmt)) {
        rewriteRequireInStmt(forStmt->initializer.get(), resolver, fromPath);
        rewriteRequireInExpr(forStmt->condition.get(), resolver, fromPath);
        rewriteRequireInExpr(forStmt->incrementor.get(), resolver, fromPath);
        rewriteRequireInStmt(forStmt->body.get(), resolver, fromPath);
        return;
    }

    if (auto* forOf = dynamic_cast<ast::ForOfStatement*>(stmt)) {
        rewriteRequireInExpr(forOf->expression.get(), resolver, fromPath);
        rewriteRequireInStmt(forOf->body.get(), resolver, fromPath);
        return;
    }

    if (auto* forIn = dynamic_cast<ast::ForInStatement*>(stmt)) {
        rewriteRequireInExpr(forIn->expression.get(), resolver, fromPath);
        rewriteRequireInStmt(forIn->body.get(), resolver, fromPath);
        return;
    }

    if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(stmt)) {
        for (auto& s : tryStmt->tryBlock) {
            rewriteRequireInStmt(s.get(), resolver, fromPath);
        }
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) {
                rewriteRequireInStmt(s.get(), resolver, fromPath);
            }
        }
        for (auto& s : tryStmt->finallyBlock) {
            rewriteRequireInStmt(s.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* throwStmt = dynamic_cast<ast::ThrowStatement*>(stmt)) {
        rewriteRequireInExpr(throwStmt->expression.get(), resolver, fromPath);
        return;
    }

    if (auto* switchStmt = dynamic_cast<ast::SwitchStatement*>(stmt)) {
        rewriteRequireInExpr(switchStmt->expression.get(), resolver, fromPath);
        for (auto& clause : switchStmt->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                rewriteRequireInExpr(cc->expression.get(), resolver, fromPath);
                for (auto& s : cc->statements) {
                    rewriteRequireInStmt(s.get(), resolver, fromPath);
                }
            } else if (auto* dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& s : dc->statements) {
                    rewriteRequireInStmt(s.get(), resolver, fromPath);
                }
            }
        }
        return;
    }

    if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt)) {
        for (auto& s : funcDecl->body) {
            rewriteRequireInStmt(s.get(), resolver, fromPath);
        }
        return;
    }

    if (auto* labeled = dynamic_cast<ast::LabeledStatement*>(stmt)) {
        rewriteRequireInStmt(labeled->statement.get(), resolver, fromPath);
        return;
    }
}

Monomorphizer::Monomorphizer() {}

void Monomorphizer::monomorphize(ast::Program* program, Analyzer& analyzer) {
    const auto& usages = analyzer.getFunctionUsages();
    std::vector<std::string> moduleInitFunctions;

    // Determine main source file path for skipping module hash on main file functions.
    // Normalize to absolute path for reliable comparison since searchPath uses absolute paths
    // but program->body[0]->sourceFile may be relative.
    std::string mainSourceFilePath;
    if (!program->body.empty() && !program->body[0]->sourceFile.empty()) {
        try {
            mainSourceFilePath = std::filesystem::canonical(program->body[0]->sourceFile).string();
        } catch (...) {
            mainSourceFilePath = std::filesystem::absolute(program->body[0]->sourceFile).string();
        }
    }

    SPDLOG_DEBUG("[MONO] Monomorphizing {} modules", analyzer.moduleOrder.size());
    for (const auto& path : analyzer.moduleOrder) {
        SPDLOG_DEBUG("[MONO] Processing module: {}", path);
        auto it = analyzer.modules.find(path);
        if (it == analyzer.modules.end()) {
            // fmt::print("Module NOT FOUND in analyzer.modules: {}\n", path);
            continue;
        }
        auto module = it->second;
        if (!module->ast) {
            // fmt::print("Module has no AST!\n");
            continue;
        }

        auto moduleInit = std::make_unique<ast::FunctionDeclaration>();
        std::string initName = "__module_init_" + std::to_string(std::hash<std::string>{}(path));
        moduleInit->name = initName;
        moduleInit->isAsync = module->isAsync;
        moduleInit->sourceFile = path;  // Set source file for debug info
        moduleInit->line = 1;  // Module init starts at line 1
        if (module->isAsync) {
            SPDLOG_DEBUG("Module {} is async, marking init as async", path);
        }
        moduleInitFunctions.push_back(initName);

        // Track which module this synthetic init belongs to so analysis/codegen can
        // apply the correct (e.g., untyped JS) semantic mode when re-analyzing bodies.
        analyzer.syntheticFunctionOwners[initName] = path;
        SPDLOG_INFO("Registered synthetic owner: {} -> {}", initName, path);

        // Add 'module' parameter to module init
        auto moduleParam = std::make_unique<ast::Parameter>();
        auto paramName = std::make_unique<ast::Identifier>();
        paramName->name = "module";
        moduleParam->name = std::move(paramName);
        moduleParam->type = "any";
        moduleInit->parameters.push_back(std::move(moduleParam));

        // Prepend exports declaration for all modules
        // const exports = module.exports;
        // (Needed for CJS directly, and for ESM export population via dynamic import)
        {
            auto exportsDecl = std::make_unique<ast::VariableDeclaration>();
            auto exportsName = std::make_unique<ast::Identifier>();
            exportsName->name = "exports";
            exportsDecl->name = std::move(exportsName);
            exportsDecl->type = "any";

            auto moduleAccess = std::make_unique<ast::PropertyAccessExpression>();
            auto moduleRef = std::make_unique<ast::Identifier>();
            moduleRef->name = "module";
            moduleAccess->expression = std::move(moduleRef);
            moduleAccess->name = "exports";
            exportsDecl->initializer = std::move(moduleAccess);
            moduleInit->body.push_back(std::move(exportsDecl));
        }

        // Inject __filename and __dirname for JavaScript modules
        // These are CJS globals that many JS modules rely on
        if (module->type == ModuleType::UntypedJavaScript || module->type == ModuleType::TypedJavaScript) {
            // __filename = "<absolute path to this file>"
            auto filenameDecl = std::make_unique<ast::VariableDeclaration>();
            auto filenameName = std::make_unique<ast::Identifier>();
            filenameName->name = "__filename";
            filenameDecl->name = std::move(filenameName);
            filenameDecl->type = "any";
            auto filenameLit = std::make_unique<ast::StringLiteral>();
            filenameLit->value = path;
            filenameDecl->initializer = std::move(filenameLit);
            moduleInit->body.push_back(std::move(filenameDecl));

            // __dirname = "<parent directory>"
            auto dirnameDecl = std::make_unique<ast::VariableDeclaration>();
            auto dirnameName = std::make_unique<ast::Identifier>();
            dirnameName->name = "__dirname";
            dirnameDecl->name = std::move(dirnameName);
            dirnameDecl->type = "any";
            auto dirnameLit = std::make_unique<ast::StringLiteral>();
            dirnameLit->value = fs::path(path).parent_path().string();
            dirnameDecl->initializer = std::move(dirnameLit);
            moduleInit->body.push_back(std::move(dirnameDecl));
        }

        // Inject import.meta object for all modules
        // import.meta = { url: "file:///...", dirname: "...", filename: "..." }
        {
            auto metaDecl = std::make_unique<ast::VariableDeclaration>();
            auto metaName = std::make_unique<ast::Identifier>();
            metaName->name = "__import_meta";
            metaDecl->name = std::move(metaName);
            metaDecl->type = "any";

            auto objLit = std::make_unique<ast::ObjectLiteralExpression>();

            // url property: "file:///" + path with forward slashes
            auto urlProp = std::make_unique<ast::PropertyAssignment>();
            urlProp->name = "url";
            auto urlLit = std::make_unique<ast::StringLiteral>();
            std::string normalizedPath = path;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
            urlLit->value = "file:///" + normalizedPath;
            urlProp->initializer = std::move(urlLit);
            objLit->properties.push_back(std::move(urlProp));

            // dirname property
            auto dirProp = std::make_unique<ast::PropertyAssignment>();
            dirProp->name = "dirname";
            auto dirLit = std::make_unique<ast::StringLiteral>();
            dirLit->value = fs::path(path).parent_path().string();
            dirProp->initializer = std::move(dirLit);
            objLit->properties.push_back(std::move(dirProp));

            // filename property
            auto fileProp = std::make_unique<ast::PropertyAssignment>();
            fileProp->name = "filename";
            auto fileLit = std::make_unique<ast::StringLiteral>();
            fileLit->value = path;
            fileProp->initializer = std::move(fileLit);
            objLit->properties.push_back(std::move(fileProp));

            metaDecl->initializer = std::move(objLit);
            moduleInit->body.push_back(std::move(metaDecl));
        }

        const bool isLodashModule =
            (path.find("node_modules\\lodash\\lodash.js") != std::string::npos) ||
            (path.size() >= 9 && path.substr(path.size() - 9) == "lodash.js");

        // For JavaScript modules, FunctionDeclarations should be moved to moduleInit
        // because JavaScript function hoisting happens at runtime within the module scope.
        // For TypeScript, FunctionDeclarations are kept in newBody and processed by the monomorphizer.
        const bool isJavaScriptModule = (module->type == ModuleType::UntypedJavaScript || 
                                         module->type == ModuleType::TypedJavaScript);

        std::vector<std::unique_ptr<ast::Statement>> newBody;
        for (auto& stmt : module->ast->body) {
            std::string kind = stmt->getKind();
            // For JavaScript: move FunctionDeclarations to moduleInit (runtime hoisting)
            // For TypeScript: keep FunctionDeclarations in newBody (compile-time processing)
            bool keepInNewBody = false;
            if (kind == "ClassDeclaration" ||
                kind == "InterfaceDeclaration" ||
                kind == "ImportDeclaration" ||
                kind == "ImportEqualsDeclaration" ||
                kind == "ExportDeclaration" ||
                kind == "EnumDeclaration" ||
                kind == "TypeAliasDeclaration" ||
                kind == "NamespaceDeclaration") {
                keepInNewBody = true;
            } else if (kind == "FunctionDeclaration") {
                // TypeScript: keep for monomorphization
                // JavaScript: move to moduleInit for runtime hoisting
                keepInNewBody = !isJavaScriptModule;
            } else if (kind == "VariableDeclaration") {
                // Keep VariableDeclarations with ClassExpression initializers
                // so they are available for HIR pipeline processing
                auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
                if (varDecl && varDecl->initializer) {
                    if (dynamic_cast<ast::ClassExpression*>(varDecl->initializer.get())) {
                        keepInNewBody = true;
                    }
                }
            }

            if (keepInNewBody) {
                newBody.push_back(std::move(stmt));
                if (isLodashModule) {
                    fmt::print("    -> Moved to newBody\n");
                }
            } else {
                // Move everything else (VariableDeclarations, ExpressionStatements, etc.) to module init
                moduleInit->body.push_back(std::move(stmt));
            }
        }

        // Targeted debug markers for lodash: DISABLED for now
        if (false && isLodashModule) {
            // Add marker showing how many statements we're about to traverse
            std::string sizeMsg = "lodash_moduleInit_stmts=" + std::to_string(moduleInit->body.size());
            moduleInit->body.insert(moduleInit->body.begin() + 1, makeConsoleLogStatement(sizeMsg));
            
            for (size_t stmtIndex = 0; stmtIndex < moduleInit->body.size(); ++stmtIndex) {
                auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(moduleInit->body[stmtIndex].get());
                if (!exprStmt || !exprStmt->expression) continue;

                auto* call = dynamic_cast<ast::CallExpression*>(exprStmt->expression.get());
                if (!call || !call->callee) continue;

                auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(call->callee.get());
                if (!prop || prop->name != "call" || !prop->expression) continue;

                // The IIFE callee is commonly parenthesized.
                auto* innerExpr = unwrapParens(prop->expression.get());
                auto* fn = dynamic_cast<ast::FunctionExpression*>(innerExpr);
                if (!fn) continue;

                // Found IIFE - add markers before and after the call
                std::string beforeMarker = "lodash_before_iife_at_stmt=" + std::to_string(stmtIndex);
                moduleInit->body.insert(moduleInit->body.begin() + stmtIndex, makeConsoleLogStatement(beforeMarker));
                
                // After inserting before marker, the IIFE is now at stmtIndex + 1
                // Insert after marker at stmtIndex + 2
                moduleInit->body.insert(moduleInit->body.begin() + stmtIndex + 2, 
                                       makeConsoleLogStatement("lodash_after_iife"));
                
                // DON'T add markers inside the IIFE - they might corrupt something
                
                break;
            }
        }

        // Rewrite internal require() calls and dynamic imports in module init bodies.
        // require('literal') → ts_module_get_cached('absolutePath')
        // require.resolve('literal') → 'resolvedPath'
        // import('literal') → ts_dynamic_import('resolvedPath')
        {
            auto& resolver = analyzer.getModuleResolver();
            // Set analyzer pointer so JSON require() can be inlined
            s_rewriteAnalyzer = &analyzer;
            // Rewrite require/require.resolve/import()/import.meta in moduleInit body
            // For JS modules: rewrites require(), require.resolve(), import(), import.meta
            // For TS modules: rewrites import() and import.meta (no require() calls present)
            for (auto& bodyStmt : moduleInit->body) {
                rewriteRequireInStmt(bodyStmt.get(), resolver, path);
            }
            // For all modules (including TS), rewrite dynamic imports in newBody
            // (TS functions stay in newBody and may contain import() expressions)
            for (auto& bodyStmt : newBody) {
                rewriteRequireInStmt(bodyStmt.get(), resolver, path);
            }
            s_rewriteAnalyzer = nullptr;
        }

        // Inject CJS import binding extraction for JavaScript module imports.
        // For each ImportDeclaration in newBody that imports from a JS/CJS module,
        // inject VariableDeclarations that call ts_module_get_cached() to extract
        // the named bindings. These get picked up by moduleGlobalVars_ in ASTToHIR.
        {
            std::vector<std::unique_ptr<ast::Statement>> cjsBindings;
            int cjsCounter = 0;
            for (const auto& stmt : newBody) {
                if (stmt->getKind() != "ImportDeclaration") continue;
                auto* importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get());
                if (!importDecl || importDecl->isTypeOnly) continue;
                if (importDecl->namedImports.empty() && importDecl->defaultImport.empty()
                    && importDecl->namespaceImport.empty()) continue;

                std::string modSpec = importDecl->moduleSpecifier;
                if (modSpec.size() > 5 && modSpec.substr(0, 5) == "node:") {
                    modSpec = modSpec.substr(5);
                }

                // Resolve the module to check its type
                auto resolved = analyzer.getModuleResolver().resolve(modSpec, path);
                if (!resolved.isValid() || resolved.type == ModuleType::Builtin ||
                    resolved.type == ModuleType::Declaration) continue;

                // Only inject for JavaScript modules (CJS interop)
                auto modIt = analyzer.modules.find(resolved.path);
                if (modIt == analyzer.modules.end()) continue;
                auto& targetModule = modIt->second;

                // Skip CJS binding injection for ESM modules
                if (targetModule->isESM) continue;

                // If target is a TypeScript barrel that re-exports from CJS modules,
                // "see through" the barrel and inject CJS binding extraction for the
                // underlying CJS module directly in the consumer's module init.
                if (targetModule->type != ModuleType::UntypedJavaScript &&
                    targetModule->type != ModuleType::TypedJavaScript) {
                    // Scan barrel's AST for ExportDeclaration nodes pointing to CJS modules
                    if (!targetModule->ast) continue;

                    // Collect CJS re-export sources from the barrel
                    // Map: CJS resolved path -> list of export names available from that CJS module
                    struct CjsReExport {
                        std::string resolvedPath;
                        bool isStar;
                        std::vector<ast::ExportSpecifier> specifiers;
                    };
                    std::vector<CjsReExport> cjsReExports;

                    for (const auto& barrelStmt : targetModule->ast->body) {
                        if (barrelStmt->getKind() != "ExportDeclaration") continue;
                        auto* exportDecl = dynamic_cast<ast::ExportDeclaration*>(barrelStmt.get());
                        if (!exportDecl || exportDecl->moduleSpecifier.empty()) continue;

                        auto cjsResolved = analyzer.getModuleResolver().resolve(
                            exportDecl->moduleSpecifier, resolved.path);
                        if (!cjsResolved.isValid()) continue;

                        auto cjsModIt = analyzer.modules.find(cjsResolved.path);
                        if (cjsModIt == analyzer.modules.end()) continue;
                        auto& cjsMod = cjsModIt->second;
                        if (cjsMod->type != ModuleType::UntypedJavaScript &&
                            cjsMod->type != ModuleType::TypedJavaScript) continue;

                        CjsReExport reExport;
                        reExport.resolvedPath = cjsResolved.path;
                        reExport.isStar = exportDecl->isStarExport;
                        reExport.specifiers = exportDecl->namedExports;
                        cjsReExports.push_back(std::move(reExport));
                    }

                    if (cjsReExports.empty()) continue;

                    // For each named import, find which CJS module provides it
                    for (const auto& spec : importDecl->namedImports) {
                        if (spec.isTypeOnly) continue;
                        std::string importedName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                        std::string localName = spec.name;
                        std::string cjsPath;
                        std::string exportedName;

                        for (const auto& reExport : cjsReExports) {
                            if (reExport.isStar) {
                                // For star re-exports, check the CJS module's export table
                                auto cjsModIt2 = analyzer.modules.find(reExport.resolvedPath);
                                if (cjsModIt2 != analyzer.modules.end() && cjsModIt2->second->exports) {
                                    auto& syms = cjsModIt2->second->exports->getGlobalSymbols();
                                    if (syms.count(importedName)) {
                                        cjsPath = reExport.resolvedPath;
                                        exportedName = importedName;
                                        break;
                                    }
                                }
                            } else {
                                // For named re-exports, check specifiers
                                for (const auto& expSpec : reExport.specifiers) {
                                    std::string srcName = expSpec.propertyName.empty() ? expSpec.name : expSpec.propertyName;
                                    std::string dstName = expSpec.name;
                                    if (dstName == importedName) {
                                        cjsPath = reExport.resolvedPath;
                                        exportedName = srcName;
                                        break;
                                    }
                                }
                                if (!cjsPath.empty()) break;
                            }
                        }

                        if (cjsPath.empty()) continue;

                        // Inject: const __cjs_thru_N = ts_module_get_cached(cjsPath);
                        std::string thruVarName = "__cjs_thru_" + std::to_string(cjsCounter++);
                        {
                            auto varDecl = std::make_unique<ast::VariableDeclaration>();
                            auto varName = std::make_unique<ast::Identifier>();
                            varName->name = thruVarName;
                            varDecl->name = std::move(varName);
                            varDecl->type = "any";

                            auto call = std::make_unique<ast::CallExpression>();
                            auto calleeId = std::make_unique<ast::Identifier>();
                            calleeId->name = "ts_module_get_cached";
                            call->callee = std::move(calleeId);
                            auto pathLit = std::make_unique<ast::StringLiteral>();
                            pathLit->value = cjsPath;
                            call->arguments.push_back(std::move(pathLit));
                            call->inferredType = std::make_shared<Type>(TypeKind::Any);

                            varDecl->initializer = std::move(call);
                            cjsBindings.push_back(std::move(varDecl));
                        }

                        // Inject: const localName = __cjs_thru_N.exportedName;
                        {
                            auto varDecl = std::make_unique<ast::VariableDeclaration>();
                            auto varNameId = std::make_unique<ast::Identifier>();
                            varNameId->name = localName;
                            varDecl->name = std::move(varNameId);
                            varDecl->type = "any";

                            auto propAccess = std::make_unique<ast::PropertyAccessExpression>();
                            auto objRef = std::make_unique<ast::Identifier>();
                            objRef->name = thruVarName;
                            objRef->inferredType = std::make_shared<Type>(TypeKind::Any);
                            propAccess->expression = std::move(objRef);
                            propAccess->name = exportedName;
                            propAccess->inferredType = std::make_shared<Type>(TypeKind::Any);

                            varDecl->initializer = std::move(propAccess);
                            cjsBindings.push_back(std::move(varDecl));
                        }
                    }

                    // Default import through barrel: const X = ts_module_get_default(cjsPath);
                    if (!importDecl->defaultImport.empty()) {
                        // Find a CJS source that might provide a default export
                        for (const auto& reExport : cjsReExports) {
                            if (!reExport.isStar) {
                                // Check for explicit "default" re-export
                                bool hasDefault = false;
                                for (const auto& expSpec : reExport.specifiers) {
                                    if (expSpec.name == "default") {
                                        hasDefault = true;
                                        break;
                                    }
                                }
                                if (!hasDefault) continue;
                            }

                            auto varDecl = std::make_unique<ast::VariableDeclaration>();
                            auto varName = std::make_unique<ast::Identifier>();
                            varName->name = importDecl->defaultImport;
                            varDecl->name = std::move(varName);
                            varDecl->type = "any";

                            auto call = std::make_unique<ast::CallExpression>();
                            auto calleeId = std::make_unique<ast::Identifier>();
                            calleeId->name = "ts_module_get_default";
                            call->callee = std::move(calleeId);
                            auto pathLit = std::make_unique<ast::StringLiteral>();
                            pathLit->value = reExport.resolvedPath;
                            call->arguments.push_back(std::move(pathLit));
                            call->inferredType = std::make_shared<Type>(TypeKind::Any);

                            varDecl->initializer = std::move(call);
                            cjsBindings.push_back(std::move(varDecl));
                            break;
                        }
                    }

                    // Namespace import through barrel: const X = ts_module_get_cached(cjsPath);
                    if (!importDecl->namespaceImport.empty() && !cjsReExports.empty()) {
                        // Use the first CJS source as the namespace target
                        auto varDecl = std::make_unique<ast::VariableDeclaration>();
                        auto varName = std::make_unique<ast::Identifier>();
                        varName->name = importDecl->namespaceImport;
                        varDecl->name = std::move(varName);
                        varDecl->type = "any";

                        auto call = std::make_unique<ast::CallExpression>();
                        auto calleeId = std::make_unique<ast::Identifier>();
                        calleeId->name = "ts_module_get_cached";
                        call->callee = std::move(calleeId);
                        auto pathLit = std::make_unique<ast::StringLiteral>();
                        pathLit->value = cjsReExports[0].resolvedPath;
                        call->arguments.push_back(std::move(pathLit));
                        call->inferredType = std::make_shared<Type>(TypeKind::Any);

                        varDecl->initializer = std::move(call);
                        cjsBindings.push_back(std::move(varDecl));
                    }

                    continue;
                }

                std::string exportsVarName = "__cjs_exports_" + std::to_string(cjsCounter++);

                // const __cjs_exports_N = ts_module_get_cached(resolvedPath);
                {
                    auto varDecl = std::make_unique<ast::VariableDeclaration>();
                    auto varName = std::make_unique<ast::Identifier>();
                    varName->name = exportsVarName;
                    varDecl->name = std::move(varName);
                    varDecl->type = "any";

                    auto call = std::make_unique<ast::CallExpression>();
                    auto calleeId = std::make_unique<ast::Identifier>();
                    calleeId->name = "ts_module_get_cached";
                    call->callee = std::move(calleeId);
                    auto pathLit = std::make_unique<ast::StringLiteral>();
                    pathLit->value = resolved.path;
                    call->arguments.push_back(std::move(pathLit));
                    call->inferredType = std::make_shared<Type>(TypeKind::Any);

                    varDecl->initializer = std::move(call);
                    cjsBindings.push_back(std::move(varDecl));
                }

                // Default import: const X = ts_module_get_default(resolvedPath);
                // Uses __esModule interop: if exports.__esModule, returns exports.default
                // Otherwise returns the whole exports object (raw CJS default)
                if (!importDecl->defaultImport.empty()) {
                    auto varDecl = std::make_unique<ast::VariableDeclaration>();
                    auto varName = std::make_unique<ast::Identifier>();
                    varName->name = importDecl->defaultImport;
                    varDecl->name = std::move(varName);
                    varDecl->type = "any";

                    auto call = std::make_unique<ast::CallExpression>();
                    auto calleeId = std::make_unique<ast::Identifier>();
                    calleeId->name = "ts_module_get_default";
                    call->callee = std::move(calleeId);
                    auto pathLit = std::make_unique<ast::StringLiteral>();
                    pathLit->value = resolved.path;
                    call->arguments.push_back(std::move(pathLit));
                    call->inferredType = std::make_shared<Type>(TypeKind::Any);

                    varDecl->initializer = std::move(call);
                    cjsBindings.push_back(std::move(varDecl));
                }

                // Namespace import: const X = __cjs_exports_N;
                if (!importDecl->namespaceImport.empty()) {
                    auto varDecl = std::make_unique<ast::VariableDeclaration>();
                    auto varName = std::make_unique<ast::Identifier>();
                    varName->name = importDecl->namespaceImport;
                    varDecl->name = std::move(varName);
                    varDecl->type = "any";

                    auto ref = std::make_unique<ast::Identifier>();
                    ref->name = exportsVarName;
                    ref->inferredType = std::make_shared<Type>(TypeKind::Any);
                    varDecl->initializer = std::move(ref);
                    cjsBindings.push_back(std::move(varDecl));
                }

                // Named imports: const X = __cjs_exports_N.Y;
                for (const auto& spec : importDecl->namedImports) {
                    if (spec.isTypeOnly) continue;
                    std::string exportedName = spec.propertyName.empty() ? spec.name : spec.propertyName;

                    auto varDecl = std::make_unique<ast::VariableDeclaration>();
                    auto varName = std::make_unique<ast::Identifier>();
                    varName->name = spec.name;
                    varDecl->name = std::move(varName);
                    varDecl->type = "any";

                    auto propAccess = std::make_unique<ast::PropertyAccessExpression>();
                    auto objRef = std::make_unique<ast::Identifier>();
                    objRef->name = exportsVarName;
                    objRef->inferredType = std::make_shared<Type>(TypeKind::Any);
                    propAccess->expression = std::move(objRef);
                    propAccess->name = exportedName;
                    propAccess->inferredType = std::make_shared<Type>(TypeKind::Any);

                    varDecl->initializer = std::move(propAccess);
                    cjsBindings.push_back(std::move(varDecl));
                }

                SPDLOG_INFO("Injected {} CJS import bindings from {} for module {}",
                    cjsBindings.size() - (cjsCounter > 0 ? cjsCounter : 0), modSpec, path);
            }

            // Insert CJS bindings at the beginning of module init (after preamble declarations)
            // Preamble: [exports?] [__filename?] [__dirname?]
            if (!cjsBindings.empty()) {
                size_t preambleCount = 1;  // exports = module.exports (always present)
                if (isJavaScriptModule) preambleCount += 2;  // __filename + __dirname
                moduleInit->body.insert(moduleInit->body.begin() + preambleCount,
                    std::make_move_iterator(cjsBindings.begin()),
                    std::make_move_iterator(cjsBindings.end()));
            }
        }

        // For ESM modules, populate module.exports with named exports
        // so that dynamic import() can retrieve them via ts_module_get_cached()
        if (module->isESM) {
            // Collect exported names from the module body
            auto collectExportedNames = [](const std::vector<std::unique_ptr<ast::Statement>>& stmts,
                                           std::vector<std::string>& names) {
                for (const auto& stmt : stmts) {
                    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        if (varDecl->isExported) {
                            if (auto* id = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                                names.push_back(id->name);
                            }
                        }
                    } else if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                        if (funcDecl->isExported && !funcDecl->name.empty()) {
                            names.push_back(funcDecl->name);
                        }
                    } else if (auto* classDecl = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                        if (classDecl->isExported && !classDecl->name.empty()) {
                            names.push_back(classDecl->name);
                        }
                    }
                }
            };

            std::vector<std::string> exportedNames;
            collectExportedNames(moduleInit->body, exportedNames);
            collectExportedNames(newBody, exportedNames);

            // Also check module->exports symbol table for re-exports
            if (module->exports) {
                for (const auto& [name, sym] : module->exports->getGlobalSymbols()) {
                    if (std::find(exportedNames.begin(), exportedNames.end(), name) == exportedNames.end()) {
                        exportedNames.push_back(name);
                    }
                }
            }

            // Inject: exports.<name> = <name>; for each exported name
            // (uses the 'exports' local = module.exports, created in the preamble)
            for (const auto& name : exportedNames) {
                auto assignExpr = std::make_unique<ast::AssignmentExpression>();

                // LHS: exports.<name>
                auto exportsAccess = std::make_unique<ast::PropertyAccessExpression>();
                auto exportsRef = std::make_unique<ast::Identifier>();
                exportsRef->name = "exports";
                exportsRef->inferredType = std::make_shared<Type>(TypeKind::Any);
                exportsAccess->expression = std::move(exportsRef);
                exportsAccess->name = name;
                exportsAccess->inferredType = std::make_shared<Type>(TypeKind::Any);
                assignExpr->left = std::move(exportsAccess);

                // RHS: <name>
                auto nameRef = std::make_unique<ast::Identifier>();
                nameRef->name = name;
                assignExpr->right = std::move(nameRef);

                auto exprStmt = std::make_unique<ast::ExpressionStatement>();
                exprStmt->expression = std::move(assignExpr);
                moduleInit->body.push_back(std::move(exprStmt));
            }
        }

        // Return module.exports at the end of module init
        {
            auto finalRet = std::make_unique<ast::ReturnStatement>();
            auto finalModuleAccess = std::make_unique<ast::PropertyAccessExpression>();
            auto finalModuleRef = std::make_unique<ast::Identifier>();
            finalModuleRef->name = "module";
            finalModuleAccess->expression = std::move(finalModuleRef);
            finalModuleAccess->name = "exports";
            finalRet->expression = std::move(finalModuleAccess);
            moduleInit->body.push_back(std::move(finalRet));
        }

        // fmt::print("Module init now has {} statements\n", moduleInit->body.size());
        module->ast->body = std::move(newBody);

        std::string mangledName = Monomorphizer::generateMangledName(initName, { std::make_shared<Type>(TypeKind::Any) }, {});

        // Some later phases may refer to the synthetic function by its mangled name.
        analyzer.syntheticFunctionOwners[mangledName] = path;
        SPDLOG_INFO("Registered synthetic owner: {} -> {}", mangledName, path);
        SPDLOG_INFO("Adding specialization for module init: {} -> {}", initName, mangledName);
        Specialization spec;
        spec.originalName = initName;
        spec.specializedName = mangledName;
        spec.argTypes = { std::make_shared<Type>(TypeKind::Any) };
        spec.returnType = std::make_shared<Type>(TypeKind::Any);
        spec.node = moduleInit.get();
        // Set module path for cross-module __modvar_ disambiguation (skip main file)
        if (path != mainSourceFilePath) {
            spec.modulePath = path;
        }

        // Set line number to 1 for synthetic module init
        moduleInit->line = 1;
        moduleInit->column = 1;
        
        auto ft = std::make_shared<FunctionType>();
        ft->returnType = std::make_shared<Type>(TypeKind::Any);
        ft->paramTypes = { std::make_shared<Type>(TypeKind::Any) };
        analyzer.getSymbolTable().define(initName, ft);

        specializations.push_back(spec);
        syntheticFunctions.push_back(std::move(moduleInit));
    }

    SPDLOG_DEBUG("[MONO] Module loop done. Creating user_main...");
    // Special case for the program passed to monomorphize if it's not in analyzer.modules
    // (Though it should be now)

    // Create synthetic user_main that calls all module inits
    // This will be named __synthetic_user_main to avoid conflicts with user-defined user_main
    auto userMain = std::make_unique<ast::FunctionDeclaration>();
    
    // Check if user defined their own user_main function
    ast::FunctionDeclaration* userDefinedMain = findFunction(analyzer, "user_main");
    bool userMainInModuleInit = false;
    if (!userDefinedMain) {
        // For JavaScript modules, user_main may have been moved from the module body
        // to a __module_init body (line ~564: keepInNewBody = !isJavaScriptModule).
        // After the body replacement (line ~1033), findFunction can't find it.
        // Search the synthetic moduleInit bodies to locate it.
        for (const auto& synthFunc : syntheticFunctions) {
            for (auto& stmt : synthFunc->body) {
                if (auto* func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    if (func->name == "user_main") {
                        userDefinedMain = func;
                        userMainInModuleInit = true;
                        break;
                    }
                }
            }
            if (userDefinedMain) break;
        }
    }
    if (userDefinedMain) {
        // Rename our synthetic function to avoid conflict
        userMain->name = "__synthetic_user_main";
    } else {
        userMain->name = "user_main";
    }
    
    bool anyAsync = false;
    for (const auto& path : analyzer.moduleOrder) {
        auto it = analyzer.modules.find(path);
        if (it != analyzer.modules.end() && it->second->isAsync) {
            anyAsync = true;
            break;
        }
    }
    userMain->isAsync = anyAsync;
    userMain->line = 1;
    userMain->column = 1;

    auto userMainFt = std::make_shared<FunctionType>();
    userMainFt->returnType = std::make_shared<Type>(TypeKind::Any);
    analyzer.getSymbolTable().define("user_main", userMainFt);

    // Phase 1: Create and register all module objects
    std::vector<std::string> moduleObjNames;
    for (size_t i = 0; i < moduleInitFunctions.size(); ++i) {
        const auto& path = analyzer.moduleOrder[i];
        std::string modObjName = "__module_obj_" + std::to_string(i);
        moduleObjNames.push_back(modObjName);

        // let __module_obj_i = { exports: {} };
        auto modDecl = std::make_unique<ast::VariableDeclaration>();
        auto modName = std::make_unique<ast::Identifier>();
        modName->name = modObjName;
        modDecl->name = std::move(modName);
        modDecl->type = "any";

        auto objLit = std::make_unique<ast::ObjectLiteralExpression>();
        auto prop = std::make_unique<ast::PropertyAssignment>();
        prop->name = "exports";
        prop->initializer = std::make_unique<ast::ObjectLiteralExpression>();
        objLit->properties.push_back(std::move(prop));
        modDecl->initializer = std::move(objLit);
        userMain->body.push_back(std::move(modDecl));

        // ts_module_register(path, __module_obj_i);
        auto registerCall = std::make_unique<ast::CallExpression>();
        auto registerId = std::make_unique<ast::Identifier>();
        registerId->name = "ts_module_register";
        registerCall->callee = std::move(registerId);
        
        auto pathLit = std::make_unique<ast::StringLiteral>();
        pathLit->value = path;
        registerCall->arguments.push_back(std::move(pathLit));
        
        auto modRef = std::make_unique<ast::Identifier>();
        modRef->name = modObjName;
        registerCall->arguments.push_back(std::move(modRef));
        
        auto registerStmt = std::make_unique<ast::ExpressionStatement>();
        registerStmt->expression = std::move(registerCall);
        userMain->body.push_back(std::move(registerStmt));
    }

    // Phase 2: Initialize all modules
    for (size_t i = 0; i < moduleInitFunctions.size(); ++i) {
        const auto& initName = moduleInitFunctions[i];
        const auto& path = analyzer.moduleOrder[i];
        const auto& modObjName = moduleObjNames[i];
        
        // Create a variable to hold the module init result
        std::string resVarName = "__module_res_" + std::to_string(i);
        auto resDecl = std::make_unique<ast::VariableDeclaration>();
        auto resName = std::make_unique<ast::Identifier>();
        resName->name = resVarName;
        resDecl->name = std::move(resName);

        auto call = std::make_unique<ast::CallExpression>();
        auto callId = std::make_unique<ast::Identifier>();
        callId->name = initName;
        auto initFt = std::make_shared<FunctionType>();
        initFt->returnType = std::make_shared<Type>(TypeKind::Any);
        // Add parameter type for 'module'
        initFt->paramTypes = { std::make_shared<Type>(TypeKind::Any) };
        callId->inferredType = initFt;
        call->callee = std::move(callId);

        // Pass the module object as argument
        auto modRef = std::make_unique<ast::Identifier>();
        modRef->name = modObjName;
        call->arguments.push_back(std::move(modRef));
        
        auto it = analyzer.modules.find(path);
        if (it != analyzer.modules.end() && it->second->isAsync) {
             auto promiseClass = std::static_pointer_cast<ClassType>(analyzer.getSymbolTable().lookupType("Promise"));
             auto wrapped = std::make_shared<ClassType>("Promise");
             wrapped->methods = promiseClass->methods;
             wrapped->typeArguments = { std::make_shared<Type>(TypeKind::Any) };
             call->inferredType = wrapped;
        } else {
             call->inferredType = std::make_shared<Type>(TypeKind::Any);
        }

        std::unique_ptr<ast::Expression> finalExpr;
        if (anyAsync) {
            auto awaitExpr = std::make_unique<ast::AwaitExpression>();
            awaitExpr->expression = std::move(call);
            awaitExpr->inferredType = std::make_shared<Type>(TypeKind::Any);
            finalExpr = std::move(awaitExpr);
        } else {
            finalExpr = std::move(call);
        }

        resDecl->initializer = std::move(finalExpr);
        userMain->body.push_back(std::move(resDecl));

        // If this is the last module init and there's NO user-defined user_main,
        // make it a return statement.
        if (i == moduleInitFunctions.size() - 1 && !userDefinedMain) {
            auto stmt = std::make_unique<ast::ReturnStatement>();
            auto finalResRef = std::make_unique<ast::Identifier>();
            finalResRef->name = resVarName;
            stmt->expression = std::move(finalResRef);
            userMain->body.push_back(std::move(stmt));
        }
    }
    
    // If user defined their own user_main, call it after all module inits
    if (userDefinedMain) {
        auto call = std::make_unique<ast::CallExpression>();
        auto callId = std::make_unique<ast::Identifier>();
        callId->name = "user_main";
        call->callee = std::move(callId);
        // Use the actual return type from the user-defined function
        if (userDefinedMain->returnType.empty()) {
            call->inferredType = std::make_shared<Type>(TypeKind::Void);
        } else {
            call->inferredType = analyzer.parseType(userDefinedMain->returnType, analyzer.getSymbolTable());
        }
        
        auto stmt = std::make_unique<ast::ReturnStatement>();
        stmt->expression = std::move(call);
        userMain->body.push_back(std::move(stmt));
        
        // Also create a specialization for the user-defined user_main
        // BUT: if user_main was found inside a __module_init body (JS modules),
        // then visitFunctionDeclaration in ASTToHIR already creates the user_main
        // LLVM function when processing the module init specialization.
        // Creating a duplicate specialization here would cause a naming collision
        // where the user code ends up in a dead block.
        if (!userMainInModuleInit) {
            Specialization userMainSpec;
            userMainSpec.originalName = "user_main";
            userMainSpec.specializedName = "user_main";
            userMainSpec.argTypes = {};
            // Use the actual return type from the user-defined function
            if (userDefinedMain->returnType.empty()) {
                userMainSpec.returnType = std::make_shared<Type>(TypeKind::Void);
            } else {
                userMainSpec.returnType = analyzer.parseType(userDefinedMain->returnType, analyzer.getSymbolTable());
            }
            userMainSpec.node = userDefinedMain;
            specializations.push_back(userMainSpec);
        }
    }

    Specialization mainSpec;
    mainSpec.originalName = userMain->name;
    mainSpec.specializedName = userMain->name;
    mainSpec.argTypes = {};
    mainSpec.returnType = std::make_shared<Type>(TypeKind::Any);
    mainSpec.node = userMain.get();
    specializations.push_back(mainSpec);

    // Use the same normalized mainSourceFilePath from above
    std::string mainSourceFile = mainSourceFilePath;

    SPDLOG_DEBUG("[MONO] Starting specialization loop...");
    std::set<std::string> processedSpecializations;
    bool changed = true;
    int iterCount = 0;
    while (changed) {
        changed = false;
        iterCount++;
        auto currentUsages = analyzer.getFunctionUsages(); // Copy the map to avoid iterator invalidation
        SPDLOG_DEBUG("[MONO] Specialization iter={} usages={}", iterCount, currentUsages.size());

        for (const auto& [name, calls] : currentUsages) {
            SPDLOG_DEBUG("[MONO]   usage: name={} callCount={}", name, calls.size());
            for (const auto& call : calls) {
                // Find function in the source module if known, otherwise in the calling module
                std::string searchPath = call.sourceModulePath.empty() ? call.modulePath : call.sourceModulePath;
                // Use the original exported name if available (handles aliased imports)
                std::string searchName = call.sourceExportedName.empty() ? name : call.sourceExportedName;
                SPDLOG_DEBUG("[MONO]     findFunction: searchName={} searchPath={}", searchName, searchPath);
                ast::FunctionDeclaration* funcNode = findFunction(analyzer, searchName, searchPath);
                if (!funcNode) {
                    SPDLOG_DEBUG("[MONO]       -> not found, skipping");
                    continue;
                }
                SPDLOG_DEBUG("[MONO]       -> found: name={} params={}", funcNode->name, funcNode->parameters.size());

                // Check if function has a rest parameter and transform argTypes accordingly
                std::vector<std::shared_ptr<Type>> adjustedArgTypes = call.argTypes;
                size_t restParamIndex = SIZE_MAX;
                for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
                    if (funcNode->parameters[i]->isRest) {
                        restParamIndex = i;
                        break;
                    }
                }

                if (restParamIndex != SIZE_MAX) {
                    // Function has rest parameter - package trailing call arguments into array type
                    std::vector<std::shared_ptr<Type>> newArgTypes;

                    // Add arguments before the rest parameter
                    for (size_t i = 0; i < restParamIndex && i < call.argTypes.size(); ++i) {
                        newArgTypes.push_back(call.argTypes[i]);
                    }

                    // Determine the element type for the rest array
                    std::shared_ptr<Type> elemType = std::make_shared<Type>(TypeKind::Any);
                    if (restParamIndex < call.argTypes.size()) {
                        // Use the first rest argument's type as the element type
                        elemType = call.argTypes[restParamIndex];
                    }

                    // Create an array type for the rest parameter
                    auto arrayType = std::make_shared<ArrayType>(elemType);
                    newArgTypes.push_back(arrayType);

                    adjustedArgTypes = newArgTypes;
                }

                // Only add module hash for functions from imported modules, not the main file.
                // Functions in the main file are called without module hash at the call site.
                // For imported modules, both cross-module and intra-module calls need the hash
                // so that same-named functions in different modules get unique LLVM names.
                std::string mangledModulePath;
                if (!searchPath.empty() && searchPath != mainSourceFile) {
                    mangledModulePath = searchPath;
                }
                std::string mangled = generateMangledName(name, adjustedArgTypes, call.typeArguments, mangledModulePath);
                std::string specKey = call.modulePath.empty() ? mangled : (call.modulePath + "::" + mangled);
                if (processedSpecializations.count(specKey)) continue;

                // The mangled name now includes a module hash, so same-named functions
                // from different modules get unique LLVM function names
                if (processedSpecializations.count(mangled)) continue;

                processedSpecializations.insert(specKey);
                processedSpecializations.insert(mangled);
                changed = true;

                Specialization spec;
                spec.originalName = name;
                spec.specializedName = mangled;
                spec.modulePath = mangledModulePath;

                if (spec.specializedName == "main") {
                    spec.specializedName = "__ts_main";
                }

                spec.argTypes = adjustedArgTypes;
                spec.typeArguments = call.typeArguments;
                spec.node = funcNode;

                // Infer return type - this might record NEW usages in analyzer.functionUsages
                spec.returnType = analyzer.analyzeFunctionBody(funcNode, adjustedArgTypes, call.typeArguments);

                specializations.push_back(spec);
            }
        }
    }

    SPDLOG_DEBUG("[MONO] Specialization loop done. specs={}", specializations.size());
    // Always ensure main is monomorphized if it exists
    ast::FunctionDeclaration* mainFunc = findFunction(analyzer, "main");
    if (mainFunc) {
        bool alreadyMonomorphized = false;
        for (const auto& spec : specializations) {
            if (spec.originalName == "main" && spec.argTypes.empty()) {
                alreadyMonomorphized = true;
                break;
            }
        }

        if (!alreadyMonomorphized) {
            Specialization spec;
            spec.originalName = "main";
            spec.specializedName = "__ts_main";
            spec.argTypes = {};
            spec.node = mainFunc;
            spec.returnType = analyzer.analyzeFunctionBody(mainFunc, {});
            specializations.push_back(spec);
        }
    }

    SPDLOG_DEBUG("[MONO] main check done. Processing decorators... modules={}", analyzer.modules.size());
    // Process Decorator Functions
    // Decorators are applied at class initialization time, so we need to ensure
    // decorator functions are generated even if they're not directly called
    for (const auto& path : analyzer.moduleOrder) {
        auto it = analyzer.modules.find(path);
        if (it == analyzer.modules.end()) continue;
        auto& module = it->second;
        if (!module->ast) continue;
        if (module->ast->body.empty()) continue;
        for (const auto& stmt : module->ast->body) {
            if (!stmt) continue;
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                for (const auto& decorator : cls->decorators) {
                    // Handle simple identifier decorators: @decorator
                    if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                        ast::FunctionDeclaration* decoratorFunc = findFunction(analyzer, id->name);
                        if (decoratorFunc) {
                            // Check if already added
                            std::string mangled = generateMangledName(id->name, { std::make_shared<Type>(TypeKind::Any) }, {});
                            if (!processedSpecializations.count(mangled)) {
                                processedSpecializations.insert(mangled);

                                Specialization spec;
                                spec.originalName = id->name;
                                spec.specializedName = mangled;
                                spec.argTypes = { std::make_shared<Type>(TypeKind::Any) };
                                spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                spec.node = decoratorFunc;
                                specializations.push_back(spec);

                                SPDLOG_INFO("Added decorator function: {} -> {}", id->name, mangled);
                            }
                        }
                    }
                    // Handle decorator factories: @decorator(args)
                    else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
                        // The callee is the factory function
                        if (auto* factoryId = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                            ast::FunctionDeclaration* factoryFunc = findFunction(analyzer, factoryId->name);
                            if (factoryFunc) {
                                // Determine argument types from the factory call
                                std::vector<std::shared_ptr<Type>> argTypes;
                                for (const auto& arg : call->arguments) {
                                    if (arg->inferredType) {
                                        argTypes.push_back(arg->inferredType);
                                    } else {
                                        argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                                    }
                                }

                                std::string mangled = generateMangledName(factoryId->name, argTypes, {});
                                if (!processedSpecializations.count(mangled)) {
                                    processedSpecializations.insert(mangled);

                                    Specialization spec;
                                    spec.originalName = factoryId->name;
                                    spec.specializedName = mangled;
                                    spec.argTypes = argTypes;
                                    spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                    spec.node = factoryFunc;
                                    specializations.push_back(spec);

                                    SPDLOG_INFO("Added decorator factory function: {} -> {}", factoryId->name, mangled);
                                }
                            }
                        }
                    }
                }

                // Process method decorators for this class
                for (const auto& member : cls->members) {
                    if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                        for (const auto& decorator : method->decorators) {
                            // Handle simple identifier decorators: @decorator
                            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                                ast::FunctionDeclaration* decoratorFunc = findFunction(analyzer, id->name);
                                if (decoratorFunc) {
                                    // Method decorator signature: (target: any, propertyKey: string, descriptor: any) -> any | void
                                    std::vector<std::shared_ptr<Type>> argTypes = {
                                        std::make_shared<Type>(TypeKind::Any),    // target
                                        std::make_shared<Type>(TypeKind::String), // propertyKey
                                        std::make_shared<Type>(TypeKind::Any)     // descriptor (PropertyDescriptor)
                                    };
                                    std::string mangled = generateMangledName(id->name, argTypes, {});
                                    if (!processedSpecializations.count(mangled)) {
                                        processedSpecializations.insert(mangled);

                                        Specialization spec;
                                        spec.originalName = id->name;
                                        spec.specializedName = mangled;
                                        spec.argTypes = argTypes;
                                        spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                        spec.node = decoratorFunc;
                                        specializations.push_back(spec);

                                        SPDLOG_INFO("Added method decorator function: {} -> {}", id->name, mangled);
                                    }
                                }
                            }
                            // Handle decorator factories: @decorator(args)
                            else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
                                if (auto* factoryId = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                                    ast::FunctionDeclaration* factoryFunc = findFunction(analyzer, factoryId->name);
                                    if (factoryFunc) {
                                        std::vector<std::shared_ptr<Type>> argTypes;
                                        for (const auto& arg : call->arguments) {
                                            if (arg->inferredType) {
                                                argTypes.push_back(arg->inferredType);
                                            } else {
                                                argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                                            }
                                        }

                                        std::string mangled = generateMangledName(factoryId->name, argTypes, {});
                                        if (!processedSpecializations.count(mangled)) {
                                            processedSpecializations.insert(mangled);

                                            Specialization spec;
                                            spec.originalName = factoryId->name;
                                            spec.specializedName = mangled;
                                            spec.argTypes = argTypes;
                                            spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                            spec.node = factoryFunc;
                                            specializations.push_back(spec);

                                            SPDLOG_INFO("Added method decorator factory function: {} -> {}", factoryId->name, mangled);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Process property decorators for this class
                for (const auto& member : cls->members) {
                    if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                        for (const auto& decorator : prop->decorators) {
                            // Handle simple identifier decorators: @decorator
                            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                                ast::FunctionDeclaration* decoratorFunc = findFunction(analyzer, id->name);
                                if (decoratorFunc) {
                                    // Property decorator signature: (target: any, propertyKey: string) -> void
                                    std::vector<std::shared_ptr<Type>> argTypes = {
                                        std::make_shared<Type>(TypeKind::Any),    // target
                                        std::make_shared<Type>(TypeKind::String)  // propertyKey
                                    };
                                    std::string mangled = generateMangledName(id->name, argTypes, {});
                                    if (!processedSpecializations.count(mangled)) {
                                        processedSpecializations.insert(mangled);

                                        Specialization spec;
                                        spec.originalName = id->name;
                                        spec.specializedName = mangled;
                                        spec.argTypes = argTypes;
                                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                                        spec.node = decoratorFunc;
                                        specializations.push_back(spec);

                                        SPDLOG_INFO("Added property decorator function: {} -> {}", id->name, mangled);
                                    }
                                }
                            }
                            // Handle decorator factories: @decorator(args)
                            else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
                                if (auto* factoryId = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                                    ast::FunctionDeclaration* factoryFunc = findFunction(analyzer, factoryId->name);
                                    if (factoryFunc) {
                                        std::vector<std::shared_ptr<Type>> argTypes;
                                        for (const auto& arg : call->arguments) {
                                            if (arg->inferredType) {
                                                argTypes.push_back(arg->inferredType);
                                            } else {
                                                argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                                            }
                                        }

                                        std::string mangled = generateMangledName(factoryId->name, argTypes, {});
                                        if (!processedSpecializations.count(mangled)) {
                                            processedSpecializations.insert(mangled);

                                            Specialization spec;
                                            spec.originalName = factoryId->name;
                                            spec.specializedName = mangled;
                                            spec.argTypes = argTypes;
                                            spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                            spec.node = factoryFunc;
                                            specializations.push_back(spec);

                                            SPDLOG_INFO("Added property decorator factory function: {} -> {}", factoryId->name, mangled);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Process parameter decorators for this class
                for (const auto& member : cls->members) {
                    if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                        for (const auto& param : method->parameters) {
                            for (const auto& decorator : param->decorators) {
                                // Handle simple identifier decorators: @decorator
                                if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                                    ast::FunctionDeclaration* decoratorFunc = findFunction(analyzer, id->name);
                                    if (decoratorFunc) {
                                        // Parameter decorator signature: (target: any, propertyKey: string, parameterIndex: number) -> void
                                        std::vector<std::shared_ptr<Type>> argTypes = {
                                            std::make_shared<Type>(TypeKind::Any),    // target
                                            std::make_shared<Type>(TypeKind::String), // propertyKey
                                            std::make_shared<Type>(TypeKind::Int)     // parameterIndex
                                        };
                                        std::string mangled = generateMangledName(id->name, argTypes, {});
                                        if (!processedSpecializations.count(mangled)) {
                                            processedSpecializations.insert(mangled);

                                            Specialization spec;
                                            spec.originalName = id->name;
                                            spec.specializedName = mangled;
                                            spec.argTypes = argTypes;
                                            spec.returnType = std::make_shared<Type>(TypeKind::Void);
                                            spec.node = decoratorFunc;
                                            specializations.push_back(spec);

                                            SPDLOG_INFO("Added parameter decorator function: {} -> {}", id->name, mangled);
                                        }
                                    }
                                }
                                // Handle decorator factories: @decorator(args)
                                else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
                                    if (auto* factoryId = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                                        ast::FunctionDeclaration* factoryFunc = findFunction(analyzer, factoryId->name);
                                        if (factoryFunc) {
                                            std::vector<std::shared_ptr<Type>> argTypes;
                                            for (const auto& arg : call->arguments) {
                                                if (arg->inferredType) {
                                                    argTypes.push_back(arg->inferredType);
                                                } else {
                                                    argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                                                }
                                            }

                                            std::string mangled = generateMangledName(factoryId->name, argTypes, {});
                                            if (!processedSpecializations.count(mangled)) {
                                                processedSpecializations.insert(mangled);

                                                Specialization spec;
                                                spec.originalName = factoryId->name;
                                                spec.specializedName = mangled;
                                                spec.argTypes = argTypes;
                                                spec.returnType = std::make_shared<Type>(TypeKind::Any);
                                                spec.node = factoryFunc;
                                                specializations.push_back(spec);

                                                SPDLOG_INFO("Added parameter decorator factory function: {} -> {}", factoryId->name, mangled);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Process Class Methods — skip for now if there are many JS modules loaded,
    // as the large number of analyzer.classDeclarations from module analysis can
    // cause RTTI corruption during dynamic_cast. This is safe because JS modules
    // don't have class declarations in their newBody (they're moved to moduleInit).
    if (analyzer.moduleOrder.size() <= 10) {
    for (const auto& path : analyzer.moduleOrder) {
        auto it = analyzer.modules.find(path);
        if (it == analyzer.modules.end()) continue;
        auto& module = it->second;
        if (!module->ast) continue;
        if (module->ast->body.empty()) continue;
        for (const auto& stmt : module->ast->body) {
            if (!stmt) continue;
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                auto classType = analyzer.getSymbolTable().lookupType(cls->name);
                if (!classType || classType->kind != TypeKind::Class) continue;
                auto ct = std::static_pointer_cast<ClassType>(classType);

                // Skip generic classes here; they are handled via classUsages
                if (!ct->typeParameters.empty()) continue;

                for (const auto& member : cls->members) {
                    if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                        if (method->isAbstract) continue;
                        // Skip overload signatures (methods without bodies) - only generate for implementation
                        if (!method->hasBody) continue;

                        Specialization spec;
                        spec.originalName = method->name;
                        spec.node = method;
                        spec.classType = ct;

                        if (method->isStatic) {
                            std::string methodName = Analyzer::manglePrivateName(method->name, cls->name);
                            spec.specializedName = cls->name + "_static_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                            if (ct->staticMethods.count(methodName)) {
                                auto methodType = ct->staticMethods[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else if (ct->getters.count(methodName)) {
                                auto methodType = ct->getters[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else if (ct->setters.count(methodName)) {
                                auto methodType = ct->setters[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else {
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            }
                        } else {
                            std::string methodName = Analyzer::manglePrivateName(method->name, cls->name);
                            spec.specializedName = cls->name + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                            // Construct argTypes: [ClassType, explicitParams...]
                            spec.argTypes.push_back(ct);

                            if (method->name == "constructor" && !ct->constructorOverloads.empty()) {
                                auto ctorType = ct->constructorOverloads[0];
                                spec.argTypes.insert(spec.argTypes.end(),
                                    ctorType->paramTypes.begin(), ctorType->paramTypes.end());
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            } else if (ct->methods.count(methodName)) {
                                auto methodType = ct->methods[methodName];
                                spec.argTypes.insert(spec.argTypes.end(),
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else if (ct->getters.count(methodName)) {
                                auto methodType = ct->getters[methodName];
                                spec.argTypes.insert(spec.argTypes.end(),
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else if (ct->setters.count(methodName)) {
                                auto methodType = ct->setters[methodName];
                                spec.argTypes.insert(spec.argTypes.end(),
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else {
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            }
                        }

                        // Deduplicate: skip if we already have a specialization with this name
                        if (processedSpecializations.count(spec.specializedName)) continue;
                        processedSpecializations.insert(spec.specializedName);

                        specializations.push_back(spec);
                    }
                }
            }
        }
    }

    // Process Class Expression Methods
    for (const auto& expr : analyzer.expressions) {
        auto* classExpr = dynamic_cast<ast::ClassExpression*>(expr);
        if (!classExpr) continue;

        if (!classExpr->inferredType || classExpr->inferredType->kind != TypeKind::Class) continue;
        auto ct = std::static_pointer_cast<ClassType>(classExpr->inferredType);

        // Skip generic classes here
        if (!ct->typeParameters.empty()) continue;

        SPDLOG_DEBUG("Processing class expression: {}", ct->name);

        for (const auto& member : classExpr->members) {
            if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (method->isAbstract) continue;
                // Skip overload signatures (methods without bodies) - only generate for implementation
                if (!method->hasBody) continue;

                Specialization spec;
                spec.originalName = method->name;
                spec.node = method;
                spec.classType = ct;

                if (method->isStatic) {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_static_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    if (ct->staticMethods.count(methodName)) {
                        auto methodType = ct->staticMethods[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                } else {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    // Construct argTypes: [ClassType, explicitParams...]
                    spec.argTypes.push_back(ct);

                    if (method->name == "constructor" && !ct->constructorOverloads.empty()) {
                        auto ctorType = ct->constructorOverloads[0];
                        spec.argTypes.insert(spec.argTypes.end(),
                            ctorType->paramTypes.begin(), ctorType->paramTypes.end());
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    } else if (ct->methods.count(methodName)) {
                        auto methodType = ct->methods[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                }

                // Deduplicate: skip if we already have a specialization with this name
                if (processedSpecializations.count(spec.specializedName)) continue;
                processedSpecializations.insert(spec.specializedName);

                SPDLOG_DEBUG("Adding specialization for class expression method: {}", spec.specializedName);
                specializations.push_back(spec);
            }
        }
    }
    } // end moduleOrder.size() <= 10 guard

    SPDLOG_DEBUG("[MONO] Class methods done. Processing local class declarations... count={}", analyzer.classDeclarations.size());
    // Process Local Class Declaration Methods (ES2022: classes declared inside functions)
    for (const auto& classDecl : analyzer.classDeclarations) {
        if (!classDecl->resolvedType || classDecl->resolvedType->kind != TypeKind::Class) continue;
        auto ct = std::static_pointer_cast<ClassType>(classDecl->resolvedType);

        // Skip generic classes here
        if (!ct->typeParameters.empty()) continue;

        SPDLOG_DEBUG("Processing local class declaration: {}", ct->name);

        for (const auto& member : classDecl->members) {
            if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (method->isAbstract) continue;
                // Skip overload signatures (methods without bodies) - only generate for implementation
                if (!method->hasBody) continue;

                Specialization spec;
                spec.originalName = method->name;
                spec.node = method;
                spec.classType = ct;

                if (method->isStatic) {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_static_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    if (ct->staticMethods.count(methodName)) {
                        auto methodType = ct->staticMethods[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                } else {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    // Construct argTypes: [ClassType, explicitParams...]
                    spec.argTypes.push_back(ct);

                    if (method->name == "constructor" && !ct->constructorOverloads.empty()) {
                        auto ctorType = ct->constructorOverloads[0];
                        spec.argTypes.insert(spec.argTypes.end(),
                            ctorType->paramTypes.begin(), ctorType->paramTypes.end());
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    } else if (ct->methods.count(methodName)) {
                        auto methodType = ct->methods[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                }

                // Deduplicate: skip if we already have a specialization with this name
                if (processedSpecializations.count(spec.specializedName)) continue;
                processedSpecializations.insert(spec.specializedName);

                SPDLOG_DEBUG("Adding specialization for local class method: {}", spec.specializedName);
                specializations.push_back(spec);
            }
        }
    }

    const auto& classUsages = analyzer.getClassUsages();
    for (const auto& [name, instances] : classUsages) {
        ast::ClassDeclaration* classNode = findClass(analyzer, name);
        if (!classNode) continue;

        // Skip non-generic classes - they are already handled by earlier loops
        if (classNode->typeParameters.empty()) continue;

        // Deduplicate by type argument strings
        std::map<std::string, std::vector<std::shared_ptr<Type>>> uniqueInstances;
        for (const auto& inst : instances) {
            std::string key;
            for (const auto& t : inst) key += t->toString() + ",";
            uniqueInstances[key] = inst;
        }

        for (const auto& [key, typeArgs] : uniqueInstances) {
            std::string mangled = generateMangledName(name, {}, typeArgs);
            
            // Specialize the class type
            auto specializedClass = analyzer.analyzeClassBody(classNode, typeArgs);
            specializedClass->name = mangled;

            std::map<std::string, std::shared_ptr<Type>> env;
            for (size_t i = 0; i < classNode->typeParameters.size() && i < typeArgs.size(); ++i) {
                env[classNode->typeParameters[i]->name] = typeArgs[i];
            }

            // Specialize all methods
            for (const auto& member : classNode->members) {
                if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                    if (method->isStatic) continue; // Static methods are not specialized per-instance
                    // Skip overload signatures (methods without bodies) - only generate for implementation
                    if (!method->hasBody) continue;

                    Specialization spec;
                    spec.originalName = method->name;
                    spec.specializedName = mangled + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + method->name;
                    spec.typeArguments = typeArgs;
                    spec.classType = specializedClass;
                    spec.node = method;
                    
                    // Infer return type for method
                    spec.returnType = analyzer.analyzeMethodBody(method, specializedClass, typeArgs);
                    
                    spec.argTypes.push_back(specializedClass);
                    for (const auto& p : method->parameters) {
                        auto pType = analyzer.parseType(p->type, analyzer.getSymbolTable());
                        spec.argTypes.push_back(analyzer.substitute(pType, env));
                    }

                    specializations.push_back(spec);
                }
            }
        }
    }

    syntheticFunctions.push_back(std::move(userMain));
}

std::string Monomorphizer::generateMangledName(const std::string& originalName, const std::vector<std::shared_ptr<Type>>& argTypes, const std::vector<std::shared_ptr<Type>>& typeArguments, const std::string& modulePath) {
    if (originalName == "main" && argTypes.empty()) {
        return "__ts_main";
    }
    std::string name = originalName;

    // Add module hash suffix for cross-module disambiguation
    // This prevents name collisions when different JS modules define functions with the same name
    if (!modulePath.empty()) {
        std::hash<std::string> hasher;
        auto hash = hasher(modulePath) % 999999;
        name += "_m" + std::to_string(hash);
    }

    if (!typeArguments.empty()) {
        name += "_T";
        for (const auto& type : typeArguments) {
            name += "_";
            switch (type->kind) {
                case TypeKind::Int: name += "int"; break;
                case TypeKind::Double: name += "dbl"; break;
                case TypeKind::String: name += "str"; break;
                case TypeKind::Boolean: name += "bool"; break;
                case TypeKind::Class: name += std::static_pointer_cast<ClassType>(type)->name; break;
                default: name += "any"; break;
            }
        }
    }

    for (const auto& type : argTypes) {
        name += "_";
        switch (type->kind) {
            case TypeKind::Int: name += "int"; break;
            case TypeKind::Double: name += "dbl"; break;
            case TypeKind::String: name += "str"; break;
            case TypeKind::Boolean: name += "bool"; break;
            case TypeKind::Void: name += "void"; break;
            case TypeKind::Class: 
                name += std::static_pointer_cast<ClassType>(type)->name; 
                break;
            case TypeKind::Interface:
                name += std::static_pointer_cast<InterfaceType>(type)->name;
                break;
            case TypeKind::Array:
                name += "arr";
                if (auto arr = std::dynamic_pointer_cast<ArrayType>(type)) {
                    if (arr->elementType->kind == TypeKind::String) name += "str";
                    else if (arr->elementType->kind == TypeKind::Int) name += "int";
                    else if (arr->elementType->kind == TypeKind::Double) name += "dbl";
                }
                break;
            default: name += "any"; break;
        }
    }
    return name;
}

ast::FunctionDeclaration* Monomorphizer::findFunction(Analyzer& analyzer, const std::string& name, const std::string& modulePath) {
    // For function overloads, multiple FunctionDeclarations exist with the same name:
    // - Overload signatures: have empty body
    // - Implementation: has the actual body
    // We need to return the implementation (the one with a body), not the signature.
    //
    // When modulePath is provided, we first search that module specifically to handle
    // the case where different modules have functions with the same name.

    ast::FunctionDeclaration* firstMatch = nullptr;

    // If modulePath is provided, search that module FIRST
    if (!modulePath.empty()) {
        auto it = analyzer.modules.find(modulePath);
        if (it != analyzer.modules.end() && it->second->ast) {
            for (auto& stmt : it->second->ast->body) {
                if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    if (func->name == name) {
                        // Prefer the function with a body (the implementation)
                        if (!func->body.empty()) {
                            return func;
                        }
                        // Keep track of the first match in case no implementation is found
                        if (!firstMatch) {
                            firstMatch = func;
                        }
                    }
                }
            }
        }
        // If we found a match in the specified module (even without body), prefer it
        if (firstMatch) {
            return firstMatch;
        }
    }

    // Fall back to searching all modules (for backward compatibility and cases where
    // modulePath is empty, like user_main and main)
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        // Skip the already-searched module if modulePath was provided
        if (!modulePath.empty() && path == modulePath) continue;

        for (auto& stmt : module->ast->body) {
            if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                if (func->name == name) {
                    // Prefer the function with a body (the implementation)
                    if (!func->body.empty()) {
                        return func;
                    }
                    // Keep track of the first match in case no implementation is found
                    if (!firstMatch) {
                        firstMatch = func;
                    }
                }
            }
        }
    }
    // Fallback: follow re-export chains to find the original declaration.
    // E.g., barrel files: export { add } from './math' -- no FunctionDeclaration in barrel
    if (!firstMatch && !modulePath.empty()) {
        auto reExportResult = followReExportChain(analyzer, name, modulePath, {});
        if (reExportResult) return reExportResult;
    }
    if (!firstMatch) {
        // Try re-export chains in all modules
        for (auto& [path, module] : analyzer.modules) {
            if (!module->ast) continue;
            auto reExportResult = followReExportChain(analyzer, name, path, {});
            if (reExportResult) return reExportResult;
        }
    }

    // Fallback: check if 'name' is a default import alias in the calling module.
    // E.g., `import myAdd from './math_default'` uses "myAdd" locally but the
    // exported function may be named "add" or anonymous.
    if (!firstMatch && !modulePath.empty()) {
        auto callingModIt = analyzer.modules.find(modulePath);
        if (callingModIt != analyzer.modules.end() && callingModIt->second->ast) {
            for (auto& stmt : callingModIt->second->ast->body) {
                auto* importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get());
                if (!importDecl || importDecl->defaultImport != name) continue;

                // Found matching default import. Try to resolve the source module
                // by matching the import specifier against loaded module paths.
                std::string spec = importDecl->moduleSpecifier;
                // Strip leading "./" for relative imports
                if (spec.size() > 2 && spec.substr(0, 2) == "./") {
                    spec = spec.substr(2);
                }

                for (auto& [srcPath, srcMod] : analyzer.modules) {
                    if (!srcMod->ast) continue;
                    // Check if this module's path matches the import specifier
                    // (path contains the specifier basename)
                    if (srcPath.find(spec) == std::string::npos) continue;

                    for (auto& srcStmt : srcMod->ast->body) {
                        auto* func = dynamic_cast<ast::FunctionDeclaration*>(srcStmt.get());
                        if (func && func->isDefaultExport && !func->body.empty()) {
                            return func;
                        }
                    }
                }

                // If specifier matching failed, search all modules as last resort
                for (auto& [srcPath, srcMod] : analyzer.modules) {
                    if (!srcMod->ast) continue;
                    for (auto& srcStmt : srcMod->ast->body) {
                        auto* func = dynamic_cast<ast::FunctionDeclaration*>(srcStmt.get());
                        if (func && func->isDefaultExport && !func->body.empty()) {
                            return func;
                        }
                    }
                }
            }
        }
    }

    return firstMatch;  // Return first match if no implementation found (edge case)
}

ast::ClassDeclaration* Monomorphizer::findClass(Analyzer& analyzer, const std::string& name) {
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        for (auto& stmt : module->ast->body) {
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                if (cls->name == name) return cls;
            }
        }
    }

    // Fallback: follow re-export chains to find the original class declaration
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        auto reExportResult = followReExportChainClass(analyzer, name, path, {});
        if (reExportResult) return reExportResult;
    }

    // Fallback: check if 'name' is a default import alias.
    // E.g., `import MyAlias from './mod'` where mod has `export default class Foo`
    for (auto& [modPath, mod] : analyzer.modules) {
        if (!mod->ast) continue;
        for (auto& stmt : mod->ast->body) {
            auto* importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get());
            if (!importDecl || importDecl->defaultImport != name) continue;

            std::string spec = importDecl->moduleSpecifier;
            if (spec.size() > 2 && spec.substr(0, 2) == "./") {
                spec = spec.substr(2);
            }

            for (auto& [srcPath, srcMod] : analyzer.modules) {
                if (!srcMod->ast) continue;
                if (srcPath.find(spec) == std::string::npos) continue;
                for (auto& srcStmt : srcMod->ast->body) {
                    auto* cls = dynamic_cast<ast::ClassDeclaration*>(srcStmt.get());
                    if (cls && cls->isDefaultExport) return cls;
                }
            }
        }
    }

    return nullptr;
}

ast::FunctionDeclaration* Monomorphizer::followReExportChain(
    Analyzer& analyzer, const std::string& name,
    const std::string& modulePath, std::set<std::string> visited)
{
    // Prevent infinite loops from circular re-exports
    if (visited.count(modulePath)) return nullptr;
    visited.insert(modulePath);

    auto it = analyzer.modules.find(modulePath);
    if (it == analyzer.modules.end() || !it->second->ast) return nullptr;

    // First: check if this module has a FunctionDeclaration with this name
    for (auto& stmt : it->second->ast->body) {
        if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            if (func->name == name && !func->body.empty()) return func;
        }
    }

    // Second: scan ExportDeclaration nodes for re-exports
    for (auto& stmt : it->second->ast->body) {
        auto* exportDecl = dynamic_cast<ast::ExportDeclaration*>(stmt.get());
        if (!exportDecl) continue;

        if (exportDecl->moduleSpecifier.empty()) {
            // Local export alias: export { localFn as publicName }
            for (auto& spec : exportDecl->namedExports) {
                if (spec.name == name) {
                    std::string localName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                    // Search for localName in THIS module's function declarations
                    for (auto& s : it->second->ast->body) {
                        if (auto func = dynamic_cast<ast::FunctionDeclaration*>(s.get())) {
                            if (func->name == localName && !func->body.empty()) return func;
                        }
                    }
                }
            }
            continue;
        }

        // Re-export from another module
        if (exportDecl->isStarExport) {
            // export * from './other' -- follow into the source module
            auto resolved = analyzer.getModuleResolver().resolve(exportDecl->moduleSpecifier,
                                                             std::filesystem::path(modulePath));
            if (resolved.isValid()) {
                auto result = followReExportChain(analyzer, name, resolved.path, visited);
                if (result) return result;
            }
        } else {
            for (auto& spec : exportDecl->namedExports) {
                if (spec.name == name) {
                    // The original name in the source module
                    std::string originalName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                    auto resolved = analyzer.getModuleResolver().resolve(exportDecl->moduleSpecifier,
                                                                     std::filesystem::path(modulePath));
                    if (resolved.isValid()) {
                        auto result = followReExportChain(analyzer, originalName, resolved.path, visited);
                        if (result) return result;
                    }
                }
            }
        }
    }

    return nullptr;
}

ast::ClassDeclaration* Monomorphizer::followReExportChainClass(
    Analyzer& analyzer, const std::string& name,
    const std::string& modulePath, std::set<std::string> visited)
{
    // Prevent infinite loops from circular re-exports
    if (visited.count(modulePath)) return nullptr;
    visited.insert(modulePath);

    auto it = analyzer.modules.find(modulePath);
    if (it == analyzer.modules.end() || !it->second->ast) return nullptr;

    // First: check if this module has a ClassDeclaration with this name
    for (auto& stmt : it->second->ast->body) {
        if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
            if (cls->name == name) return cls;
        }
    }

    // Second: scan ExportDeclaration nodes for re-exports
    for (auto& stmt : it->second->ast->body) {
        auto* exportDecl = dynamic_cast<ast::ExportDeclaration*>(stmt.get());
        if (!exportDecl) continue;

        if (exportDecl->moduleSpecifier.empty()) {
            // Local export alias
            for (auto& spec : exportDecl->namedExports) {
                if (spec.name == name) {
                    std::string localName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                    for (auto& s : it->second->ast->body) {
                        if (auto cls = dynamic_cast<ast::ClassDeclaration*>(s.get())) {
                            if (cls->name == localName) return cls;
                        }
                    }
                }
            }
            continue;
        }

        // Re-export from another module
        if (exportDecl->isStarExport) {
            auto resolved = analyzer.getModuleResolver().resolve(exportDecl->moduleSpecifier,
                                                             std::filesystem::path(modulePath));
            if (resolved.isValid()) {
                auto result = followReExportChainClass(analyzer, name, resolved.path, visited);
                if (result) return result;
            }
        } else {
            for (auto& spec : exportDecl->namedExports) {
                if (spec.name == name) {
                    std::string originalName = spec.propertyName.empty() ? spec.name : spec.propertyName;
                    auto resolved = analyzer.getModuleResolver().resolve(exportDecl->moduleSpecifier,
                                                                     std::filesystem::path(modulePath));
                    if (resolved.isValid()) {
                        auto result = followReExportChainClass(analyzer, originalName, resolved.path, visited);
                        if (result) return result;
                    }
                }
            }
        }
    }

    return nullptr;
}

} // namespace ts
