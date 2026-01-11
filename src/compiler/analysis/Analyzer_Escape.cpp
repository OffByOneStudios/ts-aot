#include "Analyzer.h"
#include "../ast/AstNodes.h"
#include <map>
#include <set>

namespace ts {

class EscapeVisitor : public ast::Visitor {
public:
    struct VariableState {
        std::set<ast::NewExpression*> potentialObjects;
    };

    std::map<std::string, VariableState> variables;
    std::set<ast::NewExpression*> allNewExpressions;

    void markAsEscaping(ast::Node* node) {
        auto objects = getPotentialObjects(node);
        for (auto obj : objects) {
            obj->escapes = true;
        }
    }

    std::set<ast::NewExpression*> getPotentialObjects(ast::Node* node) {
        if (auto newExpr = dynamic_cast<ast::NewExpression*>(node)) {
            return {newExpr};
        }
        if (auto ident = dynamic_cast<ast::Identifier*>(node)) {
            return variables[ident->name].potentialObjects;
        }
        return {};
    }

    void visitProgram(ast::Program* node) override {
        for (auto& stmt : node->body) {
            stmt->accept(this);
        }
    }

    void visitBlockStatement(ast::BlockStatement* node) override {
        for (auto& stmt : node->statements) {
            stmt->accept(this);
        }
    }

    void visitVariableDeclaration(ast::VariableDeclaration* node) override {
        if (node->initializer) {
            node->initializer->accept(this);
            if (auto ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
                variables[ident->name].potentialObjects = getPotentialObjects(node->initializer.get());
            }
        }
    }

    void visitExpressionStatement(ast::ExpressionStatement* node) override {
        node->expression->accept(this);
    }

    void visitIfStatement(ast::IfStatement* node) override {
        node->condition->accept(this);
        node->thenStatement->accept(this);
        if (node->elseStatement) node->elseStatement->accept(this);
    }

    void visitWhileStatement(ast::WhileStatement* node) override {
        node->condition->accept(this);
        node->body->accept(this);
    }

    void visitForStatement(ast::ForStatement* node) override {
        if (node->initializer) node->initializer->accept(this);
        if (node->condition) node->condition->accept(this);
        if (node->incrementor) node->incrementor->accept(this);
        node->body->accept(this);
    }

    void visitForOfStatement(ast::ForOfStatement* node) override {
        node->expression->accept(this);
        node->body->accept(this);
    }

    void visitForInStatement(ast::ForInStatement* node) override {
        node->expression->accept(this);
        node->body->accept(this);
    }

    void visitReturnStatement(ast::ReturnStatement* node) override {
        if (node->expression) {
            node->expression->accept(this);
            markAsEscaping(node->expression.get());
        }
    }

    void visitAssignmentExpression(ast::AssignmentExpression* node) override {
        node->right->accept(this);
        if (auto ident = dynamic_cast<ast::Identifier*>(node->left.get())) {
            variables[ident->name].potentialObjects = getPotentialObjects(node->right.get());
        } else {
            // Assignment to property or element: mark as escaping
            markAsEscaping(node->right.get());
        }
    }

    void visitCallExpression(ast::CallExpression* node) override {
        node->callee->accept(this);
        for (auto& arg : node->arguments) {
            arg->accept(this);
            markAsEscaping(arg.get());
        }
    }

    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override {
        if (node->expression) node->expression->accept(this);
    }

    void visitNewExpression(ast::NewExpression* node) override {
        node->expression->accept(this);
        allNewExpressions.insert(node);
        node->escapes = false; // Assume it doesn't escape until proven otherwise
        for (auto& arg : node->arguments) {
            arg->accept(this);
            markAsEscaping(arg.get());
        }
    }

    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override {
        node->expression->accept(this);
    }

    void visitElementAccessExpression(ast::ElementAccessExpression* node) override {
        node->expression->accept(this);
        node->argumentExpression->accept(this);
    }

    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override {
        for (auto& elem : node->elements) {
            elem->accept(this);
            markAsEscaping(elem.get());
        }
    }

    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override {
        for (auto& prop : node->properties) {
            if (auto pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                pa->initializer->accept(this);
                markAsEscaping(pa->initializer.get());
            } else if (auto spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
                // Shorthand properties are just identifiers, they escape if the object escapes
            } else if (auto md = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
                // Methods escape
            }
        }
    }

    void visitBinaryExpression(ast::BinaryExpression* node) override {
        node->left->accept(this);
        node->right->accept(this);
    }

    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override {
        node->operand->accept(this);
    }

