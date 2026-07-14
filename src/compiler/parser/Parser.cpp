#include <charconv>
#include "Parser.h"
#include <stdexcept>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_set>

namespace ts::parser {

// ============================================================================
// Class field initializer early-error walkers (ECMA-262 15.7.1)
// ============================================================================
//
// Per spec a FieldDefinition Initializer must NOT contain:
//   1. ContainsArguments — an IdentifierReference with name "arguments"
//      (skipping nested non-arrow function bodies, which have their own
//      arguments object; arrows do NOT shadow).
//   2. Contains SuperCall — a CallExpression with callee = SuperExpression
//      (skipping nested non-arrow functions and method definitions, which
//      have their own super-binding).
//
// Returned int is one of: 0 = clean, 1 = arguments, 2 = super-call.

namespace {
constexpr int FIELD_INIT_OK = 0;
constexpr int FIELD_INIT_ARGUMENTS = 1;
constexpr int FIELD_INIT_SUPER_CALL = 2;
constexpr int FIELD_INIT_AWAIT = 3;
// When true, the walk ALSO reports a directly-contained AwaitExpression and
// treats arrow functions as boundaries (an async arrow has its own [+Await]).
// Used by the class static block (ECMA-262 15.7.1 ContainsAwait). Single-
// threaded parser, so a file-static toggle is fine.
bool g_walkAwaitMode = false;

int containsArgumentsOrSuperCall(const ast::Node* node) {
    if (!node) return FIELD_INIT_OK;
    if (g_walkAwaitMode && dynamic_cast<const ast::AwaitExpression*>(node))
        return FIELD_INIT_AWAIT;

    // IdentifierReference "arguments"
    if (auto* ident = dynamic_cast<const ast::Identifier*>(node)) {
        if (ident->name == "arguments") return FIELD_INIT_ARGUMENTS;
        return FIELD_INIT_OK;
    }
    // SuperCall: CallExpression where callee is SuperExpression
    if (auto* call = dynamic_cast<const ast::CallExpression*>(node)) {
        if (dynamic_cast<const ast::SuperExpression*>(call->callee.get())) {
            return FIELD_INIT_SUPER_CALL;
        }
        if (int r = containsArgumentsOrSuperCall(call->callee.get())) return r;
        for (auto& a : call->arguments) {
            if (int r = containsArgumentsOrSuperCall(a.get())) return r;
        }
        return FIELD_INIT_OK;
    }

    // A ComputedPropertyName wraps its key expression (`[expr]`).
    if (auto* cpn = dynamic_cast<const ast::ComputedPropertyName*>(node)) {
        return containsArgumentsOrSuperCall(cpn->expression.get());
    }

    // Boundary nodes — own arguments / super binding, do NOT recurse into their
    // bodies. BUT a member's COMPUTED KEY (`[expr]`) is evaluated in the
    // ENCLOSING scope, so `arguments`/super in a computed key still counts
    // (`static { (class { [arguments]() {} }); }` is a SyntaxError). Recurse into
    // computed keys (name == "[computed]" → expression in nameNode) only.
    if (dynamic_cast<const ast::FunctionExpression*>(node)) return FIELD_INIT_OK;
    if (dynamic_cast<const ast::FunctionDeclaration*>(node)) return FIELD_INIT_OK;
    if (auto* md = dynamic_cast<const ast::MethodDefinition*>(node)) {
        if (md->name == "[computed]" && md->nameNode)
            return containsArgumentsOrSuperCall(md->nameNode.get());
        return FIELD_INIT_OK;
    }
    auto scanClassComputedKeys = [](const std::vector<ast::NodePtr>& members) -> int {
        for (auto& m : members) {
            if (auto* mm = dynamic_cast<const ast::MethodDefinition*>(m.get())) {
                if (mm->name == "[computed]" && mm->nameNode)
                    if (int r = containsArgumentsOrSuperCall(mm->nameNode.get())) return r;
            } else if (auto* pp = dynamic_cast<const ast::PropertyDefinition*>(m.get())) {
                if (pp->name == "[computed]" && pp->nameNode)
                    if (int r = containsArgumentsOrSuperCall(pp->nameNode.get())) return r;
            }
        }
        return FIELD_INIT_OK;
    };
    if (auto* cd = dynamic_cast<const ast::ClassDeclaration*>(node))
        return scanClassComputedKeys(cd->members);
    if (auto* ce = dynamic_cast<const ast::ClassExpression*>(node))
        return scanClassComputedKeys(ce->members);

    // Arrow functions: recurse for arguments/super (arrows have none of their
    // own), but in await mode an arrow is a boundary — an async arrow owns its
    // [+Await], so `static { (async () => await x)() }` is allowed.
    if (auto* arrow = dynamic_cast<const ast::ArrowFunction*>(node)) {
        if (g_walkAwaitMode) return FIELD_INIT_OK;
        return containsArgumentsOrSuperCall(arrow->body.get());
    }

    // Statement containers
    if (auto* block = dynamic_cast<const ast::BlockStatement*>(node)) {
        for (auto& s : block->statements) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
        return FIELD_INIT_OK;
    }
    if (auto* expr = dynamic_cast<const ast::ExpressionStatement*>(node)) {
        return containsArgumentsOrSuperCall(expr->expression.get());
    }
    if (auto* ret = dynamic_cast<const ast::ReturnStatement*>(node)) {
        return containsArgumentsOrSuperCall(ret->expression.get());
    }
    if (auto* ifStmt = dynamic_cast<const ast::IfStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(ifStmt->condition.get())) return r;
        if (int r = containsArgumentsOrSuperCall(ifStmt->thenStatement.get())) return r;
        return containsArgumentsOrSuperCall(ifStmt->elseStatement.get());
    }
    if (auto* w = dynamic_cast<const ast::WhileStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(w->condition.get())) return r;
        return containsArgumentsOrSuperCall(w->body.get());
    }
    if (auto* f = dynamic_cast<const ast::ForStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(f->initializer.get())) return r;
        if (int r = containsArgumentsOrSuperCall(f->condition.get())) return r;
        if (int r = containsArgumentsOrSuperCall(f->incrementor.get())) return r;
        return containsArgumentsOrSuperCall(f->body.get());
    }
    if (auto* fo = dynamic_cast<const ast::ForOfStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(fo->expression.get())) return r;
        return containsArgumentsOrSuperCall(fo->body.get());
    }
    if (auto* fi = dynamic_cast<const ast::ForInStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(fi->expression.get())) return r;
        return containsArgumentsOrSuperCall(fi->body.get());
    }
    if (auto* sw = dynamic_cast<const ast::SwitchStatement*>(node)) {
        if (int r = containsArgumentsOrSuperCall(sw->expression.get())) return r;
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<const ast::CaseClause*>(cl.get())) {
                if (int r = containsArgumentsOrSuperCall(cc->expression.get())) return r;
                for (auto& s : cc->statements) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
            }
            if (auto* dc = dynamic_cast<const ast::DefaultClause*>(cl.get())) {
                for (auto& s : dc->statements) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
            }
        }
        return FIELD_INIT_OK;
    }
    if (auto* tryStmt = dynamic_cast<const ast::TryStatement*>(node)) {
        for (auto& s : tryStmt->tryBlock) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
        }
        for (auto& s : tryStmt->finallyBlock) if (int r = containsArgumentsOrSuperCall(s.get())) return r;
        return FIELD_INIT_OK;
    }
    if (auto* th = dynamic_cast<const ast::ThrowStatement*>(node)) {
        return containsArgumentsOrSuperCall(th->expression.get());
    }
    if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(node)) {
        return containsArgumentsOrSuperCall(vd->initializer.get());
    }
    if (auto* lab = dynamic_cast<const ast::LabeledStatement*>(node)) {
        return containsArgumentsOrSuperCall(lab->statement.get());
    }

    // Expression containers
    if (auto* ne = dynamic_cast<const ast::NewExpression*>(node)) {
        if (int r = containsArgumentsOrSuperCall(ne->expression.get())) return r;
        for (auto& a : ne->arguments) if (int r = containsArgumentsOrSuperCall(a.get())) return r;
        return FIELD_INIT_OK;
    }
    if (auto* bin = dynamic_cast<const ast::BinaryExpression*>(node)) {
        if (int r = containsArgumentsOrSuperCall(bin->left.get())) return r;
        return containsArgumentsOrSuperCall(bin->right.get());
    }
    if (auto* as = dynamic_cast<const ast::AssignmentExpression*>(node)) {
        if (int r = containsArgumentsOrSuperCall(as->left.get())) return r;
        return containsArgumentsOrSuperCall(as->right.get());
    }
    if (auto* c = dynamic_cast<const ast::ConditionalExpression*>(node)) {
        if (int r = containsArgumentsOrSuperCall(c->condition.get())) return r;
        if (int r = containsArgumentsOrSuperCall(c->whenTrue.get())) return r;
        return containsArgumentsOrSuperCall(c->whenFalse.get());
    }
    if (auto* p = dynamic_cast<const ast::PrefixUnaryExpression*>(node)) {
        return containsArgumentsOrSuperCall(p->operand.get());
    }
    if (auto* p = dynamic_cast<const ast::PostfixUnaryExpression*>(node)) {
        return containsArgumentsOrSuperCall(p->operand.get());
    }
    if (auto* pa = dynamic_cast<const ast::PropertyAccessExpression*>(node)) {
        return containsArgumentsOrSuperCall(pa->expression.get());
    }
    if (auto* ea = dynamic_cast<const ast::ElementAccessExpression*>(node)) {
        if (int r = containsArgumentsOrSuperCall(ea->expression.get())) return r;
        return containsArgumentsOrSuperCall(ea->argumentExpression.get());
    }
    if (auto* arr = dynamic_cast<const ast::ArrayLiteralExpression*>(node)) {
        for (auto& e : arr->elements) if (int r = containsArgumentsOrSuperCall(e.get())) return r;
        return FIELD_INIT_OK;
    }
    if (auto* obj = dynamic_cast<const ast::ObjectLiteralExpression*>(node)) {
        for (auto& p : obj->properties) {
            if (auto* pa = dynamic_cast<const ast::PropertyAssignment*>(p.get())) {
                if (int r = containsArgumentsOrSuperCall(pa->initializer.get())) return r;
            }
        }
        return FIELD_INIT_OK;
    }
    if (auto* tmpl = dynamic_cast<const ast::TemplateExpression*>(node)) {
        for (auto& span : tmpl->spans) {
            if (int r = containsArgumentsOrSuperCall(span.expression.get())) return r;
        }
        return FIELD_INIT_OK;
    }
    if (auto* paren = dynamic_cast<const ast::ParenthesizedExpression*>(node)) {
        return containsArgumentsOrSuperCall(paren->expression.get());
    }
    if (auto* sp = dynamic_cast<const ast::SpreadElement*>(node)) {
        return containsArgumentsOrSuperCall(sp->expression.get());
    }
    if (auto* del = dynamic_cast<const ast::DeleteExpression*>(node)) {
        return containsArgumentsOrSuperCall(del->expression.get());
    }
    if (auto* aw = dynamic_cast<const ast::AwaitExpression*>(node)) {
        return containsArgumentsOrSuperCall(aw->expression.get());
    }
    if (auto* y = dynamic_cast<const ast::YieldExpression*>(node)) {
        return containsArgumentsOrSuperCall(y->expression.get());
    }
    if (auto* asx = dynamic_cast<const ast::AsExpression*>(node)) {
        return containsArgumentsOrSuperCall(asx->expression.get());
    }
    if (auto* nn = dynamic_cast<const ast::NonNullExpression*>(node)) {
        return containsArgumentsOrSuperCall(nn->expression.get());
    }
    return FIELD_INIT_OK;
}
}  // namespace

// ============================================================================
// Strict-mode helpers
// ============================================================================

bool Parser::processPrologueDirective(const ast::StmtPtr& stmt) {
    auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmt.get());
    if (!exprStmt) return false;
    auto* strLit = dynamic_cast<ast::StringLiteral*>(exprStmt->expression.get());
    if (!strLit) return false;
    if (strLit->value == "use fast") {
        // "use fast" opts this file into the enforced high-performance subset
        // (docs/design/use-fast.md) and implies strict mode.
        sawUseFastDirective_ = true;
        strictMode_ = true;
        sawUseStrictDirective_ = true;
        for (auto& p : pendingPrologueStrings_) {
            Lexer::validateLegacyOctalEscapes(
                p.raw, /*isStrict=*/true, /*isTemplate=*/false,
                p.line, p.column);
        }
        pendingPrologueStrings_.clear();
        return true;
    }
    if (strLit->value == "use strict") {
        strictMode_ = true;
        sawUseStrictDirective_ = true;
        // ECMA-262 11.2.1: the directive makes the WHOLE body strict,
        // including prologue strings that PRECEDE it — re-validate their
        // raw text now. (Each prologue loop clears this list on entry.)
        for (auto& p : pendingPrologueStrings_) {
            Lexer::validateLegacyOctalEscapes(
                p.raw, /*isStrict=*/true, /*isTemplate=*/false,
                p.line, p.column);
        }
        pendingPrologueStrings_.clear();
    } else if (!strLit->raw.empty()) {
        pendingPrologueStrings_.push_back(
            {strLit->raw, strLit->line, strLit->column});
    }
    return true;
}

// Per ECMA-262 12.15.5 IsValidSimpleAssignmentTarget. Returns silently
// on valid targets, throws SyntaxError on invalid ones.
void Parser::validateAssignmentTarget(const ast::Node* expr,
                                      bool forCompoundAssign) const {
    if (!expr) return;
    // Unwrap parenthesized / TS type-only wrappers.
    if (auto* paren = dynamic_cast<const ast::ParenthesizedExpression*>(expr)) {
        validateAssignmentTarget(paren->expression.get(), forCompoundAssign);
        return;
    }
    if (auto* asExpr = dynamic_cast<const ast::AsExpression*>(expr)) {
        validateAssignmentTarget(asExpr->expression.get(), forCompoundAssign);
        return;
    }
    if (auto* nn = dynamic_cast<const ast::NonNullExpression*>(expr)) {
        validateAssignmentTarget(nn->expression.get(), forCompoundAssign);
        return;
    }

    // Identifier — almost always a valid IdentifierReference. The
    // spec exceptions:
    //   - `this` is a ThisExpression in spec terms; our parser
    //     represents it as Identifier with name "this"
    //   - In strict mode, `eval` and `arguments` are invalid targets
    if (auto* ident = dynamic_cast<const ast::Identifier*>(expr)) {
        if (ident->name == "this") {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'this' is not a valid assignment target",
                expr->line, expr->column));
        }
        if (strictMode_ && (ident->name == "eval" || ident->name == "arguments")) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: '{}' is not a valid assignment target in strict mode",
                expr->line, expr->column, ident->name));
        }
        return;
    }

    // Property / member access — valid LHS unless ANY link in the
    // chain is optional (`?.`). ECMA-262 13.5.1: OptionalChain cannot
    // be the target of an assignment or update expression.
    std::function<bool(const ast::Node*)> hasOptionalChain;
    hasOptionalChain = [&](const ast::Node* n) -> bool {
        if (!n) return false;
        if (auto* p = dynamic_cast<const ast::PropertyAccessExpression*>(n)) {
            return p->isOptional || hasOptionalChain(p->expression.get());
        }
        if (auto* e = dynamic_cast<const ast::ElementAccessExpression*>(n)) {
            return e->isOptional || hasOptionalChain(e->expression.get());
        }
        if (auto* c = dynamic_cast<const ast::CallExpression*>(n)) {
            return c->isOptional || hasOptionalChain(c->callee.get());
        }
        return false;
    };
    if (auto* p = dynamic_cast<const ast::PropertyAccessExpression*>(expr)) {
        if (hasOptionalChain(p)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: optional chain ('?.') cannot be the "
                "target of an assignment or update expression",
                expr->line, expr->column));
        }
        // ECMA-262 13.3.13 ImportMeta and 13.3.12.2 NewTarget both have
        // AssignmentTargetType = invalid. `import.meta = 1` and
        // `new.target = 1` are SyntaxErrors. Detect via the AST shape
        // produced by parsePrimaryExpression's KW_import / KW_new branches:
        // an Identifier "import" or "new" left side with property name
        // "meta" / "target" respectively.
        if (auto* id = dynamic_cast<const ast::Identifier*>(p->expression.get())) {
            if ((id->name == "import" && p->name == "meta") ||
                (id->name == "new" && p->name == "target")) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}.{}' is not a valid "
                    "assignment target",
                    expr->line, expr->column, id->name, p->name));
            }
        }
        return;
    }
    if (auto* e = dynamic_cast<const ast::ElementAccessExpression*>(expr)) {
        if (hasOptionalChain(e)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: optional chain ('?.') cannot be the "
                "target of an assignment or update expression",
                expr->line, expr->column));
        }
        return;
    }

    // Object/Array literals — valid only as destructuring targets,
    // and only for plain `=` (not `+=` etc.).
    if (auto* arr = dynamic_cast<const ast::ArrayLiteralExpression*>(expr)) {
        // ECMA-262 13.15.1: a directly-parenthesized array/object literal has
        // AssignmentTargetType ~invalid~ — `([a]) = x` / `({}) = 1` are
        // SyntaxErrors (the parens prevent refinement to a destructuring
        // pattern). `[a] = x` is fine (not parenthesized); `(x) = 1` is fine
        // (Identifier branch, doesn't reach here).
        if (arr->parenthesized) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: a parenthesized destructuring pattern is "
                "not a valid assignment target", expr->line, expr->column));
        }
        // Refinement to an AssignmentPattern legitimizes any CoverInitializedName
        // inside this (possibly nested) target — clear the deferred error.
        coverInitErrorLine_ = -1;
        if (forCompoundAssign) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: destructuring pattern not allowed with compound assignment",
                expr->line, expr->column));
        }
        // ECMA-262 13.15.5.1: AssignmentRestElement must be the final
        // element. Reject (a) `[...x ,] = []` (trailing comma after rest),
        // and (b) `[...x, y] = arr` (element after rest) — both legal as
        // array literals, both early errors as AssignmentPattern.
        if (arr->restHadTrailingComma) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: rest element may not have a trailing "
                "comma in an assignment pattern",
                expr->line, expr->column));
        }
        for (size_t i = 0; i + 1 < arr->elements.size(); ++i) {
            if (dynamic_cast<const ast::SpreadElement*>(arr->elements[i].get())) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: rest element must be the last "
                    "element in an array assignment pattern",
                    expr->line, expr->column));
            }
        }
        // ECMA-262 13.15.5.1 — recursive validation: every element of
        // an ArrayAssignmentPattern is itself a DestructuringAssignmentTarget
        // (or AssignmentRestElement). Recurse into each element so e.g.
        // `[[(x,y)]] = ...` (comma-expression inside nested target) and
        // `[arguments] = []` (strict-reserved as target) error. Skip
        // OmittedExpression (elision holes) and Initializer wrappers
        // (BinaryExpression with op `=` is a DefaultedTarget; recurse into
        // its left).
        for (auto& elem : arr->elements) {
            if (!elem) continue;
            if (dynamic_cast<const ast::OmittedExpression*>(elem.get())) continue;
            const ast::Node* target = elem.get();
            if (auto* spread = dynamic_cast<const ast::SpreadElement*>(elem.get())) {
                target = spread->expression.get();
                if (!target) continue;
                // ECMA-262: AssignmentRestElement is `... DestructuringAssignment-
                // Target` with NO Initializer — `[...x = 1] = arr` (and the same in
                // a for-in/of head) is a SyntaxError.
                if (dynamic_cast<const ast::AssignmentExpression*>(target)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: a rest element may not have an "
                        "initializer in an assignment pattern",
                        expr->line, expr->column));
                }
            }
            // Default value wrapper: `[a = 0]` — the AST stores `a = 0` as
            // AssignmentExpression. Recurse into its left.
            if (auto* assn = dynamic_cast<const ast::AssignmentExpression*>(target)) {
                target = assn->left.get();
                if (!target) continue;
            }
            validateAssignmentTarget(target, forCompoundAssign);
        }
        return;
    }
    if (auto* obj = dynamic_cast<const ast::ObjectLiteralExpression*>(expr)) {
        // See array branch: a parenthesized object literal cannot be refined to
        // an ObjectAssignmentPattern — `({}) = 1` / `({a}) = x` are SyntaxErrors.
        // (`({a} = x)` is fine: there the parens wrap the assignment, so the
        // object node itself is not marked parenthesized.)
        if (obj->parenthesized) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: a parenthesized destructuring pattern is "
                "not a valid assignment target", expr->line, expr->column));
        }
        // Refinement clears any deferred CoverInitializedName error (see array).
        coverInitErrorLine_ = -1;
        if (forCompoundAssign) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: destructuring pattern not allowed with compound assignment",
                expr->line, expr->column));
        }
        // ECMA-262 13.15.5.1: AssignmentRestProperty must be the final
        // property and have NO trailing comma. Reject any SpreadElement
        // that isn't the last property (`{...rest, b} = obj` etc.).
        // Object literal stores spread as SpreadElement in properties[].
        for (size_t i = 0; i + 1 < obj->properties.size(); ++i) {
            if (dynamic_cast<const ast::SpreadElement*>(obj->properties[i].get())) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: rest property must be the last "
                    "property in an object assignment pattern",
                    expr->line, expr->column));
            }
        }
        // Recursive validation of each property's target. Reject method/
        // getter/setter members (only PropertyAssignment / shorthand / spread
        // are valid in DestructuringAssignmentPattern).
        for (auto& prop : obj->properties) {
            if (!prop) continue;
            if (auto* method = dynamic_cast<const ast::MethodDefinition*>(prop.get())) {
                const char* kind = method->isGetter ? "getter"
                                 : method->isSetter ? "setter" : "method";
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: object literal {} is not a valid "
                    "destructuring target",
                    method->line, method->column, kind));
            }
            if (auto* spread = dynamic_cast<const ast::SpreadElement*>(prop.get())) {
                if (spread->expression) {
                    validateAssignmentTarget(spread->expression.get(),
                                              forCompoundAssign);
                }
                continue;
            }
            if (auto* pa = dynamic_cast<const ast::PropertyAssignment*>(prop.get())) {
                if (pa->initializer) {
                    const ast::Node* target = pa->initializer.get();
                    // Default-value wrapper `{x: a = 0}` — recurse into LHS.
                    if (auto* assn = dynamic_cast<const ast::AssignmentExpression*>(target)) {
                        target = assn->left.get();
                        if (!target) continue;
                    }
                    validateAssignmentTarget(target, forCompoundAssign);
                }
            }
            // Shorthand `{ eval }` / `{ arguments }` — the property name IS
            // the assignment target, so strict-mode reserved-name and other
            // identifier rules apply (the CoverInitializedName `{ a = 0 }`
            // form carries a default in initializer, which is irrelevant to
            // target validity). ECMA-262 13.15.5.1.
            if (auto* sp = dynamic_cast<const ast::ShorthandPropertyAssignment*>(prop.get())) {
                if (strictMode_ && (sp->name == "eval" || sp->name == "arguments")) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' is not a valid assignment "
                        "target in strict mode",
                        sp->line, sp->column, sp->name));
                }
            }
        }
        return;
    }

    // Everything else is an invalid assignment target. ECMA-262 13.15.1 gives a
    // CallExpression AssignmentTargetType = invalid, so `f() = 1` / `f() += 1` is
    // a SyntaxError — both V8 and tsc (TS2364) reject it. Likewise literals,
    // arrow / function / class expressions, binary / conditional / unary / new /
    // await / yield / spread / template / super. (A member access ON a call,
    // `f().x = 1`, is a PropertyAccessExpression and was accepted above.)
    throw std::runtime_error(fmt::format(
        "{}:{}: SyntaxError: invalid assignment target",
        expr->line, expr->column));
}

bool Parser::isParameterListSimple(
    const std::vector<std::unique_ptr<ast::Parameter>>& params) const {
    for (const auto& p : params) {
        if (!p) continue;
        if (p->isRest || p->initializer || p->isOptional) return false;
        if (!dynamic_cast<ast::Identifier*>(p->name.get())) return false;
    }
    return true;
}

// ============================================================================
// Numeric literal property-name canonicalization (ECMA-262 13.2.5.1)
// ============================================================================

