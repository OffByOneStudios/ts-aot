#pragma once
//==============================================================================
// ASTToHIR_Internal.h — PRIVATE shared header for the split ASTToHIR_*.cpp files.
//
// NOT the public interface (that is ASTToHIR.h). This header carries:
//   * the common include set every ASTToHIR translation unit needs, and
//   * the file-local free helper functions that were `static` in the original
//     monolithic ASTToHIR.cpp. They are `inline` here so each split .cpp that
//     needs them gets one merged definition (no ODR violation).
//
// Several helpers are used across category boundaries (e.g. privateStorageKey
// in classes/object-literals/assignments; collectHoistedVarNames in functions +
// the Core `lower` pre-pass), so all six live here rather than in one category
// file. Trivial inline members like setSourceLine()/getOrCreateFileIndex() are
// already inline in ASTToHIR.h and need no duplication.
//==============================================================================

#include "ASTToHIR.h"
#include "../extensions/ExtensionLoader.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace ts::hir {

// Private-member STORAGE key (B-lever): private fields/methods live under
// the hidden internal key "\x01#x" so they never appear as own property
// keys (hasOwnProperty / getOwnPropertyNames — ECMA-262: private names are
// not property keys). The runtime get paths do a hidden-first retry for
// '#'-literal lookups (TsObject.cpp, B2). Apply ONLY where a '#'-leading
// name is a private member by grammar: member-expression assignment
// targets and class field inits — NOT object-literal keys, where "#x" is
// a legitimate string property name.
inline std::string privateStorageKey(const std::string& name) {
    if (!name.empty() && name[0] == '#') {
        return std::string("\x01") + name;
    }
    return name;
}

// Convert ext::TypeReference to HIR type.
inline std::shared_ptr<HIRType> extTypeRefToHIR(const ext::TypeReference& typeRef) {
    const auto& name = typeRef.name;
    if (name == "string") return HIRType::makeString();
    if (name == "number" || name == "int" || name == "i64") return HIRType::makeInt64();
    if (name == "double" || name == "f64" || name == "float") return HIRType::makeFloat64();
    if (name == "boolean" || name == "bool") return HIRType::makeBool();
    if (name == "void") return HIRType::makeVoid();
    if (name == "Array") return HIRType::makeArray(HIRType::makeAny());
    if (name == "Map") return HIRType::makeMap();
    if (name == "Set") return HIRType::makeSet();
    // Check if this is a known extension class type
    if (ext::ExtensionRegistry::instance().isExtensionType(name)) {
        return HIRType::makeClass(name, 0);
    }
    // For unknown types, use Any (the LoweringRegistry handles LLVM types)
    return HIRType::makeAny();
}

// Scan constructor body for `this.propName = expr` assignments and record them
// in the class shape (conservative: only top-level ExpressionStatements).
inline void scanConstructorBodyForProperties(
    const std::vector<ast::StmtPtr>& body,
    std::shared_ptr<HIRShape>& shape,
    uint32_t& propertyOffset)
{
    for (auto& stmtPtr : body) {
        // Only scan top-level ExpressionStatements (conservative: skip if/else/loops)
        auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmtPtr.get());
        if (!exprStmt || !exprStmt->expression) continue;

        auto* assign = dynamic_cast<ast::AssignmentExpression*>(exprStmt->expression.get());
        if (!assign || !assign->left) continue;

        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(assign->left.get());
        if (!propAccess || !propAccess->expression) continue;

        auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (!thisIdent || thisIdent->name != "this") continue;

        const std::string& propName = propAccess->name;
        if (propName.empty()) continue;

        // Skip if already in shape (from PropertyDefinition or base class)
        if (shape->propertyOffsets.count(propName)) continue;

        shape->propertyOffsets[propName] = propertyOffset;
        shape->propertyTypes[propName] = HIRType::makeAny();
        propertyOffset++;
    }
}

