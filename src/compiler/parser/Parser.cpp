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

int containsArgumentsOrSuperCall(const ast::Node* node) {
    if (!node) return FIELD_INIT_OK;

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

    // Boundary nodes — own arguments / super binding, do NOT recurse
    if (dynamic_cast<const ast::FunctionExpression*>(node)) return FIELD_INIT_OK;
    if (dynamic_cast<const ast::FunctionDeclaration*>(node)) return FIELD_INIT_OK;
    if (dynamic_cast<const ast::MethodDefinition*>(node)) return FIELD_INIT_OK;
    if (dynamic_cast<const ast::ClassDeclaration*>(node)) return FIELD_INIT_OK;
    if (dynamic_cast<const ast::ClassExpression*>(node)) return FIELD_INIT_OK;

    // Arrow functions: do recurse (no own arguments/super)
    if (auto* arrow = dynamic_cast<const ast::ArrowFunction*>(node)) {
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
    if (strLit->value == "use strict") {
        strictMode_ = true;
        sawUseStrictDirective_ = true;
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

    // Property / member access — always valid LHS.
    if (dynamic_cast<const ast::PropertyAccessExpression*>(expr)) return;
    if (dynamic_cast<const ast::ElementAccessExpression*>(expr)) return;

    // Object/Array literals — valid only as destructuring targets,
    // and only for plain `=` (not `+=` etc.).
    if (dynamic_cast<const ast::ObjectLiteralExpression*>(expr) ||
        dynamic_cast<const ast::ArrayLiteralExpression*>(expr)) {
        if (forCompoundAssign) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: destructuring pattern not allowed with compound assignment",
                expr->line, expr->column));
        }
        return;
    }

    // CallExpression: the spec strictly forbids it as
    // SimpleAssignmentTarget, but legacy non-strict code (and many
    // browsers) tolerate it; we follow tsc's behavior and accept it
    // at parse time. Downstream type-check would catch real errors.
    if (dynamic_cast<const ast::CallExpression*>(expr)) return;

    // Everything else is invalid: literals, arrow / function /
    // class expressions, binary / conditional / unary / new /
    // await / yield / spread / template / super.
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
    char buf[32];
    snprintf(buf, sizeof(buf), "%.17g", value);
    return std::string(buf);
}

// ============================================================================
// Public API
// ============================================================================

std::unique_ptr<ast::Program> Parser::parse(const std::string& source,
                                              const std::string& fileName) {
    source_ = &source;
    fileName_ = fileName;
    lexer_ = std::make_unique<Lexer>(source, fileName);
    current_ = lexer_->nextToken();
    previous_ = current_;

    auto program = std::make_unique<ast::Program>();
    program->sourceFile = fileName;

    // Parse triple-slash references from comments at the start
    program->tripleSlashReferences = parseTripleSlashReferences();

    // Directive prologue handling. ECMA-262: leading
    // ExpressionStatements wrapping a single string literal form a
    // directive prologue. If `"use strict"` appears among them, the
    // body is strict from then on. Prologue strings themselves are
    // parsed in the outer (typically sloppy) mode and are not
    // subject to legacy-octal rejection.
    bool inPrologue = true;
    while (!isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (!stmt) continue;
        if (inPrologue && !processPrologueDirective(stmt)) {
            inPrologue = false;
        }
        program->body.push_back(std::move(stmt));
    }

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

    current_ = lexer_->nextToken();
    return previous_;
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
        param->initializer = parseAssignmentExpression();
    }

    return param;
}