std::string Parser::canonicalNumericPropertyName(std::string_view lexeme) {
    // Parse the lexeme as a double using the same logic as
    // Parser_Expressions.cpp NumericLiteral handling, then format the
    // result to its canonical Number::toString form. Property keys
    // built from numeric literals must use this canonical form so
    // `class C { get 0b10() {} }` registers the getter under "2",
    // matching test code that probes `C.prototype['2']`.
    std::string text(lexeme);
    // Strip numeric separator underscores (already lexer-validated).
    std::string clean;
    clean.reserve(text.size());
    for (char c : text) if (c != '_') clean += c;
    double value = 0.0;
    auto safeStod = [](const std::string& s) -> double {
        try { return std::stod(s); }
        catch (const std::out_of_range&) {
            for (char c : s) {
                if (c == '.') continue;
                if (c == 'e' || c == 'E') break;
                if (c >= '1' && c <= '9') return std::numeric_limits<double>::infinity();
            }
            return 0.0;
        }
        catch (...) { return 0.0; }
    };
    auto safeStoull = [](const std::string& s, int base) -> double {
        try { return static_cast<double>(std::stoull(s, nullptr, base)); }
        catch (...) { return std::numeric_limits<double>::infinity(); }
    };
    if (clean.size() > 1 && clean[0] == '0') {
        if (clean[1] == 'x' || clean[1] == 'X') value = safeStoull(clean, 16);
        else if (clean[1] == 'o' || clean[1] == 'O') value = safeStoull(clean.substr(2), 8);
        else if (clean[1] == 'b' || clean[1] == 'B') value = safeStoull(clean.substr(2), 2);
        else value = safeStod(clean);
    } else {
        value = safeStod(clean);
    }
    // Number::toString canonical form: integer doubles in (-2^53, 2^53)
    // print as their integer representation, otherwise %.17g for full
    // precision. NaN/±Infinity should be unreachable here (lexer
    // doesn't produce them) but guard for safety.
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0.0) return "0";
    if (value == std::floor(value) && std::abs(value) < 1e21) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)value);
        return std::string(buf);
    }
    {
        // Non-integer: ECMA-262 Number::toString uses the SHORTEST decimal that
        // round-trips, not full %.17g precision (so `0.1`→"0.1", not
        // "0.10000000000000001"). std::to_chars gives the shortest form; then
        // normalize the exponent to JS style ("1e-07" → "1e-7").
        char buf[40];
        auto res = std::to_chars(buf, buf + sizeof(buf), value);
        std::string s(buf, res.ptr);
        auto epos = s.find('e');
        if (epos != std::string::npos) {
            std::string mant = s.substr(0, epos);
            std::string exp = s.substr(epos + 1);
            char sign = '+';
            size_t i = 0;
            if (!exp.empty() && (exp[0] == '+' || exp[0] == '-')) { sign = exp[0]; i = 1; }
            while (i + 1 < exp.size() && exp[i] == '0') i++;
            s = mant + "e" + sign + exp.substr(i);
        }
        return s;
    }
}

// ============================================================================
// Public API
// ============================================================================

std::unique_ptr<ast::Program> Parser::parse(const std::string& source,
                                              const std::string& fileName) {
    source_ = &source;
    fileName_ = fileName;
    lexer_ = std::make_unique<Lexer>(source, fileName);
    // Per-file goal: TS_SCRIPT_GOAL applies to the entry file; imported
    // modules force the module goal (see setForceModuleGoal). Must be set
    // BEFORE the first nextToken (HTML-comment lexing is goal-dependent).
    if (forceModuleGoal_) lexer_->moduleGoal_ = true;
    current_ = lexer_->nextToken();
    previous_ = current_;

    scriptGoal_ = forceModuleGoal_
        ? false
        : (std::getenv("TS_SCRIPT_GOAL") != nullptr);
    // Module goal: top-level code is an [+Await] context (ES2022 top-level
    // await) — `await expr` parses as an AwaitExpression at ANY statement
    // depth outside function bodies (blocks, loops, switch...). Function
    // boundaries save/restore inAsync_ as before. Previously only some
    // direct-statement forms happened to work; `{ await 1; }` at module top
    // level was "Expected ';'" (~150 module-code/top-level-await tests).
    inAsync_ = !scriptGoal_;

    auto program = std::make_unique<ast::Program>();
    program->sourceFile = fileName;

    // Parse triple-slash references from comments at the start
    program->tripleSlashReferences = parseTripleSlashReferences();

    // Directive prologue handling. ECMA-262: leading
    // ExpressionStatements wrapping a single string literal form a
    // directive prologue. If `"use strict"` appears among them, the
    // body is strict from then on. Prologue strings themselves are
    // parsed in the outer (typically sloppy) mode; a "use strict" among
    // them re-validates the earlier ones via pendingPrologueStrings_.
    pendingPrologueStrings_.clear();
    bool inPrologue = true;
    // ECMA-262 16.1.1 / 16.2.1: the top level of a Script/Module is a
    // declaration scope — duplicate LexicallyDeclaredNames (and lex-vs-var
    // conflicts) are early errors there too. Without a pushed scope,
    // declareLexicalName is a no-op, so `class A {} class A {}` and
    // `let a; class a {}` slip through (block/function scopes already catch them).
    pushLexicalScope();
    while (!isAtEnd()) {
        atTopLevel_ = true;  // consumed by parseDeclarationOrStatement
        auto stmt = parseDeclarationOrStatement();
        if (!stmt) continue;
        if (inPrologue) {
            if (processPrologueDirective(stmt)) {
                // Stamp file-level strictness on the Program node -- the
                // Monomorphizer moves body statements into function specs
                // before ASTToHIR runs, so the prologue can't be re-scanned.
                if (strictMode_) program->isStrict = true;
                if (sawUseFastDirective_) program->isFast = true;
            } else {
                inPrologue = false;
            }
        }
        program->body.push_back(std::move(stmt));
    }
    // ES 16.2.1 module early error: every local referenced by a from-less
    // `export { ... }` clause must be a module-level declared name. Checked
    // against the program scope BEFORE it pops.
    if (!scriptGoal_ && !moduleExportLocalRefs_.empty() && !lexicalScopes_.empty()) {
        const auto& top = lexicalScopes_.front().names;
        for (const auto& [local, line] : moduleExportLocalRefs_) {
            if (!top.count(local) && !moduleImportBindings_.count(local)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: export of undeclared name '{}'",
                    fileName_, line, local));
            }
        }
    }
    popLexicalScope();

    // EVAL-001 §11: stamp the lexer's eval-identifier taint on the Program
    // (like isStrict, the Monomorphizer restructures the body before
    // ASTToHIR, so it must ride on the Program node).
    if (lexer_ && lexer_->sawEvalIdent_) program->referencesEval = true;

    return program;
}

// ============================================================================
// Token manipulation
// ============================================================================

Token Parser::advance() {
    previous_ = current_;

    // Set regex-allowed based on the token we just consumed.
    // After identifiers, literals, and closing brackets: '/' is division.
    // After everything else (operators, opening brackets, keywords): '/' starts a regex.
    switch (previous_.kind) {
        case TokenKind::Identifier:
        case TokenKind::NumericLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::BigIntLiteral:
        case TokenKind::RegularExpressionLiteral:
        case TokenKind::NoSubstitutionTemplate:
        case TokenKind::TemplateTail:
        case TokenKind::KW_true:
        case TokenKind::KW_false:
        case TokenKind::KW_null:
        case TokenKind::KW_undefined:
        case TokenKind::KW_this:
        case TokenKind::KW_super:
        // Contextual keywords that are valid IdentifierReferences must also
        // disable regex-after, otherwise `instance/of/g` is mis-scanned as
        // an identifier followed by a regex literal `/of/g`.
        case TokenKind::KW_let:
        case TokenKind::KW_of:
        case TokenKind::KW_as:
        case TokenKind::KW_is:
        case TokenKind::KW_get:
        case TokenKind::KW_set:
        case TokenKind::KW_from:
        case TokenKind::KW_async:
        case TokenKind::KW_await:
        case TokenKind::KW_yield:
        case TokenKind::KW_type:
        case TokenKind::KW_namespace:
        case TokenKind::KW_module:
        case TokenKind::KW_declare:
        case TokenKind::KW_abstract:
        case TokenKind::KW_readonly:
        case TokenKind::KW_interface:
        case TokenKind::KW_implements:
        case TokenKind::KW_public:
        case TokenKind::KW_private:
        case TokenKind::KW_protected:
        case TokenKind::KW_static:
        case TokenKind::KW_constructor:
        case TokenKind::KW_keyof:
        case TokenKind::KW_infer:
        case TokenKind::KW_asserts:
        case TokenKind::KW_satisfies:
        case TokenKind::KW_override:
        case TokenKind::KW_out:
        case TokenKind::KW_require:
        case TokenKind::CloseParen:
        case TokenKind::CloseBracket:
        case TokenKind::CloseBrace:
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus:
            lexer_->setRegexAllowed(false);
            break;
        default:
            lexer_->setRegexAllowed(true);
            break;
    }

    if (hasLookahead_) {
        // A peeked token is already lexed and buffered; pop it.
        current_ = lookaheadToken_;
        hasLookahead_ = false;
    } else {
        current_ = lexer_->nextToken();
    }
    return previous_;
}

const Token& Parser::peekAhead() {
    if (!hasLookahead_) {
        // current_ is an Identifier here (the only caller is the `struct`
        // contextual-keyword dispatch), so the lexer must be in division mode
        // (regex disallowed) to scan the following token correctly.
        lexer_->setRegexAllowed(false);
        lookaheadToken_ = lexer_->nextToken();
        hasLookahead_ = true;
    }
    return lookaheadToken_;
}

bool Parser::match(TokenKind kind) {
    if (current_.kind == kind) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenKind kind, const char* msg) {
    if (current_.kind == kind) {
        return advance();
    }
    throw std::runtime_error(fmt::format("{}:{}: Expected {} but got '{}' ({})",
        fileName_, current_.line, msg,
        std::string(current_.text), Lexer::tokenKindToString(current_.kind)));
}

bool Parser::checkContextual(const char* keyword) const {
    return current_.kind == TokenKind::Identifier && current_.text == keyword;
}

bool Parser::matchContextual(const char* keyword) {
    if (checkContextual(keyword)) {
        advance();
        return true;
    }
    return false;
}

// ============================================================================
// ASI (Automatic Semicolon Insertion)
// ============================================================================

bool Parser::canInsertSemicolon() const {
    if (current_.kind == TokenKind::Semicolon) return true;
    if (current_.kind == TokenKind::CloseBrace) return true;
    if (current_.kind == TokenKind::EndOfFile) return true;
    if (current_.hadNewlineBefore) return true;
    return false;
}

void Parser::expectSemicolon() {
    if (match(TokenKind::Semicolon)) return;
    if (canInsertSemicolon()) return;  // ASI
    throw std::runtime_error(fmt::format("{}:{}: Expected ';' but got '{}'",
        fileName_, current_.line, std::string(current_.text)));
}

// ============================================================================
// Source location
// ============================================================================

void Parser::setLocation(ast::Node* node, const Token& tok) {
    if (!node) return;
    node->line = tok.line;
    node->column = tok.column;
    node->sourceFile = fileName_;
}

void Parser::setLocation(ast::Node* node, int line, int col) {
    if (!node) return;
    node->line = line;
    node->column = col;
    node->sourceFile = fileName_;
}

// ============================================================================
// Helpers
// ============================================================================

bool Parser::isIdentifierOrKeyword() const {
    if (current_.kind == TokenKind::Identifier) return true;
    // Many keywords can be used as identifiers in property position
    return Lexer::isKeyword(current_.kind);
}

std::string Parser::identifierName() {
    if (current_.kind == TokenKind::Identifier || Lexer::isKeyword(current_.kind)) {
        // Prefer the lexer-decoded text when the source contained Unicode
        // escapes (`\uXXXX` / `\u{...}`). Per ECMA-262 the decoded form is
        // the spec-meaningful identifier name in every position this helper
        // is called from (PropertyName, MemberExpression `.foo`, Import/
        // Export specifier names, class member names, etc.). Without this,
        // `obj.foo` would store as the literal source span and
        // `obj.foo` would miss the property.
        std::string name = !current_.decodedText.empty()
            ? current_.decodedText
            : std::string(current_.text);
        advance();
        return name;
    }
    throw std::runtime_error(fmt::format("{}:{}: Expected identifier but got '{}'",
        fileName_, current_.line, std::string(current_.text)));
}

Parser::SavedState Parser::saveState() const {
    SavedState s;
    s.current = current_;
    s.previous = previous_;
    s.lexerState = lexer_->saveLexerState();
    return s;
}

void Parser::restoreState(const SavedState& state) {
    lexer_->restoreLexerState(state.lexerState);
    current_ = state.current;
    previous_ = state.previous;
}

void Parser::pushLexicalScope() {
    lexicalScopes_.push_back(LexicalScope{});
}

void Parser::popLexicalScope() {
    if (!lexicalScopes_.empty()) lexicalScopes_.pop_back();
}

bool Parser::declareLexicalName(const std::string& name, PDeclKind kind) {
    if (lexicalScopes_.empty()) return true;
    auto& scope = lexicalScopes_.back();
    auto it = scope.names.find(name);
    if (it != scope.names.end()) {
        PDeclKind existing = it->second;
        // var + var is OK
        if (existing == PDeclKind::Var && kind == PDeclKind::Var) {
            return true;
        }
        // ECMA-262 14.1: FunctionDeclaration at function-scope is hoisted into
        // VarDeclaredNames, not LexicallyDeclaredNames. So `function f() { var
        // x; function x() {} }` is well-formed (the fn-decl shadows/merges with
        // the var). Both directions: pre-existing var then fn-decl, or pre-
        // existing fn-decl then var. The var+fn (in either order) and fn+var
        // (in either order) carveout below makes function-body scope push
        // (commit 50566e3) work without regressing this pattern.
        if ((existing == PDeclKind::Var && kind == PDeclKind::Function) ||
            (existing == PDeclKind::Function && kind == PDeclKind::Var)) {
            // MODULE goal, top level: FunctionDeclarations are
            // LexicallyDeclaredNames (ES 16.2.1), so `var x; function x(){}`
            // IS a duplicate there (it's only legal in scripts/function
            // bodies, where fn-decls hoist into VarDeclaredNames).
            if (!scriptGoal_ && lexicalScopes_.size() == 1) {
                throw std::runtime_error(fmt::format(
                    "{}: SyntaxError: Identifier '{}' has already been "
                    "declared", fileName_, name));
            }
            // Promote the slot to Function (the fn-decl wins; later var
            // re-declarations are fine since var+fn is allowed).
            scope.names[name] = PDeclKind::Function;
            return true;
        }
        // ECMA-262 Annex B.3.3.4: in non-strict code, duplicate
        // LexicallyDeclaredNames bound ONLY by FunctionDeclarations are
        // allowed in a Block. Mirror the carveout already applied to
        // switch CaseBlock (commit e4d724d).
        if (!strictMode_ && existing == PDeclKind::Function && kind == PDeclKind::Function) {
            return true;
        }
        // ECMA-262 Annex B.3.4: in non-strict code, a CatchParameter that
        // is a BindingIdentifier does NOT conflict with VarDeclaredNames of
        // the catch Block. parseCatchClause pre-declares the catch
        // parameter as PDeclKind::CatchParam; a subsequent `var x` here
        // should be allowed in non-strict mode. (let / const / class /
        // function declarations still conflict — they go into
        // LexicallyDeclaredNames per the un-modified rule.)
        if (!strictMode_ && existing == PDeclKind::CatchParam && kind == PDeclKind::Var) {
            return true;
        }
        // Everything else is a redeclaration error
        fprintf(stderr, "SyntaxError: Identifier '%s' has already been declared\n", name.c_str());
        errorCount_++;
        return false;
    }
    scope.names[name] = kind;
    return true;
}

bool Parser::isStartOfExpression() const {
    switch (current_.kind) {
        case TokenKind::Identifier:
        case TokenKind::NumericLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::TemplateHead:
        case TokenKind::NoSubstitutionTemplate:
        case TokenKind::RegularExpressionLiteral:
        case TokenKind::BigIntLiteral:
        case TokenKind::OpenParen:
        case TokenKind::OpenBracket:
        case TokenKind::OpenBrace:
        case TokenKind::KW_true:
        case TokenKind::KW_false:
        case TokenKind::KW_null:
        case TokenKind::KW_undefined:
        case TokenKind::KW_this:
        case TokenKind::KW_super:
        case TokenKind::KW_new:
        case TokenKind::KW_delete:
        case TokenKind::KW_typeof:
        case TokenKind::KW_void:
        case TokenKind::KW_function:
        case TokenKind::KW_class:
        case TokenKind::KW_async:
        case TokenKind::KW_await:
        case TokenKind::KW_yield:
        case TokenKind::KW_import:
        // Contextual keywords usable as IdentifierReference (parsePrimaryExpression
        // accepts these and returns them as Identifier nodes).
        case TokenKind::KW_let:
        case TokenKind::KW_of:
        case TokenKind::KW_as:
        case TokenKind::KW_is:
        case TokenKind::KW_get:
        case TokenKind::KW_set:
        case TokenKind::KW_from:
        case TokenKind::KW_require:
        case TokenKind::KW_constructor:
        case TokenKind::KW_keyof:
        case TokenKind::KW_infer:
        case TokenKind::KW_asserts:
        case TokenKind::KW_satisfies:
        case TokenKind::KW_out:
        case TokenKind::KW_type:
        case TokenKind::KW_module:
        case TokenKind::KW_namespace:
        case TokenKind::KW_static:
        case TokenKind::KW_abstract:
        case TokenKind::KW_readonly:
        case TokenKind::KW_declare:
        case TokenKind::KW_public:
        case TokenKind::KW_private:
        case TokenKind::KW_protected:
        case TokenKind::KW_implements:
        case TokenKind::KW_interface:
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::ExclamationMark:
        case TokenKind::Tilde:
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus:
        case TokenKind::DotDotDot:
        case TokenKind::Slash:  // regex
        case TokenKind::LessThan:  // JSX or type assertion
            return true;
        default:
            return false;
    }
}

bool Parser::isStartOfStatement() const {
    if (isStartOfExpression()) return true;
    switch (current_.kind) {
        case TokenKind::KW_var:
        case TokenKind::KW_let:
        case TokenKind::KW_const:
        case TokenKind::KW_if:
        case TokenKind::KW_while:
        case TokenKind::KW_do:
        case TokenKind::KW_for:
        case TokenKind::KW_switch:
        case TokenKind::KW_try:
        case TokenKind::KW_return:
        case TokenKind::KW_throw:
        case TokenKind::KW_break:
        case TokenKind::KW_continue:
        case TokenKind::KW_debugger:
        case TokenKind::KW_with:
        case TokenKind::Semicolon:
        case TokenKind::At:  // decorator
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Triple-slash references
// ============================================================================

std::vector<ast::TripleSlashReference> Parser::parseTripleSlashReferences() {
    // Triple-slash references are handled as special comments by the lexer
    // In our system, they were already parsed in dump_ast.js
    // For now, return empty - we'll add this in a later pass if needed
    return {};
}

// ============================================================================
// Decorator parsing
// ============================================================================

std::vector<ast::Decorator> Parser::parseDecorators() {
    std::vector<ast::Decorator> decorators;
    while (check(TokenKind::At)) {
        decorators.push_back(parseDecorator());
    }
    return decorators;
}

ast::Decorator Parser::parseDecorator() {
    expect(TokenKind::At, "'@'");
    ast::Decorator dec;

    // Parse the decorator expression: could be @name, @name.prop, @name(args)
    // First get the name
    dec.name = identifierName();
    std::string fullName = dec.name;

    // Parse dotted access: @a.b.c
    while (match(TokenKind::Dot)) {
        fullName += ".";
        fullName += identifierName();
    }

    // Build the decorator expression AST
    auto nameExpr = std::make_unique<ast::Identifier>();
    nameExpr->name = fullName;
    dec.name = fullName;

    // If it's a factory: @decorator(args)
    if (check(TokenKind::OpenParen)) {
        advance(); // (
        auto call = std::make_unique<ast::CallExpression>();
        call->callee = std::move(nameExpr);
        while (!check(TokenKind::CloseParen) && !isAtEnd()) {
            call->arguments.push_back(parseAssignmentExpression());
            if (!check(TokenKind::CloseParen)) {
                expect(TokenKind::Comma, "','");
            }
        }
        expect(TokenKind::CloseParen, "')'");
        dec.expression = std::shared_ptr<ast::Expression>(call.release());
    } else {
        dec.expression = std::shared_ptr<ast::Expression>(nameExpr.release());
    }

    return dec;
}

// ============================================================================
// Parameter parsing
// ============================================================================

std::unique_ptr<ast::Parameter> Parser::parseParameter() {
    auto param = std::make_unique<ast::Parameter>();
    setLocation(param.get(), current_);

    // Parameter decorators
    param->decorators = parseDecorators();

    // Access modifier
    if (current_.kind == TokenKind::KW_public || current_.kind == TokenKind::KW_private ||
        current_.kind == TokenKind::KW_protected) {
        if (current_.kind == TokenKind::KW_private) param->access = ts::AccessModifier::Private;
        else if (current_.kind == TokenKind::KW_protected) param->access = ts::AccessModifier::Protected;
        param->isParameterProperty = true;
        advance();
    }

    // Readonly
    if (current_.kind == TokenKind::KW_readonly) {
        param->isReadonly = true;
        param->isParameterProperty = true;
        advance();
    }

    // 'this' parameter
    if (current_.kind == TokenKind::KW_this) {
        param->isThisParameter = true;
        auto id = std::make_unique<ast::Identifier>();
        id->name = "this";
        setLocation(id.get(), current_);
        param->name = std::move(id);
        advance();
    }
    // Rest parameter
    else if (match(TokenKind::DotDotDot)) {
        param->isRest = true;
        param->name = parseBindingNameOrPattern();
    }
    // Regular parameter
    else {
        param->name = parseBindingNameOrPattern();
    }

    // Optional marker
    if (match(TokenKind::QuestionMark)) {
        param->isOptional = true;
    }

    // Type annotation
    if (check(TokenKind::Colon)) {
        param->type = parseTypeAnnotation();
    } else {
        param->type = "";
    }

    // Default value
    if (check(TokenKind::Equals)) {
        // Per ECMA-262 14.1: BindingRestElement may not have an initializer.
        // `function f(...x = []) {}` is a SyntaxError in any mode.
        if (param->isRest) {
            throw std::runtime_error(fmt::format(
                "{}:{}: rest parameter may not have a default initializer",
                current_.line, current_.column));
        }
        advance();
        // ECMA-262: a generator/async function's FormalParameters may not contain
        // a YieldExpression/AwaitExpression. Mark that we're in a parameter
        // default so the yield/await parse sites can reject it (reset to false
        // inside any nested function/method body — see those body parsers).
        bool prevInParamDefault = inParamDefault_;
        inParamDefault_ = true;
        param->initializer = parseAssignmentExpression();
        inParamDefault_ = prevInParamDefault;
    }

    return param;
}

// Forward declaration of file-static helper defined later in this file.
static void collectBoundIdentNames(const ast::Node* n,
                                   std::vector<std::pair<std::string, int>>& out);

void Parser::predeclareFormalParamsAsVar(
        const std::vector<std::unique_ptr<ast::Parameter>>& params) {
    for (auto& p : params) {
        if (!p || !p->name) continue;
        std::vector<std::pair<std::string, int>> pnames;
        collectBoundIdentNames(p->name.get(), pnames);
        for (auto& pn : pnames) declareLexicalName(pn.first, PDeclKind::Var);
    }
}

void Parser::checkForHeadLexicalVsBodyVar(const ast::Node* initializer,
                                          const ast::Node* body) {
    auto* vd = dynamic_cast<const ast::VariableDeclaration*>(initializer);
    if (!vd || vd->varKind == ast::VarKind::Var) return;  // only let/const heads
    std::vector<std::pair<std::string, int>> headPairs;
    collectBoundIdentNames(vd->name.get(), headPairs);
    if (headPairs.empty() || !body) return;
    auto headNamesHas = [&](const std::string& nm) {
        for (auto& h : headPairs) if (h.first == nm) return true;
        return false;
    };

    // Walk the body collecting VarDeclaredNames (var only), stopping at function
    // and class boundaries (their own var scope). Mirrors the switch CaseBlock
    // collector. Throw on the first body `var` that shadows a head lexical name.
    std::function<void(const ast::Node*)> walk = [&](const ast::Node* n) {
        if (!n) return;
        if (auto* d = dynamic_cast<const ast::VariableDeclaration*>(n)) {
            if (d->varKind == ast::VarKind::Var) {
                std::vector<std::pair<std::string, int>> names;
                collectBoundIdentNames(d->name.get(), names);
                for (auto& [nm, ln] : names) {
                    if (headNamesHas(nm)) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: '{}' is a lexical binding of the "
                            "for-loop head and cannot be re-declared as 'var' in "
                            "the body", fileName_, ln, nm));
                    }
                }
            }
            return;
        }
        if (auto* b = dynamic_cast<const ast::BlockStatement*>(n)) { for (auto& s : b->statements) walk(s.get()); return; }
        if (auto* ifs = dynamic_cast<const ast::IfStatement*>(n)) { walk(ifs->thenStatement.get()); walk(ifs->elseStatement.get()); return; }
        if (auto* ws = dynamic_cast<const ast::WhileStatement*>(n)) { walk(ws->body.get()); return; }  // do-while is a WhileStatement w/ isDoWhile
        if (auto* fs = dynamic_cast<const ast::ForStatement*>(n)) { walk(fs->initializer.get()); walk(fs->body.get()); return; }
        if (auto* fos = dynamic_cast<const ast::ForOfStatement*>(n)) { walk(fos->initializer.get()); walk(fos->body.get()); return; }
        if (auto* fis = dynamic_cast<const ast::ForInStatement*>(n)) { walk(fis->initializer.get()); walk(fis->body.get()); return; }
        if (auto* tr = dynamic_cast<const ast::TryStatement*>(n)) {
            for (auto& s : tr->tryBlock) walk(s.get());
            if (tr->catchClause) for (auto& s : tr->catchClause->block) walk(s.get());
            for (auto& s : tr->finallyBlock) walk(s.get());
            return;
        }
        if (auto* lbl = dynamic_cast<const ast::LabeledStatement*>(n)) { walk(lbl->statement.get()); return; }
        if (auto* sw = dynamic_cast<const ast::SwitchStatement*>(n)) {
            for (auto& c : sw->clauses) {
                if (auto* cc = dynamic_cast<const ast::CaseClause*>(c.get())) for (auto& s : cc->statements) walk(s.get());
                else if (auto* dc = dynamic_cast<const ast::DefaultClause*>(c.get())) for (auto& s : dc->statements) walk(s.get());
            }
            return;
        }
        // FunctionDeclaration / class / arrow bodies are their own var scope — stop.
    };
    walk(body);
}

