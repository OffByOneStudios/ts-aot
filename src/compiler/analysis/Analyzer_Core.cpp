#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fstream>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;
void Analyzer::analyze(ast::Program* program, const std::string& path) {
    SPDLOG_DEBUG("Analyzer::analyze starting for {}", path);
    currentFilePath = fs::absolute(path).string();
    currentModuleType = moduleResolver.getModuleType(currentFilePath);
    // Strategy B Phase 7b: select profile from module type. The legacy
    // skipUntypedSemantic field is gone — readers consult activeOptions.minimalTraversal.
    AnalyzerOptions prevOptions = activeOptions;
    activeOptions = (currentModuleType == ModuleType::UntypedJavaScript)
        ? kUntypedProfile
        : kTypedProfile;
    SPDLOG_DEBUG("Analyzer::analyze profile = {}",
        (currentModuleType == ModuleType::UntypedJavaScript) ? "untyped" : "typed");
    // Entry-point untyped JS must do full traversal to discover nested
    // require()/import calls inside variable initializers, so child modules
    // get loaded and registered with `{exports: {}}` in the monomorphizer.
    // This matches the analyzeModule() behavior for child modules (~line 114).
    // Without this, `const b = require('./b')` at the top level never triggers
    // loadModule('./b'), b.js is never in analyzer.moduleOrder, Phase 1 of
    // synthetic user_main never registers it, and runtime lookups return
    // undefined.
    if (currentModuleType == ModuleType::UntypedJavaScript) {
        activeOptions.minimalTraversal = false;
        SPDLOG_INFO("Permissive mode: analyzing untyped JavaScript entry point {}", currentFilePath);
    } else if (activeOptions.minimalTraversal) {
        SPDLOG_INFO("Permissive mode: skipping semantic checks for {}", currentFilePath);
    }
    
    auto mainModule = std::make_shared<Module>();
    mainModule->path = currentFilePath;
    mainModule->type = currentModuleType;
    // We don't own the main program's AST, but we can wrap it in a shared_ptr with a no-op deleter
    // or just assume it lives long enough.
    mainModule->ast = std::shared_ptr<ast::Program>(program, [](ast::Program*){});
    currentModule = mainModule;
    modules[currentFilePath] = mainModule;

    // symbols.enterScope(); // Don't enter a new scope, use the global scope

    // Inject module.exports for CommonJS support
    // Note: We add to the existing module type (registered in registerModule())
    // rather than creating a new one, to preserve all the module API methods
    auto existingModule = symbols.lookup("module");
    if (existingModule && existingModule->type->kind == TypeKind::Object) {
        auto moduleObjType = std::static_pointer_cast<ObjectType>(existingModule->type);
        moduleObjType->fields["exports"] = std::make_shared<Type>(TypeKind::Any);
    } else {
        // Fallback: create new module type if not found
        auto moduleType = std::make_shared<ObjectType>();
        moduleType->fields["exports"] = std::make_shared<Type>(TypeKind::Any);
        symbols.define("module", moduleType);
    }
    symbols.define("exports", std::make_shared<Type>(TypeKind::Any));

    // __dirname and __filename are available in all module types
    symbols.define("__dirname", std::make_shared<Type>(TypeKind::String));
    symbols.define("__filename", std::make_shared<Type>(TypeKind::String));

    // Strategy B Phase 5e-ii Site #2: defineCommonJSGlobals
    if (activeOptions.defineCommonJSGlobals) {
        auto requireFn = std::make_shared<FunctionType>();
        requireFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        requireFn->returnType = std::make_shared<Type>(TypeKind::Any);
        symbols.define("require", requireFn);
        symbols.define("global", std::make_shared<Type>(TypeKind::Any));
        symbols.define("self", std::make_shared<Type>(TypeKind::Any));
        symbols.define("window", std::make_shared<Type>(TypeKind::Any));
        symbols.define("Function", std::make_shared<Type>(TypeKind::Any));
        symbols.define("process", std::make_shared<Type>(TypeKind::Any));
        symbols.define("console", std::make_shared<Type>(TypeKind::Any));
        auto parseFloatFn = std::make_shared<FunctionType>();
        parseFloatFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        parseFloatFn->returnType = std::make_shared<Type>(TypeKind::Any);
        symbols.define("parseFloat", parseFloatFn);
        auto parseIntFn = std::make_shared<FunctionType>();
        parseIntFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        parseIntFn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
        parseIntFn->returnType = std::make_shared<Type>(TypeKind::Any);
        symbols.define("parseInt", parseIntFn);
    }

    visitProgram(program);
    // symbols.exitScope();
    
    mainModule->analyzed = true;
    moduleOrder.push_back(currentFilePath);

    performEscapeAnalysis(program);
    activeOptions = prevOptions;  // Strategy B Phase 5e-i: restore profile
}