    void visitDeleteExpression(ast::DeleteExpression* node) override {
        node->expression->accept(this);
    }

    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override {
        node->operand->accept(this);
    }

    void visitConditionalExpression(ast::ConditionalExpression* node) override {
        node->condition->accept(this);
        node->whenTrue->accept(this);
        node->whenFalse->accept(this);
    }

    void visitIdentifier(ast::Identifier* node) override {}
    void visitNumericLiteral(ast::NumericLiteral* node) override {}
    void visitBigIntLiteral(ast::BigIntLiteral* node) override {}
    void visitStringLiteral(ast::StringLiteral* node) override {}
    void visitBooleanLiteral(ast::BooleanLiteral* node) override {}
    void visitNullLiteral(ast::NullLiteral* node) override {}
    void visitUndefinedLiteral(ast::UndefinedLiteral* node) override {}
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) override {}
    void visitSuperExpression(ast::SuperExpression* node) override {}
    void visitTemplateExpression(ast::TemplateExpression* node) override {}
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) override {
        node->tag->accept(this);
        node->templateExpr->accept(this);
    }
    void visitAwaitExpression(ast::AwaitExpression* node) override {
        node->expression->accept(this);
        markAsEscaping(node->expression.get());
    }

    void visitYieldExpression(ast::YieldExpression* node) override {
        if (node->expression) {
            node->expression->accept(this);
            markAsEscaping(node->expression.get());
        }
    }

    void visitFunctionDeclaration(ast::FunctionDeclaration* node) override {
        // For now, assume everything in a function escapes if it's not local
        // We could do intra-procedural analysis here
        for (auto& stmt : node->body) {
            stmt->accept(this);
        }
    }

    void visitClassDeclaration(ast::ClassDeclaration* node) override {}
    void visitClassExpression(ast::ClassExpression* node) override {}
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node) override {}
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) override {}
    void visitEnumDeclaration(ast::EnumDeclaration* node) override {}
    void visitImportDeclaration(ast::ImportDeclaration* node) override {}
    void visitExportDeclaration(ast::ExportDeclaration* node) override {}
    void visitExportAssignment(ast::ExportAssignment* node) override {}
    void visitPropertyAssignment(ast::PropertyAssignment* node) override {
        if (node->nameNode) node->nameNode->accept(this);
        node->initializer->accept(this);
    }
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) override {}
    void visitComputedPropertyName(ast::ComputedPropertyName* node) override {
        node->expression->accept(this);
    }
    void visitMethodDefinition(ast::MethodDefinition* node) override {
        for (auto& stmt : node->body) {
            stmt->accept(this);
        }
    }

    void visitStaticBlock(ast::StaticBlock* node) override {
        for (auto& stmt : node->body) {
            stmt->accept(this);
        }
    }
    void visitArrowFunction(ast::ArrowFunction* node) override {}
    void visitFunctionExpression(ast::FunctionExpression* node) override {}
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node) override {}
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node) override {}
    void visitBindingElement(ast::BindingElement* node) override {}
    void visitSpreadElement(ast::SpreadElement* node) override {}
    void visitOmittedExpression(ast::OmittedExpression* node) override {}
    void visitAsExpression(ast::AsExpression* node) override {
        node->expression->accept(this);
    }

    void visitSwitchStatement(ast::SwitchStatement* node) override {
        node->expression->accept(this);
        for (auto& clause : node->clauses) {
            if (auto c = dynamic_cast<ast::CaseClause*>(clause.get())) {
                if (c->expression) c->expression->accept(this);
                for (auto& stmt : c->statements) stmt->accept(this);
            } else if (auto d = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& stmt : d->statements) stmt->accept(this);
            }
        }
    }

    void visitTryStatement(ast::TryStatement* node) override {
        for (auto& stmt : node->tryBlock) stmt->accept(this);
        if (node->catchClause) {
            for (auto& stmt : node->catchClause->block) stmt->accept(this);
        }
        for (auto& stmt : node->finallyBlock) stmt->accept(this);
    }

    void visitThrowStatement(ast::ThrowStatement* node) override {
        node->expression->accept(this);
        markAsEscaping(node->expression.get());
    }

    void visitBreakStatement(ast::BreakStatement* node) override {}
    void visitContinueStatement(ast::ContinueStatement* node) override {}
};

void Analyzer::performEscapeAnalysis(ast::Program* program) {
    EscapeVisitor visitor;
    program->accept(&visitor);
}

} // namespace ts


