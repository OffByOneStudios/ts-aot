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
    // Strategy B Phase 5e-ii Site #11: defaultExpressionsToAny (variable
    // declarations are forced to Any in untyped JS — same flag as Site #5).
    bool isJavaScript = activeOptions.defaultExpressionsToAny;
    
    if (isJavaScript) {
        // JavaScript slow path: all variables are Any
        type = std::make_shared<Type>(TypeKind::Any);
    } else if (!node->type.empty()) {
        type = parseType(node->type, symbols);
    }

    // Declare the binding. The hoisting pass already registered the name with
    // the correct DeclKind, so use the plain overload here which allows
    // re-definition in the same scope (updating the type).
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
            currentModule->reDirectExports.insert(id->name);
        }
    }
}

void Analyzer::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        visit(node->expression.get());
        currentReturnType = lastType;
        // Step-2 checker: returned value vs the DECLARED (annotated) return
        // type of the enclosing function. Only concrete, non-generic pairs
        // are compared (declaredReturnType_ is null when unannotated).
        if (declaredReturnType_ && lastType &&
            declaredReturnType_->kind != TypeKind::Any &&
            declaredReturnType_->kind != TypeKind::Unknown &&
            declaredReturnType_->kind != TypeKind::TypeParameter &&
            declaredReturnType_->kind != TypeKind::Function &&
            lastType->kind != TypeKind::Any &&
            lastType->kind != TypeKind::Unknown &&
            lastType->kind != TypeKind::TypeParameter &&
            !lastType->isAssignableTo(declaredReturnType_)) {
            reportError(fmt::format("Type {} is not assignable to type {}",
                                    lastType->toString(),
                                    declaredReturnType_->toString()));
        }
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

            // Discriminated union narrowing: shape.kind === "circle"
            if (!narrowedType) {
                PropertyAccessExpression* propAccess = nullptr;
                StringLiteral* literalValue = nullptr;

                if (auto left = dynamic_cast<PropertyAccessExpression*>(bin->left.get())) {
                    if (auto right = dynamic_cast<StringLiteral*>(bin->right.get())) {
                        propAccess = left;
                        literalValue = right;
                    }
                } else if (auto right = dynamic_cast<PropertyAccessExpression*>(bin->right.get())) {
                    if (auto left = dynamic_cast<StringLiteral*>(bin->left.get())) {
                        propAccess = right;
                        literalValue = left;
                    }
                }

                if (propAccess && literalValue) {
                    if (auto objId = dynamic_cast<Identifier*>(propAccess->expression.get())) {
                        auto sym = symbols.lookup(objId->name);
                        if (sym && sym->type->kind == TypeKind::Union) {
                            auto unionType = std::static_pointer_cast<UnionType>(sym->type);
                            std::vector<std::shared_ptr<Type>> matching;
                            std::string propName = propAccess->name;
                            std::string targetValue = literalValue->value;

                            for (auto& t : unionType->types) {
                                bool matches = false;
                                std::shared_ptr<Type> propType = nullptr;

                                // Get the property type from the union member
                                if (t->kind == TypeKind::Interface) {
                                    auto iface = std::static_pointer_cast<InterfaceType>(t);
                                    if (iface->fields.count(propName)) {
                                        propType = iface->fields[propName];
                                    }
                                } else if (t->kind == TypeKind::Object) {
                                    auto obj = std::static_pointer_cast<ObjectType>(t);
                                    if (obj->fields.count(propName)) {
                                        propType = obj->fields[propName];
                                    }
                                } else if (t->kind == TypeKind::Class) {
                                    auto cls = std::static_pointer_cast<ClassType>(t);
                                    if (cls->fields.count(propName)) {
                                        propType = cls->fields[propName];
                                    }
                                }

                                // Check if the property exists and is string-typed
                                // Note: Without literal types in the type system, we narrow to
                                // union members that have this property as a string. The runtime
                                // equality check ensures only the matching type's branch executes.
                                if (propType && propType->kind == TypeKind::String) {
                                    matches = true;
                                }

                                if (matches) {
                                    matching.push_back(t);
                                }
                            }

                            if (!matching.empty()) {
                                if (matching.size() == 1) {
                                    narrowedType = matching[0];
                                } else {
                                    narrowedType = std::make_shared<UnionType>(matching);
                                }
                                narrowedVar = objId->name;
                            }
                        }
                    }
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

void Analyzer::visitLabeledStatement(ast::LabeledStatement* node) {
    // Visit the inner statement
    visit(node->statement.get());
}

// Collect identifiers ASSIGNED anywhere under `n` (plain/compound assignment,
// ++/--). Used by the with-body poison below. Best-effort structural walk
// over the common statement/expression forms (missing an exotic form only
// means that shape keeps the old behavior).
static void collectAssignedIdents(ast::Node* n, std::set<std::string>& out) {
    if (!n) return;
    if (auto* a = dynamic_cast<ast::AssignmentExpression*>(n)) {
        if (auto* id = dynamic_cast<ast::Identifier*>(a->left.get()))
            out.insert(id->name);
        collectAssignedIdents(a->left.get(), out);
        collectAssignedIdents(a->right.get(), out);
        return;
    }
    if (auto* pre = dynamic_cast<ast::PrefixUnaryExpression*>(n)) {
        if (pre->op == "++" || pre->op == "--")
            if (auto* id = dynamic_cast<ast::Identifier*>(pre->operand.get()))
                out.insert(id->name);
        collectAssignedIdents(pre->operand.get(), out);
        return;
    }
    if (auto* post = dynamic_cast<ast::PostfixUnaryExpression*>(n)) {
        if (auto* id = dynamic_cast<ast::Identifier*>(post->operand.get()))
            out.insert(id->name);
        collectAssignedIdents(post->operand.get(), out);
        return;
    }
    if (auto* es = dynamic_cast<ast::ExpressionStatement*>(n)) {
        collectAssignedIdents(es->expression.get(), out);
        return;
    }
    if (auto* b = dynamic_cast<ast::BinaryExpression*>(n)) {
        collectAssignedIdents(b->left.get(), out);
        collectAssignedIdents(b->right.get(), out);
        return;
    }
    if (auto* c = dynamic_cast<ast::ConditionalExpression*>(n)) {
        collectAssignedIdents(c->condition.get(), out);
        collectAssignedIdents(c->whenTrue.get(), out);
        collectAssignedIdents(c->whenFalse.get(), out);
        return;
    }
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(n)) {
        collectAssignedIdents(p->expression.get(), out);
        return;
    }
    if (auto* call = dynamic_cast<ast::CallExpression*>(n)) {
        collectAssignedIdents(call->callee.get(), out);
        for (auto& arg : call->arguments) collectAssignedIdents(arg.get(), out);
        return;
    }
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(n)) {
        collectAssignedIdents(vd->initializer.get(), out);
        return;
    }
    if (auto* blk = dynamic_cast<ast::BlockStatement*>(n)) {
        for (auto& s : blk->statements) collectAssignedIdents(s.get(), out);
        return;
    }
    if (auto* i = dynamic_cast<ast::IfStatement*>(n)) {
        collectAssignedIdents(i->condition.get(), out);
        collectAssignedIdents(i->thenStatement.get(), out);
        collectAssignedIdents(i->elseStatement.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<ast::WhileStatement*>(n)) {
        collectAssignedIdents(w->condition.get(), out);
        collectAssignedIdents(w->body.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ast::ForStatement*>(n)) {
        collectAssignedIdents(f->initializer.get(), out);
        collectAssignedIdents(f->condition.get(), out);
        collectAssignedIdents(f->incrementor.get(), out);
        collectAssignedIdents(f->body.get(), out);
        return;
    }
    if (auto* fo = dynamic_cast<ast::ForOfStatement*>(n)) {
        collectAssignedIdents(fo->initializer.get(), out);
        collectAssignedIdents(fo->body.get(), out);
        return;
    }
    if (auto* fi = dynamic_cast<ast::ForInStatement*>(n)) {
        collectAssignedIdents(fi->initializer.get(), out);
        collectAssignedIdents(fi->body.get(), out);
        return;
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(n)) {
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get()))
                for (auto& s : cc->statements) collectAssignedIdents(s.get(), out);
            if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get()))
                for (auto& s : dc->statements) collectAssignedIdents(s.get(), out);
        }
        return;
    }
    if (auto* t = dynamic_cast<ast::TryStatement*>(n)) {
        for (auto& s : t->tryBlock) collectAssignedIdents(s.get(), out);
        if (t->catchClause)
            for (auto& s : t->catchClause->block) collectAssignedIdents(s.get(), out);
        for (auto& s : t->finallyBlock) collectAssignedIdents(s.get(), out);
        return;
    }
    if (auto* l = dynamic_cast<ast::LabeledStatement*>(n)) {
        collectAssignedIdents(l->statement.get(), out);
        return;
    }
}