void Analyzer::analyzeModule(std::shared_ptr<Module> module) {
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;
    auto oldModuleType = currentModuleType;
    // Strategy B Phase 5e-i: save/restore activeOptions across nested module
    // analysis. Profile is selected from module->type below.
    AnalyzerOptions oldOptions = activeOptions;

    currentModule = module;
    currentFilePath = module->path;
    currentModuleType = module->type;
    activeOptions = (module->type == ModuleType::UntypedJavaScript)
        ? kUntypedProfile
        : kTypedProfile;
    if (module->type != ModuleType::TypeScript) {
        activeOptions.suppressErrors = true;
    }
    // Strategy B Phase 5e-ii Site #3: at analyzeModule entry, untyped JS files
    // need full traversal even though analyze() entry uses minimal traversal.
    if (currentModuleType == ModuleType::UntypedJavaScript) {
        activeOptions.minimalTraversal = false;
        SPDLOG_INFO("Permissive mode: analyzing untyped JavaScript for {}", currentFilePath);
    }

    symbols.enterScope();
    
    // Inject module and exports for CommonJS support
    auto moduleType = std::make_shared<ObjectType>();
    moduleType->fields["exports"] = std::make_shared<Type>(TypeKind::Any);
    symbols.define("module", moduleType);
    symbols.define("exports", std::make_shared<Type>(TypeKind::Any));
    // CommonJS/Node-ish globals (safe defaults; already permissive for untyped JS)
    {
        auto ensureAny = [&](const std::string& name) {
            if (!symbols.lookup(name)) symbols.define(name, std::make_shared<Type>(TypeKind::Any));
        };
        auto ensureFnAny = [&](const std::string& name, size_t arity) {
            if (symbols.lookup(name)) return;
            auto fn = std::make_shared<FunctionType>();
            for (size_t i = 0; i < arity; ++i) fn->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            fn->returnType = std::make_shared<Type>(TypeKind::Any);
            symbols.define(name, fn);
        };

        ensureFnAny("require", 1);
        ensureAny("global");
        ensureAny("globalThis");
        ensureAny("self");
        ensureAny("window");
        // JavaScript built-in constructors/objects — must be defined so the
        // Analyzer infers them as Any (not undefined) in untyped JS modules.
        ensureAny("Object");
        ensureAny("Array");
        ensureAny("String");
        ensureAny("Number");
        ensureAny("Boolean");
        ensureAny("Function");
        ensureAny("Symbol");
        ensureAny("Map");
        ensureAny("Set");
        ensureAny("WeakMap");
        ensureAny("WeakSet");
        ensureAny("Date");
        ensureAny("RegExp");
        ensureAny("Promise");
        ensureAny("Proxy");
        ensureAny("Reflect");
        ensureAny("Temporal");
        ensureAny("Error");
        ensureAny("TypeError");
        ensureAny("RangeError");
        ensureAny("ReferenceError");
        ensureAny("SyntaxError");
        ensureAny("EvalError");
        ensureAny("URIError");
        ensureAny("ArrayBuffer");
        ensureAny("DataView");
        ensureAny("SharedArrayBuffer");
        ensureAny("GeneratorFunction");
        ensureAny("AsyncFunction");
        ensureAny("AsyncGeneratorFunction");
        ensureAny("Math");
        ensureAny("JSON");
        ensureAny("Buffer");
        ensureAny("process");
        ensureAny("console");
        if (!symbols.lookup("__dirname")) symbols.define("__dirname", std::make_shared<Type>(TypeKind::String));
        if (!symbols.lookup("__filename")) symbols.define("__filename", std::make_shared<Type>(TypeKind::String));
        ensureFnAny("parseFloat", 1);
        ensureFnAny("parseInt", 2);
        ensureFnAny("isNaN", 1);
        ensureFnAny("isFinite", 1);
        ensureFnAny("encodeURIComponent", 1);
        ensureFnAny("decodeURIComponent", 1);
        ensureFnAny("encodeURI", 1);
        ensureFnAny("decodeURI", 1);
        ensureFnAny("setInterval", 2);
        ensureFnAny("clearInterval", 1);
        ensureFnAny("setTimeout", 2);
        ensureFnAny("clearTimeout", 1);
        ensureFnAny("setImmediate", 1);
        ensureFnAny("clearImmediate", 1);
    }

    visitProgram(module->ast.get());

    // Phase 9j: CommonJS default export fallback. Modules that use
    // `module.exports = value` don't get their default export captured
    // during visitor analysis because the assignment goes through the
    // normal expression visitor, not the export declaration handler.
    // Register Any as the default so that `import X from 'package'`
    // binds X to Any rather than leaving it undefined.
    if (!module->isESM && !module->exports->lookup("default")) {
        module->exports->define("default", std::make_shared<Type>(TypeKind::Any));
    }

    // Save all module-level symbols before exiting scope.
    // This allows us to restore them when re-analyzing function bodies during monomorphization.
    for (const auto& [name, sym] : symbols.getCurrentScopeSymbols()) {
        module->moduleSymbols->define(name, sym->type);
    }
    for (const auto& [name, type] : symbols.getCurrentScopeTypes()) {
        module->moduleSymbols->defineType(name, type);
    }

    symbols.exitScope();
    
    module->analyzed = true;
    moduleOrder.push_back(module->path);

    currentModule = oldModule;
    currentFilePath = oldPath;
    currentModuleType = oldModuleType;
    activeOptions = oldOptions;  // Strategy B Phase 5e-i: restore profile
}

