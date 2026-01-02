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
    
    auto mainModule = std::make_shared<Module>();
    mainModule->path = currentFilePath;
    mainModule->type = currentModuleType;
    // We don't own the main program's AST, but we can wrap it in a shared_ptr with a no-op deleter
    // or just assume it lives long enough.
    mainModule->ast = std::shared_ptr<ast::Program>(program, [](ast::Program*){});
    currentModule = mainModule;
    modules[currentFilePath] = mainModule;

    // symbols.enterScope(); // Don't enter a new scope, use the global scope
    visitProgram(program);
    // symbols.exitScope();
    
    mainModule->analyzed = true;
    moduleOrder.push_back(currentFilePath);

    performEscapeAnalysis(program);
}

void Analyzer::analyzeModule(std::shared_ptr<Module> module) {
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;
    auto oldModuleType = currentModuleType;

    currentModule = module;
    currentFilePath = module->path;
    currentModuleType = module->type;

    symbols.enterScope();
    visitProgram(module->ast.get());
    symbols.exitScope();
    
    module->analyzed = true;
    moduleOrder.push_back(module->path);

    currentModule = oldModule;
    currentFilePath = oldPath;
    currentModuleType = oldModuleType;
}

ResolvedModule Analyzer::resolveModule(const std::string& specifier) {
    return moduleResolver.resolve(specifier, fs::path(currentFilePath));
}

void Analyzer::visit(Node* node) {
    if (!node) return;
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