std::vector<std::unique_ptr<ast::Parameter>> Parser::parseParameterList(bool checkDuplicates, bool uniqueParams) {
    std::vector<std::unique_ptr<ast::Parameter>> params;
    expect(TokenKind::OpenParen, "'('");
    // Per ECMA-262 14.1.2: It is a SyntaxError if IsSimpleParameterList of
    // FormalParameters is false and BoundNames contains any duplicate
    // elements. We track simple-ness and seen bound names of plain
    // identifier params; destructuring binding patterns count as
    // non-simple but we don't enumerate their bound names here.
    std::unordered_set<std::string> seenIdentNames;
    bool hasNonSimple = false;
    while (!check(TokenKind::CloseParen) && !isAtEnd()) {
        params.push_back(parseParameter());
        auto& p = params.back();
        const bool wasRest = p && p->isRest;
        // Determine simple-ness: a single binding-identifier with no
        // default, no rest, no question mark, and not destructuring.
        bool paramSimple = false;
        std::string paramName;
        if (p && !p->isRest && !p->initializer && !p->isOptional) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(p->name.get())) {
                paramSimple = true;
                paramName = ident->name;
            }
        } else if (p && !p->isRest && !p->isOptional) {
            // has initializer: not simple but still an Identifier name we
            // can track
            if (auto* ident = dynamic_cast<ast::Identifier*>(p->name.get())) {
                paramName = ident->name;
            }
        } else if (p && p->isRest) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(p->name.get())) {
                paramName = ident->name;
            }
        }
        if (!paramSimple) hasNonSimple = true;
        if (!paramName.empty()) {
            // Per ECMA-262 14.1.2: duplicates are a SyntaxError when EITHER
            // (a) the param list is non-simple (has rest, default, optional,
            // or destructuring), OR (b) we're in strict mode (which class
            // method bodies always are, per 10.2.1). Without the strict-mode
            // arm, `class C { foo(a, a) {} }` was silently accepted.
            if ((hasNonSimple || strictMode_ || uniqueParams) &&
                seenIdentNames.count(paramName)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: duplicate parameter name '{}' is not allowed in "
                    "this context",
                    current_.line, current_.column, paramName));
            }
            seenIdentNames.insert(paramName);
        }
        if (!check(TokenKind::CloseParen)) {
            // Per ECMA-262 14.1: FunctionRestParameter must not be followed
            // by a trailing comma. `(... a,)` is a SyntaxError in any mode.
            if (wasRest && check(TokenKind::Comma)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: rest parameter must not be followed by a trailing comma",
                    current_.line, current_.column));
            }
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::CloseParen, "')'");

    // ECMA-262 14.1.2 / 14.3.1: duplicate BoundNames in FormalParameters
    // are a Syntax Error when EITHER (a) IsSimpleParameterList is false
    // (so destructuring/default/rest params live here), OR (b) the
    // surrounding code is strict mode (class bodies + 'use strict'). The
    // inside-loop check only enumerates top-level Identifier names; this
    // sweep enumerates BoundNames of nested binding patterns so
    // `(x, [x])` / `([x, x])` / `(x, ...[x])` / `({a:x}, x)` etc. are
    // correctly rejected. ArrowParameters always trigger via (a) since
    // any param shape that introduces a duplicate is non-simple.
    //
    // Skipped when checkDuplicates=false (arrow speculative cover-grammar
    // parse): the caller re-runs the check after `=>` is confirmed, so
    // the parens-as-expression interpretation isn't poisoned.
    if (checkDuplicates && (hasNonSimple || strictMode_ || uniqueParams)) {
        std::vector<std::pair<std::string, int>> bound;
        for (auto& p : params) {
            if (!p) continue;
            collectBoundIdentNames(p->name.get(), bound);
        }
        std::unordered_set<std::string> seen;
        for (auto& entry : bound) {
            if (!seen.insert(entry.first).second) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: duplicate parameter name '{}' is "
                    "not allowed in this context",
                    fileName_, entry.second, entry.first));
            }
        }
    }
    return params;
}

// ============================================================================
// Type parameter parsing
// ============================================================================

std::unique_ptr<ast::TypeParameter> Parser::parseTypeParameter() {
    auto tp = std::make_unique<ast::TypeParameter>();
    setLocation(tp.get(), current_);
    tp->name = identifierName();

    // 'in' or 'out' variance modifiers (skip them)
    // constraint: extends Type
    if (current_.kind == TokenKind::KW_extends) {
        advance();
        tp->constraint = scanTypeExpression();
    }
    // default: = Type
    if (match(TokenKind::Equals)) {
        tp->defaultType = scanTypeExpression();
    }
    return tp;
}

std::vector<std::unique_ptr<ast::TypeParameter>> Parser::parseTypeParameterList() {
    std::vector<std::unique_ptr<ast::TypeParameter>> params;
    if (!check(TokenKind::LessThan)) return params;
    advance(); // <
    while (!check(TokenKind::GreaterThan) && !isAtEnd()) {
        // Skip variance modifiers (in/out)
        if (current_.kind == TokenKind::KW_in || current_.kind == TokenKind::KW_out) {
            advance();
        }
        params.push_back(parseTypeParameter());
        if (!check(TokenKind::GreaterThan)) {
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::GreaterThan, "'>'");
    return params;
}

// ============================================================================
// Binding patterns
// ============================================================================

ast::NodePtr Parser::parseBindingNameOrPattern() {
    if (check(TokenKind::OpenBrace)) {
        return parseObjectBindingPattern();
    }
    if (check(TokenKind::OpenBracket)) {
        return parseArrayBindingPattern();
    }
    // Per ES spec, BindingIdentifier cannot be a reserved word. While
    // identifierName() accepts any keyword (necessary for member access
    // like `obj.class`), a binding context (e.g. `var class = ...`) must
    // reject reserved words with a SyntaxError.
    {
        TokenKind k = current_.kind;
        bool isReservedBinding =
            k == TokenKind::KW_break || k == TokenKind::KW_case ||
            k == TokenKind::KW_catch || k == TokenKind::KW_class ||
            k == TokenKind::KW_const || k == TokenKind::KW_continue ||
            k == TokenKind::KW_debugger || k == TokenKind::KW_default ||
            k == TokenKind::KW_delete || k == TokenKind::KW_do ||
            k == TokenKind::KW_else || k == TokenKind::KW_enum ||
            k == TokenKind::KW_export || k == TokenKind::KW_extends ||
            k == TokenKind::KW_false || k == TokenKind::KW_finally ||
            k == TokenKind::KW_for || k == TokenKind::KW_function ||
            k == TokenKind::KW_if || k == TokenKind::KW_import ||
            k == TokenKind::KW_in || k == TokenKind::KW_instanceof ||
            k == TokenKind::KW_new || k == TokenKind::KW_null ||
            k == TokenKind::KW_return || k == TokenKind::KW_super ||
            k == TokenKind::KW_switch || k == TokenKind::KW_this ||
            k == TokenKind::KW_throw || k == TokenKind::KW_true ||
            k == TokenKind::KW_try || k == TokenKind::KW_typeof ||
            k == TokenKind::KW_var || k == TokenKind::KW_void ||
            k == TokenKind::KW_while || k == TokenKind::KW_with;
        if (isReservedBinding) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: '{}' is a reserved word and cannot be "
                "used as a binding identifier",
                fileName_, current_.line, std::string(current_.text)));
        }
        // Same restriction for identifiers whose decoded form matches a
        // reserved word via Unicode escape (`var for = 1`).
        if (current_.escapedReservedWord) {
            // Strict-mode-only future-reserved-words: `implements`,
            // `interface`, `package`, `protected`, `public`, `private`,
            // `static`, `let`, `yield`. In non-strict code these are valid
            // BindingIdentifiers. Allow their escape-encoded forms through
            // in non-strict mode.
            const std::string& decoded = current_.decodedText;
            bool isStrictOnlyFRW =
                decoded == "implements" || decoded == "interface" ||
                decoded == "package"    || decoded == "protected" ||
                decoded == "public"     || decoded == "private"   ||
                decoded == "static"     || decoded == "let";
            bool isYield = decoded == "yield";
            if (!((isStrictOnlyFRW && !strictMode_) ||
                  (isYield && !strictMode_ && !inGenerator_))) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: identifier resolves to reserved word "
                    "via Unicode escape and cannot be used as a binding",
                    fileName_, current_.line));
            }
        }
        // Per ECMA-262 12.1.1: `await` is not a valid BindingIdentifier
        // inside an [Await] context (async function body or module). The
        // lexer maps the `await` keyword to KW_await; in a non-await
        // context it is allowed (treated as an Identifier when used as
        // a binding name). Likewise, `yield` is not valid inside a
        // [Yield] context (generator function body) or strict mode.
        if (k == TokenKind::KW_await && inAsync_) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'await' is not allowed as a binding "
                "identifier inside an async function",
                fileName_, current_.line));
        }
        if (k == TokenKind::KW_yield && (inGenerator_ || strictMode_)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'yield' is not allowed as a binding "
                "identifier inside a generator function or strict mode",
                fileName_, current_.line));
        }
        // Per ECMA-262 12.1.1: in strict-mode code, BindingIdentifier
        // cannot be `eval` or `arguments`. The lexer emits these as
        // regular IdentifierName tokens (not keywords), so check by
        // text here. Outside strict mode they're fine.
        if (strictMode_ && (k == TokenKind::Identifier) &&
            (current_.text == "eval" || current_.text == "arguments")) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: '{}' is not allowed as a binding "
                "identifier in strict mode",
                fileName_, current_.line, std::string(current_.text)));
        }
        // Per ECMA-262 12.7.1: the FutureReservedWords `implements`,
        // `interface`, `let`, `package`, `private`, `protected`, `public`,
        // and `static` are reserved in strict-mode code and cannot be used
        // as BindingIdentifiers. (`yield`/`await` are handled above; these
        // are lexed as KW_* tokens, except `package` which is a plain
        // Identifier.) In sloppy mode they remain valid binding names.
        if (strictMode_) {
            static const std::unordered_set<std::string> kStrictFRW = {
                "implements", "interface", "let",    "package",
                "private",    "protected", "public", "static"};
            // Use the decoded name so an escape-encoded form (`var package`)
            // is rejected too — an escaped reserved word is still that word.
            std::string nm = current_.decodedText.empty()
                ? std::string(current_.text) : current_.decodedText;
            if (kStrictFRW.count(nm)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' is a reserved word in strict "
                    "mode and cannot be used as a binding identifier",
                    fileName_, current_.line, nm));
            }
        }
    }
    // Simple identifier
    auto id = std::make_unique<ast::Identifier>();
    setLocation(id.get(), current_);
    // Handle # prefix for private fields
    if (match(TokenKind::Hash)) {
        // ECMA-262: PrivateName is a single token — no whitespace between
        // '#' and the IdentifierName.
        if (current_.offset != previous_.offset + 1) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: no whitespace or line terminator allowed between '#' and identifier",
                previous_.line, previous_.column));
        }
        id->isPrivate = true;
        id->name = identifierName();
    } else {
        id->name = identifierName();
    }
    return id;
}

ast::NodePtr Parser::parseObjectBindingPattern() {
    auto pat = std::make_unique<ast::ObjectBindingPattern>();
    setLocation(pat.get(), current_);
    expect(TokenKind::OpenBrace, "'{'");

    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto elem = std::make_unique<ast::BindingElement>();
        setLocation(elem.get(), current_);

        // Check for rest element: ...name
        if (match(TokenKind::DotDotDot)) {
            elem->isSpread = true;
            elem->name = parseBindingNameOrPattern();
        } else if (check(TokenKind::NumericLiteral) ||
                   check(TokenKind::StringLiteral) ||
                   check(TokenKind::BigIntLiteral)) {
            // PropertyName : BindingElement with numeric/string/bigint key.
            // ECMA-262 14.3.3 BindingProperty : PropertyName : BindingElement.
            std::string propName;
            if (check(TokenKind::StringLiteral)) {
                propName = Lexer::getStringValue(current_.text);
            } else if (check(TokenKind::BigIntLiteral)) {
                propName = std::string(current_.text);
                if (!propName.empty() && propName.back() == 'n') propName.pop_back();
            } else {
                propName = canonicalNumericPropertyName(current_.text);
            }
            advance();
            expect(TokenKind::Colon, "':'");
            elem->propertyName = propName;
            elem->name = parseBindingNameOrPattern();

            if (match(TokenKind::Equals)) {
                bool prevNoIn = noIn_;
                noIn_ = false;
                elem->initializer = parseAssignmentExpression();
                noIn_ = prevNoIn;
            }
        } else if (check(TokenKind::OpenBracket)) {
            // ECMA-262 14.3.3 BindingProperty : PropertyName : BindingElement,
            // and PropertyName includes ComputedPropertyName [ AssignmentExpression[+In] ].
            advance();  // consume '['
            auto cpn = std::make_unique<ast::ComputedPropertyName>();
            setLocation(cpn.get(), previous_);
            bool prevNoIn = noIn_;
            noIn_ = false;
            cpn->expression = parseAssignmentExpression();
            noIn_ = prevNoIn;
            expect(TokenKind::CloseBracket, "']'");
            expect(TokenKind::Colon, "':'");
            elem->computedPropertyName = std::move(cpn);
            elem->propertyName = "[computed]";
            elem->name = parseBindingNameOrPattern();
            if (match(TokenKind::Equals)) {
                bool prevNoIn2 = noIn_;
                noIn_ = false;
                elem->initializer = parseAssignmentExpression();
                noIn_ = prevNoIn2;
            }
        } else {
            // propertyName: binding or just binding
            // We need to look ahead: if there's a ':', it's propertyName: binding
            // Capture escapedReservedWord before identifierName() advances —
            // shorthand `{ break }` (escape-encoded reserved word) is a
            // SyntaxError as a BindingIdentifier, but `{ break: x }` is
            // fine (PropertyName position).
            bool propEscapedReserved = current_.escapedReservedWord;
            int propLine = current_.line;
            // Capture the reserved-word kind BEFORE identifierName()
            // consumes the token. Shorthand binding `{ default }` must
            // be a SyntaxError because `default` is a reserved word and
            // BindingIdentifier rejects reserved words. The identifier-
            // alone-as-property-name form (full `{ default: x }`) is OK
            // since `default` there is a PropertyName, not a binding.
            TokenKind propKind = current_.kind;
            bool propIsReserved =
                propKind == TokenKind::KW_break    || propKind == TokenKind::KW_case ||
                propKind == TokenKind::KW_catch    || propKind == TokenKind::KW_class ||
                propKind == TokenKind::KW_const    || propKind == TokenKind::KW_continue ||
                propKind == TokenKind::KW_debugger || propKind == TokenKind::KW_default ||
                propKind == TokenKind::KW_delete   || propKind == TokenKind::KW_do ||
                propKind == TokenKind::KW_else     || propKind == TokenKind::KW_enum ||
                propKind == TokenKind::KW_export   || propKind == TokenKind::KW_extends ||
                propKind == TokenKind::KW_false    || propKind == TokenKind::KW_finally ||
                propKind == TokenKind::KW_for      || propKind == TokenKind::KW_function ||
                propKind == TokenKind::KW_if       || propKind == TokenKind::KW_import ||
                propKind == TokenKind::KW_in       || propKind == TokenKind::KW_instanceof ||
                propKind == TokenKind::KW_new      || propKind == TokenKind::KW_null ||
                propKind == TokenKind::KW_return   || propKind == TokenKind::KW_super ||
                propKind == TokenKind::KW_switch   || propKind == TokenKind::KW_this ||
                propKind == TokenKind::KW_throw    || propKind == TokenKind::KW_true ||
                propKind == TokenKind::KW_try      || propKind == TokenKind::KW_typeof ||
                propKind == TokenKind::KW_var      || propKind == TokenKind::KW_void ||
                propKind == TokenKind::KW_while    || propKind == TokenKind::KW_with;
            std::string propName = identifierName();

            if (match(TokenKind::Colon)) {
                elem->propertyName = propName;
                elem->name = parseBindingNameOrPattern();
            } else {
                if (propEscapedReserved) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: identifier resolves to reserved "
                        "word via Unicode escape and cannot be used as a "
                        "shorthand binding",
                        fileName_, propLine));
                }
                if (propIsReserved) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' is a reserved word and "
                        "cannot be used as a shorthand binding",
                        fileName_, propLine, propName));
                }
                // [+Await] context (async body / class static block): `await`
                // is reserved as a BindingIdentifier — `var {await} = {}` errors.
                if (propName == "await" && inAsync_) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'await' cannot be used as a binding "
                        "identifier in this context", fileName_, propLine));
                }
                // Strict-mode FutureReservedWords (incl. escape-encoded forms —
                // propName is the decoded name): `({ package }) => {}` in strict
                // code is a SyntaxError.
                if (strictMode_) {
                    static const std::unordered_set<std::string> kStrictFRWsh = {
                        "implements", "interface", "let",    "package",
                        "private",    "protected", "public", "static", "yield"};
                    if (kStrictFRWsh.count(propName)) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: '{}' is a reserved word in strict "
                            "mode and cannot be used as a shorthand binding",
                            fileName_, propLine, propName));
                    }
                }
                auto id = std::make_unique<ast::Identifier>();
                id->name = propName;
                elem->name = std::move(id);
            }

            // Default value
            if (match(TokenKind::Equals)) {
                elem->initializer = parseAssignmentExpression();
            }
        }

        pat->elements.push_back(std::move(elem));
        if (!check(TokenKind::CloseBrace)) {
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::CloseBrace, "'}'");
    return pat;
}

ast::NodePtr Parser::parseArrayBindingPattern() {
    auto pat = std::make_unique<ast::ArrayBindingPattern>();
    setLocation(pat.get(), current_);
    expect(TokenKind::OpenBracket, "'['");

    while (!check(TokenKind::CloseBracket) && !isAtEnd()) {
        if (check(TokenKind::Comma)) {
            // Omitted element
            auto omit = std::make_unique<ast::OmittedExpression>();
            setLocation(omit.get(), current_);
            pat->elements.push_back(std::move(omit));
        } else {
            auto elem = std::make_unique<ast::BindingElement>();
            setLocation(elem.get(), current_);

            if (match(TokenKind::DotDotDot)) {
                elem->isSpread = true;
                elem->name = parseBindingNameOrPattern();
                pat->elements.push_back(std::move(elem));
                // ECMA-262 ArrayBindingPattern: a BindingRestElement must
                // be the last element. Anything other than `]` after the
                // rest binding is a SyntaxError.
                if (!check(TokenKind::CloseBracket)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: Rest element must be the last element in array destructuring pattern",
                        fileName_, current_.line));
                }
                break;
            } else {
                elem->name = parseBindingNameOrPattern();
                if (match(TokenKind::Equals)) {
                    elem->initializer = parseAssignmentExpression();
                }
            }
            pat->elements.push_back(std::move(elem));
        }
        if (!check(TokenKind::CloseBracket)) {
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::CloseBracket, "']'");
    return pat;
}

// ============================================================================
// Statement parsing
// ============================================================================

ast::StmtPtr Parser::parseStatementOnly(bool allowAnnexBFunction) {
    TokenKind k = current_.kind;
    int line = current_.line;
    auto reject = [&](const std::string& form) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: {} is not a Statement (Declaration "
            "forms are not allowed as the body of "
            "if/while/for/do-while/labeled-statement)",
            fileName_, line, form));
    };
    if (k == TokenKind::KW_let) {
        auto saved = saveState();
        advance();
        TokenKind n = current_.kind;
        bool nextHasNewline = current_.hadNewlineBefore;
        restoreState(saved);
        // `let [` is always a LexicalDeclaration (ExpressionStatement
        // lookahead restriction includes `let [`). Always reject.
        if (n == TokenKind::OpenBracket) reject("'let' declaration");
        // `let X` or `let {` on the same line is a LexicalDeclaration.
        // With a LineTerminator before X / `{`, ASI splits `let;` from
        // the next statement — fall through to parseDeclarationOrStatement
        // which will take the let-as-identifier path.
        bool isDecl =
            n == TokenKind::Identifier ||
            n == TokenKind::OpenBrace ||
            n == TokenKind::KW_async || n == TokenKind::KW_await ||
            n == TokenKind::KW_yield || n == TokenKind::KW_of ||
            n == TokenKind::KW_from || n == TokenKind::KW_as ||
            n == TokenKind::KW_get || n == TokenKind::KW_set ||
            n == TokenKind::KW_let || n == TokenKind::KW_static ||
            n == TokenKind::KW_type || n == TokenKind::KW_module ||
            n == TokenKind::KW_namespace || n == TokenKind::KW_interface ||
            n == TokenKind::KW_declare || n == TokenKind::KW_abstract ||
            n == TokenKind::KW_readonly || n == TokenKind::KW_implements ||
            n == TokenKind::KW_public || n == TokenKind::KW_private ||
            n == TokenKind::KW_protected ||
            n == TokenKind::KW_constructor || n == TokenKind::KW_keyof ||
            n == TokenKind::KW_infer || n == TokenKind::KW_asserts ||
            n == TokenKind::KW_satisfies || n == TokenKind::KW_undefined;
        if (isDecl && !nextHasNewline) reject("'let' declaration");
    } else if (k == TokenKind::KW_const) {
        reject("'const' declaration");
    } else if (k == TokenKind::KW_class) {
        reject("'class' declaration");
    } else if (k == TokenKind::KW_function) {
        auto saved = saveState();
        advance();
        bool isGen = (current_.kind == TokenKind::Star);
        restoreState(saved);
        if (isGen) reject("generator declaration");
        // Plain `function` is allowed via Annex B.3.2 only in if-body
        // and labeled-statement body, in non-strict mode. Loop bodies
        // (while/for/do-while) reject regardless of strict mode.
        if (!allowAnnexBFunction) reject("'function' declaration");
        if (strictMode_) reject("'function' declaration");
    } else if (k == TokenKind::KW_async) {
        auto saved = saveState();
        advance();
        bool isAsyncFn = (current_.kind == TokenKind::KW_function);
        restoreState(saved);
        if (isAsyncFn) reject("async function declaration");
    }
    return parseDeclarationOrStatement();
}