// ECMA-262 §14.3.2: collect all `var` declarations and `function`
// declarations reachable from `node`, without crossing a nested
// FunctionDeclaration / FunctionExpression / ArrowFunction boundary.
// Used by visitFunctionDeclaration / spec lowering to pre-declare every
// hoisted name on entry so assignments inside conditional branches bind
// to the same function-scope slot.
inline void collectHoistedVarNames(ast::Node* node, std::vector<std::string>& out) {
    if (!node) return;
    // Stop at nested function bodies — they have their own VariableEnvironment.
    if (dynamic_cast<ast::FunctionExpression*>(node)) return;
    if (dynamic_cast<ast::FunctionDeclaration*>(node)) {
        // Hoist the function name itself if it's a declaration.
        auto* fd = static_cast<ast::FunctionDeclaration*>(node);
        if (!fd->name.empty()) out.push_back(fd->name);
        return;
    }
    if (dynamic_cast<ast::ArrowFunction*>(node)) return;
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(node)) {
        if (vd->varKind == ast::VarKind::Var) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get())) {
                out.push_back(ident->name);
            }
            // (binding patterns left to existing per-statement lowering)
        }
        if (vd->initializer) collectHoistedVarNames(vd->initializer.get(), out);
        return;
    }
    if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& s : block->statements) collectHoistedVarNames(s.get(), out);
        return;
    }
    if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) {
        collectHoistedVarNames(expr->expression.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) {
        collectHoistedVarNames(ret->expression.get(), out);
        return;
    }
    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        collectHoistedVarNames(ifStmt->thenStatement.get(), out);
        collectHoistedVarNames(ifStmt->elseStatement.get(), out);
        return;
    }
    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        collectHoistedVarNames(whileStmt->body.get(), out);
        return;
    }
    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        collectHoistedVarNames(forStmt->initializer.get(), out);
        collectHoistedVarNames(forStmt->body.get(), out);
        return;
    }
    if (auto* forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        collectHoistedVarNames(forOf->initializer.get(), out);
        collectHoistedVarNames(forOf->body.get(), out);
        return;
    }
    if (auto* forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        collectHoistedVarNames(forIn->initializer.get(), out);
        collectHoistedVarNames(forIn->body.get(), out);
        return;
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                for (auto& s : cc->statements) collectHoistedVarNames(s.get(), out);
            }
            if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                for (auto& s : dc->statements) collectHoistedVarNames(s.get(), out);
            }
        }
        return;
    }
    if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
        for (auto& s : tryStmt->tryBlock) collectHoistedVarNames(s.get(), out);
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) collectHoistedVarNames(s.get(), out);
        }
        for (auto& s : tryStmt->finallyBlock) collectHoistedVarNames(s.get(), out);
        return;
    }
    if (auto* labeled = dynamic_cast<ast::LabeledStatement*>(node)) {
        collectHoistedVarNames(labeled->statement.get(), out);
        return;
    }
    // Expressions that may contain statements — none in JS, but recurse into
    // nested expressions that have child statements anyway. Most expression
    // forms can't introduce hoisted vars at this level.
}

// Check if a function body uses the 'arguments' identifier.
// Does NOT recurse into nested FunctionDeclaration/FunctionExpression (they have own arguments).
// DOES recurse into ArrowFunction (arrow functions inherit outer arguments).
inline bool containsArgumentsIdentifier(ast::Node* node);

// True when any parameter INITIALIZER references `arguments` — ES 10.2.11
// creates the arguments object BEFORE IteratorBindingInitialization, so
// `f(x = arguments[2])` must bind it even when the BODY never mentions it.
inline bool paramsReferenceArguments(const std::vector<std::unique_ptr<ast::Parameter>>& params) {
    for (auto& prm : params)
        if (prm && prm->initializer && containsArgumentsIdentifier(prm->initializer.get()))
            return true;
    return false;
}