void Analyzer::analyzeDeclarationModule(std::shared_ptr<Module> module) {
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;
    auto oldModuleType = currentModuleType;
    AnalyzerOptions oldOptions = activeOptions;  // Strategy B Phase 5e-i

    currentModule = module;
    currentFilePath = module->path;
    currentModuleType = ModuleType::Declaration;
    // Declaration files are type-only; use the typed profile with errors suppressed.
    activeOptions = kTypedProfile;
    activeOptions.suppressErrors = true;

    symbols.enterScope();

    // Visit program — hoisting pass populates module->exports
    visitProgram(module->ast.get());

    // Save module-level symbols
    for (const auto& [name, sym] : symbols.getCurrentScopeSymbols()) {
        module->moduleSymbols->define(name, sym->type);
    }
    for (const auto& [name, type] : symbols.getCurrentScopeTypes()) {
        module->moduleSymbols->defineType(name, type);
    }

    symbols.exitScope();
    module->analyzed = true;
    // NOTE: No moduleOrder.push_back() — .d.ts has no executable code

    // Clear AST after analysis — prevents Monomorphizer from generating code
    module->ast = nullptr;

    currentModule = oldModule;
    currentFilePath = oldPath;
    currentModuleType = oldModuleType;
    activeOptions = oldOptions;  // Strategy B Phase 5e-i: restore profile
}

std::unique_ptr<ast::Program> Analyzer::parseSourceFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return nullptr;
    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    try {
        parser::Parser nativeParser;
        return nativeParser.parse(source, path);
    } catch (const std::exception& e) {
        SPDLOG_WARN("Native parser failed for {}: {}", path, e.what());
        return nullptr;
    }
}