ast::StmtPtr Parser::parseLoopBody() {
    iterationDepth_++;
    auto body = parseStatementOnly();
    iterationDepth_--;
    return body;
}

ast::StmtPtr Parser::parseDeclarationOrStatement() {
    // ModuleItem position: only the module body's top-level loop sets this;
    // consume it so every nested statement parse (blocks, if/while bodies,
    // switch cases, function bodies) sees false.
    bool moduleItemPos = atTopLevel_;
    atTopLevel_ = false;
    auto decorators = parseDecorators();

    // Handle export/import at top level
    if (check(TokenKind::KW_export)) {
        // ES 16.2.1: an ExportDeclaration is a ModuleItem — a nested
        // statement position (`{ export default null; }`, an if body, a
        // function body...) is a parse-phase SyntaxError
        // (module-code/parse-err-decl-pos-export-*).
        if (!moduleItemPos) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'export' declarations may only appear "
                "at the top level of a module", fileName_, current_.line));
        }
        auto stmt = parseExportDeclaration();
        return stmt;
    }

    if (check(TokenKind::KW_import)) {
        // Could be import declaration or import() expression
        // Look ahead: if next token after 'import' is '(' or '.', it's an expression
        // Otherwise it's a declaration
        // Actually: import.meta and import() are expressions
        // Save state for speculation
        auto saved = saveState();
        advance(); // consume 'import'
        if (check(TokenKind::OpenParen) || check(TokenKind::Dot)) {
            restoreState(saved);
            return parseExpressionStatement();
        }
        restoreState(saved);
        // Same ModuleItem rule for ImportDeclaration (import() / import.meta
        // EXPRESSIONS took the branch above and stay legal anywhere).
        if (!moduleItemPos) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'import' declarations may only appear "
                "at the top level of a module", fileName_, current_.line));
        }
        return parseImportDeclaration();
    }

    // Contextual `struct Foo {}` — the "use fast" value type
    // (docs/design/use-fast.md). ONLY recognized in a "use fast" file (the
    // top-level prologue is parsed before any body statement, so the flag is
    // set by now); non-fast files keep `struct` as an ordinary identifier, so
    // ts-aot stays a strict TS subset outside fast files. `struct` is not
    // reserved, so also require an identifier (the type name) to follow —
    // `struct = 5` / `struct.foo()` keep their identifier meaning.
    if (sawUseFastDirective_ && checkContextual("struct") &&
        peekAhead().kind == TokenKind::Identifier) {
        advance();  // consume the contextual `struct` (pops the name into current_)
        auto stmt = parseClassDeclaration(false, false, false, /*isStruct=*/true);
        if (!decorators.empty()) stmt->decorators = std::move(decorators);
        return stmt;
    }

    // Handle 'declare' keyword (ambient declarations)
    if (current_.kind == TokenKind::KW_declare) {
        advance(); // consume 'declare'
        if (current_.kind == TokenKind::KW_enum) {
            return parseEnumDeclaration(false, true);
        }
        // Skip other declare declarations (functions, classes, etc.)
        // For now, parse them normally
        if (current_.kind == TokenKind::KW_function) {
            return parseFunctionDeclaration(false, false, false);
        }
        if (current_.kind == TokenKind::KW_class) {
            return parseClassDeclaration(false, false, false);
        }
        if (current_.kind == TokenKind::KW_abstract) {
            advance();
            return parseClassDeclaration(true, false, false);
        }
        if (current_.kind == TokenKind::KW_interface) {
            return parseInterfaceDeclaration(false, false);
        }
        if (current_.kind == TokenKind::KW_type) {
            // `type` is contextual: `type X = ...` declares a type alias,
            // but `type = ...` / `type.foo` / `type()` etc. are
            // assignments/member-access on a variable named `type`. Peek
            // the next token to disambiguate. If it isn't an identifier
            // (or contextual keyword identifier), fall through to
            // expression-statement.
            auto saved = saveState();
            advance();
            bool isTypeAlias = (current_.kind == TokenKind::Identifier ||
                                Lexer::isKeyword(current_.kind))
                               && !current_.hadNewlineBefore;
            restoreState(saved);
            if (isTypeAlias) {
                return parseTypeAliasDeclaration(false);
            }
            // Otherwise, fall through to expression statement.
        }
        if (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
            current_.kind == TokenKind::KW_const) {
            auto stmts = parseVariableDeclarationList(false, /*isAmbient=*/true);
            if (stmts.size() == 1) return std::move(stmts[0]);
            // Wrap in block (synthetic - no new scope for var declarations)
            auto block = std::make_unique<ast::BlockStatement>();
            block->isSynthetic = true;
            for (auto& s : stmts) block->statements.push_back(std::move(s));
            return block;
        }
        if (current_.kind == TokenKind::KW_namespace || current_.kind == TokenKind::KW_module) {
            advance(); // consume 'namespace'/'module'
            auto ns = std::make_unique<ast::NamespaceDeclaration>();
            if (check(TokenKind::StringLiteral)) {
                // declare module 'string-literal' — ambient module declaration
                ns->name = Lexer::getStringValue(current_.text);
                ns->isAmbientModule = true;
                advance();
            } else {
                ns->name = identifierName();
            }
            if (check(TokenKind::OpenBrace)) {
                advance(); // {
                while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
                    // Track whether member had 'export' keyword
                    bool memberExported = false;
                    bool memberDefaultExport = false;
                    if (current_.kind == TokenKind::KW_export) {
                        // For ambient modules, preserve export status; for regular namespaces, just skip
                        memberExported = ns->isAmbientModule;
                        advance();
                        if (current_.kind == TokenKind::KW_default) {
                            memberDefaultExport = ns->isAmbientModule;
                            advance();
                        }
                    }
                    // Skip 'declare' keyword inside module body (common in .d.ts)
                    if (current_.kind == TokenKind::KW_declare) {
                        advance();
                    }
                    // Inside declare namespace, members are implicitly 'declare'
                    // so handle them the same way as top-level 'declare' statements
                    if (current_.kind == TokenKind::KW_function) {
                        ns->body.push_back(parseFunctionDeclaration(memberExported, memberDefaultExport, false));
                    } else if (current_.kind == TokenKind::KW_class) {
                        ns->body.push_back(parseClassDeclaration(memberExported, memberDefaultExport, false));
                    } else if (current_.kind == TokenKind::KW_interface) {
                        ns->body.push_back(parseInterfaceDeclaration(memberExported, memberDefaultExport));
                    } else if (current_.kind == TokenKind::KW_type) {
                        ns->body.push_back(parseTypeAliasDeclaration(memberExported));
                    } else if (current_.kind == TokenKind::KW_enum) {
                        ns->body.push_back(parseEnumDeclaration(memberExported, true));
                    } else if (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
                               current_.kind == TokenKind::KW_const) {
                        auto stmts = parseVariableDeclarationList(memberExported, /*isAmbient=*/true);
                        for (auto& s : stmts) ns->body.push_back(std::move(s));
                    } else {
                        // Skip unknown tokens to avoid infinite loop
                        advance();
                    }
                }
                expect(TokenKind::CloseBrace, "'}'");
            }
            return ns;
        }
        // Fallthrough to normal parsing
    }

    // 'abstract class'
    if (current_.kind == TokenKind::KW_abstract) {
        advance();
        if (current_.kind == TokenKind::KW_class) {
            auto stmt = parseClassDeclaration(true, false, false);
            if (!decorators.empty()) {
                stmt->decorators = std::move(decorators);
            }
            return stmt;
        }
        // Not followed by class, treat as identifier
        auto es = std::make_unique<ast::ExpressionStatement>();
        auto id = std::make_unique<ast::Identifier>();
        id->name = "abstract";
        es->expression = std::move(id);
        return es;
    }

    ast::StmtPtr result;

    switch (current_.kind) {
        case TokenKind::KW_function:
            result = parseFunctionDeclaration(false, false, false);
            break;
        case TokenKind::KW_async: {
            // async function or async arrow
            auto saved = saveState();
            advance(); // consume 'async'
            if (check(TokenKind::KW_function) && !current_.hadNewlineBefore) {
                result = parseFunctionDeclaration(true, false, false);
            } else {
                restoreState(saved);
                result = parseExpressionStatement();
            }
            break;
        }
        case TokenKind::KW_class:
            result = parseClassDeclaration(false, false, false);
            break;
        case TokenKind::KW_var:
        case TokenKind::KW_let:
        case TokenKind::KW_const: {
            // const enum -> parseEnumDeclaration handles the 'const' keyword
            if (current_.kind == TokenKind::KW_const) {
                // Peek ahead to check for 'enum'
                auto saved = saveState();
                advance(); // consume 'const'
                if (current_.kind == TokenKind::KW_enum) {
                    restoreState(saved);
                    result = parseEnumDeclaration(false, false);
                    break;
                }
                restoreState(saved);
            }
            // ES262 13.3.1: in non-strict mode, `let` is an Identifier
            // when not followed by a BindingIdentifier/`[`/`{`.
            // BindingIdentifier includes contextual keywords like
            // `async`, `await`, `yield`, `of`, `from`, `let` (recursive),
            // etc., that can appear as identifiers — so the lookahead
            // must accept those too.
            if (current_.kind == TokenKind::KW_let && !strictMode_) {
                auto saved = saveState();
                advance();
                TokenKind k = current_.kind;
                bool looksLikeDecl =
                    k == TokenKind::Identifier ||
                    k == TokenKind::OpenBracket ||
                    k == TokenKind::OpenBrace ||
                    // Contextual keywords usable as BindingIdentifier:
                    k == TokenKind::KW_async || k == TokenKind::KW_await ||
                    k == TokenKind::KW_yield || k == TokenKind::KW_of ||
                    k == TokenKind::KW_from || k == TokenKind::KW_as ||
                    k == TokenKind::KW_get || k == TokenKind::KW_set ||
                    k == TokenKind::KW_let || k == TokenKind::KW_static ||
                    k == TokenKind::KW_type || k == TokenKind::KW_module ||
                    k == TokenKind::KW_namespace || k == TokenKind::KW_interface ||
                    k == TokenKind::KW_declare || k == TokenKind::KW_abstract ||
                    k == TokenKind::KW_readonly || k == TokenKind::KW_implements ||
                    k == TokenKind::KW_public || k == TokenKind::KW_private ||
                    k == TokenKind::KW_protected ||
                    k == TokenKind::KW_constructor || k == TokenKind::KW_keyof ||
                    k == TokenKind::KW_infer || k == TokenKind::KW_asserts ||
                    k == TokenKind::KW_satisfies || k == TokenKind::KW_undefined;
                restoreState(saved);
                if (!looksLikeDecl) {
                    result = parseLabeledOrExpressionStatement();
                    break;
                }
            }
            auto stmts = parseVariableDeclarationList(false);
            if (stmts.size() == 1) {
                result = std::move(stmts[0]);
            } else {
                auto block = std::make_unique<ast::BlockStatement>();
                block->isSynthetic = true;
                setLocation(block.get(), stmts[0]->line, stmts[0]->column);
                for (auto& s : stmts) block->statements.push_back(std::move(s));
                result = std::move(block);
            }
            break;
        }
        case TokenKind::KW_if:
            result = parseIfStatement();
            break;
        case TokenKind::KW_while:
            result = parseWhileStatement();
            break;
        case TokenKind::KW_do:
            result = parseDoWhileStatement();
            break;
        case TokenKind::KW_for:
            result = parseForStatement();
            break;
        case TokenKind::KW_switch:
            result = parseSwitchStatement();
            break;
        case TokenKind::KW_try:
            result = parseTryStatement();
            break;
        case TokenKind::KW_return:
            result = parseReturnStatement();
            break;
        case TokenKind::KW_throw:
            result = parseThrowStatement();
            break;
        case TokenKind::KW_break:
            result = parseBreakStatement();
            break;
        case TokenKind::KW_continue:
            result = parseContinueStatement();
            break;
        case TokenKind::KW_debugger:
            result = parseDebuggerStatement();
            break;
        case TokenKind::KW_with: {
            // ECMA-262 §13.11.1: WithStatement is a Syntax Error in strict
            // mode. We don't implement dynamic-scope semantics, but we DO
            // parse the syntax in non-strict mode so test262's
            // dynamic-import-with-with-binding tests can compile (their
            // assertions are about the inner specifier syntax, not with's
            // dynamic-scope behavior). Strict mode still rejects.
            if (strictMode_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'with' statements are not allowed in strict mode",
                    fileName_, current_.line));
            }
            advance(); // 'with'
            expect(TokenKind::OpenParen, "'('");
            // Evaluate the head expression (parsing only — its result is
            // discarded). This still surfaces parse errors inside the
            // expression for the test262 negative-parse cases.
            auto head = parseExpression();
            expect(TokenKind::CloseParen, "')'");
            // Body is a regular statement. Treat the whole `with (...) stmt`
            // as a SYNTHETIC block — the with-scope semantics are not modeled,
            // but `var` declarations inside should hoist to the enclosing
            // function/script scope per JS spec (var hoisting traverses any
            // block boundaries). isSynthetic=true makes the analyzer skip
            // entering a new lexical scope so vars land in the enclosing
            // scope, where outer code can find them.
            auto block = std::make_unique<ast::BlockStatement>();
            setLocation(block.get(), current_);
            block->isSynthetic = true;
            // Preserve the head: ASTToHIR pushes ToObject(head) on the
            // runtime with-scope stack around the body (real `with`
            // semantics — the resolver consults the stack for otherwise
            // unresolved identifiers).
            block->withHead = std::move(head);
            // ECMA-262 14.11.1: IsLabelledFunction(Statement) of a WithStatement
            // body is a Syntax Error — `with ({}) L: function f(){}` is rejected
            // (mirrors the if/else-body rule).
            bool prevLBF = labelBodyForbidsFunction_;
            labelBodyForbidsFunction_ = true;
            auto body = parseStatementOnly();
            labelBodyForbidsFunction_ = prevLBF;
            if (body) block->statements.push_back(std::move(body));
            result = std::move(block);
            break;
        }
        case TokenKind::OpenBrace:
            result = parseBlockStatement();
            break;
        case TokenKind::Semicolon:
            advance();
            result = std::make_unique<ast::ExpressionStatement>();
            break;
        case TokenKind::KW_interface:
            result = parseInterfaceDeclaration(false, false);
            break;
        case TokenKind::KW_type: {
            // Contextual: `type X = ...` is a type alias, but `type =`/
            // `type.foo`/`type()` etc. are expressions using `type` as an
            // identifier. Peek the next token (must be an identifier-like
            // start of a type-alias name, no newline).
            auto saved = saveState();
            advance();
            bool isTypeAlias = (current_.kind == TokenKind::Identifier ||
                                Lexer::isKeyword(current_.kind))
                               && !current_.hadNewlineBefore;
            restoreState(saved);
            if (isTypeAlias) {
                result = parseTypeAliasDeclaration(false);
            } else {
                result = parseLabeledOrExpressionStatement();
            }
            break;
        }
        case TokenKind::KW_enum:
            result = parseEnumDeclaration(false, false);
            break;
        default:
            result = parseLabeledOrExpressionStatement();
            break;
    }

    if (result && !decorators.empty()) {
        result->decorators = std::move(decorators);
    }

    return result;
}

ast::StmtPtr Parser::parseFunctionDeclaration(bool isAsync, bool isExported, bool isDefaultExport) {
    auto startTok = current_;
    expect(TokenKind::KW_function, "'function'");

    auto node = std::make_unique<ast::FunctionDeclaration>();
    setLocation(node.get(), startTok);
    node->isAsync = isAsync;
    node->isExported = isExported;
    node->isDefaultExport = isDefaultExport;

    // Generator
    if (match(TokenKind::Star)) {
        node->isGenerator = true;
    }

    // Name (optional for default exports)
    if (isIdentifierOrKeyword()) {
        node->name = identifierName();
        // ECMA-262: in a [+Await] context (async function body / class static
        // block) `await` is reserved and cannot be a function BindingIdentifier.
        // The name is parsed in the enclosing context, so inAsync_ here is that
        // context's value (the function's own body hasn't been entered yet).
        if (node->name == "await" && inAsync_) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'await' cannot be used as a binding "
                "identifier in this context", fileName_, node->line));
        }
        // Track in lexical scope for redeclaration detection.
        // ECMA-262 Annex B.3.3 legacy hoisting / var-fn-merge only covers
        // plain (sync-non-generator) FunctionDeclarations. Async functions,
        // generators, and async-generators are strictly lex-scoped (same
        // conflict rules as `let`/`const`/`class`). Use PDeclKind::Let for
        // those so `{ async function f() {} function f() {} }` and similar
        // patterns correctly error per spec.
        if (!node->name.empty()) {
            PDeclKind kind = (isAsync || node->isGenerator)
                ? PDeclKind::Let
                : PDeclKind::Function;
            declareLexicalName(node->name, kind);
        }
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // ECMA-262: parameter list is parsed under the new function's [Await]/
    // [Yield] flags, NOT the outer context's. So `function* g() { function
    // f(yield) {} }` is valid — the inner f's params have [Yield]=false.
    // Save current flags and set per-function flags before parseParameterList.
    bool prevAsyncOuter = inAsync_;
    bool prevGenOuter = inGenerator_;
    inAsync_ = node->isAsync;
    inGenerator_ = node->isGenerator;
    // ECMA-262 13.3.7.1: FunctionDeclaration body has no [HomeObject], so
    // SuperReference (super(...) or super.x) is forbidden inside both the
    // params and the body. Save+set false here; restored at function end.
    bool prevSuperAllowed = superAllowed_;
    superAllowed_ = false;

    // Parameters
    node->parameters = parseParameterList();

    // Return type
    if (check(TokenKind::Colon)) {
        node->returnType = parseReturnTypeAnnotation();
    }

    // Body
    if (check(TokenKind::OpenBrace)) {
        bool prevAsync = inAsync_;
        bool prevGen = inGenerator_;
        StrictModeGuard sg(this);  // Save strictMode_; restore on exit.
        inAsync_ = node->isAsync;
        inGenerator_ = node->isGenerator;
        functionDepth_++;
        nonArrowFunctionDepth_++;
        int prevIter = iterationDepth_, prevSwitch = switchDepth_;
        iterationDepth_ = 0; switchDepth_ = 0;
        // ECMA-262 8.6 (ContainsUndefinedBreakTarget/ContinueTarget walks
        // stop at function boundaries): a label in the outer scope is NOT
        // visible inside the function body.
        std::vector<ActiveLabel> savedLabels;
        savedLabels.swap(activeLabels_);
        bool prevSawUseStrict = sawUseStrictDirective_;
        sawUseStrictDirective_ = false;

        expect(TokenKind::OpenBrace, "'{'");
        // ECMA-262 14.2.1: FunctionBody introduces its own LexicalDeclarations
        // scope. Without this push, `function f() { let t = ...; }` would land
        // in the enclosing scope, conflicting with sibling `let t` decls in
        // other functions or outer blocks (has-instance-jitted.js hit this).
        pushLexicalScope();
        predeclareFormalParamsAsVar(node->parameters);
        bool prevInParamDefault = inParamDefault_; inParamDefault_ = false;
        pendingPrologueStrings_.clear();
        bool inPrologue = true;
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) {
                if (inPrologue && !processPrologueDirective(stmt)) {
                    inPrologue = false;
                }
                node->body.push_back(std::move(stmt));
            }
        }
        popLexicalScope();
        inParamDefault_ = prevInParamDefault;
        expect(TokenKind::CloseBrace, "'}'");

        // Per ECMA-262 14.1.1: It is a SyntaxError if ContainsUseStrict of
        // FunctionBody is true and IsSimpleParameterList of FormalParameters
        // is false.
        if (sawUseStrictDirective_) {
            if (!isParameterListSimple(node->parameters)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: function with non-simple parameter list may not "
                    "declare \"use strict\"",
                    current_.line, current_.column));
            }
            // ECMA-262 15.2: a "use strict" directive retroactively forbids
            // `eval`/`arguments` as parameter names.
            std::vector<std::pair<std::string,int>> pnames;
            for (auto& p : node->parameters)
                if (p) collectBoundIdentNames(p->name.get(), pnames);
            for (auto& pr : pnames)
                if (pr.first == "eval" || pr.first == "arguments")
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' may not be a parameter name "
                        "when the body declares \"use strict\"",
                        fileName_, pr.second, pr.first));
        }
        sawUseStrictDirective_ = prevSawUseStrict;

        functionDepth_--;
        nonArrowFunctionDepth_--;
        iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
        activeLabels_.swap(savedLabels);
        inAsync_ = prevAsync;
        inGenerator_ = prevGen;
    } else {
        // Overload signature (no body) - consume the semicolon
        expectSemicolon();
    }
    // Restore the outer flags now that param-list + (optional) body are done.
    inAsync_ = prevAsyncOuter;
    inGenerator_ = prevGenOuter;
    superAllowed_ = prevSuperAllowed;

    return node;
}

// Recursively collect BindingIdentifier names from a binding pattern.
// Used to enforce static-semantics rules like "BoundNames of LexicalDeclaration
// must not contain 'let'" (ECMA-262 13.3.1.1).
static void collectBoundIdentNames(const ast::Node* n, std::vector<std::pair<std::string, int>>& out) {
    if (!n) return;
    if (auto* id = dynamic_cast<const ast::Identifier*>(n)) {
        out.push_back({id->name, id->line});
        return;
    }
    if (auto* be = dynamic_cast<const ast::BindingElement*>(n)) {
        collectBoundIdentNames(be->name.get(), out);
        return;
    }
    if (auto* obj = dynamic_cast<const ast::ObjectBindingPattern*>(n)) {
        for (auto& e : obj->elements) collectBoundIdentNames(e.get(), out);
        return;
    }
    if (auto* arr = dynamic_cast<const ast::ArrayBindingPattern*>(n)) {
        for (auto& e : arr->elements) collectBoundIdentNames(e.get(), out);
        return;
    }
}

std::vector<ast::StmtPtr> Parser::parseVariableDeclarationList(bool isExported, bool isAmbient) {
    auto startTok = current_;
    // var / let / const
    advance(); // consume the keyword

    std::vector<ast::StmtPtr> result;

    do {
        auto decl = std::make_unique<ast::VariableDeclaration>();
        setLocation(decl.get(), current_);
        decl->isExported = isExported;
        if (startTok.kind == TokenKind::KW_let) decl->varKind = ast::VarKind::Let;
        else if (startTok.kind == TokenKind::KW_const) decl->varKind = ast::VarKind::Const;

        // Name or binding pattern
        decl->name = parseBindingNameOrPattern();

        // ECMA-262 13.3.1.1: BoundNames of LexicalDeclaration may not
        // contain "let", and BoundNames of a single LexicalBinding
        // pattern may not contain duplicates (`let [x, x] = arr`).
        if (decl->varKind == ast::VarKind::Let || decl->varKind == ast::VarKind::Const) {
            std::vector<std::pair<std::string, int>> names;
            collectBoundIdentNames(decl->name.get(), names);
            std::unordered_map<std::string, int> seen;
            for (auto& [nm, ln] : names) {
                if (nm == "let") {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'let' is not a valid binding "
                        "identifier in let/const declarations",
                        fileName_, ln));
                }
                if (seen.count(nm)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: duplicate binding name '{}' in "
                        "destructuring pattern",
                        fileName_, ln, nm));
                }
                seen[nm] = ln;
            }
        }

        // Track declarations for redeclaration detection
        if (auto* ident = dynamic_cast<ast::Identifier*>(decl->name.get())) {
            PDeclKind dk = PDeclKind::Var;
            if (decl->varKind == ast::VarKind::Let) dk = PDeclKind::Let;
            else if (decl->varKind == ast::VarKind::Const) dk = PDeclKind::Const;
            declareLexicalName(ident->name, dk);
        }

        // Type annotation
        if (check(TokenKind::Colon)) {
            decl->type = parseTypeAnnotation();
        }

        // Initializer
        if (match(TokenKind::Equals)) {
            decl->initializer = parseAssignmentExpression();
        }
        // ECMA-262 14.3.1.1: const declarations require an initializer.
        // The exception — `for (const x in obj)` / `for (const x of arr)`
        // — is parsed by parseForStatement, not this function, so this
        // path is always the bare `const x;` form.
        // Ambient declarations (`declare const x: T;`) legitimately have
        // no initializer -- 64 tsconf conformance tests use exactly that.
        if (decl->varKind == ast::VarKind::Const && !decl->initializer &&
            !isAmbient) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'const' declaration requires an "
                "initializer",
                fileName_, decl->line));
        }

        result.push_back(std::move(decl));
    } while (match(TokenKind::Comma));

    expectSemicolon();
    return result;
}