inline bool containsArgumentsIdentifier(ast::Node* node) {
    if (!node) return false;
    // Check if this is an Identifier named "arguments"
    if (auto* ident = dynamic_cast<ast::Identifier*>(node)) {
        return ident->name == "arguments";
    }
    // Do NOT recurse into FunctionExpression or FunctionDeclaration - they have their own arguments
    if (dynamic_cast<ast::FunctionExpression*>(node)) return false;
    if (dynamic_cast<ast::FunctionDeclaration*>(node)) return false;
    // DO recurse into ArrowFunction (arrow functions don't have own arguments)
    if (auto* arrow = dynamic_cast<ast::ArrowFunction*>(node)) {
        return containsArgumentsIdentifier(arrow->body.get());
    }
    // Statements
    if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& s : block->statements) if (containsArgumentsIdentifier(s.get())) return true;
        return false;
    }
    if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) {
        return containsArgumentsIdentifier(expr->expression.get());
    }
    if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) {
        return containsArgumentsIdentifier(ret->expression.get());
    }
    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        return containsArgumentsIdentifier(ifStmt->condition.get()) ||
               containsArgumentsIdentifier(ifStmt->thenStatement.get()) ||
               containsArgumentsIdentifier(ifStmt->elseStatement.get());
    }
    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        return containsArgumentsIdentifier(whileStmt->condition.get()) ||
               containsArgumentsIdentifier(whileStmt->body.get());
    }
    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        return containsArgumentsIdentifier(forStmt->initializer.get()) ||
               containsArgumentsIdentifier(forStmt->condition.get()) ||
               containsArgumentsIdentifier(forStmt->incrementor.get()) ||
               containsArgumentsIdentifier(forStmt->body.get());
    }
    if (auto* forOf = dynamic_cast<ast::ForOfStatement*>(node)) {
        return containsArgumentsIdentifier(forOf->expression.get()) ||
               containsArgumentsIdentifier(forOf->body.get());
    }
    if (auto* forIn = dynamic_cast<ast::ForInStatement*>(node)) {
        return containsArgumentsIdentifier(forIn->expression.get()) ||
               containsArgumentsIdentifier(forIn->body.get());
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
        if (containsArgumentsIdentifier(sw->expression.get())) return true;
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                if (containsArgumentsIdentifier(cc->expression.get())) return true;
                for (auto& s : cc->statements) if (containsArgumentsIdentifier(s.get())) return true;
            }
            if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                for (auto& s : dc->statements) if (containsArgumentsIdentifier(s.get())) return true;
            }
        }
        return false;
    }
    if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
        for (auto& s : tryStmt->tryBlock) if (containsArgumentsIdentifier(s.get())) return true;
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) if (containsArgumentsIdentifier(s.get())) return true;
        }
        for (auto& s : tryStmt->finallyBlock) if (containsArgumentsIdentifier(s.get())) return true;
        return false;
    }
    if (auto* throwStmt = dynamic_cast<ast::ThrowStatement*>(node)) {
        return containsArgumentsIdentifier(throwStmt->expression.get());
    }
    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) {
        return containsArgumentsIdentifier(varDecl->initializer.get());
    }
    if (auto* labeled = dynamic_cast<ast::LabeledStatement*>(node)) {
        return containsArgumentsIdentifier(labeled->statement.get());
    }
    // Expressions
    if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
        if (containsArgumentsIdentifier(call->callee.get())) return true;
        for (auto& arg : call->arguments) if (containsArgumentsIdentifier(arg.get())) return true;
        return false;
    }
    if (auto* newExpr = dynamic_cast<ast::NewExpression*>(node)) {
        if (containsArgumentsIdentifier(newExpr->expression.get())) return true;
        for (auto& arg : newExpr->arguments) if (containsArgumentsIdentifier(arg.get())) return true;
        return false;
    }
    if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
        return containsArgumentsIdentifier(bin->left.get()) ||
               containsArgumentsIdentifier(bin->right.get());
    }
    if (auto* assign = dynamic_cast<ast::AssignmentExpression*>(node)) {
        return containsArgumentsIdentifier(assign->left.get()) ||
               containsArgumentsIdentifier(assign->right.get());
    }
    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(node)) {
        return containsArgumentsIdentifier(cond->condition.get()) ||
               containsArgumentsIdentifier(cond->whenTrue.get()) ||
               containsArgumentsIdentifier(cond->whenFalse.get());
    }
    if (auto* prefix = dynamic_cast<ast::PrefixUnaryExpression*>(node)) {
        return containsArgumentsIdentifier(prefix->operand.get());
    }
    if (auto* postfix = dynamic_cast<ast::PostfixUnaryExpression*>(node)) {
        return containsArgumentsIdentifier(postfix->operand.get());
    }
    if (auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node)) {
        return containsArgumentsIdentifier(prop->expression.get());
    }
    if (auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node)) {
        return containsArgumentsIdentifier(elem->expression.get()) ||
               containsArgumentsIdentifier(elem->argumentExpression.get());
    }
    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
        for (auto& e : arr->elements) if (containsArgumentsIdentifier(e.get())) return true;
        return false;
    }
    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
        for (auto& p : obj->properties) {
            if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(p.get())) {
                if (containsArgumentsIdentifier(pa->initializer.get())) return true;
            }
        }
        return false;
    }
    if (auto* tmpl = dynamic_cast<ast::TemplateExpression*>(node)) {
        for (auto& span : tmpl->spans) if (containsArgumentsIdentifier(span.expression.get())) return true;
        return false;
    }
    if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(node)) {
        return containsArgumentsIdentifier(paren->expression.get());
    }
    if (auto* spread = dynamic_cast<ast::SpreadElement*>(node)) {
        return containsArgumentsIdentifier(spread->expression.get());
    }
    if (auto* del = dynamic_cast<ast::DeleteExpression*>(node)) {
        return containsArgumentsIdentifier(del->expression.get());
    }
    if (auto* await_ = dynamic_cast<ast::AwaitExpression*>(node)) {
        return containsArgumentsIdentifier(await_->expression.get());
    }
    if (auto* yield_ = dynamic_cast<ast::YieldExpression*>(node)) {
        return containsArgumentsIdentifier(yield_->expression.get());
    }
    if (auto* asExpr = dynamic_cast<ast::AsExpression*>(node)) {
        return containsArgumentsIdentifier(asExpr->expression.get());
    }
    if (auto* nonNull = dynamic_cast<ast::NonNullExpression*>(node)) {
        return containsArgumentsIdentifier(nonNull->expression.get());
    }
    return false;
}

