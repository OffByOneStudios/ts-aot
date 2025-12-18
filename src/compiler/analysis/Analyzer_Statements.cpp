#include "Analyzer.h"

namespace ts {

using namespace ast;

void Analyzer::visitExpressionStatement(ExpressionStatement* node) {
    visit(node->expression.get());
}

void Analyzer::visitVariableDeclaration(VariableDeclaration* node) {
    // Declare first so it's in scope for the initializer (e.g. recursive functions or closures)
    auto type = std::make_shared<Type>(TypeKind::Any);
    declareBindingPattern(node->name.get(), type);

    if (node->initializer) {
        visit(node->initializer.get());
        if (lastType) {
            type = lastType;
            // Update the type in the symbol table
            if (auto id = dynamic_cast<Identifier*>(node->name.get())) {
                symbols.update(id->name, type);
            }
        }
    }
    node->resolvedType = type;
    
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

void Analyzer::visitIfStatement(IfStatement* node) {
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

void Analyzer::visitWhileStatement(WhileStatement* node) {
    visit(node->condition.get());
    visit(node->body.get());
}

void Analyzer::visitForStatement(ForStatement* node) {
    symbols.enterScope();
    if (node->initializer) visit(node->initializer.get());
    if (node->condition) visit(node->condition.get());
    if (node->incrementor) visit(node->incrementor.get());
    visit(node->body.get());
    symbols.exitScope();
}

void Analyzer::visitForOfStatement(ForOfStatement* node) {
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

void Analyzer::visitSwitchStatement(SwitchStatement* node) {
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