ResolvedModule Analyzer::resolveModule(const std::string& specifier) {
    return moduleResolver.resolve(specifier, fs::path(currentFilePath));
}

void Analyzer::visit(Node* node) {
    if (!node) return;
    // Strategy B Phase 5e-ii Site #4: minimal traversal short-circuit
    if (activeOptions.minimalTraversal) {
        // Minimal traversal for raw JS: detect require() and ESM imports to pull deps, otherwise treat as any.
        if (auto call = dynamic_cast<ast::CallExpression*>(node)) {
            if (auto id = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                if (id->name == "require" && !call->arguments.empty()) {
                    if (auto lit = dynamic_cast<ast::StringLiteral*>(call->arguments[0].get())) {
                        loadModule(lit->value);
                    }
                }
            }
        }
        // ESM imports: import ... from 'module'
        if (auto importDecl = dynamic_cast<ast::ImportDeclaration*>(node)) {
            if (!importDecl->moduleSpecifier.empty() && !importDecl->isTypeOnly) {
                loadModule(importDecl->moduleSpecifier);
            }
        }
        lastType = std::make_shared<Type>(TypeKind::Any);
        if (auto expr = dynamic_cast<Expression*>(node)) {
            expr->inferredType = lastType;
            expressions.push_back(expr);
        }
        return;
    }
    node->accept(this);

    // Strategy B Phase 5e-ii Site #5: defaultExpressionsToAny
    if (activeOptions.defaultExpressionsToAny) {
        // Don't force Any for identifiers, so we can still find functions
        // and other symbols by their original type in codegen.
        if (!dynamic_cast<Identifier*>(node)) {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    }

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
        expressions.push_back(expr);
    }
}

void Analyzer::declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type) {
    if (auto id = dynamic_cast<Identifier*>(pattern)) {
        if (!symbols.define(id->name, type)) {
            symbols.update(id->name, type);
        }
    } else if (auto obp = dynamic_cast<ObjectBindingPattern*>(pattern)) {
        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<BindingElement*>(elementNode.get());
            if (!element) continue;

            std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
            if (type) {
                if (element->isSpread) {
                    if (type->kind == TypeKind::Object) {
                        auto objType = std::static_pointer_cast<ObjectType>(type);
                        auto newObjType = std::make_shared<ObjectType>();
                        newObjType->fields = objType->fields;
                        
                        // Remove fields that were already destructured
                        for (auto& prevNode : obp->elements) {
                            if (prevNode.get() == elementNode.get()) break;
                            if (auto prev = dynamic_cast<BindingElement*>(prevNode.get())) {
                                std::string name;
                                if (!prev->propertyName.empty()) name = prev->propertyName;
                                else if (auto id = dynamic_cast<Identifier*>(prev->name.get())) name = id->name;
                                
                                if (!name.empty()) {
                                    newObjType->fields.erase(name);
                                }
                            }
                        }
                        elementType = newObjType;
                    } else {
                        // Fallback for classes/interfaces/any
                        elementType = type;
                    }
                } else {
                    if (type->kind == TypeKind::Class) {
                        auto classType = std::static_pointer_cast<ClassType>(type);
                        std::string fieldName;
                        if (!element->propertyName.empty()) {
                            fieldName = element->propertyName;
                        } else if (auto nameId = dynamic_cast<Identifier*>(element->name.get())) {
                            fieldName = nameId->name;
                        }
                        
                        if (!fieldName.empty()) {
                            auto current = classType;
                            while (current) {
                                if (current->fields.count(fieldName)) {
                                    elementType = current->fields[fieldName];
                                    break;
                                }
                                current = current->baseClass;
                            }
                        }
                    } else if (type->kind == TypeKind::Object) {
                        auto objType = std::static_pointer_cast<ObjectType>(type);
                        std::string fieldName;
                        if (!element->propertyName.empty()) {
                            fieldName = element->propertyName;
                        } else if (auto nameId = dynamic_cast<Identifier*>(element->name.get())) {
                            fieldName = nameId->name;
                        }
                        if (!fieldName.empty() && objType->fields.count(fieldName)) {
                            elementType = objType->fields[fieldName];
                        }
                    }
                }
            }
            declareBindingPattern(element->name.get(), elementType);
            flagUnresolvedDefaultRef(element->initializer.get());
        }
    } else if (auto abp = dynamic_cast<ArrayBindingPattern*>(pattern)) {
        std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
        if (type && type->kind == TypeKind::Array) {
            elementType = std::static_pointer_cast<ArrayType>(type)->elementType;
        }
        for (auto& elementNode : abp->elements) {
            if (auto oe = dynamic_cast<OmittedExpression*>(elementNode.get())) continue;
            if (auto element = dynamic_cast<BindingElement*>(elementNode.get())) {
                if (element->isSpread) {
                    declareBindingPattern(element->name.get(), type); // Spread of array is array
                } else {
                    declareBindingPattern(element->name.get(), elementType);
                }
                flagUnresolvedDefaultRef(element->initializer.get());
            }
        }
    }
}