void Parser::parseClassHeritageClause(std::string& baseClass,
                                      std::vector<std::string>& implementsOut,
                                      bool& hasHeritage) {
    auto startTok = current_;
    // ECMA-262 10.2.1 / 15.7: a class definition is strict-mode code in its
    // entirety, including the ClassHeritage — so e.g. a `with` statement in a
    // function used as the superclass expression is a SyntaxError.
    StrictModeGuard heritageStrict(this);
    strictMode_ = true;
    if (match(TokenKind::KW_extends)) {

        hasHeritage = true;
        // ECMA-262 ClassHeritage : extends LeftHandSideExpression. The AST
        // currently stores baseClass as a plain identifier string. For the
        // simple `Identifier (<TypeArgs>)?` shape we keep the legacy fast
        // path. For more complex heritage (e.g. `extends Object.getPrototypeOf(X)`)
        // we parse the whole expression to consume tokens and best-effort
        // record the leading identifier as baseClass for analyzer lookups.
        bool simple = false;
        // Only attempt simple-identifier path for actual identifiers;
        // reserved words like `class`, `function`, `null`, `true`, `false`,
        // `new`, `super`, `this` are not valid IdentifierReference and must
        // go through the LHS-expression path so e.g. `extends class {}` is
        // parsed correctly as an anonymous class expression.
        if (current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::KW_async ||
            current_.kind == TokenKind::KW_await ||
            current_.kind == TokenKind::KW_yield ||
            current_.kind == TokenKind::KW_of ||
            current_.kind == TokenKind::KW_from ||
            current_.kind == TokenKind::KW_as ||
            current_.kind == TokenKind::KW_get ||
            current_.kind == TokenKind::KW_set ||
            current_.kind == TokenKind::KW_type) {
            auto saved = saveState();
            std::string firstName(current_.text);
            advance();
            // Dotted heritage (`extends Temporal.Duration` / `Intl.X`): keep
            // consuming `.Identifier` segments so the runtime builtin-base
            // link receives the full path (it resolves dotted names by
            // walking globals). Only plain identifier segments qualify —
            // calls/indexing restore to the complex-LHS path below.
            while (check(TokenKind::Dot)) {
                auto dotSaved = saveState();
                advance();
                if (current_.kind == TokenKind::Identifier) {
                    firstName += ".";
                    firstName += std::string(current_.text);
                    advance();
                } else {
                    restoreState(dotSaved);
                    break;
                }
            }
            if (check(TokenKind::OpenBrace) ||
                check(TokenKind::KW_implements) ||
                check(TokenKind::LessThan)) {
                baseClass = firstName;
                if (check(TokenKind::LessThan)) {
                    skipTypeExpression();
                }
                simple = true;
            } else {
                restoreState(saved);
            }
        }
        if (!simple) {
            // Complex LHS path. Per ECMA-262 ClassHeritage : extends
            // LeftHandSideExpression — only valid LHS starts are Identifier,
            // `new`, `super`, `this`. Anything else (paren expression,
            // arrow function, array/object/function/class expression
            // literal) is NOT a valid heritage and we fall through so the
            // OpenBrace expect below raises a parse error. This keeps the
            // negative-parse tests (`class C extends () => {}`,
            // `class C extends [] {}`, `class C extends function(){} {}`)
            // correctly rejected.
            bool lhsStart = current_.kind == TokenKind::Identifier ||
                            check(TokenKind::KW_new) ||
                            check(TokenKind::KW_super) ||
                            check(TokenKind::KW_this) ||
                            check(TokenKind::KW_class) ||
                            check(TokenKind::KW_function) ||
                            check(TokenKind::KW_null) ||
                            check(TokenKind::KW_true) ||
                            check(TokenKind::KW_false) ||
                            // PrimaryExpression literals also valid per ES262
                            // ClassHeritage : extends LeftHandSideExpression
                            // → PrimaryExpression. Runtime throws TypeError
                            // if not a constructor.
                            check(TokenKind::NumericLiteral) ||
                            check(TokenKind::StringLiteral) ||
                            check(TokenKind::TemplateHead) ||
                            check(TokenKind::NoSubstitutionTemplate) ||
                            check(TokenKind::RegularExpressionLiteral) ||
                            check(TokenKind::BigIntLiteral) ||
                            check(TokenKind::OpenParen) ||
                            check(TokenKind::OpenBracket);
            if (lhsStart) {
                // Best-effort baseClass: leave empty so analyzer treats
                // this as no user-defined base; downstream still registers
                // the class. Parse the full LHS expression to consume tokens.
                auto heritageExpr = parseCallExpression();
                // ECMA-262 ClassHeritage : extends LeftHandSideExpression. An
                // arrow function is an AssignmentExpression, not a LHS, so
                // `extends () => {}` is a SyntaxError (parseCallExpression
                // greedily consumes the `=> body`, so detect it here).
                if (dynamic_cast<ast::ArrowFunction*>(heritageExpr.get())) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: an arrow function is not a valid "
                        "class heritage (extends requires a LeftHandSideExpression)",
                        fileName_, startTok.line));
                }
            }
        }
    }

    // implements
    if (current_.kind == TokenKind::KW_implements) {
        advance();
        do {
            implementsOut.push_back(identifierName());
            // Skip generic type args
            if (check(TokenKind::LessThan)) {
                skipTypeExpression();
            }
        } while (match(TokenKind::Comma));
    }
}

void Parser::parseClassBodyInto(std::vector<ast::NodePtr>& members,
                                bool hasHeritage) {
    // Body. ECMA-262 §10.2.1: ClassBody is always strict-mode code.
    StrictModeGuard sg(this);
    strictMode_ = true;
    // ECMA-262 15.7.1: track whether this class has a ClassHeritage so
    // parseMethodDefinition can validate the constructor's HasDirectSuper
    // invariant. Save+restore to handle nested classes correctly.
    bool prevClassHasHeritage = currentClassHasHeritage_;
    currentClassHasHeritage_ = hasHeritage;
    // ECMA-262 15.7.1 / 15.7.2 AllPrivateIdentifiersValid: push a fresh
    // class scope for collecting #name declarations and references.
    classPrivateScopes_.push_back({});
    expect(TokenKind::OpenBrace, "'{'");
    int constructorCount = 0;
    // ECMA-262 15.7.1 Static Semantics: Early Errors — ClassBody.
    // PrivateBoundNames of ClassBody must not contain duplicate
    // entries unless the name is used exactly once for a getter and
    // once for a setter (and in no other entries). Track each private
    // name across instance/static and method/field/getter/setter
    // distinctions; flag duplicates that violate the get/set
    // exception. Static and instance share the private namespace.
    struct PrivateEntry {
        bool isGetter = false;
        bool isSetter = false;
        bool isOther = false;  // method, field, async, generator, etc.
        bool getterStatic = false;
        bool setterStatic = false;
        int line = 0;
    };
    std::unordered_map<std::string, PrivateEntry> privateNames;
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto member = parseClassMember();
        if (member) {
            // ECMA-262 15.7.1 Static Semantics: Early Errors —
            // ClassBody : ClassElementList. It is a Syntax Error if
            // PrototypePropertyNameList of ClassElementList contains
            // more than one occurrence of "constructor".
            if (auto* m = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (m->name == "constructor" && !m->isStatic && !m->isGetter && !m->isSetter &&
                    m->hasBody) {
                    // TS constructor OVERLOAD SIGNATURES (bodiless) don't
                    // count toward the ES one-constructor early error; only
                    // implementations do.
                    constructorCount++;
                    if (constructorCount > 1) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: A class may only have one constructor",
                            fileName_, m->line));
                    }
                }
                // Private name duplicate check. Restricted to ASCII-
                // only names because the AST stores escape-sequence
                // private names in a normalized form that may not
                // round-trip distinctly across multibyte sequences,
                // causing false-positive duplicates when distinct
                // astral-plane code points collide post-decoding.
                bool nameIsAscii = true;
                for (unsigned char c : m->name) if (c >= 0x80) { nameIsAscii = false; break; }
                if (nameIsAscii && !m->name.empty() && m->name[0] == '#') {
                    auto& e = privateNames[m->name];
                    bool conflict = false;
                    // ECMA-262 15.7.1: a private name may appear twice only as a
                    // getter+setter PAIR, and the two must agree on static-ness
                    // (`get #f(){}` + `static set #f(v){}` is a SyntaxError).
                    if (m->isGetter) {
                        if (e.isGetter || e.isOther) conflict = true;
                        if (e.isSetter && e.setterStatic != m->isStatic) conflict = true;
                        e.isGetter = true;
                        e.getterStatic = m->isStatic;
                    } else if (m->isSetter) {
                        if (e.isSetter || e.isOther) conflict = true;
                        if (e.isGetter && e.getterStatic != m->isStatic) conflict = true;
                        e.isSetter = true;
                        e.setterStatic = m->isStatic;
                    } else {
                        if (e.isGetter || e.isSetter || e.isOther) conflict = true;
                        e.isOther = true;
                    }
                    if (conflict) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: duplicate private name '{}' in class body",
                            fileName_, m->line, m->name));
                    }
                    e.line = m->line;
                }
                // Record in AllPrivateIdentifiersValid scope regardless of
                // ASCII (the duplicate guard above is for cross-encoding
                // safety; resolution should still see the name).
                if (!m->name.empty() && m->name[0] == '#' && !classPrivateScopes_.empty()) {
                    classPrivateScopes_.back().declared.insert(m->name);
                }
            } else if (auto* p = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                bool propIsAscii = true;
                for (unsigned char c : p->name) if (c >= 0x80) { propIsAscii = false; break; }
                if (propIsAscii && !p->name.empty() && p->name[0] == '#') {
                    auto& e = privateNames[p->name];
                    bool conflict = (e.isGetter || e.isSetter || e.isOther);
                    e.isOther = true;
                    if (conflict) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: duplicate private name '{}' in class body",
                            fileName_, p->line, p->name));
                    }
                    e.line = p->line;
                }
                if (!p->name.empty() && p->name[0] == '#' && !classPrivateScopes_.empty()) {
                    classPrivateScopes_.back().declared.insert(p->name);
                }
            }
            members.push_back(std::move(member));
        }
        // Consume trailing semicolons between members
        while (match(TokenKind::Semicolon)) {}
    }
    expect(TokenKind::CloseBrace, "'}'");
    currentClassHasHeritage_ = prevClassHasHeritage;
    // Validate AllPrivateIdentifiersValid for this class body. Pop scope
    // first so unresolved refs can resolve against enclosing class scopes
    // too (PrivateEnvironment chain).
    {
        auto scope = std::move(classPrivateScopes_.back());
        classPrivateScopes_.pop_back();
        for (auto& [name, line] : scope.unresolved) {
            bool resolved = scope.declared.count(name) > 0;
            if (!resolved) {
                for (auto& outer : classPrivateScopes_) {
                    if (outer.declared.count(name)) { resolved = true; break; }
                }
            }
            if (!resolved) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: undeclared private name '{}'",
                    fileName_, line, name));
            }
        }
    }
}

ast::StmtPtr Parser::parseClassDeclaration(bool isAbstract, bool isExported, bool isDefaultExport, bool isStruct) {
    auto startTok = current_;
    // `struct Foo {}` (the "use fast" value type) has no `class` keyword — the
    // caller already consumed the contextual `struct`. `class Foo {}` does.
    if (!isStruct) expect(TokenKind::KW_class, "'class'");

    auto node = std::make_unique<ast::ClassDeclaration>();
    setLocation(node.get(), startTok);
    node->isAbstract = isAbstract;
    node->isExported = isExported;
    node->isDefaultExport = isDefaultExport;
    node->isStruct = isStruct;

    // Name (optional for expressions). Class names are BindingIdentifier
    // and the class body is always strict (ES262 10.2.1), so escape-
    // encoded reserved words including contextual-strict ones (let,
    // static, yield) must be rejected here.
    if (isIdentifierOrKeyword() && !check(TokenKind::KW_extends) && !check(TokenKind::KW_implements) && !check(TokenKind::OpenBrace)) {
        if (current_.escapedReservedWord) {
            // ECMA-262: `await` is NOT strict-reserved; it's reserved only
            // in modules / async function bodies. Class body being strict
            // doesn't make `await` reserved (strict reserves `yield`, not
            // `await`).
            bool isAwaitEscape = current_.decodedText == "await";
            if (!isAwaitEscape || inAsync_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: identifier resolves to reserved word "
                    "via Unicode escape and cannot be used as a class name",
                    fileName_, current_.line));
            }
        }
        // ECMA-262 14.6.1 + 13.1.1: the class BindingIdentifier is evaluated
        // in strict mode (a class body is always strict), so the strict
        // reserved words — let, static, yield, and the future-reserved set —
        // are not valid class names even when the surrounding code is sloppy.
        {
            std::string nm = !current_.decodedText.empty()
                ? current_.decodedText : std::string(current_.text);
            static const std::unordered_set<std::string> kStrictReserved = {
                "let", "static", "yield", "implements", "interface",
                "package", "private", "protected", "public"};
            if (kStrictReserved.count(nm)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' is a reserved word and cannot "
                    "be used as a class name in strict mode",
                    fileName_, current_.line, nm));
            }
            // [+Await] context (async function body / class static block): `await`
            // is reserved and cannot be a BindingIdentifier.
            if (nm == "await" && inAsync_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'await' cannot be used as a class name "
                    "in this context", fileName_, current_.line));
            }
        }
        node->name = identifierName();
        // ECMA-262 13.2.1.1 (Block early error) + 14.1.2: ClassDeclaration
        // contributes to LexicallyDeclaredNames. Track the class name so
        // `{ class f {} class f {} }`, `{ class f {} let f }`,
        // `{ function f() {} class f {} }`, etc. all error per spec.
        // Use PDeclKind::Let since Class is a strict lex declaration (same
        // conflict rules as `let`).
        if (!node->name.empty()) {
            declareLexicalName(node->name, PDeclKind::Let);
        }
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends + implements + body — shared with parseClassExpression so the
    // ECMA-262 15.7.1 early errors are enforced once for both forms.
    bool hasHeritage = false;
    parseClassHeritageClause(node->baseClass, node->implementsInterfaces, hasHeritage);
    parseClassBodyInto(node->members, hasHeritage);
    return node;
}

ast::NodePtr Parser::parseClassMember() {
    auto decorators = parseDecorators();

    // Static block: static { ... }
    if (current_.kind == TokenKind::KW_static) {
        auto saved = saveState();
        advance(); // consume 'static'
        if (check(TokenKind::OpenBrace)) {
            // Check if this is really a static block (not a static method called with computed name)
            auto block = std::make_unique<ast::StaticBlock>();
            setLocation(block.get(), saved.current);
            expect(TokenKind::OpenBrace, "'{'");
            // ECMA-262 15.7: ClassStaticBlock has [HomeObject] = the class,
            // so SuperProperty is allowed inside. SuperCall is not allowed
            // (no [[ConstructorKind]]) but our parser-level check covers
            // only presence/absence of super; the directSuper guard
            // handles that within methods.
            bool prevSuperAllowed = superAllowed_;
            superAllowed_ = true;
            // ECMA-262 15.7: ClassStaticBlockBody is parsed with [Await=true,
            // Yield=false]. `await` is therefore a reserved word inside a
            // static block and cannot be used as IdentifierReference /
            // BindingIdentifier (and `yield` is treated normally per the
            // surrounding scope, but we override to false). The body is
            // also not a function/loop/switch, so iterationDepth_/switchDepth_
            // must be reset so a bare `break;`/`continue;` inside errors.
            // activeLabels_ also doesn't carry across the static-block boundary.
            bool prevAsync = inAsync_;
            bool prevGen = inGenerator_;
            inAsync_ = true;       // await reserved
            inGenerator_ = false;  // yield not reserved (per spec)
            int prevIter = iterationDepth_, prevSwitch = switchDepth_;
            iterationDepth_ = 0;
            switchDepth_ = 0;
            // ECMA-262 15.7: a static block is its own function-like
            // boundary — a bare `return` directly inside it is a
            // SyntaxError even when the class sits inside a function. Reset
            // functionDepth_ so the return-at-top-level check fires (a
            // nested function re-increments it, so its returns still work).
            int prevFuncDepth = functionDepth_;
            functionDepth_ = 0;
            std::vector<ActiveLabel> savedLabels;
            savedLabels.swap(activeLabels_);
            // try/catch to guarantee swap-back of activeLabels_ even if
            // a contained parse throws. parseLabeledOrExpressionStatement's
            // own try/catch does `activeLabels_.pop_back()` during unwind;
            // if activeLabels_ is still empty (the swapped-out state) at
            // that moment, the pop_back is undefined behavior. Swapping
            // back here keeps activeLabels_ matching the outer scope's
            // view at every catch site upstream.
            // ECMA-262 15.7.1: the ClassStaticBlockBody is its own lexical scope;
            // duplicate let/const and let-vs-var conflicts are early errors.
            pushLexicalScope();
            try {
                while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
                    auto stmt = parseDeclarationOrStatement();
                    if (stmt) block->body.push_back(std::move(stmt));
                }
            } catch (...) {
                popLexicalScope();
                activeLabels_.swap(savedLabels);
                iterationDepth_ = prevIter;
                switchDepth_ = prevSwitch;
                functionDepth_ = prevFuncDepth;
                inAsync_ = prevAsync;
                inGenerator_ = prevGen;
                superAllowed_ = prevSuperAllowed;
                throw;
            }
            popLexicalScope();
            // ECMA-262 15.7.1: ClassStaticBlockStatementList may not Contain
            // `arguments` (no arguments object), a SuperCall (no
            // [[ConstructorKind]]), or an AwaitExpression. Nested non-arrow
            // functions/methods have their own bindings and are skipped by the
            // walk; for arguments/super an arrow inherits (recurse), for await
            // an arrow is a boundary (await-mode pass below).
            for (auto& s : block->body) {
                int v = containsArgumentsOrSuperCall(s.get());
                if (v == FIELD_INIT_ARGUMENTS) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'arguments' is not allowed in a "
                        "class static initialization block", fileName_, block->line));
                }
                if (v == FIELD_INIT_SUPER_CALL) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: a super() call is not allowed in a "
                        "class static initialization block", fileName_, block->line));
                }
                g_walkAwaitMode = true;
                int aw = containsArgumentsOrSuperCall(s.get());
                g_walkAwaitMode = false;
                if (aw == FIELD_INIT_AWAIT) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: an await expression is not allowed in "
                        "a class static initialization block", fileName_, block->line));
                }
            }
            activeLabels_.swap(savedLabels);
            iterationDepth_ = prevIter;
            switchDepth_ = prevSwitch;
            functionDepth_ = prevFuncDepth;
            inAsync_ = prevAsync;
            inGenerator_ = prevGen;
            superAllowed_ = prevSuperAllowed;
            expect(TokenKind::CloseBrace, "'}'");
            return block;
        }
        restoreState(saved);
    }

    // Parse modifiers
    bool isStatic = false;
    bool isAbstract = false;
    bool isAsync = false;
    bool isGenerator = false;
    bool isGetter = false;
    bool isSetter = false;
    bool isReadonly = false;
    bool isOverride = false;
    ts::AccessModifier access = ts::AccessModifier::Public;

    // Access modifier
    if (current_.kind == TokenKind::KW_public) { access = ts::AccessModifier::Public; advance(); }
    else if (current_.kind == TokenKind::KW_private) { access = ts::AccessModifier::Private; advance(); }
    else if (current_.kind == TokenKind::KW_protected) { access = ts::AccessModifier::Protected; advance(); }

    // static
    if (current_.kind == TokenKind::KW_static) {
        isStatic = true;
        advance();
    }

    // abstract
    if (current_.kind == TokenKind::KW_abstract) {
        isAbstract = true;
        advance();
    }

    // override
    if (current_.kind == TokenKind::KW_override) {
        isOverride = true;
        advance();
    }

    // readonly
    if (current_.kind == TokenKind::KW_readonly) {
        isReadonly = true;
        advance();
    }

    // TS 4.9 auto-accessor: `accessor x = 1`. Contextual keyword -- only a
    // modifier when a property name follows on the same line. Lowered as a
    // plain field (permissive: the get/set-pair-over-private-backing
    // distinction is only observable via decorators/inheritance tricks).
    if (current_.kind == TokenKind::Identifier && current_.text == "accessor") {
        auto saved = saveState();
        advance();
        bool nameFollows = !current_.hadNewlineBefore &&
            (isIdentifierOrKeyword() || check(TokenKind::OpenBracket) ||
             check(TokenKind::Hash) || check(TokenKind::StringLiteral) ||
             check(TokenKind::NumericLiteral) || check(TokenKind::BigIntLiteral));
        if (!nameFollows) restoreState(saved);
    }

    // async
    // In class bodies, async is always a method modifier (no ASI concern like in expressions)
    if (current_.kind == TokenKind::KW_async) {
        auto saved = saveState();
        advance();
        // If followed by a property name start, it's an async method.
        // PropertyName includes Identifier/keyword (treated as name), `[`
        // (ComputedPropertyName), `#` (PrivateName), and ECMA-262 literal
        // PropertyName variants: StringLiteral, NumericLiteral,
        // BigIntLiteral.
        if (isIdentifierOrKeyword() || check(TokenKind::Star) ||
            check(TokenKind::OpenBracket) || check(TokenKind::Hash) ||
            check(TokenKind::StringLiteral) || check(TokenKind::NumericLiteral) ||
            check(TokenKind::BigIntLiteral)) {
            isAsync = true;
        } else {
            restoreState(saved);
        }
    }

    // generator
    if (match(TokenKind::Star)) {
        isGenerator = true;
    }

    // getter/setter
    if (current_.kind == TokenKind::KW_get || current_.kind == TokenKind::KW_set) {
        auto saved = saveState();
        bool isGet = current_.kind == TokenKind::KW_get;
        advance();
        // Only treat as getter/setter if followed by a property name
        // (Identifier/keyword/CPN/StringLit/NumericLit/BigIntLit/PrivateName).
        if (isIdentifierOrKeyword() || check(TokenKind::OpenBracket) ||
            check(TokenKind::StringLiteral) || check(TokenKind::NumericLiteral) ||
            check(TokenKind::BigIntLiteral) || check(TokenKind::Hash)) {
            if (isGet) isGetter = true;
            else isSetter = true;
        } else {
            restoreState(saved);
        }
    }

    // Member name
    std::string name;
    ast::NodePtr nameNode;

    if (check(TokenKind::OpenBracket)) {
        // Could be index signature [key: type]: valueType; or computed property name
        auto saved = saveState();
        advance(); // [
        if (isIdentifierOrKeyword()) {
            auto savedInner = saveState();
            std::string keyName = identifierName();
            if (check(TokenKind::Colon)) {
                // Index signature: [key: type]: valueType;
                advance(); // :
                std::string keyType = scanTypeExpression();
                expect(TokenKind::CloseBracket, "']'");
                if (check(TokenKind::Colon)) {
                    advance(); // :
                    std::string valueType = scanTypeExpression();
                    auto idx = std::make_unique<ast::IndexSignature>();
                    idx->keyType = keyType;
                    idx->valueType = valueType;
                    match(TokenKind::Semicolon);
                    return idx;
                }
                // Not an index signature after all, restore
                restoreState(saved);
            } else {
                restoreState(saved);
            }
        } else {
            restoreState(saved);
        }

        if (check(TokenKind::OpenBracket)) {
            // Computed property name — ECMA-262 14.5: ComputedPropertyName is
            // [ AssignmentExpression[+In] ], so `in` is always allowed inside.
            advance(); // [
            auto cpn = std::make_unique<ast::ComputedPropertyName>();
            setLocation(cpn.get(), previous_);
            bool prevNoIn = noIn_;
            noIn_ = false;
            cpn->expression = parseAssignmentExpression();
            noIn_ = prevNoIn;
            expect(TokenKind::CloseBracket, "']'");
            name = "[computed]";
            nameNode = std::move(cpn);
        }
    } else if (check(TokenKind::Hash)) {
        // Private field/method
        advance(); // #
        // ECMA-262: PrivateName is a single token "#IdentifierName" — the #
        // and the identifier are scanned together, with NO whitespace,
        // line terminator, or comment between them.
        if (current_.offset != previous_.offset + 1) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: no whitespace or line terminator allowed between '#' and identifier",
                previous_.line, previous_.column));
        }
        name = "#" + identifierName();
        // ECMA-262 15.7.1: It is a Syntax Error if StringValue of a
        // PrivateName declared in a class body is "#constructor".
        if (name == "#constructor") {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: classes may not have a private name "
                "'#constructor'",
                fileName_, previous_.line));
        }
        auto id = std::make_unique<ast::Identifier>();
        id->name = name;
        id->isPrivate = true;
        nameNode = std::move(id);
    } else if (check(TokenKind::StringLiteral)) {
        name = Lexer::getStringValue(current_.text);
        auto lit = std::make_unique<ast::StringLiteral>();
        lit->value = name;
        nameNode = std::move(lit);
        advance();
    } else if (check(TokenKind::NumericLiteral)) {
        name = canonicalNumericPropertyName(current_.text);
        advance();
    } else if (check(TokenKind::BigIntLiteral)) {
        // ECMA-262: BigIntLiteral as PropertyName -> its decimal-string form.
        std::string lex(current_.text);
        if (!lex.empty() && lex.back() == 'n') lex.pop_back();
        name = lex;
        advance();
    } else if (current_.kind == TokenKind::KW_constructor) {
        name = "constructor";
        advance();
    } else if (isIdentifierOrKeyword()) {
        name = identifierName();
    } else {
        // Unknown member, skip to next
        advance();
        return nullptr;
    }

    // Is it a method (has parentheses)?
    if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
        // ECMA-262 15.7.1 Static Semantics: Early Errors for ClassElement
        //   - It is a Syntax Error if PropName is "constructor" and
        //     SpecialMethod is true (async / generator / async-generator /
        //     get / set).
        //   - It is a Syntax Error if PropName of `static MethodDefinition`
        //     is "prototype".
        // (Class-only — object-literal setters/getters can be named anything.)
        bool isSpecial = isAsync || isGenerator || isGetter || isSetter;
        if (!isStatic && name == "constructor" && isSpecial) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: class constructor cannot be a "
                "generator, async, getter, or setter",
                current_.line, current_.column));
        }
        if (isStatic && name == "prototype") {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: class static method cannot be "
                "named 'prototype'",
                current_.line, current_.column));
        }
        auto method = parseMethodDefinition(name, std::move(nameNode), isStatic, isAbstract,
                                             isAsync, isGenerator, isGetter, isSetter, access, std::move(decorators));
        return method;
    }

    // Property definition
    auto prop = std::make_unique<ast::PropertyDefinition>();
    setLocation(prop.get(), current_);
    prop->name = name;
    prop->nameNode = std::move(nameNode);  // ComputedPropertyName when name=="[computed]"
    prop->access = access;
    prop->isStatic = isStatic;
    prop->isReadonly = isReadonly;
    prop->decorators = std::move(decorators);

    // ECMA-262 15.7.1:
    //   - It is a Syntax Error if PropName of FieldDefinition is
    //     "constructor" (a class field cannot be named "constructor").
    //   - It is a Syntax Error if PropName of `static FieldDefinition`
    //     is "prototype".
    if (name == "constructor") {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: class field cannot be named 'constructor'",
            prop->line, prop->column));
    }
    if (isStatic && name == "prototype") {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: class static field cannot be named 'prototype'",
            prop->line, prop->column));
    }

    // Optional marker
    if (match(TokenKind::QuestionMark)) { prop->isOptional = true; }
    // Definite assignment assertion
    if (match(TokenKind::ExclamationMark)) {}

    // Type annotation
    if (check(TokenKind::Colon)) {
        prop->type = parseTypeAnnotation();
    }

    // Initializer
    if (match(TokenKind::Equals)) {
        // ECMA-262: class field initializers have [HomeObject] so
        // SuperProperty access is allowed. SuperCall is still rejected
        // by HasDirectSuper in parseMethodDefinition; field initializers
        // aren't method definitions, so super(...) here would only be
        // caught at runtime — which is consistent with spec for the
        // common cases we hit.
        bool prevSuperAllowed = superAllowed_;
        superAllowed_ = true;
        prop->initializer = parseAssignmentExpression();
        superAllowed_ = prevSuperAllowed;
        // ECMA-262 15.7.1: It is a Syntax Error if Initializer is present and
        //   - ContainsArguments of Initializer is true, or
        //   - Initializer Contains SuperCall is true.
        int err = containsArgumentsOrSuperCall(prop->initializer.get());
        if (err == FIELD_INIT_ARGUMENTS) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'arguments' is not allowed in class field initializer",
                prop->initializer->line, prop->initializer->column));
        }
        if (err == FIELD_INIT_SUPER_CALL) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'super(...)' call is not allowed in class field initializer",
                prop->initializer->line, prop->initializer->column));
        }
    }

    expectSemicolon();
    return prop;
}