std::vector<std::unique_ptr<ast::Parameter>> Parser::parseParameterList() {
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
            if ((hasNonSimple || strictMode_) && seenIdentNames.count(paramName)) {
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
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: identifier resolves to reserved word "
                "via Unicode escape and cannot be used as a binding",
                fileName_, current_.line));
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
                   check(TokenKind::StringLiteral)) {
            // PropertyName : BindingElement with numeric or string key.
            // Per ECMA-262 14.1.2 BindingProperty : PropertyName : BindingElement
            // — PropertyName includes NumericLiteral and StringLiteral. The
            // shorthand path doesn't apply here; a `:` is mandatory.
            std::string propName;
            if (check(TokenKind::StringLiteral)) {
                propName = Lexer::getStringValue(current_.text);
            } else {
                propName = canonicalNumericPropertyName(current_.text);
            }
            advance();
            expect(TokenKind::Colon, "':'");
            elem->propertyName = propName;
            elem->name = parseBindingNameOrPattern();

            if (match(TokenKind::Equals)) {
                elem->initializer = parseAssignmentExpression();
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

ast::StmtPtr Parser::parseDeclarationOrStatement() {
    auto decorators = parseDecorators();

    // Handle export/import at top level
    if (check(TokenKind::KW_export)) {
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
        return parseImportDeclaration();
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
            return parseTypeAliasDeclaration(false);
        }
        if (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
            current_.kind == TokenKind::KW_const) {
            auto stmts = parseVariableDeclarationList(false);
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
                        auto stmts = parseVariableDeclarationList(memberExported);
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
                    k == TokenKind::KW_protected;
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
            // Wrap head as an ExpressionStatement so any side-effecting
            // expression in the head still executes.
            auto headStmt = std::make_unique<ast::ExpressionStatement>();
            headStmt->expression = std::move(head);
            block->statements.push_back(std::move(headStmt));
            auto body = parseDeclarationOrStatement();
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
        case TokenKind::KW_type:
            result = parseTypeAliasDeclaration(false);
            break;
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
        // Track in lexical scope for redeclaration detection (block-scoped functions)
        if (!node->name.empty()) {
            declareLexicalName(node->name, PDeclKind::Function);
        }
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

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
        bool prevSawUseStrict = sawUseStrictDirective_;
        sawUseStrictDirective_ = false;

        expect(TokenKind::OpenBrace, "'{'");
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
        expect(TokenKind::CloseBrace, "'}'");

        // Per ECMA-262 14.1.1: It is a SyntaxError if ContainsUseStrict of
        // FunctionBody is true and IsSimpleParameterList of FormalParameters
        // is false.
        if (sawUseStrictDirective_ &&
            !isParameterListSimple(node->parameters)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: function with non-simple parameter list may not "
                "declare \"use strict\"",
                current_.line, current_.column));
        }
        sawUseStrictDirective_ = prevSawUseStrict;

        functionDepth_--;
        inAsync_ = prevAsync;
        inGenerator_ = prevGen;
    } else {
        // Overload signature (no body) - consume the semicolon
        expectSemicolon();
    }

    return node;
}

std::vector<ast::StmtPtr> Parser::parseVariableDeclarationList(bool isExported) {
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

        result.push_back(std::move(decl));
    } while (match(TokenKind::Comma));

    expectSemicolon();
    return result;
}

