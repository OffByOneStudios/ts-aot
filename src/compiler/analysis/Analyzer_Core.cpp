#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
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
    bool prevSkipUntyped = skipUntypedSemantic;
    if (currentModuleType == ModuleType::UntypedJavaScript) {
        skipUntypedSemantic = true;
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
    
    // Inject module and exports for CommonJS support
    auto moduleType = std::make_shared<ObjectType>();
    moduleType->fields["exports"] = std::make_shared<Type>(TypeKind::Any);
    symbols.define("module", moduleType);
    symbols.define("exports", std::make_shared<Type>(TypeKind::Any));
    if (currentModuleType == ModuleType::UntypedJavaScript) {
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
        symbols.define("__dirname", std::make_shared<Type>(TypeKind::String));
        symbols.define("__filename", std::make_shared<Type>(TypeKind::String));
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
    skipUntypedSemantic = prevSkipUntyped;
}

void Analyzer::analyzeModule(std::shared_ptr<Module> module) {
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;
    auto oldModuleType = currentModuleType;
    bool oldSuppressErrors = suppressErrors;
    bool oldSkipUntyped = skipUntypedSemantic;

    currentModule = module;
    currentFilePath = module->path;
    currentModuleType = module->type;
    if (module->type != ModuleType::TypeScript) {
        suppressErrors = true;
    }
    if (currentModuleType == ModuleType::UntypedJavaScript) {
        // For real-world JS (e.g., lodash), we still need a full traversal to build enough
        // symbol info for codegen, but we do it permissively (suppressErrors=true).
        // Avoid the ultra-minimal skipUntypedSemantic path here.
        skipUntypedSemantic = false;
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
        ensureAny("self");
        ensureAny("window");
        ensureAny("Function");
        ensureAny("process");
        ensureAny("console");
        if (!symbols.lookup("__dirname")) symbols.define("__dirname", std::make_shared<Type>(TypeKind::String));
        if (!symbols.lookup("__filename")) symbols.define("__filename", std::make_shared<Type>(TypeKind::String));
        ensureFnAny("parseFloat", 1);
        // parseInt commonly takes (string, radix)
        ensureFnAny("parseInt", 2);
    }

    visitProgram(module->ast.get());
    symbols.exitScope();
    
    module->analyzed = true;
    moduleOrder.push_back(module->path);

    currentModule = oldModule;
    currentFilePath = oldPath;
    currentModuleType = oldModuleType;
    suppressErrors = oldSuppressErrors;
    skipUntypedSemantic = oldSkipUntyped;
}

ResolvedModule Analyzer::resolveModule(const std::string& specifier) {
    return moduleResolver.resolve(specifier, fs::path(currentFilePath));
}

void Analyzer::visit(Node* node) {
    if (!node) return;
    if (skipUntypedSemantic && currentModuleType == ModuleType::UntypedJavaScript) {
        // Minimal traversal for raw JS: detect require() to pull deps, otherwise treat as any.
        if (auto call = dynamic_cast<ast::CallExpression*>(node)) {
            if (auto id = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                if (id->name == "require" && !call->arguments.empty()) {
                    if (auto lit = dynamic_cast<ast::StringLiteral*>(call->arguments[0].get())) {
                        loadModule(lit->value);
                    }
                }
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

    if (currentModuleType == ModuleType::UntypedJavaScript) {
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