std::unique_ptr<ast::MethodDefinition> Parser::parseMethodDefinition(
    const std::string& name, ast::NodePtr nameNode,
    bool isStatic, bool isAbstract, bool isAsync, bool isGenerator,
    bool isGetter, bool isSetter, ts::AccessModifier access,
    std::vector<ast::Decorator> decorators) {

    auto method = std::make_unique<ast::MethodDefinition>();
    setLocation(method.get(), previous_);
    method->name = name;
    method->nameNode = std::move(nameNode);
    method->isStatic = isStatic;
    method->isAbstract = isAbstract;
    method->isAsync = isAsync;
    method->isGenerator = isGenerator;
    method->isGetter = isGetter;
    method->isSetter = isSetter;
    method->access = access;
    method->decorators = std::move(decorators);

    // ECMA-262 15.7.1: HasDirectSuper scoped to this MethodDefinition.
    // Save+reset BEFORE parameter parsing so super(...) in default-value
    // expressions of formal params is counted too (and not attributed to
    // an outer scope). Body and validation handled in the body branch.
    int prevDirectSuper = directSuperCount_;
    directSuperCount_ = 0;
    // ECMA-262 13.3.7.1: MethodDefinition body has [HomeObject], so
    // SuperReference is allowed in both class methods and object literal
    // methods. (parseClassDeclaration restricts ClassHeritage via
    // currentClassHasHeritage_ to gate super(...) by isCtor-of-derived.)
    bool prevSuperAllowed = superAllowed_;
    superAllowed_ = true;
    // ECMA-262: the parameter list of an AsyncMethod / GeneratorMethod /
    // AsyncGeneratorMethod is parsed under that method's [Await] / [Yield]
    // flags. So `async foo(x = await)` / `async foo(await)` /
    // `*foo(yield)` must see `await`/`yield` as reserved during the
    // parameter parse, not as plain identifiers. Set the flags here
    // (before parseParameterList) and restore at function end. Matches
    // parseFunctionDeclaration's flag handling.
    bool prevAsyncOuter = inAsync_;
    bool prevGenOuter = inGenerator_;
    inAsync_ = method->isAsync;
    inGenerator_ = method->isGenerator;

    // Type parameters
    method->typeParameters = parseTypeParameterList();

    // Parameters — MethodDefinition has UniqueFormalParameters, so
    // duplicate bound names are a SyntaxError even with a simple list in
    // sloppy mode (`({ foo(a, a) {} })`).
    method->parameters = parseParameterList(/*checkDuplicates=*/true,
                                            /*uniqueParams=*/true);

    // ECMA-262 15.4.1 (accessor MethodDefinition early errors): a getter's
    // formal-parameter list must be empty (`get x()`); a setter's must be a
    // single non-rest BindingElement (`set x(v)` — `set x()`, `set x(a,b)`,
    // `set x(...a)` are all SyntaxErrors). A leading TypeScript `this:`
    // parameter is a type annotation, not a runtime parameter, so it does not
    // count. Applies to both class bodies and object literals (both route
    // through parseMethodDefinition).
    if (isGetter || isSetter) {
        size_t paramCount = 0;
        bool hasRest = false;
        for (auto& p : method->parameters) {
            if (p->isThisParameter) continue;
            paramCount++;
            if (p->isRest) hasRest = true;
        }
        if (isGetter && paramCount != 0) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: Getter must not have any formal parameters",
                fileName_, method->line));
        }
        if (isSetter && (paramCount != 1 || hasRest)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: Setter must have exactly one formal parameter",
                fileName_, method->line));
        }
    }

    // Return type
    if (check(TokenKind::Colon)) {
        method->returnType = parseReturnTypeAnnotation();
    }

    // Body (or abstract/declaration without body). Methods are nested
    // inside class bodies which are already strict; the guard simply
    // preserves the parent's mode while still allowing a redundant
    // "use strict" directive in the method body itself.
    if (check(TokenKind::OpenBrace)) {
        method->hasBody = true;
        bool prevAsync = inAsync_;
        bool prevGen = inGenerator_;
        StrictModeGuard sg(this);
        inAsync_ = method->isAsync;
        inGenerator_ = method->isGenerator;
        functionDepth_++;
        nonArrowFunctionDepth_++;
        int prevIter = iterationDepth_, prevSwitch = switchDepth_;
        iterationDepth_ = 0; switchDepth_ = 0;
        // ECMA-262 8.6: label scope does not cross function/method bodies.
        std::vector<ActiveLabel> savedLabels;
        savedLabels.swap(activeLabels_);
        bool prevSawUseStrict = sawUseStrictDirective_;
        sawUseStrictDirective_ = false;

        expect(TokenKind::OpenBrace, "'{'");
        // Method body has its own LexicalDeclarations scope (same rationale
        // as parseFunctionDeclaration). Without this, `let X` in sibling
        // method bodies of an object literal or class mistakenly conflict.
        pushLexicalScope();
        predeclareFormalParamsAsVar(method->parameters);  // body let/const must not duplicate a param
        bool prevInParamDefault = inParamDefault_; inParamDefault_ = false;
        pendingPrologueStrings_.clear();
        bool inPrologue = true;
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) {
                if (inPrologue && !processPrologueDirective(stmt)) {
                    inPrologue = false;
                }
                method->body.push_back(std::move(stmt));
            }
        }
        popLexicalScope();
        inParamDefault_ = prevInParamDefault;
        expect(TokenKind::CloseBrace, "'}'");

        if (sawUseStrictDirective_) {
            if (!isParameterListSimple(method->parameters)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: method with non-simple parameter list may not "
                    "declare \"use strict\"",
                    current_.line, current_.column));
            }
            // ECMA-262 15.2/16.2.4: a "use strict" directive retroactively
            // forbids `eval`/`arguments` as parameter names — `set x(eval){
            // "use strict"; }` is a SyntaxError.
            std::vector<std::pair<std::string,int>> pnames;
            for (auto& p : method->parameters)
                if (p) collectBoundIdentNames(p->name.get(), pnames);
            for (auto& pr : pnames)
                if (pr.first == "eval" || pr.first == "arguments")
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' may not be a parameter name "
                        "when the body declares \"use strict\"",
                        fileName_, pr.second, pr.first));
        }
        sawUseStrictDirective_ = prevSawUseStrict;

        functionDepth_--;
        nonArrowFunctionDepth_--;
        iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
        activeLabels_.swap(savedLabels);
        inAsync_ = prevAsync;
        inGenerator_ = prevGen;
    } else {
        method->hasBody = false;
        expectSemicolon();
    }

    // ECMA-262 15.7.1 Static Semantics: Early Errors —
    //   ClassElement : MethodDefinition
    //     It is a Syntax Error if PropName of MethodDefinition is not
    //     "constructor" and HasDirectSuper of MethodDefinition is true.
    //   ClassTail : ClassHeritage_opt { ClassBody }
    //     If ClassHeritage is not present and HasDirectSuper of the
    //     constructor is true, it is a Syntax Error.
    // Also forbids super(...) in PropertyDefinition MethodDefinition
    // (object literal methods) since they have no [[ConstructorKind]].
    // Run after both params and body so super(...) in either contributes.
    if (directSuperCount_ > 0) {
        bool isCtor = (method->name == "constructor") &&
                      !method->isStatic && !method->isGetter &&
                      !method->isSetter && !method->isAsync &&
                      !method->isGenerator;
        if (!isCtor) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'super' call is only allowed in a "
                "derived class constructor",
                fileName_, method->line));
        }
        if (!currentClassHasHeritage_) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'super' call is only allowed in a "
                "class that extends another class",
                fileName_, method->line));
        }
    }
    directSuperCount_ = prevDirectSuper;
    superAllowed_ = prevSuperAllowed;
    inAsync_ = prevAsyncOuter;
    inGenerator_ = prevGenOuter;

    return method;
}

ast::StmtPtr Parser::parseIfStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_if, "'if'");
    expect(TokenKind::OpenParen, "'('");

    auto node = std::make_unique<ast::IfStatement>();
    setLocation(node.get(), startTok);

    node->condition = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    // Annex B.3.2: a FunctionDeclaration in IfStatement body position is
    // implicitly wrapped in a Block — give it its own lexical scope so a
    // \`let X = ...; if (true) function X() {}\` doesn't trigger an outer-
    // scope redeclaration error. We push a scope for any thenStatement
    // that's a FunctionDeclaration; the same applies to elseStatement.
    if (current_.kind == TokenKind::KW_function) {
        pushLexicalScope();
        node->thenStatement = parseStatementOnly(/*allowAnnexBFunction=*/true);
        popLexicalScope();
    } else {
        // Non-direct-function body: a labelled function here is forbidden
        // (IsLabelledFunction early error), though a direct function above is OK.
        bool prevLBF = labelBodyForbidsFunction_;
        labelBodyForbidsFunction_ = true;
        node->thenStatement = parseStatementOnly(/*allowAnnexBFunction=*/true);
        labelBodyForbidsFunction_ = prevLBF;
    }

    if (match(TokenKind::KW_else)) {
        if (current_.kind == TokenKind::KW_function) {
            pushLexicalScope();
            node->elseStatement = parseStatementOnly(/*allowAnnexBFunction=*/true);
            popLexicalScope();
        } else {
            bool prevLBF = labelBodyForbidsFunction_;
            labelBodyForbidsFunction_ = true;
            node->elseStatement = parseStatementOnly(/*allowAnnexBFunction=*/true);
            labelBodyForbidsFunction_ = prevLBF;
        }
    }

    return node;
}

ast::StmtPtr Parser::parseWhileStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_while, "'while'");
    expect(TokenKind::OpenParen, "'('");

    auto node = std::make_unique<ast::WhileStatement>();
    setLocation(node.get(), startTok);

    node->condition = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    node->body = parseLoopBody();

    return node;
}

ast::StmtPtr Parser::parseDoWhileStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_do, "'do'");

    auto node = std::make_unique<ast::WhileStatement>();
    setLocation(node.get(), startTok);
    node->isDoWhile = true;

    node->body = parseLoopBody();
    expect(TokenKind::KW_while, "'while'");
    expect(TokenKind::OpenParen, "'('");
    node->condition = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    // ECMA-262 13.7.2: the trailing `;` of a do-while is auto-inserted
    // unconditionally — it's optional regardless of LineTerminator. So
    // `do break; while (0) x = 42;` parses as do-while followed by `x =
    // 42;`. Use match() rather than expectSemicolon() (which would only
    // ASI-insert across a newline).
    match(TokenKind::Semicolon);

    return node;
}

ast::StmtPtr Parser::parseForStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_for, "'for'");

    // for await (
    bool isAwait = false;
    if (current_.kind == TokenKind::KW_await) {
        isAwait = true;
        advance();
    }

    expect(TokenKind::OpenParen, "'('");

    // =====================================================================
    // Three variants:
    //   for (initializer; condition; incrementor) body
    //   for (variable in expression) body
    //   for (variable of expression) body
    // =====================================================================

    // --- Empty initializer: for (;;) ---
    if (check(TokenKind::Semicolon)) {
        advance(); // consume first ;
        ast::ExprPtr condition;
        if (!check(TokenKind::Semicolon)) {
            condition = parseExpression();
        }
        expect(TokenKind::Semicolon, "';'");
        ast::ExprPtr incrementor;
        if (!check(TokenKind::CloseParen)) {
            incrementor = parseExpression();
        }
        expect(TokenKind::CloseParen, "')'");

        auto node = std::make_unique<ast::ForStatement>();
        setLocation(node.get(), startTok);
        node->body = parseLoopBody();
        node->condition = std::move(condition);
        node->incrementor = std::move(incrementor);
        return node;
    }

    // --- Variable declaration initializer: for (var/let/const ...) ---
    // ES262 13.7.4: in `for (` context, `let` followed by anything other
    // than `[` or BindingIdentifier is interpreted as IdentifierReference
    // (per the Lookahead restriction). Specifically, `for (let = 3; ;)`
    // and `for (let; ;)` and `for ([let][0]; ;)` are valid in non-strict.
    // Speculatively probe: if KW_let is followed by `=`, `;`, `,`, `)`,
    // `.`, `[` (member access — not `[` binding pattern though), etc.,
    // it's the Identifier path. Strict mode forbids `let` as identifier
    // entirely so this only matters non-strict.
    bool isLetAsIdent = false;
    if (current_.kind == TokenKind::KW_let && !strictMode_) {
        // Look at next token. Per ES262 13.7.4, in `for (`, `let X` /
        // `let [` / `let {` is a LexicalDeclaration; anything else
        // (`let =`, `let ;`, `let .`, `let in`, `let of`) means `let`
        // is an IdentifierReference.
        // Special case: `for (let of [])` — spec disallows `let` as
        // LHS in for-of (lookahead ≠ let). We treat it as identifier
        // and emit the spec SyntaxError later.
        auto saved = saveState();
        advance();
        TokenKind k = current_.kind;
        bool looksLikeDecl =
            k == TokenKind::Identifier ||
            k == TokenKind::OpenBracket ||
            k == TokenKind::OpenBrace ||
            k == TokenKind::KW_async || k == TokenKind::KW_await ||
            k == TokenKind::KW_yield || k == TokenKind::KW_from ||
            k == TokenKind::KW_as || k == TokenKind::KW_get ||
            k == TokenKind::KW_set || k == TokenKind::KW_static ||
            k == TokenKind::KW_type || k == TokenKind::KW_module ||
            k == TokenKind::KW_namespace || k == TokenKind::KW_interface ||
            k == TokenKind::KW_declare || k == TokenKind::KW_abstract ||
            k == TokenKind::KW_readonly || k == TokenKind::KW_implements ||
            k == TokenKind::KW_public || k == TokenKind::KW_private ||
            k == TokenKind::KW_protected ||
            k == TokenKind::KW_constructor || k == TokenKind::KW_keyof ||
            k == TokenKind::KW_infer || k == TokenKind::KW_asserts ||
            k == TokenKind::KW_satisfies || k == TokenKind::KW_undefined;
        // NOTE: KW_of intentionally not in the list — `for (let of [])`
        // is a spec parse error (`let` cannot be LHS of for-of).
        bool letFollowedByOf = (k == TokenKind::KW_of);
        restoreState(saved);
        if (letFollowedByOf) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'let' cannot be the LeftHandSide of "
                "a for-of loop (ES262 13.7.5)",
                fileName_, current_.line));
        }
        isLetAsIdent = !looksLikeDecl;
    }

    if (!isLetAsIdent &&
        (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
         current_.kind == TokenKind::KW_const)) {
        auto kwTok = current_;
        advance(); // consume var/let/const

        // Parse first (and possibly only) declaration
        auto firstDecl = std::make_unique<ast::VariableDeclaration>();
        setLocation(firstDecl.get(), current_);
        if (kwTok.kind == TokenKind::KW_let) firstDecl->varKind = ast::VarKind::Let;
        else if (kwTok.kind == TokenKind::KW_const) firstDecl->varKind = ast::VarKind::Const;
        firstDecl->name = parseBindingNameOrPattern();
        // ECMA-262 13.3.1.1 / 14.7.5.1: BoundNames of LexicalDeclaration
        // may not contain "let", and the BoundNames of a ForDeclaration
        // must not contain duplicates (`for (const [x, x] of arr)`).
        if (kwTok.kind == TokenKind::KW_let || kwTok.kind == TokenKind::KW_const) {
            std::vector<std::pair<std::string, int>> names;
            collectBoundIdentNames(firstDecl->name.get(), names);
            std::unordered_map<std::string, int> seen;
            for (auto& [nm, ln] : names) {
                if (nm == "let") {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'let' is not a valid binding "
                        "identifier in let/const declarations",
                        fileName_, ln));
                }
                if (seen.count(nm)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: duplicate binding name '{}' in "
                        "ForDeclaration",
                        fileName_, ln, nm));
                }
                seen[nm] = ln;
            }
        }
        if (check(TokenKind::Colon)) {
            firstDecl->type = parseTypeAnnotation();
        }

        // Check for for-of: for (const x of iterable)
        if (current_.kind == TokenKind::KW_of) {
            advance(); // consume 'of'
            auto iterable = parseAssignmentExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForOfStatement>();
            setLocation(node.get(), startTok);
            node->isAwait = isAwait;
            node->initializer = std::move(firstDecl);
            node->expression = std::move(iterable);
            node->body = parseLoopBody();
            checkForHeadLexicalVsBodyVar(node->initializer.get(), node->body.get());
            return node;
        }

        // Check for for-in: for (const x in object)
        if (current_.kind == TokenKind::KW_in) {
            advance(); // consume 'in'
            auto iterable = parseExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForInStatement>();
            setLocation(node.get(), startTok);
            node->initializer = std::move(firstDecl);
            node->expression = std::move(iterable);
            node->body = parseLoopBody();
            checkForHeadLexicalVsBodyVar(node->initializer.get(), node->body.get());
            return node;
        }

        // Regular for loop: for (let i = 0; ...) or for (let i = 0, j = 0; ...)
        if (match(TokenKind::Equals)) {
            // Annex B.3.5 (`for (var X = init in obj)`): permitted in
            // non-strict scripts only, var-only, single binding,
            // BindingIdentifier-only (NOT array/object patterns —
            // `for (var [a] = init in obj)` is SyntaxError per spec).
            // Suppress `in` as a binary operator while parsing the
            // initializer so the trailing `in obj` belongs to the
            // for-in head.
            bool isBindingIdent =
                dynamic_cast<ast::Identifier*>(firstDecl->name.get()) != nullptr;
            bool eligible = !strictMode_ && kwTok.kind == TokenKind::KW_var &&
                            isBindingIdent;
            bool prevNoIn = noIn_;
            if (eligible) noIn_ = true;
            firstDecl->initializer = parseAssignmentExpression();
            noIn_ = prevNoIn;

            if (eligible && current_.kind == TokenKind::KW_in) {
                advance();  // consume 'in'
                auto iterable = parseExpression();
                expect(TokenKind::CloseParen, "')'");
                auto node = std::make_unique<ast::ForInStatement>();
                setLocation(node.get(), startTok);
                node->initializer = std::move(firstDecl);
                node->expression = std::move(iterable);
                node->body = parseLoopBody();
                return node;
            }
        }

        // Collect into a list (may have multiple: for (let i = 0, j = 10; ...))
        std::vector<ast::StmtPtr> decls;
        decls.push_back(std::move(firstDecl));
        while (match(TokenKind::Comma)) {
            auto decl = std::make_unique<ast::VariableDeclaration>();
            setLocation(decl.get(), current_);
            if (kwTok.kind == TokenKind::KW_let) decl->varKind = ast::VarKind::Let;
            else if (kwTok.kind == TokenKind::KW_const) decl->varKind = ast::VarKind::Const;
            decl->name = parseBindingNameOrPattern();
            if (check(TokenKind::Colon)) {
                decl->type = parseTypeAnnotation();
            }
            if (match(TokenKind::Equals)) {
                decl->initializer = parseAssignmentExpression();
            }
            decls.push_back(std::move(decl));
        }

        expect(TokenKind::Semicolon, "';'");

        ast::ExprPtr condition;
        if (!check(TokenKind::Semicolon)) {
            condition = parseExpression();
        }
        expect(TokenKind::Semicolon, "';'");
        ast::ExprPtr incrementor;
        if (!check(TokenKind::CloseParen)) {
            incrementor = parseExpression();
        }
        expect(TokenKind::CloseParen, "')'");

        ast::StmtPtr initializer;
        if (decls.size() == 1) {
            initializer = std::move(decls[0]);
        } else {
            auto block = std::make_unique<ast::BlockStatement>();
            setLocation(block.get(), kwTok);
            block->isSynthetic = true;
            for (auto& d : decls) block->statements.push_back(std::move(d));
            initializer = std::move(block);
        }

        auto node = std::make_unique<ast::ForStatement>();
        setLocation(node.get(), startTok);
        node->initializer = std::move(initializer);
        node->condition = std::move(condition);
        node->incrementor = std::move(incrementor);
        node->body = parseLoopBody();
        checkForHeadLexicalVsBodyVar(node->initializer.get(), node->body.get());
        return node;
    }

    // --- Expression initializer: for (expr; ...) or for (x in obj) or for (x of arr) ---
    {
        // Suppress 'in' as binary operator so 'for (x in obj)' doesn't parse as
        // a binary expression 'x in obj'
        bool prevNoIn = noIn_;
        noIn_ = true;
        // The for-(of|in) head is a pattern-candidate position: defer any
        // CoverInitializedName so `for ({a=1} of it)` isn't rejected before
        // validateAssignmentTarget refines the head.
        bool prevCoverCandidate = inCoverCandidate_;
        inCoverCandidate_ = true;
        auto expr = parseAssignmentExpression();
        inCoverCandidate_ = prevCoverCandidate;
        noIn_ = prevNoIn;

        // Check for for-of: for (x of iterable)
        if (current_.kind == TokenKind::KW_of) {
            // ECMA-262 14.7.5: `for ( [lookahead ≠ async of] LHS of ...)`. A bare
            // `async` as a (non-await) for-of head is a SyntaxError (disambiguates
            // from `for await`). `for ((async) of …)` and `for await (async of …)`
            // are unaffected.
            if (!isAwait) {
                auto* id = dynamic_cast<ast::Identifier*>(expr.get());
                if (id && id->name == "async" && !expr->parenthesized) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'async' is not allowed as the "
                        "left-hand side of a for-of loop", fileName_, expr->line));
                }
            }
            // ECMA-262 14.7.5.1: the head LHS must be a valid assignment
            // target / destructuring pattern (`for (this of [])`,
            // `for ([...x, y] of [[]])`, strict `for ({eval} of ...)` are
            // all early errors).
            validateAssignmentTarget(expr.get(), /*forCompoundAssign=*/false);
            advance(); // consume 'of'
            auto iterable = parseAssignmentExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForOfStatement>();
            setLocation(node.get(), startTok);
            node->isAwait = isAwait;
            auto es = std::make_unique<ast::ExpressionStatement>();
            setLocation(es.get(), expr->line, expr->column);
            es->expression = std::move(expr);
            node->initializer = std::move(es);
            node->expression = std::move(iterable);
            node->body = parseLoopBody();
            return node;
        }

        // Check for for-in: for (x in obj)
        if (current_.kind == TokenKind::KW_in) {
            // ECMA-262 14.7.5.1: same head-LHS validity rules as for-of.
            validateAssignmentTarget(expr.get(), /*forCompoundAssign=*/false);
            advance(); // consume 'in'
            auto iterable = parseExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForInStatement>();
            setLocation(node.get(), startTok);
            auto es = std::make_unique<ast::ExpressionStatement>();
            setLocation(es.get(), expr->line, expr->column);
            es->expression = std::move(expr);
            node->initializer = std::move(es);
            node->expression = std::move(iterable);
            node->body = parseLoopBody();
            return node;
        }

        // Regular for loop with expression initializer
        // May have comma-separated expressions
        if (match(TokenKind::Comma)) {
            // Multiple expressions in initializer: for (i = 0, j = 0; ...)
            auto bin = std::make_unique<ast::BinaryExpression>();
            setLocation(bin.get(), expr->line, expr->column);
            bin->op = ",";
            bin->left = std::move(expr);
            noIn_ = true;
            bin->right = parseAssignmentExpression();
            noIn_ = prevNoIn;
            while (match(TokenKind::Comma)) {
                auto outer = std::make_unique<ast::BinaryExpression>();
                setLocation(outer.get(), bin->line, bin->column);
                outer->op = ",";
                outer->left = std::move(bin);
                noIn_ = true;
                outer->right = parseAssignmentExpression();
                noIn_ = prevNoIn;
                bin = std::move(outer);
            }
            expr = std::move(bin);
        }

        expect(TokenKind::Semicolon, "';'");

        auto es = std::make_unique<ast::ExpressionStatement>();
        setLocation(es.get(), expr->line, expr->column);
        es->expression = std::move(expr);

        ast::ExprPtr condition;
        if (!check(TokenKind::Semicolon)) {
            condition = parseExpression();
        }
        expect(TokenKind::Semicolon, "';'");
        ast::ExprPtr incrementor;
        if (!check(TokenKind::CloseParen)) {
            incrementor = parseExpression();
        }
        expect(TokenKind::CloseParen, "')'");

        auto node = std::make_unique<ast::ForStatement>();
        setLocation(node.get(), startTok);
        node->initializer = std::move(es);
        node->condition = std::move(condition);
        node->incrementor = std::move(incrementor);
        node->body = parseLoopBody();
        return node;
    }
}