void Analyzer::visitBlockStatement(ast::BlockStatement* node) {
    // Synthetic blocks (e.g., multi-var-decl `const a=1, b=2, c=3` wrapped
    // by the parser) must NOT enter a new scope — their variables belong
    // to the enclosing scope.
    if (!node->isSynthetic) {
        symbols.enterScope();
    }
    // `with (head) body` (parser desugars to a block carrying withHead):
    // every identifier ASSIGNED in the body resolves DYNAMICALLY at runtime
    // (the with-object may supply the binding, and the RHS reads resolve
    // through the with-stack as boxed Any). A statically String/number-typed
    // outer var assigned here would have Any values stored into its typed
    // slot — reads then crashed (ts_string_extract_ptr on NANBOX_UNDEFINED,
    // the S12.10_A1.10 with-family crash). Widen assigned symbols to Any.
    if (node->withHead) {
        std::set<std::string> assigned;
        for (auto& stmt : node->statements) {
            collectAssignedIdents(stmt.get(), assigned);
        }
        for (const auto& nm : assigned) {
            if (auto sym = symbols.lookup(nm)) {
                if (sym->type && sym->type->kind != TypeKind::Any) {
                    sym->type = std::make_shared<Type>(TypeKind::Any);
                }
            }
        }
    }
    for (auto& stmt : node->statements) {
        visit(stmt.get());
    }
    if (!node->isSynthetic) {
        symbols.exitScope();
    }
}

} // namespace ts