// Check whether an expression subtree contains a function/arrow closure literal.
inline bool containsClosureExpression(ast::Node* node) {
    if (!node) return false;
    std::string kind = node->getKind();
    if (kind == "FunctionExpression" || kind == "ArrowFunction") return true;
    // Check CallExpression arguments (e.g., setInterval(function() {...}, ...))
    if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
        for (auto& arg : call->arguments) {
            if (containsClosureExpression(arg.get())) return true;
        }
        if (containsClosureExpression(call->callee.get())) return true;
    }
    // Check NewExpression arguments
    if (auto* newExpr = dynamic_cast<ast::NewExpression*>(node)) {
        for (auto& arg : newExpr->arguments) {
            if (containsClosureExpression(arg.get())) return true;
        }
    }
    // Check array literals
    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
        for (auto& elem : arr->elements) {
            if (containsClosureExpression(elem.get())) return true;
        }
    }
    // Check object literals
    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
        for (auto& prop : obj->properties) {
            if (auto* p = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
                if (containsClosureExpression(p->initializer.get())) return true;
            }
        }
    }
    // Check ternary / binary
    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(node)) {
        if (containsClosureExpression(cond->whenTrue.get())) return true;
        if (containsClosureExpression(cond->whenFalse.get())) return true;
    }
    if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
        if (containsClosureExpression(bin->left.get())) return true;
        if (containsClosureExpression(bin->right.get())) return true;
    }
    return false;
}

}  // namespace ts::hir