ast::StmtPtr Parser::parseSwitchStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_switch, "'switch'");
    expect(TokenKind::OpenParen, "'('");

    auto node = std::make_unique<ast::SwitchStatement>();
    setLocation(node.get(), startTok);
    node->expression = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    expect(TokenKind::OpenBrace, "'{'");

    switchDepth_++;
    // ECMA-262 13.12: CaseBlock is its own lexical scope. Push one
    // scope for the whole switch so lex/function declarations inside
    // case clauses don't conflict with names in the enclosing Block
    // (e.g. Annex B.3.3.5 `{ let f; switch(_) { case 1: function f(){} } }`).
    pushLexicalScope();
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        if (match(TokenKind::KW_case)) {
            auto clause = std::make_unique<ast::CaseClause>();
            clause->expression = parseExpression();
            expect(TokenKind::Colon, "':'");
            while (!check(TokenKind::KW_case) && !check(TokenKind::KW_default) &&
                   !check(TokenKind::CloseBrace) && !isAtEnd()) {
                auto stmt = parseDeclarationOrStatement();
                if (stmt) clause->statements.push_back(std::move(stmt));
            }
            node->clauses.push_back(std::move(clause));
        } else if (match(TokenKind::KW_default)) {
            // ECMA-262 13.12.1: a CaseBlock may contain at most one DefaultClause.
            for (auto& cl : node->clauses)
                if (dynamic_cast<const ast::DefaultClause*>(cl.get()))
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: more than one default clause in a "
                        "switch statement", fileName_, previous_.line));
            auto clause = std::make_unique<ast::DefaultClause>();
            expect(TokenKind::Colon, "':'");
            while (!check(TokenKind::KW_case) && !check(TokenKind::KW_default) &&
                   !check(TokenKind::CloseBrace) && !isAtEnd()) {
                auto stmt = parseDeclarationOrStatement();
                if (stmt) clause->statements.push_back(std::move(stmt));
            }
            node->clauses.push_back(std::move(clause));
        } else {
            // ECMA-262 13.12: a CaseBlock contains only CaseClauses and a
            // DefaultClause. Any other token here (e.g. a statement before the
            // first `case`, as in `switch(v){ x=2; case 0: }`) is a SyntaxError.
            // Without this branch neither match() consumed the token, so the loop
            // never advanced and the parser spun forever on malformed switches.
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: Unexpected token in switch body; expected 'case' or 'default'",
                fileName_, current_.line));
        }
    }
    popLexicalScope();
    switchDepth_--;
    expect(TokenKind::CloseBrace, "'}'");

    // ECMA-262 13.12.1: SwitchStatement CaseBlock early errors —
    //   - LexicallyDeclaredNames of CaseBlock must not contain duplicates.
    //     (Annex B.3.3.5: in non-strict mode, function+function pairs
    //     are allowed.)
    //   - LexicallyDeclaredNames of CaseBlock must not intersect
    //     VarDeclaredNames of CaseBlock.
    // VarDeclaredNames includes var declarations at any depth inside
    // the case clauses (recursing into blocks / if / loops / try /
    // labeled statements / switch) but NOT inside nested functions.
    {
        // entry kind: 1 = function, 2 = lexical (let/const/class)
        std::unordered_map<std::string, std::pair<int, int>> lexEntries; // name -> (kind, line)
        std::unordered_map<std::string, int> varEntries;                 // name -> line
        auto recordLexName = [&](const std::string& nm, int line, int kind) {
            if (nm.empty()) return;
            // Check against var names first (lex-var overlap is illegal).
            auto vit = varEntries.find(nm);
            if (vit != varEntries.end()) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' has already been declared as 'var' in the switch case block",
                    fileName_, line, nm));
            }
            auto it = lexEntries.find(nm);
            if (it != lexEntries.end()) {
                bool annexBAllowed = !strictMode_ && kind == 1 && it->second.first == 1;
                if (!annexBAllowed) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' has already been declared in switch case block",
                        fileName_, line, nm));
                }
                return;
            }
            lexEntries[nm] = {kind, line};
        };
        auto recordVarName = [&](const std::string& nm, int line) {
            if (nm.empty()) return;
            // Check against lex names (var-lex overlap is also illegal).
            auto lit = lexEntries.find(nm);
            if (lit != lexEntries.end()) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' has already been declared lexically in the switch case block",
                    fileName_, line, nm));
            }
            varEntries[nm] = line;
        };
        // Recursively walk nested statements to gather var-declared
        // names. Recurses into Block / If / For / While / DoWhile /
        // Try / Labeled / Switch but stops at function boundaries
        // (a nested function's vars belong to that function).
        std::function<void(const ast::Node*)> collectVars = [&](const ast::Node* n) {
            if (!n) return;
            if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(n)) {
                if (vd->varKind == ast::VarKind::Var) {
                    std::vector<std::pair<std::string, int>> names;
                    collectBoundIdentNames(vd->name.get(), names);
                    for (auto& [nm, ln] : names) recordVarName(nm, ln);
                }
                return;
            }
            if (auto* b = dynamic_cast<const ast::BlockStatement*>(n)) {
                for (auto& s : b->statements) collectVars(s.get());
                return;
            }
            if (auto* ifs = dynamic_cast<const ast::IfStatement*>(n)) {
                collectVars(ifs->thenStatement.get());
                collectVars(ifs->elseStatement.get());
                return;
            }
            if (auto* ws = dynamic_cast<const ast::WhileStatement*>(n)) {
                collectVars(ws->body.get());
                return;
            }
            if (auto* fs = dynamic_cast<const ast::ForStatement*>(n)) {
                collectVars(fs->initializer.get());
                collectVars(fs->body.get());
                return;
            }
            if (auto* fos = dynamic_cast<const ast::ForOfStatement*>(n)) {
                collectVars(fos->initializer.get());
                collectVars(fos->body.get());
                return;
            }
            if (auto* fis = dynamic_cast<const ast::ForInStatement*>(n)) {
                collectVars(fis->initializer.get());
                collectVars(fis->body.get());
                return;
            }
            if (auto* ts = dynamic_cast<const ast::TryStatement*>(n)) {
                for (auto& s : ts->tryBlock) collectVars(s.get());
                collectVars(ts->catchClause.get());
                for (auto& s : ts->finallyBlock) collectVars(s.get());
                return;
            }
            if (auto* lbl = dynamic_cast<const ast::LabeledStatement*>(n)) {
                collectVars(lbl->statement.get());
                return;
            }
            // Don't recurse into FunctionDeclaration, ClassDeclaration,
            // ArrowFunction, FunctionExpression — their var names are
            // function-scope, not switch-scope.
        };
        auto collectLexFromStmts = [&](const std::vector<ast::StmtPtr>& stmts) {
            for (const auto& s : stmts) {
                if (!s) continue;
                if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(s.get())) {
                    if (vd->varKind == ast::VarKind::Let || vd->varKind == ast::VarKind::Const) {
                        if (auto* id = dynamic_cast<const ast::Identifier*>(vd->name.get())) {
                            recordLexName(id->name, vd->line, 2);
                        }
                    }
                } else if (auto* fd = dynamic_cast<const ast::FunctionDeclaration*>(s.get())) {
                    recordLexName(fd->name, fd->line, 1);
                } else if (auto* cd = dynamic_cast<const ast::ClassDeclaration*>(s.get())) {
                    recordLexName(cd->name, cd->line, 2);
                }
            }
        };
        for (const auto& clause : node->clauses) {
            const std::vector<ast::StmtPtr>* stmts = nullptr;
            if (auto* cc = dynamic_cast<const ast::CaseClause*>(clause.get())) {
                stmts = &cc->statements;
            } else if (auto* dc = dynamic_cast<const ast::DefaultClause*>(clause.get())) {
                stmts = &dc->statements;
            }
            if (!stmts) continue;
            collectLexFromStmts(*stmts);
            for (const auto& s : *stmts) collectVars(s.get());
        }
    }
    return node;
}

ast::StmtPtr Parser::parseTryStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_try, "'try'");

    auto node = std::make_unique<ast::TryStatement>();
    setLocation(node.get(), startTok);

    // try block
    expect(TokenKind::OpenBrace, "'{'");
    pushLexicalScope();
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->tryBlock.push_back(std::move(stmt));
    }
    popLexicalScope();
    expect(TokenKind::CloseBrace, "'}'");

    // catch clause
    if (match(TokenKind::KW_catch)) {
        node->catchClause = std::make_unique<ast::CatchClause>();
        if (match(TokenKind::OpenParen)) {
            // catch (e) or catch (e: Type). ECMA-262 14.15: when parens are
            // present a CatchParameter is REQUIRED — `catch ()` (empty parens) is
            // a SyntaxError; the binding-less form is `catch { }` with NO parens.
            if (check(TokenKind::CloseParen)) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: missing catch binding (the binding-less "
                    "catch form omits the parentheses entirely)",
                    fileName_, current_.line));
            }
            node->catchClause->variable = parseBindingNameOrPattern();
            // Optional type annotation on catch variable
            if (check(TokenKind::Colon)) {
                parseTypeAnnotation(); // Skip the type
            }
            expect(TokenKind::CloseParen, "')'");
        }
        // catch block
        expect(TokenKind::OpenBrace, "'{'");
        pushLexicalScope();
        // ECMA-262 14.15.2 Static Semantics: Early Errors —
        //   CatchClause : catch ( CatchParameter ) Block
        //   1. It is a Syntax Error if BoundNames of CatchParameter contains
        //      any duplicate elements.
        //   2. It is a Syntax Error if any element of the BoundNames of
        //      CatchParameter also occurs in the LexicallyDeclaredNames of
        //      Block.
        //   3. It is a Syntax Error if any element of the BoundNames of
        //      CatchParameter also occurs in the VarDeclaredNames of Block,
        //      unless that VarDeclaredName comes from a hoisted
        //      FunctionDeclaration (Annex B).
        // Implementation: enumerate BoundNames of the catch binding, check
        // for self-duplicates, and pre-declare each into the catch block's
        // lexical scope (Let). Subsequent `let X` / `const X` / `class X` /
        // `function X` inside the block will then conflict via the existing
        // declareLexicalName redeclaration check.
        if (node->catchClause->variable) {
            std::vector<std::pair<std::string, int>> bound;
            collectBoundIdentNames(node->catchClause->variable.get(), bound);
            std::unordered_set<std::string> seen;
            for (auto& entry : bound) {
                if (!seen.insert(entry.first).second) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: duplicate name '{}' in catch "
                        "parameter binding",
                        fileName_, entry.second, entry.first));
                }
                declareLexicalName(entry.first, PDeclKind::CatchParam);
            }
        }
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) node->catchClause->block.push_back(std::move(stmt));
        }
        popLexicalScope();
        expect(TokenKind::CloseBrace, "'}'");
    }

    // finally clause
    if (match(TokenKind::KW_finally)) {
        expect(TokenKind::OpenBrace, "'{'");
        pushLexicalScope();
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) node->finallyBlock.push_back(std::move(stmt));
        }
        popLexicalScope();
        expect(TokenKind::CloseBrace, "'}'");
    }

    return node;
}

ast::StmtPtr Parser::parseReturnStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_return, "'return'");

    // ECMA-262 14.10: ReturnStatement is only valid inside a function
    // body (FunctionDeclaration / FunctionExpression / ArrowFunction /
    // MethodDefinition / Generator / Async). Top-level `return` is a
    // SyntaxError.
    if (functionDepth_ == 0) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'return' statement is not allowed at "
            "the top level",
            fileName_, startTok.line));
    }

    auto node = std::make_unique<ast::ReturnStatement>();
    setLocation(node.get(), startTok);

    // Return value is optional; if followed by newline/semicolon/}, no expression
    if (!canInsertSemicolon() && !check(TokenKind::Semicolon) && !check(TokenKind::CloseBrace)) {
        node->expression = parseExpression();
    }

    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseThrowStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_throw, "'throw'");

    auto node = std::make_unique<ast::ThrowStatement>();
    setLocation(node.get(), startTok);

    // throw requires an expression (no ASI allowed before the expression)
    if (current_.hadNewlineBefore) {
        throw std::runtime_error(fmt::format("{}:{}: No line break allowed after 'throw'",
            fileName_, startTok.line));
    }
    node->expression = parseExpression();
    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseBlockStatement() {
    auto startTok = current_;
    expect(TokenKind::OpenBrace, "'{'");

    auto node = std::make_unique<ast::BlockStatement>();
    setLocation(node.get(), startTok);

    pushLexicalScope();
    // A block is a StatementList position: labelled functions inside it are fine
    // even when the block is the body of an if/loop (`if (x) { L: function f(){} }`).
    bool prevLBF = labelBodyForbidsFunction_;
    labelBodyForbidsFunction_ = false;
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->statements.push_back(std::move(stmt));
    }
    labelBodyForbidsFunction_ = prevLBF;
    popLexicalScope();
    expect(TokenKind::CloseBrace, "'}'");

    // ECMA-262 14.2.1 Block static semantics (early errors): it is a SyntaxError
    // if any element of LexicallyDeclaredNames(StatementList) also occurs in
    // VarDeclaredNames(StatementList) — where VarDeclaredNames includes `var`
    // declarations at ANY nesting depth within the block, stopping at function
    // boundaries. The per-statement declareLexicalName check only catches
    // same-statement-list collisions, so `{ {var f;} let f; }` and
    // `{ var f; function f(){} }` slipped through. This mirrors the switch
    // CaseBlock pass above. It is confined to genuine Block statements — function
    // bodies and the program top level are parsed elsewhere, so the legal
    // function/script-scope `var f; function f(){}` idiom is unaffected.
    {
        // entry kind: 1 = function (lexical at block level), 2 = let/const/class
        std::unordered_map<std::string, std::pair<int, int>> lexEntries;
        std::unordered_map<std::string, int> varEntries;
        auto recordLexName = [&](const std::string& nm, int line, int kind) {
            if (nm.empty()) return;
            if (varEntries.find(nm) != varEntries.end()) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: Identifier '{}' has already been declared",
                    fileName_, line, nm));
            }
            auto it = lexEntries.find(nm);
            if (it != lexEntries.end()) {
                // Annex B.3.3.4: function+function pairs are allowed in a
                // non-strict Block. Everything else is a redeclaration error.
                bool annexBAllowed = !strictMode_ && kind == 1 && it->second.first == 1;
                if (!annexBAllowed) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: Identifier '{}' has already been declared",
                        fileName_, line, nm));
                }
                return;
            }
            lexEntries[nm] = {kind, line};
        };
        auto recordVarName = [&](const std::string& nm, int line) {
            if (nm.empty()) return;
            if (lexEntries.find(nm) != lexEntries.end()) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: Identifier '{}' has already been declared",
                    fileName_, line, nm));
            }
            varEntries[nm] = line;
        };
        // VarDeclaredNames: recurse through nested statements, stopping at
        // function/class boundaries (mirrors the switch CaseBlock collectVars).
        std::function<void(const ast::Node*)> collectVars = [&](const ast::Node* n) {
            if (!n) return;
            if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(n)) {
                if (vd->varKind == ast::VarKind::Var) {
                    std::vector<std::pair<std::string, int>> names;
                    collectBoundIdentNames(vd->name.get(), names);
                    for (auto& [nm, ln] : names) recordVarName(nm, ln);
                }
                return;
            }
            if (auto* b = dynamic_cast<const ast::BlockStatement*>(n)) {
                for (auto& s : b->statements) collectVars(s.get());
                return;
            }
            if (auto* ifs = dynamic_cast<const ast::IfStatement*>(n)) {
                collectVars(ifs->thenStatement.get());
                collectVars(ifs->elseStatement.get());
                return;
            }
            if (auto* ws = dynamic_cast<const ast::WhileStatement*>(n)) {
                collectVars(ws->body.get());
                return;
            }
            if (auto* fs = dynamic_cast<const ast::ForStatement*>(n)) {
                collectVars(fs->initializer.get());
                collectVars(fs->body.get());
                return;
            }
            if (auto* fos = dynamic_cast<const ast::ForOfStatement*>(n)) {
                collectVars(fos->initializer.get());
                collectVars(fos->body.get());
                return;
            }
            if (auto* fis = dynamic_cast<const ast::ForInStatement*>(n)) {
                collectVars(fis->initializer.get());
                collectVars(fis->body.get());
                return;
            }
            if (auto* ts = dynamic_cast<const ast::TryStatement*>(n)) {
                for (auto& s : ts->tryBlock) collectVars(s.get());
                collectVars(ts->catchClause.get());
                for (auto& s : ts->finallyBlock) collectVars(s.get());
                return;
            }
            if (auto* lbl = dynamic_cast<const ast::LabeledStatement*>(n)) {
                collectVars(lbl->statement.get());
                return;
            }
            if (auto* sw = dynamic_cast<const ast::SwitchStatement*>(n)) {
                for (auto& c : sw->clauses) {
                    if (auto* cc = dynamic_cast<const ast::CaseClause*>(c.get()))
                        for (auto& s : cc->statements) collectVars(s.get());
                    else if (auto* dc = dynamic_cast<const ast::DefaultClause*>(c.get()))
                        for (auto& s : dc->statements) collectVars(s.get());
                }
                return;
            }
            // Stop at FunctionDeclaration/ClassDeclaration/Arrow/FunctionExpression.
        };
        // LexicallyDeclaredNames of the block: top-level let/const (kind 2),
        // class (kind 2), and EVERY FunctionDeclaration (kind 1 — lexical at
        // block level). Record lex names first so var-vs-lex collisions throw.
        for (const auto& s : node->statements) {
            if (!s) continue;
            if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(s.get())) {
                if (vd->varKind == ast::VarKind::Let || vd->varKind == ast::VarKind::Const) {
                    std::vector<std::pair<std::string, int>> names;
                    collectBoundIdentNames(vd->name.get(), names);
                    for (auto& [nm, ln] : names) recordLexName(nm, ln, 2);
                }
            } else if (auto* fd = dynamic_cast<const ast::FunctionDeclaration*>(s.get())) {
                recordLexName(fd->name, fd->line, 1);
            } else if (auto* cd = dynamic_cast<const ast::ClassDeclaration*>(s.get())) {
                recordLexName(cd->name, cd->line, 2);
            }
        }
        for (const auto& s : node->statements) collectVars(s.get());
    }
    return node;
}

