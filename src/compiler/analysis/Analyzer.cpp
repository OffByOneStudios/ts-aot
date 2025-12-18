#include "Analyzer.h"
#include <iostream>
#include <fmt/core.h>
#include <sstream>

namespace ts {

using namespace ast;

Analyzer::Analyzer() {}

void Analyzer::analyze(Program* program) {
    visitProgram(program);
}

void Analyzer::visitProgram(Program* node) {
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
}

void Analyzer::visit(Node* node) {
    if (!node) return;

    if (auto p = dynamic_cast<Program*>(node)) visitProgram(p);
    else if (auto f = dynamic_cast<FunctionDeclaration*>(node)) visitFunctionDeclaration(f);
    else if (auto c = dynamic_cast<ClassDeclaration*>(node)) visitClassDeclaration(c);
    else if (auto v = dynamic_cast<VariableDeclaration*>(node)) visitVariableDeclaration(v);
    else if (auto e = dynamic_cast<ExpressionStatement*>(node)) visitExpressionStatement(e);
    else if (auto r = dynamic_cast<ReturnStatement*>(node)) visitReturnStatement(r);
    else if (auto c = dynamic_cast<CallExpression*>(node)) visitCallExpression(c);
    else if (auto n = dynamic_cast<NewExpression*>(node)) visitNewExpression(n);
    else if (auto obj = dynamic_cast<ObjectLiteralExpression*>(node)) visitObjectLiteralExpression(obj);
    else if (auto arr = dynamic_cast<ArrayLiteralExpression*>(node)) visitArrayLiteralExpression(arr);
    else if (auto elem = dynamic_cast<ElementAccessExpression*>(node)) visitElementAccessExpression(elem);
    else if (auto pa = dynamic_cast<PropertyAccessExpression*>(node)) visitPropertyAccessExpression(pa);
    else if (auto as = dynamic_cast<AsExpression*>(node)) visitAsExpression(as);
    else if (auto be = dynamic_cast<BinaryExpression*>(node)) visitBinaryExpression(be);
    else if (auto assign = dynamic_cast<AssignmentExpression*>(node)) visitAssignmentExpression(assign);
    else if (auto ifStmt = dynamic_cast<IfStatement*>(node)) visitIfStatement(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStatement*>(node)) visitWhileStatement(whileStmt);
    else if (auto forStmt = dynamic_cast<ForStatement*>(node)) visitForStatement(forStmt);
    else if (auto forOfStmt = dynamic_cast<ForOfStatement*>(node)) visitForOfStatement(forOfStmt);
    else if (auto sw = dynamic_cast<SwitchStatement*>(node)) visitSwitchStatement(sw);
    else if (auto tryStmt = dynamic_cast<TryStatement*>(node)) visitTryStatement(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStatement*>(node)) visitThrowStatement(throwStmt);
    else if (auto br = dynamic_cast<BreakStatement*>(node)) visitBreakStatement(br);
    else if (auto cont = dynamic_cast<ContinueStatement*>(node)) visitContinueStatement(cont);
    else if (auto block = dynamic_cast<BlockStatement*>(node)) visitBlockStatement(block);
    else if (auto id = dynamic_cast<Identifier*>(node)) visitIdentifier(id);
    else if (auto sl = dynamic_cast<StringLiteral*>(node)) visitStringLiteral(sl);
    else if (auto nl = dynamic_cast<NumericLiteral*>(node)) visitNumericLiteral(nl);
    else if (auto bl = dynamic_cast<BooleanLiteral*>(node)) visitBooleanLiteral(bl);
    else if (auto arrow = dynamic_cast<ArrowFunction*>(node)) visitArrowFunction(arrow);
    else if (auto tmpl = dynamic_cast<TemplateExpression*>(node)) visitTemplateExpression(tmpl);
    else if (auto pre = dynamic_cast<PrefixUnaryExpression*>(node)) visitPrefixUnaryExpression(pre);
    else if (auto sup = dynamic_cast<SuperExpression*>(node)) visitSuperExpression(sup);
    else if (auto obp = dynamic_cast<ObjectBindingPattern*>(node)) visitObjectBindingPattern(obp);
    else if (auto abp = dynamic_cast<ArrayBindingPattern*>(node)) visitArrayBindingPattern(abp);
    else if (auto be = dynamic_cast<BindingElement*>(node)) visitBindingElement(be);
    else if (auto se = dynamic_cast<SpreadElement*>(node)) visitSpreadElement(se);
    else if (auto oe = dynamic_cast<OmittedExpression*>(node)) visitOmittedExpression(oe);

    if (auto expr = dynamic_cast<Expression*>(node)) {
        expr->inferredType = lastType;
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
void Analyzer::visitOmittedExpression(ast::OmittedExpression* node) {
    lastType = std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<Type> Analyzer::resolveType(const std::string& typeName) {
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

    return parseType(typeName, symbols);
}

std::shared_ptr<Type> Analyzer::parseType(const std::string& typeName, SymbolTable& symbols) {
    if (typeName == "number") return std::make_shared<Type>(TypeKind::Double);
    if (typeName == "string") return std::make_shared<Type>(TypeKind::String);
    if (typeName == "boolean") return std::make_shared<Type>(TypeKind::Boolean);
    if (typeName == "void") return std::make_shared<Type>(TypeKind::Void);
    if (typeName == "any") return std::make_shared<Type>(TypeKind::Any);
    
    auto type = symbols.lookupType(typeName);
    if (type) return type;
    
    return std::make_shared<Type>(TypeKind::Any);
}

std::shared_ptr<FunctionType> Analyzer::resolveOverload(const std::vector<std::shared_ptr<FunctionType>>& overloads, const std::vector<std::shared_ptr<Type>>& argTypes) {
    if (overloads.empty()) return nullptr;
    
    for (const auto& overload : overloads) {
        if (overload->paramTypes.size() != argTypes.size()) continue;
        
        bool match = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]->isAssignableTo(overload->paramTypes[i])) {
                match = false;
                break;
            }
        }
        if (match) return overload;
    }
    
    return overloads[0]; // Fallback
}

void Analyzer::reportError(const std::string& message) {
    fmt::print(stderr, "Error: {}\n", message);
    errorCount++;
}

} // namespace ts