// ECMA-262 6.2.4.8 GetValue: evaluating a default Initializer that is an
// unresolvable reference throws ReferenceError. The HIR throw path
// (ts_resolve_identifier_or_throw, gated on node->isUnresolvedReference) only
// fires for identifiers the analyzer flagged. The normal flagging happens in
// visitIdentifier, but declareBindingPattern never visits a BindingElement's
// default, so `{ x = unresolvableRef } = {}` silently yielded undefined.
//
// We flag ONLY a bare-identifier default here — deliberately side-effect-free
// (no Analyzer::visit, which would perturb NamedEvaluation / type-inference and
// regress fn-name / init-skipped cases). The lazy default-eval lowering ensures
// the resulting throw fires only when the default is actually evaluated.
void Analyzer::flagUnresolvedDefaultRef(ast::Node* initializer) {
    auto* id = dynamic_cast<Identifier*>(initializer);
    if (!id) return;  // only the bare-identifier default form (the test262 cluster)
    if (id->name == "null" || id->name == "undefined") return;
    if (symbols.lookup(id->name) || symbols.lookupType(id->name)) return;  // resolvable
    id->isUnresolvedReference = true;
}

void Analyzer::declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type, DeclKind kind) {
    if (auto id = dynamic_cast<Identifier*>(pattern)) {
        if (!symbols.define(id->name, type, kind)) {
            if (kind == DeclKind::Var) {
                // var+var is allowed, just update the type
                symbols.update(id->name, type);
            } else {
                reportError("SyntaxError: Identifier '" + id->name + "' has already been declared");
            }
        }
    } else if (auto obp = dynamic_cast<ObjectBindingPattern*>(pattern)) {
        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<BindingElement*>(elementNode.get());
            if (!element) continue;
            declareBindingPattern(element->name.get(), std::make_shared<Type>(TypeKind::Any), kind);
        }
    } else if (auto abp = dynamic_cast<ArrayBindingPattern*>(pattern)) {
        for (auto& elementNode : abp->elements) {
            if (auto oe = dynamic_cast<OmittedExpression*>(elementNode.get())) continue;
            if (auto element = dynamic_cast<BindingElement*>(elementNode.get())) {
                declareBindingPattern(element->name.get(), std::make_shared<Type>(TypeKind::Any), kind);
            }
        }
    }
}

void Analyzer::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {}
void Analyzer::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {}
void Analyzer::visitBindingElement(ast::BindingElement* node) {}
void Analyzer::visitSpreadElement(ast::SpreadElement* node) {
    visit(node->expression.get());
}

bool Analyzer::loadTsConfig(const std::string& tsconfigPath) {
    SPDLOG_INFO("Loading tsconfig.json from {}", tsconfigPath);
    return moduleResolver.loadTsConfig(fs::path(tsconfigPath));
}

void Analyzer::setProjectRoot(const std::string& rootPath) {
    SPDLOG_DEBUG("Setting project root to {}", rootPath);
    moduleResolver.setProjectRoot(fs::path(rootPath));
}

} // namespace ts