ast::StmtPtr Parser::parseExpressionStatement() {
    auto node = std::make_unique<ast::ExpressionStatement>();
    auto startTok = current_;
    setLocation(node.get(), startTok);
    node->expression = parseExpression();
    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseLabeledOrExpressionStatement() {
    // Check if this is a labeled statement: identifier ':'
    // ECMA-262: LabelIdentifier = BindingIdentifier minus context-specific
    // reservations. `await` is a valid label in non-async/non-module code,
    // and `yield` is a valid label in non-strict/non-generator code.
    TokenKind k = current_.kind;
    bool isLabelCandidate =
        k == TokenKind::Identifier ||
        (k == TokenKind::KW_await && !inAsync_) ||
        (k == TokenKind::KW_yield && !inGenerator_ && !strictMode_);
    if (isLabelCandidate) {
        auto saved = saveState();
        std::string name(current_.text);
        std::string decodedName = !current_.decodedText.empty()
            ? current_.decodedText : name;
        int line = current_.line;
        int col = current_.column;
        bool labelEscapedReserved = current_.escapedReservedWord;
        advance();
        if (match(TokenKind::Colon)) {
            if (labelEscapedReserved) {
                // `await` and `yield` are context-sensitive (not strict-
                // reserved in script/non-generator/non-async). Allow their
                // escape-encoded forms as labels in the same contexts as
                // their raw forms.
                bool awaitOk = decodedName == "await" && !inAsync_;
                bool yieldOk = decodedName == "yield" && !inGenerator_ && !strictMode_;
                if (!awaitOk && !yieldOk) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: identifier resolves to reserved "
                        "word via Unicode escape and cannot be used as a label",
                        fileName_, line));
                }
            }
            // It's a labeled statement. Annex B.3.2.1: FunctionDeclaration
            // is allowed as LabelledItem in non-strict, EXCEPT when the
            // LabelledStatement is nested in an IterationStatement.
            // ECMA-262 13.7.2.1/13.7.3.1/13.7.4.1 say:
            //   It is a Syntax Error if IsLabelledFunction(Statement) is
            //   true (where Statement is the iteration body).
            // The check applies "regardless of the language mode", so we
            // suppress the Annex B carveout whenever iterationDepth_ > 0.
            // The recursive parseStatementOnly call for nested
            // `label1: label2: function f(){}` re-enters this routine, which
            // again sees iterationDepth_ > 0 and propagates the rejection.
            auto node = std::make_unique<ast::LabeledStatement>();
            setLocation(node.get(), line, col);
            node->label = decodedName;
            bool allowAnnexB = (iterationDepth_ == 0 && !labelBodyForbidsFunction_);
            // ECMA-262 14.13 / 14.14: push label so `break LABEL` /
            // `continue LABEL` can verify LABEL is in scope. Determine whether
            // this label ultimately denotes an IterationStatement (for/while/do),
            // skipping any nested label prefixes, so `continue LABEL` can reject a
            // label attached to a non-iteration statement (e.g. a block).
            bool labelsIteration = false;
            {
                auto savedLA = saveState();
                while (true) {
                    if (current_.kind == TokenKind::KW_for ||
                        current_.kind == TokenKind::KW_while ||
                        current_.kind == TokenKind::KW_do) {
                        labelsIteration = true;
                        break;
                    }
                    // A nested label prefix is `<name> :` — detect it by peeking
                    // for the ':' (works for identifier and contextual-keyword
                    // label names, and stops at a block `{`, `if`, etc.).
                    auto peek = saveState();
                    advance();
                    bool isLabelPrefix = (current_.kind == TokenKind::Colon);
                    restoreState(peek);
                    if (!isLabelPrefix) break;
                    advance();  // consume the nested label name
                    advance();  // consume its ':'
                }
                restoreState(savedLA);
            }
            // ECMA-262 14.13.1: a LabelledStatement's label may not duplicate an
            // enclosing label in the same LabelSet — `x: x: 0;` is a SyntaxError.
            // (Sequential reuse `x: a; x: b;` is fine — the outer label has been
            // popped by then.)
            for (auto& al : activeLabels_) {
                if (al.name == decodedName) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: label '{}' has already been declared",
                        fileName_, line, decodedName));
                }
            }
            activeLabels_.push_back({decodedName, labelsIteration});
            try {
                node->statement = parseStatementOnly(allowAnnexB);
            } catch (...) {
                activeLabels_.pop_back();
                throw;
            }
            activeLabels_.pop_back();
            return node;
        }
        restoreState(saved);
    }
    return parseExpressionStatement();
}

ast::StmtPtr Parser::parseBreakStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_break, "'break'");

    auto node = std::make_unique<ast::BreakStatement>();
    setLocation(node.get(), startTok);

    if (!canInsertSemicolon() && current_.kind == TokenKind::Identifier) {
        node->label = std::string(current_.text);
        advance();
    }
    // ECMA-262 14.13: unlabeled `break` requires an enclosing
    // IterationStatement or SwitchStatement; labeled `break` requires
    // LABEL to be a label in scope of an enclosing statement.
    if (node->label.empty() && iterationDepth_ == 0 && switchDepth_ == 0) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'break' must be inside a loop or switch",
            fileName_, startTok.line));
    }
    if (!node->label.empty()) {
        bool found = false;
        for (auto& l : activeLabels_) {
            if (l.name == node->label) { found = true; break; }
        }
        if (!found) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: undefined label '{}'",
                fileName_, startTok.line, node->label));
        }
    }
    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseContinueStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_continue, "'continue'");

    auto node = std::make_unique<ast::ContinueStatement>();
    setLocation(node.get(), startTok);

    if (!canInsertSemicolon() && current_.kind == TokenKind::Identifier) {
        node->label = std::string(current_.text);
        advance();
    }
    // ECMA-262 14.13: `continue` (labeled or not) requires an
    // enclosing IterationStatement; labeled `continue` requires LABEL
    // to be a label in scope of an enclosing IterationStatement.
    if (iterationDepth_ == 0) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'continue' must be inside a loop",
            fileName_, startTok.line));
    }
    if (!node->label.empty()) {
        bool found = false, isIter = false;
        for (auto& l : activeLabels_) {
            if (l.name == node->label) { found = true; isIter = l.isIteration; break; }
        }
        if (!found) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: undefined label '{}'",
                fileName_, startTok.line, node->label));
        }
        // ECMA-262 14.13.3: a `continue` target label must denote an
        // IterationStatement — `label: { for(;;){ continue label; } }` (label on
        // a block) is a SyntaxError.
        if (!isIter) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'continue' label '{}' does not denote an "
                "iteration statement", fileName_, startTok.line, node->label));
        }
    }
    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseDebuggerStatement() {
    advance(); // consume 'debugger'
    expectSemicolon();
    // Just produce an empty expression statement
    auto node = std::make_unique<ast::ExpressionStatement>();
    auto undef = std::make_unique<ast::UndefinedLiteral>();
    node->expression = std::move(undef);
    return node;
}

// ============================================================================
// Import / Export
// ============================================================================

void Parser::declareModuleExportName(const std::string& name, int line) {
    if (scriptGoal_ || name.empty()) return;
    if (!moduleExportedNames_.insert(name).second) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: duplicate export of name '{}'",
            fileName_, line, name));
    }
}

void Parser::checkModuleImportBinding(const std::string& name, int line) {
    if (scriptGoal_) return;
    // Import bindings are module-level declarations: the post-parse
    // export-resolvability check must accept `import * as ns ...;
    // export { ns };`.
    moduleImportBindings_.insert(name);
    if (name == "eval" || name == "arguments") {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: '{}' may not be bound by an import "
            "declaration in a module", fileName_, line, name));
    }
}

ast::StmtPtr Parser::parseImportDeclaration() {
    auto startTok = current_;
    // ECMA-262: an ImportDeclaration is only a ModuleItem — illegal in a Script.
    if (scriptGoal_) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'import' declarations may only appear in a module",
            fileName_, current_.line));
    }
    expect(TokenKind::KW_import, "'import'");

    // import X = require('module') — TypeScript import equals
    if (isIdentifierOrKeyword() && !check(TokenKind::StringLiteral)) {
        auto saved = saveState();
        std::string name = identifierName();
        if (match(TokenKind::Equals)) {
            // Check for require(...)
            if (current_.text == "require") {
                advance(); // consume 'require'
                expect(TokenKind::OpenParen, "'('");
                if (check(TokenKind::StringLiteral)) {
                    std::string moduleSpec = Lexer::getStringValue(current_.text);
                    advance();
                    expect(TokenKind::CloseParen, "')'");
                    expectSemicolon();
                    auto ieq = std::make_unique<ast::ImportEqualsDeclaration>();
                    setLocation(ieq.get(), startTok);
                    ieq->name = name;
                    ieq->moduleSpecifier = moduleSpec;
                    return ieq;
                }
            }
        }
        restoreState(saved);
    }

    auto node = std::make_unique<ast::ImportDeclaration>();
    setLocation(node.get(), startTok);

    // import type { ... } from '...' (skip 'type' keyword)
    bool isTypeOnly = false;
    if (current_.kind == TokenKind::KW_type) {
        auto saved = saveState();
        advance();
        if (check(TokenKind::OpenBrace) || check(TokenKind::Star) || current_.kind == TokenKind::Identifier) {
            isTypeOnly = true;
            node->isTypeOnly = true;
        } else {
            restoreState(saved);
        }
    }

    // import 'module' (side-effect import)
    if (check(TokenKind::StringLiteral)) {
        node->moduleSpecifier = Lexer::getStringValue(current_.text);
        advance();
        expectSemicolon();
        return node;
    }

    // import * as ns from 'module'
    if (match(TokenKind::Star)) {
        expect(TokenKind::KW_as, "'as'");
        int nsLine = current_.line;
        node->namespaceImport = identifierName();
        checkModuleImportBinding(node->namespaceImport, nsLine);
    }
    // import { a, b } from 'module' or import defaultExport from 'module'
    else if (check(TokenKind::OpenBrace)) {
        // Named imports
        advance(); // {
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            // Skip 'type' in individual imports: import { type Foo } from '...'
            bool specIsTypeOnly = false;
            if (current_.kind == TokenKind::KW_type) {
                auto saved = saveState();
                advance();
                if (isIdentifierOrKeyword() && !check(TokenKind::Comma) && !check(TokenKind::CloseBrace)) {
                    specIsTypeOnly = true;
                } else {
                    restoreState(saved);
                }
            }
            ast::ImportSpecifier spec;
            int specLine = current_.line;
            spec.name = identifierName();
            spec.isTypeOnly = specIsTypeOnly;

            if (current_.kind == TokenKind::KW_as) {
                advance();
                spec.propertyName = spec.name;
                specLine = current_.line;
                spec.name = identifierName();
            }
            // ES 16.2.1: the LOCAL BoundNames of a module must be unique and
            // may not be eval/arguments (module code is strict).
            checkModuleImportBinding(spec.name, specLine);
            for (const auto& prev : node->namedImports) {
                if (prev.name == spec.name) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: duplicate import binding '{}'",
                        fileName_, specLine, spec.name));
                }
            }
            node->namedImports.push_back(spec);

            if (!check(TokenKind::CloseBrace)) {
                expect(TokenKind::Comma, "','");
            }
        }
        expect(TokenKind::CloseBrace, "'}'");
    }
    // import defaultExport or import defaultExport, { named }
    else if (isIdentifierOrKeyword()) {
        int defLine = current_.line;
        node->defaultImport = identifierName();
        checkModuleImportBinding(node->defaultImport, defLine);

        // import defaultExport, { named } or import defaultExport, * as ns
        if (match(TokenKind::Comma)) {
            if (match(TokenKind::Star)) {
                expect(TokenKind::KW_as, "'as'");
                int nsLine2 = current_.line;
                node->namespaceImport = identifierName();
                checkModuleImportBinding(node->namespaceImport, nsLine2);
            } else if (check(TokenKind::OpenBrace)) {
                advance(); // {
                while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
                    ast::ImportSpecifier spec;
                    int specLine2 = current_.line;
                    spec.name = identifierName();
                    if (current_.kind == TokenKind::KW_as) {
                        advance();
                        spec.propertyName = spec.name;
                        specLine2 = current_.line;
                        spec.name = identifierName();
                    }
                    checkModuleImportBinding(spec.name, specLine2);
                    node->namedImports.push_back(spec);
                    if (!check(TokenKind::CloseBrace)) {
                        expect(TokenKind::Comma, "','");
                    }
                }
                expect(TokenKind::CloseBrace, "'}'");
            }
        }
    }

    // from 'module'
    if (current_.kind == TokenKind::KW_from) {
        advance();
    }
    if (check(TokenKind::StringLiteral)) {
        node->moduleSpecifier = Lexer::getStringValue(current_.text);
        advance();
    }

    expectSemicolon();
    return node;
}

ast::StmtPtr Parser::parseExportDeclaration() {
    auto startTok = current_;
    // ECMA-262: an ExportDeclaration is only a ModuleItem — illegal in a Script.
    if (scriptGoal_) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'export' declarations may only appear in a module",
            fileName_, current_.line));
    }
    expect(TokenKind::KW_export, "'export'");

    // export default
    if (match(TokenKind::KW_default)) {
        declareModuleExportName("default", startTok.line);
        if (current_.kind == TokenKind::KW_function) {
            return parseFunctionDeclaration(false, true, true);
        }
        if (current_.kind == TokenKind::KW_async) {
            auto saved = saveState();
            advance();
            if (check(TokenKind::KW_function)) {
                return parseFunctionDeclaration(true, true, true);
            }
            restoreState(saved);
        }
        if (current_.kind == TokenKind::KW_class) {
            return parseClassDeclaration(false, true, true);
        }
        if (current_.kind == TokenKind::KW_abstract) {
            advance();
            return parseClassDeclaration(true, true, true);
        }
        if (current_.kind == TokenKind::KW_interface) {
            return parseInterfaceDeclaration(true, true);
        }

        // export default expression
        auto node = std::make_unique<ast::ExportAssignment>();
        setLocation(node.get(), startTok);
        node->expression = parseAssignmentExpression();
        expectSemicolon();
        return node;
    }

    // export type
    bool isTypeOnly = false;
    if (current_.kind == TokenKind::KW_type) {
        auto saved = saveState();
        advance();
        if (check(TokenKind::OpenBrace) || check(TokenKind::Star)) {
            isTypeOnly = true;
        } else if (isIdentifierOrKeyword()) {
            // export type Foo = ... (type alias)
            restoreState(saved);
            return parseTypeAliasDeclaration(true);
        } else {
            restoreState(saved);
        }
    }

    // export * from 'module'
    if (match(TokenKind::Star)) {
        auto node = std::make_unique<ast::ExportDeclaration>();
        setLocation(node.get(), startTok);
        node->isStarExport = true;

        // export * as ns from 'module'
        if (current_.kind == TokenKind::KW_as) {
            advance();
            int nsLine = current_.line;
            node->namespaceExport = identifierName();
            declareModuleExportName(node->namespaceExport, nsLine);
        }

        expect(TokenKind::KW_from, "'from'");
        node->moduleSpecifier = Lexer::getStringValue(current_.text);
        advance();
        expectSemicolon();
        return node;
    }

    // export { a, b } or export { a, b } from 'module'
    if (check(TokenKind::OpenBrace)) {
        auto node = std::make_unique<ast::ExportDeclaration>();
        setLocation(node.get(), startTok);

        advance(); // {
        std::vector<int> specLines;
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            ast::ExportSpecifier spec;
            int specLine = current_.line;
            spec.name = identifierName();
            if (current_.kind == TokenKind::KW_as) {
                advance();
                spec.propertyName = spec.name;
                specLine = current_.line;
                spec.name = identifierName();
            }
            node->namedExports.push_back(spec);
            specLines.push_back(specLine);
            declareModuleExportName(spec.name, specLine);
            if (!check(TokenKind::CloseBrace)) {
                expect(TokenKind::Comma, "','");
            }
        }
        expect(TokenKind::CloseBrace, "'}'");

        // from 'module' (optional)
        bool hasFrom = false;
        if (current_.kind == TokenKind::KW_from) {
            hasFrom = true;
            advance();
            node->moduleSpecifier = Lexer::getStringValue(current_.text);
            advance();
        }
        if (!hasFrom && !scriptGoal_) {
            // Locals referenced by a from-less export clause must resolve to
            // module-level declarations (checked after the whole parse).
            for (size_t i = 0; i < node->namedExports.size(); i++) {
                const auto& sp = node->namedExports[i];
                const std::string& local =
                    sp.propertyName.empty() ? sp.name : sp.propertyName;
                moduleExportLocalRefs_.push_back({local, specLines[i]});
            }
        }

        expectSemicolon();
        return node;
    }

    // export var/let/const (including export const enum)
    if (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
        current_.kind == TokenKind::KW_const) {
        // Check for 'export const enum'
        if (current_.kind == TokenKind::KW_const) {
            auto saved = saveState();
            advance(); // consume 'const'
            if (current_.kind == TokenKind::KW_enum) {
                restoreState(saved);
                auto enumDecl = parseEnumDeclaration(true, false);
                return enumDecl;
            }
            restoreState(saved);
        }
        auto stmts = parseVariableDeclarationList(true);
        if (stmts.size() == 1) return std::move(stmts[0]);
        auto block = std::make_unique<ast::BlockStatement>();
        block->isSynthetic = true;
        for (auto& s : stmts) block->statements.push_back(std::move(s));
        return block;
    }

    // export function
    if (current_.kind == TokenKind::KW_function) {
        return parseFunctionDeclaration(false, true, false);
    }

    // export async function
    if (current_.kind == TokenKind::KW_async) {
        advance();
        return parseFunctionDeclaration(true, true, false);
    }

    // export class
    if (current_.kind == TokenKind::KW_class) {
        return parseClassDeclaration(false, true, false);
    }

    // export abstract class
    if (current_.kind == TokenKind::KW_abstract) {
        advance();
        return parseClassDeclaration(true, true, false);
    }

    // export interface
    if (current_.kind == TokenKind::KW_interface) {
        return parseInterfaceDeclaration(true, false);
    }

    // export enum
    if (current_.kind == TokenKind::KW_enum) {
        return parseEnumDeclaration(true, false);
    }

    // export declare
    if (current_.kind == TokenKind::KW_declare) {
        advance();
        if (current_.kind == TokenKind::KW_enum) {
            return parseEnumDeclaration(true, true);
        }
        // Handle other declare exports...
        return parseDeclarationOrStatement();
    }

    // export = expression (TypeScript export assignment)
    if (match(TokenKind::Equals)) {
        auto node = std::make_unique<ast::ExportAssignment>();
        setLocation(node.get(), startTok);
        node->isExportEquals = true;
        node->expression = parseAssignmentExpression();
        expectSemicolon();
        return node;
    }

    throw std::runtime_error(fmt::format("{}:{}: Unexpected token after 'export': '{}'",
        fileName_, current_.line, std::string(current_.text)));
}

// ============================================================================
// Interface declarations
// ============================================================================

ast::StmtPtr Parser::parseInterfaceDeclaration(bool isExported, bool isDefaultExport) {
    auto startTok = current_;
    expect(TokenKind::KW_interface, "'interface'");

    auto node = std::make_unique<ast::InterfaceDeclaration>();
    setLocation(node.get(), startTok);
    node->isExported = isExported;
    node->isDefaultExport = isDefaultExport;
    node->name = identifierName();

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends
    if (match(TokenKind::KW_extends)) {
        do {
            node->baseInterfaces.push_back(identifierName());
            if (check(TokenKind::LessThan)) {
                skipTypeExpression();
            }
        } while (match(TokenKind::Comma));
    }

    // Body
    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        // Parse interface members: methods, properties, call signatures, construct signatures
        // Call signature: (params): ReturnType
        if (check(TokenKind::OpenParen)) {
            auto sig = std::make_unique<ast::CallSignature>();
            sig->parameters = parseParameterList();
            if (check(TokenKind::Colon)) {
                sig->returnType = parseReturnTypeAnnotation();
            }
            node->callSignatures.push_back(std::move(sig));
            match(TokenKind::Semicolon);
            match(TokenKind::Comma);
            continue;
        }

        // Construct signature: new (params): ReturnType
        if (current_.kind == TokenKind::KW_new) {
            advance();
            auto sig = std::make_unique<ast::ConstructSignature>();
            if (check(TokenKind::LessThan)) {
                sig->typeParameters = parseTypeParameterList();
            }
            sig->parameters = parseParameterList();
            if (check(TokenKind::Colon)) {
                sig->returnType = parseReturnTypeAnnotation();
            }
            node->constructSignatures.push_back(std::move(sig));
            match(TokenKind::Semicolon);
            match(TokenKind::Comma);
            continue;
        }

        // Index signature: [key: string]: value
        if (check(TokenKind::OpenBracket)) {
            auto saved = saveState();
            advance(); // [
            if (isIdentifierOrKeyword()) {
                std::string keyName = identifierName();
                if (check(TokenKind::Colon)) {
                    // It's an index signature
                    advance(); // :
                    std::string keyType = scanTypeExpression();
                    expect(TokenKind::CloseBracket, "']'");
                    expect(TokenKind::Colon, "':'");
                    std::string valueType = scanTypeExpression();
                    auto idx = std::make_unique<ast::IndexSignature>();
                    idx->keyType = keyType;
                    idx->valueType = valueType;
                    node->members.push_back(std::move(idx));
                    match(TokenKind::Semicolon);
                    match(TokenKind::Comma);
                    continue;
                }
            }
            restoreState(saved);
        }

        // Regular member (method or property)
        bool isReadonly = false;
        if (current_.kind == TokenKind::KW_readonly) {
            isReadonly = true;
            advance();
        }

        std::string name;
        ast::NodePtr nameNode;

        if (check(TokenKind::OpenBracket)) {
            advance();
            auto cpn = std::make_unique<ast::ComputedPropertyName>();
            bool prevNoIn = noIn_;
            noIn_ = false;
            cpn->expression = parseAssignmentExpression();
            noIn_ = prevNoIn;
            expect(TokenKind::CloseBracket, "']'");
            name = "[computed]";
            nameNode = std::move(cpn);
        } else {
            name = identifierName();
        }

        bool isOptional = match(TokenKind::QuestionMark);

        // Method signature
        if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
            auto method = std::make_unique<ast::MethodDefinition>();
            setLocation(method.get(), previous_);
            method->name = name;
            method->nameNode = std::move(nameNode);
            method->typeParameters = parseTypeParameterList();
            method->parameters = parseParameterList();
            if (check(TokenKind::Colon)) {
                method->returnType = parseReturnTypeAnnotation();
            }
            method->hasBody = false;
            node->members.push_back(std::move(method));
        } else {
            // Property signature
            auto prop = std::make_unique<ast::PropertyDefinition>();
            setLocation(prop.get(), previous_);
            prop->name = name;
            prop->isReadonly = isReadonly;
            prop->isOptional = isOptional;
            if (check(TokenKind::Colon)) {
                prop->type = parseTypeAnnotation();
            }
            node->members.push_back(std::move(prop));
        }

        match(TokenKind::Semicolon);
        match(TokenKind::Comma);
    }
    expect(TokenKind::CloseBrace, "'}'");
    return node;
}

// ============================================================================
// Type alias declarations
// ============================================================================

ast::StmtPtr Parser::parseTypeAliasDeclaration(bool isExported) {
    auto startTok = current_;
    expect(TokenKind::KW_type, "'type'");

    auto node = std::make_unique<ast::TypeAliasDeclaration>();
    setLocation(node.get(), startTok);
    node->isExported = isExported;
    node->name = identifierName();

    // Type parameters
    if (check(TokenKind::LessThan)) {
        auto tps = parseTypeParameterList();
        for (auto& tp : tps) {
            node->typeParameters.push_back(std::move(*tp));
        }
    }

    expect(TokenKind::Equals, "'='");
    node->type = scanTypeExpression();
    expectSemicolon();
    return node;
}

// ============================================================================
// Enum declarations
// ============================================================================

ast::StmtPtr Parser::parseEnumDeclaration(bool isExported, bool isDeclare) {
    auto startTok = current_;
    // const enum
    bool isConst = false;
    if (current_.kind == TokenKind::KW_const) {
        isConst = true;
        advance();
    }
    expect(TokenKind::KW_enum, "'enum'");

    auto node = std::make_unique<ast::EnumDeclaration>();
    setLocation(node.get(), startTok);
    node->isExported = isExported;
    node->isDeclare = isDeclare;
    node->name = identifierName();

    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        ast::EnumMember member;
        if (check(TokenKind::StringLiteral)) {
            member.name = Lexer::getStringValue(current_.text);
            advance();
        } else {
            member.name = identifierName();
        }
        if (match(TokenKind::Equals)) {
            member.initializer = parseAssignmentExpression();
        }
        node->members.push_back(std::move(member));
        if (!check(TokenKind::CloseBrace)) {
            match(TokenKind::Comma);
        }
    }
    expect(TokenKind::CloseBrace, "'}'");
    return node;
}

} // namespace ts::parser