ast::StmtPtr Parser::parseClassDeclaration(bool isAbstract, bool isExported, bool isDefaultExport) {
    auto startTok = current_;
    expect(TokenKind::KW_class, "'class'");

    auto node = std::make_unique<ast::ClassDeclaration>();
    setLocation(node.get(), startTok);
    node->isAbstract = isAbstract;
    node->isExported = isExported;
    node->isDefaultExport = isDefaultExport;

    // Name (optional for expressions). Class names are BindingIdentifier
    // and the class body is always strict (ES262 10.2.1), so escape-
    // encoded reserved words including contextual-strict ones (let,
    // static, yield) must be rejected here.
    if (isIdentifierOrKeyword() && !check(TokenKind::KW_extends) && !check(TokenKind::KW_implements) && !check(TokenKind::OpenBrace)) {
        if (current_.escapedReservedWord) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: identifier resolves to reserved word "
                "via Unicode escape and cannot be used as a class name",
                fileName_, current_.line));
        }
        node->name = identifierName();
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends
    if (match(TokenKind::KW_extends)) {
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
            if (check(TokenKind::OpenBrace) ||
                check(TokenKind::KW_implements) ||
                check(TokenKind::LessThan)) {
                node->baseClass = firstName;
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
                            check(TokenKind::KW_false);
            if (lhsStart) {
                // Best-effort baseClass: leave empty so analyzer treats
                // this as no user-defined base; downstream still registers
                // the class. Parse the full LHS expression to consume tokens.
                (void)parseCallExpression();
            }
        }
    }

    // implements
    if (current_.kind == TokenKind::KW_implements) {
        advance();
        do {
            node->implementsInterfaces.push_back(identifierName());
            // Skip generic type args
            if (check(TokenKind::LessThan)) {
                skipTypeExpression();
            }
        } while (match(TokenKind::Comma));
    }

    // Body. ECMA-262 §10.2.1: ClassBody is always strict-mode code.
    StrictModeGuard sg(this);
    strictMode_ = true;
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
                if (m->name == "constructor" && !m->isStatic && !m->isGetter && !m->isSetter) {
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
                    if (m->isGetter) {
                        if (e.isGetter || e.isOther) conflict = true;
                        e.isGetter = true;
                    } else if (m->isSetter) {
                        if (e.isSetter || e.isOther) conflict = true;
                        e.isSetter = true;
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
            }
            node->members.push_back(std::move(member));
        }
        // Consume trailing semicolons between members
        while (match(TokenKind::Semicolon)) {}
    }
    expect(TokenKind::CloseBrace, "'}'");

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
            while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
                auto stmt = parseDeclarationOrStatement();
                if (stmt) block->body.push_back(std::move(stmt));
            }
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

    // async
    // In class bodies, async is always a method modifier (no ASI concern like in expressions)
    if (current_.kind == TokenKind::KW_async) {
        auto saved = saveState();
        advance();
        // If followed by identifier/keyword/star/open-bracket/open-paren, it's async
        if (isIdentifierOrKeyword() || check(TokenKind::Star) ||
            check(TokenKind::OpenBracket) || check(TokenKind::Hash)) {
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
        if (isIdentifierOrKeyword() || check(TokenKind::OpenBracket) ||
            check(TokenKind::StringLiteral) || check(TokenKind::NumericLiteral) ||
            check(TokenKind::Hash)) {
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
            // Computed property name
            advance(); // [
            auto cpn = std::make_unique<ast::ComputedPropertyName>();
            setLocation(cpn.get(), previous_);
            cpn->expression = parseAssignmentExpression();
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
        prop->initializer = parseAssignmentExpression();
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

    // Type parameters
    method->typeParameters = parseTypeParameterList();

    // Parameters
    method->parameters = parseParameterList();

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
        bool prevSawUseStrict = sawUseStrictDirective_;
        sawUseStrictDirective_ = false;

        expect(TokenKind::OpenBrace, "'{'");
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
        expect(TokenKind::CloseBrace, "'}'");

        if (sawUseStrictDirective_ &&
            !isParameterListSimple(method->parameters)) {
            throw std::runtime_error(fmt::format(
                "{}:{}: method with non-simple parameter list may not "
                "declare \"use strict\"",
                current_.line, current_.column));
        }
        sawUseStrictDirective_ = prevSawUseStrict;

        functionDepth_--;
        inAsync_ = prevAsync;
        inGenerator_ = prevGen;
    } else {
        method->hasBody = false;
        expectSemicolon();
    }

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
        node->thenStatement = parseDeclarationOrStatement();
        popLexicalScope();
    } else {
        node->thenStatement = parseDeclarationOrStatement();
    }

    if (match(TokenKind::KW_else)) {
        if (current_.kind == TokenKind::KW_function) {
            pushLexicalScope();
            node->elseStatement = parseDeclarationOrStatement();
            popLexicalScope();
        } else {
            node->elseStatement = parseDeclarationOrStatement();
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
    node->body = parseDeclarationOrStatement();

    return node;
}

ast::StmtPtr Parser::parseDoWhileStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_do, "'do'");

    auto node = std::make_unique<ast::WhileStatement>();
    setLocation(node.get(), startTok);
    node->isDoWhile = true;

    node->body = parseDeclarationOrStatement();
    expect(TokenKind::KW_while, "'while'");
    expect(TokenKind::OpenParen, "'('");
    node->condition = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    expectSemicolon();

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
        node->body = parseDeclarationOrStatement();
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
            k == TokenKind::KW_protected;
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
        firstDecl->name = parseBindingNameOrPattern();
        if (check(TokenKind::Colon)) {
            firstDecl->type = parseTypeAnnotation();
        }

        // Check for for-of: for (const x of iterable)
        if (current_.kind == TokenKind::KW_of) {
            advance(); // consume 'of'
            auto iterable = parseExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForOfStatement>();
            setLocation(node.get(), startTok);
            node->isAwait = isAwait;
            node->initializer = std::move(firstDecl);
            node->expression = std::move(iterable);
            node->body = parseDeclarationOrStatement();
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
            node->body = parseDeclarationOrStatement();
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
                node->body = parseDeclarationOrStatement();
                return node;
            }
        }

        // Collect into a list (may have multiple: for (let i = 0, j = 10; ...))
        std::vector<ast::StmtPtr> decls;
        decls.push_back(std::move(firstDecl));
        while (match(TokenKind::Comma)) {
            auto decl = std::make_unique<ast::VariableDeclaration>();
            setLocation(decl.get(), current_);
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
        node->body = parseDeclarationOrStatement();
        return node;
    }

    // --- Expression initializer: for (expr; ...) or for (x in obj) or for (x of arr) ---
    {
        // Suppress 'in' as binary operator so 'for (x in obj)' doesn't parse as
        // a binary expression 'x in obj'
        bool prevNoIn = noIn_;
        noIn_ = true;
        auto expr = parseAssignmentExpression();
        noIn_ = prevNoIn;

        // Check for for-of: for (x of iterable)
        if (current_.kind == TokenKind::KW_of) {
            advance(); // consume 'of'
            auto iterable = parseExpression();
            expect(TokenKind::CloseParen, "')'");

            auto node = std::make_unique<ast::ForOfStatement>();
            setLocation(node.get(), startTok);
            node->isAwait = isAwait;
            auto es = std::make_unique<ast::ExpressionStatement>();
            setLocation(es.get(), expr->line, expr->column);
            es->expression = std::move(expr);
            node->initializer = std::move(es);
            node->expression = std::move(iterable);
            node->body = parseDeclarationOrStatement();
            return node;
        }

        // Check for for-in: for (x in obj)
        if (current_.kind == TokenKind::KW_in) {
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
            node->body = parseDeclarationOrStatement();
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
        node->body = parseDeclarationOrStatement();
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
            auto clause = std::make_unique<ast::DefaultClause>();
            expect(TokenKind::Colon, "':'");
            while (!check(TokenKind::KW_case) && !check(TokenKind::KW_default) &&
                   !check(TokenKind::CloseBrace) && !isAtEnd()) {
                auto stmt = parseDeclarationOrStatement();
                if (stmt) clause->statements.push_back(std::move(stmt));
            }
            node->clauses.push_back(std::move(clause));
        }
    }
    expect(TokenKind::CloseBrace, "'}'");

    // ECMA-262 13.12.1: It is a Syntax Error if the LexicallyDeclared-
    // Names of CaseBlock contains any duplicate entries. CaseBlock is
    // the union of all case/default clauses; lexical names come from
    // let, const, function, and class declarations at any clause's
    // top level. Annex B.3.3.5: in non-strict mode, duplicates are
    // allowed when ALL duplicates are bound by FunctionDeclarations.
    {
        // entry kind: 1 = function, 2 = lexical (let/const/class)
        std::unordered_map<std::string, std::pair<int, int>> entries; // name -> (kind, line)
        auto recordName = [&](const std::string& nm, int line, int kind) {
            if (nm.empty()) return;
            auto it = entries.find(nm);
            if (it != entries.end()) {
                bool annexBAllowed = !strictMode_ && kind == 1 && it->second.first == 1;
                if (!annexBAllowed) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' has already been declared in switch case block",
                        fileName_, line, nm));
                }
                return;
            }
            entries[nm] = {kind, line};
        };
        auto collectFromStmts = [&](const std::vector<ast::StmtPtr>& stmts) {
            for (const auto& s : stmts) {
                if (!s) continue;
                if (auto* vd = dynamic_cast<const ast::VariableDeclaration*>(s.get())) {
                    if (vd->varKind == ast::VarKind::Let || vd->varKind == ast::VarKind::Const) {
                        if (auto* id = dynamic_cast<const ast::Identifier*>(vd->name.get())) {
                            recordName(id->name, vd->line, 2);
                        }
                    }
                } else if (auto* fd = dynamic_cast<const ast::FunctionDeclaration*>(s.get())) {
                    recordName(fd->name, fd->line, 1);
                } else if (auto* cd = dynamic_cast<const ast::ClassDeclaration*>(s.get())) {
                    recordName(cd->name, cd->line, 2);
                }
            }
        };
        for (const auto& clause : node->clauses) {
            if (auto* cc = dynamic_cast<const ast::CaseClause*>(clause.get())) {
                collectFromStmts(cc->statements);
            } else if (auto* dc = dynamic_cast<const ast::DefaultClause*>(clause.get())) {
                collectFromStmts(dc->statements);
            }
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
            // catch (e) or catch (e: Type) or just catch { }
            if (!check(TokenKind::CloseParen)) {
                node->catchClause->variable = parseBindingNameOrPattern();
                // Optional type annotation on catch variable
                if (check(TokenKind::Colon)) {
                    parseTypeAnnotation(); // Skip the type
                }
            }
            expect(TokenKind::CloseParen, "')'");
        }
        // catch block
        expect(TokenKind::OpenBrace, "'{'");
        pushLexicalScope();
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
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->statements.push_back(std::move(stmt));
    }
    popLexicalScope();
    expect(TokenKind::CloseBrace, "'}'");
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
    if (current_.kind == TokenKind::Identifier) {
        auto saved = saveState();
        std::string name(current_.text);
        int line = current_.line;
        int col = current_.column;
        // Capture before advance(): label identifiers are
        // BindingIdentifier-form per spec, so escape-encoded reserved
        // words must be rejected. Without this, e.g. \`\\u0061wait:\` as
        // a label inside async would slip through.
        bool labelEscapedReserved = current_.escapedReservedWord;
        advance();
        if (match(TokenKind::Colon)) {
            if (labelEscapedReserved) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: identifier resolves to reserved "
                    "word via Unicode escape and cannot be used as a label",
                    fileName_, line));
            }
            // It's a labeled statement
            auto node = std::make_unique<ast::LabeledStatement>();
            setLocation(node.get(), line, col);
            node->label = name;
            node->statement = parseDeclarationOrStatement();
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

