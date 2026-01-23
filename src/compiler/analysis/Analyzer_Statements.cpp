#include "Analyzer.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {

using namespace ast;

void Analyzer::visitExpressionStatement(ast::ExpressionStatement* node) {
    visit(node->expression.get());
}

void Analyzer::visitVariableDeclaration(ast::VariableDeclaration* node) {
    std::shared_ptr<Type> type = std::make_shared<Type>(TypeKind::Any);
    bool isJavaScript = (currentModuleType == ModuleType::UntypedJavaScript);
    
    if (isJavaScript) {
        // JavaScript slow path: all variables are Any
        type = std::make_shared<Type>(TypeKind::Any);
    } else if (!node->type.empty()) {
        type = parseType(node->type, symbols);
    }

    // Declare first so it's in scope for the initializer
    declareBindingPattern(node->name.get(), type);

    if (node->initializer) {
        visit(node->initializer.get());
        if (lastType) {
            // For JavaScript, keep type as Any - don't infer from initializer
            if (!isJavaScript && node->type.empty()) {
                type = lastType;
                // Update the type in the symbol table
                if (auto id = dynamic_cast<Identifier*>(node->name.get())) {
                    SPDLOG_DEBUG("  Updating {} to type: {}", id->name, type->toString());
                    symbols.update(id->name, type);
                } else if (dynamic_cast<ast::ObjectBindingPattern*>(node->name.get()) || 
                           dynamic_cast<ast::ArrayBindingPattern*>(node->name.get())) {
                    // Re-declare with the correct type to update individual variables
                    declareBindingPattern(node->name.get(), type);
                }
            } else if (!isJavaScript) {
                // Type annotation is present - check assignability
                if (!lastType->isAssignableTo(type)) {
                    reportError(fmt::format("Type {} is not assignable to type {}", lastType->toString(), type->toString()));
                }
                
                // Only override initializer's inferredType when the declared type provides
                // more specific information (e.g., `const arr: Set<T>[] = []`).
                // Do NOT override when the declared type is Any - this would lose the
                // original type info (e.g., function reference → undefined in codegen).
                if (type->kind != TypeKind::Any) {
                    node->initializer->inferredType = type;
                }
            }
        }
    }
    node->resolvedType = type;
    
    // Add to top-level variables only if at module scope (not inside any function or nested scope)
    // symbols.getDepth() == 1 means we're in the top-level global scope (enterScope was called once at start)
    if (functionDepth == 0 && symbols.getDepth() == 1) {
        if (auto id = dynamic_cast<Identifier*>(node->name.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->name = id->name;
            sym->type = type;  // Now uses the correctly inferred type
            sym->modulePath = currentModule ? currentModule->path : "";
            topLevelVariables.push_back(sym);
        }
    }
    
    if (node->isExported && currentModule) {
        if (auto id = dynamic_cast<Identifier*>(node->name.get())) {
            currentModule->exports->define(id->name, type);
        }
    }
}

void Analyzer::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        currentReturnType = lastType;
    }
}

