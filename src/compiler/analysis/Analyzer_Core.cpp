#include "Analyzer.h"
#include "../ast/AstLoader.h"
#include <iostream>
#include <fmt/core.h>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts {

using namespace ast;
void Analyzer::analyze(ast::Program* program, const std::string& path) {
    fprintf(stderr, "Analyzer::analyze starting for %s\n", path.c_str());
    currentFilePath = fs::absolute(path).string();
    auto mainModule = std::make_shared<Module>();
    mainModule->path = currentFilePath;
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

    currentModule = module;
    currentFilePath = module->path;

    symbols.enterScope();
    visitProgram(module->ast.get());
    symbols.exitScope();
    
    module->analyzed = true;
    moduleOrder.push_back(module->path);

    currentModule = oldModule;
    currentFilePath = oldPath;
}

std::string Analyzer::resolveModulePath(const std::string& specifier) {
    fs::path base = fs::path(currentFilePath).parent_path();
    fs::path resolved = base / specifier;
    
    // Try .ts, then .json (pre-compiled AST)
    if (fs::exists(resolved.string() + ".ts")) return fs::absolute(resolved.string() + ".ts").string();
    if (fs::exists(resolved.string() + ".json")) return fs::absolute(resolved.string() + ".json").string();
    
    return "";
}

void Analyzer::visit(Node* node) {
    if (!node) return;
    node->accept(this);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
        expressions.push_back(expr);
    }
}

void Analyzer::declareBindingPattern(ast::Node* pattern, std::shared_ptr<Type> type) {
    if (auto id = dynamic_cast<Identifier*>(pattern)) {
        symbols.define(id->name, type);
    } else if (auto obp = dynamic_cast<ObjectBindingPattern*>(pattern)) {
        for (auto& elementNode : obp->elements) {
            auto element = dynamic_cast<BindingElement*>(elementNode.get());
            if (!element) continue;

            std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
            if (type) {
                if (element->isSpread) {
                    // For now, spread of an object is just Any or the same object type (simplified)
                    elementType = type; 
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
} // namespace ts