ast::StmtPtr Parser::parseImportDeclaration() {
    auto startTok = current_;
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
        node->namespaceImport = identifierName();
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
            spec.name = identifierName();
            spec.isTypeOnly = specIsTypeOnly;

            if (current_.kind == TokenKind::KW_as) {
                advance();
                spec.propertyName = spec.name;
                spec.name = identifierName();
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
        node->defaultImport = identifierName();

        // import defaultExport, { named } or import defaultExport, * as ns
        if (match(TokenKind::Comma)) {
            if (match(TokenKind::Star)) {
                expect(TokenKind::KW_as, "'as'");
                node->namespaceImport = identifierName();
            } else if (check(TokenKind::OpenBrace)) {
                advance(); // {
                while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
                    ast::ImportSpecifier spec;
                    spec.name = identifierName();
                    if (current_.kind == TokenKind::KW_as) {
                        advance();
                        spec.propertyName = spec.name;
                        spec.name = identifierName();
                    }
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
    expect(TokenKind::KW_export, "'export'");

    // export default
    if (match(TokenKind::KW_default)) {
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
            node->namespaceExport = identifierName();
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
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            ast::ExportSpecifier spec;
            spec.name = identifierName();
            if (current_.kind == TokenKind::KW_as) {
                advance();
                spec.propertyName = spec.name;
                spec.name = identifierName();
            }
            node->namedExports.push_back(spec);
            if (!check(TokenKind::CloseBrace)) {
                expect(TokenKind::Comma, "','");
            }
        }
        expect(TokenKind::CloseBrace, "'}'");

        // from 'module' (optional)
        if (current_.kind == TokenKind::KW_from) {
            advance();
            node->moduleSpecifier = Lexer::getStringValue(current_.text);
            advance();
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
            cpn->expression = parseAssignmentExpression();
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