void Analyzer::visitIfStatement(ast::IfStatement* node) {
    visit(node->condition.get());
    
    // Basic type narrowing
    std::string narrowedVar;
    std::shared_ptr<Type> narrowedType;
    
    if (auto bin = dynamic_cast<BinaryExpression*>(node->condition.get())) {
        if (bin->op == "===" || bin->op == "==") {
            PrefixUnaryExpression* typeofExpr = nullptr;
            StringLiteral* typeString = nullptr;
            
            if (auto left = dynamic_cast<PrefixUnaryExpression*>(bin->left.get())) {
                if (left->op == "typeof") {
                    typeofExpr = left;
                    typeString = dynamic_cast<StringLiteral*>(bin->right.get());
                }
            } else if (auto right = dynamic_cast<PrefixUnaryExpression*>(bin->right.get())) {
                if (right->op == "typeof") {
                    typeofExpr = right;
                    typeString = dynamic_cast<StringLiteral*>(bin->left.get());
                }
            }
            
            if (typeofExpr && typeString) {
                if (auto id = dynamic_cast<Identifier*>(typeofExpr->operand.get())) {
                    narrowedVar = id->name;
                    if (typeString->value == "string") narrowedType = std::make_shared<Type>(TypeKind::String);
                    else if (typeString->value == "number") narrowedType = std::make_shared<Type>(TypeKind::Double);
                    else if (typeString->value == "boolean") narrowedType = std::make_shared<Type>(TypeKind::Boolean);
                }
            }
        } else if (bin->op == "!==" || bin->op == "!=") {
            // x !== null narrowing
            Identifier* id = nullptr;
            bool isNull = false;
            bool isUndefined = false;

            // Check left side is identifier and right side is null/undefined literal
            if (auto left = dynamic_cast<Identifier*>(bin->left.get())) {
                if (dynamic_cast<NullLiteral*>(bin->right.get())) {
                    id = left;
                    isNull = true;
                } else if (dynamic_cast<UndefinedLiteral*>(bin->right.get())) {
                    id = left;
                    isUndefined = true;
                } else if (auto rightId = dynamic_cast<Identifier*>(bin->right.get())) {
                    // Handle undefined as identifier (global variable)
                    if (rightId->name == "undefined") {
                        id = left;
                        isUndefined = true;
                    }
                }
            }
            // Check right side is identifier and left side is null/undefined literal
            else if (auto right = dynamic_cast<Identifier*>(bin->right.get())) {
                if (dynamic_cast<NullLiteral*>(bin->left.get())) {
                    id = right;
                    isNull = true;
                } else if (dynamic_cast<UndefinedLiteral*>(bin->left.get())) {
                    id = right;
                    isUndefined = true;
                } else if (auto leftId = dynamic_cast<Identifier*>(bin->left.get())) {
                    // Handle undefined as identifier (global variable)
                    if (leftId->name == "undefined") {
                        id = right;
                        isUndefined = true;
                    }
                }
            }

            if (id && (isNull || isUndefined)) {
                auto sym = symbols.lookup(id->name);
                if (sym && sym->type->kind == TypeKind::Union) {
                    auto unionType = std::static_pointer_cast<UnionType>(sym->type);
                    std::vector<std::shared_ptr<Type>> remaining;

                    for (auto& t : unionType->types) {
                        if (isNull && t->kind == TypeKind::Null) continue;
                        if (isUndefined && t->kind == TypeKind::Undefined) continue;
                        remaining.push_back(t);
                    }

                    if (remaining.size() == 1) narrowedType = remaining[0];
                    else if (remaining.size() > 1) {
                        narrowedType = std::make_shared<UnionType>(remaining);
                    }
                    narrowedVar = id->name;
                }
            }
        } else if (bin->op == "instanceof") {
            if (auto id = dynamic_cast<Identifier*>(bin->left.get())) {
                if (auto rightId = dynamic_cast<Identifier*>(bin->right.get())) {
                    auto type = symbols.lookupType(rightId->name);
                    if (type) {
                        narrowedVar = id->name;
                        narrowedType = type;
                    }
                }
            }
        } else if (bin->op == "in") {
            // "prop" in obj narrowing - narrow to types that have the property
            StringLiteral* propName = dynamic_cast<StringLiteral*>(bin->left.get());
            Identifier* objId = dynamic_cast<Identifier*>(bin->right.get());

            if (propName && objId) {
                auto sym = symbols.lookup(objId->name);
                if (sym && sym->type->kind == TypeKind::Union) {
                    auto unionType = std::static_pointer_cast<UnionType>(sym->type);
                    std::vector<std::shared_ptr<Type>> remaining;

                    for (auto& t : unionType->types) {
                        // Keep types that have the property
                        bool hasProperty = false;
                        if (t->kind == TypeKind::Object) {
                            auto objType = std::static_pointer_cast<ObjectType>(t);
                            if (objType->fields.count(propName->value) > 0) {
                                hasProperty = true;
                            }
                        } else if (t->kind == TypeKind::Class) {
                            auto classType = std::static_pointer_cast<ClassType>(t);
                            if (classType->fields.count(propName->value) > 0 ||
                                classType->methods.count(propName->value) > 0) {
                                hasProperty = true;
                            }
                        } else if (t->kind == TypeKind::Interface) {
                            auto interfaceType = std::static_pointer_cast<InterfaceType>(t);
                            if (interfaceType->fields.count(propName->value) > 0 ||
                                interfaceType->methods.count(propName->value) > 0) {
                                hasProperty = true;
                            }
                        }
                        if (hasProperty) {
                            remaining.push_back(t);
                        }
                    }

                    if (!remaining.empty()) {
                        if (remaining.size() == 1) narrowedType = remaining[0];
                        else narrowedType = std::make_shared<UnionType>(remaining);
                        narrowedVar = objId->name;
                    }
                }
            }
        }
    } else if (auto id = dynamic_cast<Identifier*>(node->condition.get())) {
        // if (x) truthiness narrowing
        auto sym = symbols.lookup(id->name);
        if (sym && sym->type->kind == TypeKind::Union) {
            auto unionType = std::static_pointer_cast<UnionType>(sym->type);
            std::vector<std::shared_ptr<Type>> remaining;
            for (auto& t : unionType->types) {
                if (t->kind == TypeKind::Null || t->kind == TypeKind::Undefined) continue;
                remaining.push_back(t);
            }
            if (!remaining.empty()) {
                narrowedVar = id->name;
                if (remaining.size() == 1) narrowedType = remaining[0];
                else {
                    narrowedType = std::make_shared<UnionType>(remaining);
                }
            }
        }
    } else if (auto prefix = dynamic_cast<PrefixUnaryExpression*>(node->condition.get())) {
        if (prefix->op == "!") {
            if (auto id = dynamic_cast<Identifier*>(prefix->operand.get())) {
                // if (!x) narrowing for the ELSE block
                auto sym = symbols.lookup(id->name);
                if (sym && sym->type->kind == TypeKind::Union) {
                    auto unionType = std::static_pointer_cast<UnionType>(sym->type);
                    std::vector<std::shared_ptr<Type>> remaining;
                    for (auto& t : unionType->types) {
                        if (t->kind == TypeKind::Null || t->kind == TypeKind::Undefined) continue;
                        remaining.push_back(t);
                    }
                    if (!remaining.empty()) {
                        // We need to apply this to the else block, but visitIfStatement doesn't easily support that yet
                        // For now, let's just handle the simple case where we narrow in the then block if it's NOT a return
                    }
                }
            }
        }
    } else if (auto call = dynamic_cast<CallExpression*>(node->condition.get())) {
        // Array.isArray(x) narrowing
        if (auto prop = dynamic_cast<PropertyAccessExpression*>(call->callee.get())) {
            if (auto objId = dynamic_cast<Identifier*>(prop->expression.get())) {
                if (objId->name == "Array" && prop->name == "isArray" && call->arguments.size() == 1) {
                    if (auto argId = dynamic_cast<Identifier*>(call->arguments[0].get())) {
                        auto sym = symbols.lookup(argId->name);
                        if (sym) {
                            // Find array types in the union
                            if (sym->type->kind == TypeKind::Union) {
                                auto unionType = std::static_pointer_cast<UnionType>(sym->type);
                                std::vector<std::shared_ptr<Type>> remaining;
                                for (auto& t : unionType->types) {
                                    if (t->kind == TypeKind::Array) {
                                        remaining.push_back(t);
                                    }
                                }
                                if (!remaining.empty()) {
                                    if (remaining.size() == 1) narrowedType = remaining[0];
                                    else narrowedType = std::make_shared<UnionType>(remaining);
                                    narrowedVar = argId->name;
                                }
                            } else if (sym->type->kind == TypeKind::Array) {
                                // Already an array, just narrow to the same type
                                narrowedType = sym->type;
                                narrowedVar = argId->name;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!narrowedVar.empty() && narrowedType) {
        symbols.enterScope();
        symbols.define(narrowedVar, narrowedType);
        visit(node->thenStatement.get());
        symbols.exitScope();
    } else {
        visit(node->thenStatement.get());
    }

    if (node->elseStatement) {
        visit(node->elseStatement.get());
    }
}

void Analyzer::visitWhileStatement(ast::WhileStatement* node) {
    visit(node->condition.get());
    visit(node->body.get());
}

void Analyzer::visitForStatement(ast::ForStatement* node) {
    symbols.enterScope();
    if (node->initializer) visit(node->initializer.get());
    if (node->condition) visit(node->condition.get());
    if (node->incrementor) visit(node->incrementor.get());
    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitForOfStatement(ast::ForOfStatement* node) {
    symbols.enterScope();
    
    visit(node->expression.get());
    auto iterableType = lastType;
    
    std::shared_ptr<Type> elemType;
    if (iterableType->kind == TypeKind::Array) {
        elemType = std::static_pointer_cast<ArrayType>(iterableType)->elementType;
    } else if (iterableType->kind == TypeKind::String) {
        elemType = std::make_shared<Type>(TypeKind::String); // Iterating string yields strings
    } else {
        elemType = std::make_shared<Type>(TypeKind::Any);
    }

    // Handle initializer (VariableDeclaration)
    if (auto varDecl = dynamic_cast<VariableDeclaration*>(node->initializer.get())) {
        declareBindingPattern(varDecl->name.get(), elemType);
    }

    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitForInStatement(ast::ForInStatement* node) {
    symbols.enterScope();
    
    visit(node->expression.get());
    // For..in always yields strings (keys)
    std::shared_ptr<Type> keyType = std::make_shared<Type>(TypeKind::String);

    // Handle initializer (VariableDeclaration)
    if (auto varDecl = dynamic_cast<VariableDeclaration*>(node->initializer.get())) {
        declareBindingPattern(varDecl->name.get(), keyType);
    }

    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitSwitchStatement(ast::SwitchStatement* node) {
    visit(node->expression.get());
    for (auto& clause : node->clauses) {
        if (auto cc = dynamic_cast<CaseClause*>(clause.get())) {
            visit(cc->expression.get());
            for (auto& stmt : cc->statements) visit(stmt.get());
        } else if (auto dc = dynamic_cast<DefaultClause*>(clause.get())) {
            for (auto& stmt : dc->statements) visit(stmt.get());
        }
    }
}

void Analyzer::visitTryStatement(ast::TryStatement* node) {
    for (auto& stmt : node->tryBlock) {
        visit(stmt.get());
    }
    if (node->catchClause) {
        symbols.enterScope();
        if (node->catchClause->variable) {
            declareBindingPattern(node->catchClause->variable.get(), std::make_shared<Type>(TypeKind::Any));
        }
        for (auto& stmt : node->catchClause->block) {
            visit(stmt.get());
        }
        symbols.exitScope();
    }
    for (auto& stmt : node->finallyBlock) {
        visit(stmt.get());
    }
}

void Analyzer::visitThrowStatement(ast::ThrowStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
    }
}

void Analyzer::visitBreakStatement(ast::BreakStatement* node) {
    // No analysis needed for now
}

void Analyzer::visitContinueStatement(ast::ContinueStatement* node) {
    // No analysis needed for now
}

void Analyzer::visitBlockStatement(ast::BlockStatement* node) {
    symbols.enterScope();
    for (auto& stmt : node->statements) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

} // namespace ts

