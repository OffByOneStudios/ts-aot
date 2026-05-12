#include "Parser.h"
#include <stdexcept>
#include <functional>
#include <fmt/format.h>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace ts::parser {

// Helper: check if a token kind is a contextual keyword that can be used as an identifier
// in arrow function parameter position (e.g., `type => { ... }`).
// This matches the same set treated as identifiers in parsePrimaryExpression.
static bool isContextualKeywordAsIdentifier(TokenKind kind) {
    switch (kind) {
        case TokenKind::KW_module:
        case TokenKind::KW_namespace:
        case TokenKind::KW_type:
        case TokenKind::KW_declare:
        case TokenKind::KW_abstract:
        case TokenKind::KW_interface:
        case TokenKind::KW_readonly:
        case TokenKind::KW_override:
        case TokenKind::KW_implements:
        case TokenKind::KW_from:
        case TokenKind::KW_of:
        case TokenKind::KW_as:
        case TokenKind::KW_is:
        case TokenKind::KW_get:
        case TokenKind::KW_set:
        case TokenKind::KW_require:
        case TokenKind::KW_asserts:
        case TokenKind::KW_satisfies:
        case TokenKind::KW_out:
        case TokenKind::KW_keyof:
        case TokenKind::KW_infer:
        case TokenKind::KW_constructor:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Precedence table for binary/ternary operators
// ============================================================================

int Parser::getBinaryPrecedence(TokenKind kind) const {
    switch (kind) {
        case TokenKind::QuestionQuestion:   return 4;
        case TokenKind::PipePipe:           return 5;
        case TokenKind::AmpersandAmpersand: return 6;
        case TokenKind::Pipe:               return 7;
        case TokenKind::Caret:              return 8;
        case TokenKind::Ampersand:          return 9;
        case TokenKind::EqualsEquals:
        case TokenKind::ExclamationEquals:
        case TokenKind::EqualsEqualsEquals:
        case TokenKind::ExclamationEqualsEquals: return 10;
        case TokenKind::LessThan:
        case TokenKind::GreaterThan:
        case TokenKind::LessThanEquals:
        case TokenKind::GreaterThanEquals:
        case TokenKind::KW_instanceof:      return 11;
        case TokenKind::KW_in:
            return noIn_ ? 0 : 11;  // Suppress 'in' in for-loop initializers
        case TokenKind::LessThanLessThan:
        case TokenKind::GreaterThanGreaterThan:
        case TokenKind::GreaterThanGreaterThanGreaterThan: return 12;
        case TokenKind::Plus:
        case TokenKind::Minus:              return 13;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:            return 14;
        case TokenKind::StarStar:           return 15;
        default: return 0;
    }
}

bool Parser::isRightAssociative(TokenKind kind) {
    return kind == TokenKind::StarStar;
}

std::string Parser::tokenToOperator(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::StarStar: return "**";
        case TokenKind::Ampersand: return "&";
        case TokenKind::Pipe: return "|";
        case TokenKind::Caret: return "^";
        case TokenKind::LessThanLessThan: return "<<";
        case TokenKind::GreaterThanGreaterThan: return ">>";
        case TokenKind::GreaterThanGreaterThanGreaterThan: return ">>>";
        case TokenKind::EqualsEquals: return "==";
        case TokenKind::ExclamationEquals: return "!=";
        case TokenKind::EqualsEqualsEquals: return "===";
        case TokenKind::ExclamationEqualsEquals: return "!==";
        case TokenKind::LessThan: return "<";
        case TokenKind::GreaterThan: return ">";
        case TokenKind::LessThanEquals: return "<=";
        case TokenKind::GreaterThanEquals: return ">=";
        case TokenKind::AmpersandAmpersand: return "&&";
        case TokenKind::PipePipe: return "||";
        case TokenKind::QuestionQuestion: return "??";
        case TokenKind::KW_instanceof: return "instanceof";
        case TokenKind::KW_in: return "in";
        case TokenKind::Comma: return ",";
        default: return "?";
    }
}

bool Parser::isAssignmentOperator(TokenKind kind) {
    switch (kind) {
        case TokenKind::Equals:
        case TokenKind::PlusEquals:
        case TokenKind::MinusEquals:
        case TokenKind::StarEquals:
        case TokenKind::SlashEquals:
        case TokenKind::PercentEquals:
        case TokenKind::StarStarEquals:
        case TokenKind::AmpersandEquals:
        case TokenKind::PipeEquals:
        case TokenKind::CaretEquals:
        case TokenKind::LessThanLessThanEquals:
        case TokenKind::GreaterThanGreaterThanEquals:
        case TokenKind::GreaterThanGreaterThanGreaterThanEquals:
        case TokenKind::AmpersandAmpersandEquals:
        case TokenKind::PipePipeEquals:
        case TokenKind::QuestionQuestionEquals:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Expression entry points
// ============================================================================

ast::ExprPtr Parser::parseExpression() {
    // Comma-separated expressions: expr, expr, expr
    auto expr = parseAssignmentExpression();

    while (match(TokenKind::Comma)) {
        auto right = parseAssignmentExpression();
        // Produce a BinaryExpression with "," operator
        auto bin = std::make_unique<ast::BinaryExpression>();
        setLocation(bin.get(), expr->line, expr->column);
        bin->op = ",";
        bin->left = std::move(expr);
        bin->right = std::move(right);
        expr = std::move(bin);
    }

    return expr;
}

ast::ExprPtr Parser::parseAssignmentExpression() {
    // Try arrow function first: (params) => or async (params) => or ident =>
    // We use speculative parsing here

    // Check for arrow function patterns:
    // 1. async? identifier =>
    // 2. async? ( ... ) =>
    bool isAsync = false;
    if (current_.kind == TokenKind::KW_async && !current_.hadNewlineBefore) {
        auto saved = saveState();
        advance();
        // async identifier =>
        if ((current_.kind == TokenKind::Identifier || isContextualKeywordAsIdentifier(current_.kind)) && !current_.hadNewlineBefore) {
            auto saved2 = saveState();
            advance();
            if (check(TokenKind::Arrow)) {
                restoreState(saved);
                return parseArrowFunctionOrParenthesized();
            }
            restoreState(saved2);
        }
        // async ( ... ) =>
        if (check(TokenKind::OpenParen)) {
            // Look for the matching ')' then '=>'
            restoreState(saved);
            auto expr = parseArrowFunctionOrParenthesized();
            if (expr->getKind() == "ArrowFunction") {
                return expr;
            }
            // Not an arrow function, continue with normal parsing
            return expr;
        }
        restoreState(saved);
    }

    // Simple arrow: ident => body
    // Also handle contextual keywords used as identifiers (e.g., `type => { ... }`)
    if (current_.kind == TokenKind::Identifier || isContextualKeywordAsIdentifier(current_.kind)) {
        auto saved = saveState();
        std::string name(current_.text);
        int line = current_.line, col = current_.column;
        advance();
        if (check(TokenKind::Arrow) && !current_.hadNewlineBefore) {
            // ECMA-262 14.7: in strict mode, the binding identifier of
            // an arrow function parameter cannot be `eval` or
            // `arguments`. The lexer emits these as plain
            // IdentifierName tokens, so the check has to live here.
            if (strictMode_ && (name == "eval" || name == "arguments")) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' is not allowed as an arrow "
                    "function parameter in strict mode",
                    fileName_, line, name));
            }
            advance(); // =>
            auto arrow = std::make_unique<ast::ArrowFunction>();
            setLocation(arrow.get(), line, col);
            auto param = std::make_unique<ast::Parameter>();
            auto id = std::make_unique<ast::Identifier>();
            id->name = name;
            param->name = std::move(id);
            param->type = "";
            arrow->parameters.push_back(std::move(param));

            bool prevAsync = inAsync_;
            StrictModeGuard sg(this);
            inAsync_ = false;
            functionDepth_++;
            int prevIter = iterationDepth_, prevSwitch = switchDepth_;
            iterationDepth_ = 0; switchDepth_ = 0;

            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }

            functionDepth_--;
            iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
            inAsync_ = prevAsync;
            return arrow;
        }
        restoreState(saved);
    }

    // Parenthesized expression or arrow function: (...) => or (expr)
    // NOTE: OpenParen is handled by parsePrimaryExpression -> parseArrowFunctionOrParenthesized.
    // We do NOT intercept it here because that would bypass parseCallExpression, breaking
    // IIFE patterns like (function(){})() where the () call must be parsed as postfix.

    // Yield expression
    if (current_.kind == TokenKind::KW_yield && inGenerator_) {
        auto startTok = current_;
        advance(); // yield

        auto node = std::make_unique<ast::YieldExpression>();
        setLocation(node.get(), startTok);

        if (match(TokenKind::Star)) {
            node->isAsterisk = true;
        }

        if (!canInsertSemicolon() && isStartOfExpression()) {
            node->expression = parseAssignmentExpression();
        }
        return node;
    }

    // Regular expression with binary/ternary
    auto expr = parsePrecedenceExpression(4); // Start above assignment

    // Ternary conditional
    if (match(TokenKind::QuestionMark)) {
        auto cond = std::make_unique<ast::ConditionalExpression>();
        setLocation(cond.get(), expr->line, expr->column);
        cond->condition = std::move(expr);
        // Per ECMA-262 ConditionalExpression[In, Yield] grammar:
        //   LogicalOR ? AssignmentExpression[+In, ?Yield] : AssignmentExpression[?In, ?Yield]
        // The whenTrue branch ALWAYS allows `in` (+In overrides outer
        // context), but the whenFalse branch INHERITS the outer In
        // flag. So `for (true ? '' in obj : 0; false;);` is valid
        // (whenTrue allows in) but `for (true ? 0 : 0 in {}; false;);`
        // is a SyntaxError (whenFalse inherits NoIn from for-init).
        bool prevNoIn = noIn_;
        noIn_ = false;
        cond->whenTrue = parseAssignmentExpression();
        expect(TokenKind::Colon, "':'");
        noIn_ = prevNoIn;
        cond->whenFalse = parseAssignmentExpression();
        return cond;
    }

    // Assignment operators
    if (isAssignmentOperator(current_.kind)) {
        auto opTok = current_;
        // Per ECMA-262 12.15.5: AssignmentExpression's LHS must be a
        // valid AssignmentTarget. Reject e.g. `(x => x) = 1`,
        // `(a + b) = 1`, `1 = x`, `({ a = 1 }) = x` outside
        // destructuring, etc. The check is here (post-LHS-parse,
        // pre-`=` consume) so the error points at the LHS expression.
        bool isCompound = opTok.kind != TokenKind::Equals;
        validateAssignmentTarget(expr.get(), isCompound);
        advance();

        if (opTok.kind == TokenKind::Equals) {
            auto assign = std::make_unique<ast::AssignmentExpression>();
            setLocation(assign.get(), expr->line, expr->column);
            assign->left = std::move(expr);
            assign->right = parseAssignmentExpression();
            return assign;
        } else {
            // Compound assignment: +=, -=, etc. -> BinaryExpression
            auto bin = std::make_unique<ast::BinaryExpression>();
            setLocation(bin.get(), expr->line, expr->column);
            bin->op = std::string(opTok.text);
            bin->left = std::move(expr);
            bin->right = parseAssignmentExpression();
            return bin;
        }
    }

    // 'as' type assertion (TypeScript)
    if (current_.kind == TokenKind::KW_as && !current_.hadNewlineBefore) {
        advance();
        auto asExpr = std::make_unique<ast::AsExpression>();
        setLocation(asExpr.get(), expr->line, expr->column);
        asExpr->expression = std::move(expr);
        asExpr->type = scanTypeExpression();
        return asExpr;
    }

    return expr;
}

ast::ExprPtr Parser::parsePrecedenceExpression(int minPrec) {
    auto left = parseUnaryExpression();

    while (true) {
        int prec = getBinaryPrecedence(current_.kind);
        if (prec < minPrec) break;

        auto opTok = current_;
        advance();
        lexer_->setRegexAllowed(true);

        int nextMinPrec = isRightAssociative(opTok.kind) ? prec : prec + 1;
        auto right = parsePrecedenceExpression(nextMinPrec);

        auto bin = std::make_unique<ast::BinaryExpression>();
        setLocation(bin.get(), left->line, left->column);
        bin->op = tokenToOperator(opTok.kind);
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

ast::ExprPtr Parser::parseUnaryExpression() {
    // ECMA-262 13.6.1: UnaryExpression cannot directly precede `**`
    // (ExponentiationExpression's LHS is UpdateExpression). Detected
    // after the unary expression is built but before any `**` is
    // consumed by a higher-level parsePrecedenceExpression — i.e.,
    // right here in the unary-token cases.
    auto checkNotFollowedByStarStar = [&](const Token& opTok, const char* opName) {
        if (current_.kind == TokenKind::StarStar) {
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: unparenthesized unary '{}' "
                "expression cannot be the left operand of '**'",
                fileName_, opTok.line, opName));
        }
    };
    switch (current_.kind) {
        case TokenKind::ExclamationMark:
        case TokenKind::Tilde:
        case TokenKind::Plus:
        case TokenKind::Minus: {
            // Only treat as prefix unary if not followed by assignment (compound)
            auto tok = current_;
            if (tok.kind == TokenKind::Plus || tok.kind == TokenKind::Minus) {
                // Check it's not ++ or --
            }
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = std::string(tok.text);
            node->operand = parseUnaryExpression();
            checkNotFollowedByStarStar(tok, node->op.c_str());
            return node;
        }
        case TokenKind::PlusPlus:
        case TokenKind::MinusMinus: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = std::string(tok.text);
            node->operand = parseUnaryExpression();
            // ECMA-262 13.4 UpdateExpression: operand must be a valid
            // simple assignment target.
            validateAssignmentTarget(node->operand.get(), false);
            return node;
        }
        case TokenKind::KW_typeof: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = "typeof";
            node->operand = parseUnaryExpression();
            checkNotFollowedByStarStar(tok, "typeof");
            return node;
        }
        case TokenKind::KW_void: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = "void";
            node->operand = parseUnaryExpression();
            checkNotFollowedByStarStar(tok, "void");
            return node;
        }
        case TokenKind::KW_delete: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::DeleteExpression>();
            setLocation(node.get(), tok);
            node->expression = parseUnaryExpression();
            checkNotFollowedByStarStar(tok, "delete");
            // ECMA-262 §13.5.1.1: in strict mode, the operand of `delete`
            // must not be a plain Identifier reference. `delete obj.prop`
            // and `delete obj[key]` are fine; `delete x` where x is just
            // an Identifier is a SyntaxError.
            if (strictMode_ && dynamic_cast<ast::Identifier*>(node->expression.get())) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'delete' of an unqualified "
                    "identifier is not allowed in strict mode",
                    fileName_, tok.line));
            }
            // ECMA-262 §13.5.1.1: It is a Syntax Error if the
            // UnaryExpression is contained in strict mode code and the
            // operand derives a MemberExpression : MemberExpression .
            // PrivateIdentifier (i.e. `delete obj.#x` is always invalid).
            // The "covered" rule walks through parens / sequence / comma
            // expressions before applying this check.
            std::function<bool(const ast::Node*)> containsPrivateMember;
            containsPrivateMember = [&](const ast::Node* n) -> bool {
                if (!n) return false;
                if (auto* p = dynamic_cast<const ast::PropertyAccessExpression*>(n)) {
                    if (!p->name.empty() && p->name[0] == '#') return true;
                    return containsPrivateMember(p->expression.get());
                }
                if (auto* paren = dynamic_cast<const ast::ParenthesizedExpression*>(n)) {
                    return containsPrivateMember(paren->expression.get());
                }
                if (auto* bin = dynamic_cast<const ast::BinaryExpression*>(n)) {
                    if (bin->op == ",") {
                        return containsPrivateMember(bin->left.get()) ||
                               containsPrivateMember(bin->right.get());
                    }
                }
                return false;
            };
            if (containsPrivateMember(node->expression.get())) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: cannot delete a private member",
                    fileName_, tok.line));
            }
            return node;
        }
        case TokenKind::KW_await: {
            // await-expression: only valid in async function/method body.
            // Top-level await (functionDepth_ == 0) is valid only in module
            // mode, not in script mode — and our test262 harness defaults
            // to script. Outside async, fall through to parsePostfix ->
            // parsePrimary which handles KW_await as an Identifier (per
            // ES262 13.1.1).
            if (inAsync_) {
                auto tok = current_;
                advance();
                auto node = std::make_unique<ast::AwaitExpression>();
                setLocation(node.get(), tok);
                node->expression = parseUnaryExpression();
                return node;
            }
            break;
        }
        default:
            break;
    }

    return parsePostfixExpression();
}

ast::ExprPtr Parser::parsePostfixExpression() {
    auto expr = parseCallExpression();

    // Postfix ++ and -- (no newline before)
    if (!current_.hadNewlineBefore) {
        if (current_.kind == TokenKind::PlusPlus) {
            auto tok = current_;
            advance();
            // ECMA-262 13.4: postfix UpdateExpression operand must be a
            // valid simple assignment target.
            validateAssignmentTarget(expr.get(), false);
            auto node = std::make_unique<ast::PostfixUnaryExpression>();
            setLocation(node.get(), expr->line, expr->column);
            node->op = "++";
            node->operand = std::move(expr);
            return node;
        }
        if (current_.kind == TokenKind::MinusMinus) {
            auto tok = current_;
            advance();
            validateAssignmentTarget(expr.get(), false);
            auto node = std::make_unique<ast::PostfixUnaryExpression>();
            setLocation(node.get(), expr->line, expr->column);
            node->op = "--";
            node->operand = std::move(expr);
            return node;
        }
        // Non-null assertion expr! is handled in parseCallExpression() loop
        // so that expr!.property chains work correctly.
    }

    return expr;
}

ast::ExprPtr Parser::parseCallExpression() {
    auto expr = parseMemberExpression();

    while (true) {
        if (check(TokenKind::OpenParen)) {
            // Function call
            advance();
            auto call = std::make_unique<ast::CallExpression>();
            setLocation(call.get(), expr->line, expr->column);
            call->callee = std::move(expr);
            while (!check(TokenKind::CloseParen) && !isAtEnd()) {
                if (check(TokenKind::DotDotDot)) {
                    // Spread argument
                    auto spreadTok = current_;
                    advance();
                    auto spread = std::make_unique<ast::SpreadElement>();
                    setLocation(spread.get(), spreadTok);
                    spread->expression = parseAssignmentExpression();
                    call->arguments.push_back(std::move(spread));
                } else {
                    call->arguments.push_back(parseAssignmentExpression());
                }
                if (!check(TokenKind::CloseParen)) {
                    expect(TokenKind::Comma, "','");
                }
            }
            expect(TokenKind::CloseParen, "')'");
            lexer_->setRegexAllowed(false);
            expr = std::move(call);
        } else if (check(TokenKind::OpenBracket)) {
            // Element access
            advance();
            auto access = std::make_unique<ast::ElementAccessExpression>();
            setLocation(access.get(), expr->line, expr->column);
            access->expression = std::move(expr);
            access->argumentExpression = parseExpression();
            expect(TokenKind::CloseBracket, "']'");
            lexer_->setRegexAllowed(false);
            expr = std::move(access);
        } else if (check(TokenKind::Dot)) {
            // Property access
            advance();
            auto access = std::make_unique<ast::PropertyAccessExpression>();
            setLocation(access.get(), expr->line, expr->column);
            access->expression = std::move(expr);
            if (check(TokenKind::Hash)) {
                advance();
                access->name = "#" + identifierName();
            } else {
                access->name = identifierName();
            }
            lexer_->setRegexAllowed(false);
            expr = std::move(access);
        } else if (check(TokenKind::QuestionDot)) {
            // Optional chaining: ?.
            advance();
            if (check(TokenKind::OpenParen)) {
                // Optional call: expr?.()
                advance();
                auto call = std::make_unique<ast::CallExpression>();
                setLocation(call.get(), expr->line, expr->column);
                call->callee = std::move(expr);
                call->isOptional = true;
                while (!check(TokenKind::CloseParen) && !isAtEnd()) {
                    call->arguments.push_back(parseAssignmentExpression());
                    if (!check(TokenKind::CloseParen)) {
                        expect(TokenKind::Comma, "','");
                    }
                }
                expect(TokenKind::CloseParen, "')'");
                expr = std::move(call);
            } else if (check(TokenKind::OpenBracket)) {
                // Optional element access: expr?.[index]
                advance();
                auto access = std::make_unique<ast::ElementAccessExpression>();
                setLocation(access.get(), expr->line, expr->column);
                access->expression = std::move(expr);
                access->isOptional = true;
                access->argumentExpression = parseExpression();
                expect(TokenKind::CloseBracket, "']'");
                expr = std::move(access);
            } else {
                // Optional property access: expr?.prop
                auto access = std::make_unique<ast::PropertyAccessExpression>();
                setLocation(access.get(), expr->line, expr->column);
                access->expression = std::move(expr);
                access->isOptional = true;
                access->name = identifierName();
                expr = std::move(access);
            }
        } else if (current_.kind == TokenKind::ExclamationMark && !current_.hadNewlineBefore) {
            // Non-null assertion: expr!
            // Must be in this loop (not parsePostfixExpression) so expr!.prop works
            auto saved = saveState();
            advance();
            if (!check(TokenKind::Equals) && !check(TokenKind::EqualsEquals) && !check(TokenKind::EqualsEqualsEquals)) {
                auto node = std::make_unique<ast::NonNullExpression>();
                setLocation(node.get(), expr->line, expr->column);
                node->expression = std::move(expr);
                expr = std::move(node);
                continue;
            }
            restoreState(saved);
            break;
        } else if (check(TokenKind::TemplateHead) || check(TokenKind::NoSubstitutionTemplate)) {
            // Tagged template: expr`...`
            expr = parseTaggedTemplate(std::move(expr));
        } else if (check(TokenKind::LessThan)) {
            // Could be generic type arguments for a call: expr<T>(args)
            // Or a comparison. Try speculative parsing.
            auto saved = saveState();
            try {
                auto typeArgs = parseTypeArguments();
                if (check(TokenKind::OpenParen)) {
                    // It's a generic call
                    advance();
                    auto call = std::make_unique<ast::CallExpression>();
                    setLocation(call.get(), expr->line, expr->column);
                    call->callee = std::move(expr);
                    call->typeArguments = std::move(typeArgs);
                    while (!check(TokenKind::CloseParen) && !isAtEnd()) {
                        call->arguments.push_back(parseAssignmentExpression());
                        if (!check(TokenKind::CloseParen)) {
                            expect(TokenKind::Comma, "','");
                        }
                    }
                    expect(TokenKind::CloseParen, "')'");
                    expr = std::move(call);
                    continue;
                }
            } catch (...) {}
            restoreState(saved);
            break;
        } else {
            break;
        }
    }

    return expr;
}

ast::ExprPtr Parser::parseMemberExpression() {
    return parsePrimaryExpression();
}

// ============================================================================
// Primary expressions
// ============================================================================

ast::ExprPtr Parser::parsePrimaryExpression() {
    auto tok = current_;

    switch (tok.kind) {
        case TokenKind::Identifier: {
            // ES262 12.6.1: an Identifier whose decoded form matches a
            // reserved word is a SyntaxError as IdentifierReference.
            // PropertyName / member-expression positions reach the
            // identifier through identifierName(), which doesn't consult
            // this flag, so they accept silently. Object-literal shorthand
            // is handled separately in parseObjectLiteral.
            if (tok.escapedReservedWord) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: identifier resolves to reserved word "
                    "via Unicode escape and cannot be used as a reference",
                    fileName_, tok.line));
            }
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = std::string(tok.text);
            return node;
        }

        case TokenKind::NumericLiteral: {
            // ECMA-262 Annex B.1.1: LegacyOctalIntegerLiteral and
            // NonCanonical-leading-zero decimal numerals are Syntax
            // Errors in strict mode.
            if (tok.isLegacyOctal && strictMode_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: legacy octal literals are not "
                    "allowed in strict mode",
                    fileName_, tok.line));
            }
            advance();
            auto node = std::make_unique<ast::NumericLiteral>();
            setLocation(node.get(), tok);
            std::string text(tok.text);
            // Per ECMA-262 12.8.3 NumericValue, literals that exceed the
            // double range round to +Infinity (or 0 for underflow). std::stod
            // throws std::out_of_range in those cases; std::stoull throws
            // for hex/oct/bin literals beyond uint64. Catch and clamp.
            // Only catch out_of_range (per ECMA-262 12.8.3 NumericValue
            // rounds to ±Infinity / 0 for unrepresentable magnitudes).
            // std::invalid_argument (e.g. malformed `0x` with no digits)
            // is a real parse error and must propagate so negative-parse
            // test262 tests still reject incomplete literals.
            auto safeStod = [](const std::string& s) -> double {
                try {
                    return std::stod(s);
                } catch (const std::out_of_range&) {
                    bool sawNonZeroDigit = false;
                    for (char c : s) {
                        if (c == '.') continue;
                        if (c == 'e' || c == 'E') break;
                        if (c >= '1' && c <= '9') { sawNonZeroDigit = true; break; }
                    }
                    return sawNonZeroDigit ? std::numeric_limits<double>::infinity() : 0.0;
                }
            };
            auto safeStoull = [](const std::string& s, int base) -> double {
                try {
                    return static_cast<double>(std::stoull(s, nullptr, base));
                } catch (const std::out_of_range&) {
                    return std::numeric_limits<double>::infinity();
                }
            };
            // Handle hex, octal, binary
            if (text.size() > 1 && text[0] == '0') {
                if (text[1] == 'x' || text[1] == 'X') {
                    node->value = safeStoull(text, 16);
                } else if (text[1] == 'o' || text[1] == 'O') {
                    node->value = safeStoull(text, 8);
                } else if (text[1] == 'b' || text[1] == 'B') {
                    node->value = safeStoull(text, 2);
                } else {
                    std::string clean;
                    for (char c : text) if (c != '_') clean += c;
                    node->value = safeStod(clean);
                }
            } else {
                std::string clean;
                for (char c : text) if (c != '_') clean += c;
                node->value = safeStod(clean);
            }
            return node;
        }

        case TokenKind::BigIntLiteral: {
            advance();
            auto node = std::make_unique<ast::BigIntLiteral>();
            setLocation(node.get(), tok);
            // Remove trailing 'n'
            std::string text(tok.text);
            if (!text.empty() && text.back() == 'n') text.pop_back();
            node->value = text;
            return node;
        }

        case TokenKind::StringLiteral: {
            advance();
            auto node = std::make_unique<ast::StringLiteral>();
            setLocation(node.get(), tok);
            // ECMA-262 12.8.4.1: in strict mode, reject \1-\7,
            // \0<digit>, \8, \9. Throws std::runtime_error on
            // violation, which propagates as a parse-phase
            // SyntaxError per test262 negative.phase: parse.
            Lexer::validateLegacyOctalEscapes(
                tok.text, strictMode_, /*isTemplate=*/false,
                tok.line, tok.column);
            node->value = Lexer::getStringValue(tok.text);
            return node;
        }

        case TokenKind::RegularExpressionLiteral: {
            advance();
            auto node = std::make_unique<ast::RegularExpressionLiteral>();
            setLocation(node.get(), tok);
            node->text = std::string(tok.text);
            return node;
        }

        case TokenKind::NoSubstitutionTemplate: {
            advance();
            auto node = std::make_unique<ast::TemplateExpression>();
            setLocation(node.get(), tok);
            // Remove backticks
            std::string text(tok.text);
            if (text.size() >= 2) {
                text = text.substr(1, text.size() - 2);
            }
            // Templates always reject legacy octals + \8/\9 per spec.
            Lexer::validateLegacyOctalEscapes(
                text, strictMode_, /*isTemplate=*/true,
                tok.line, tok.column);
            node->head = Lexer::processTemplateEscapes(text);
            return node;
        }

        case TokenKind::TemplateHead:
            return parseTemplateLiteral();

        case TokenKind::KW_true: {
            advance();
            auto node = std::make_unique<ast::BooleanLiteral>();
            setLocation(node.get(), tok);
            node->value = true;
            return node;
        }

        case TokenKind::KW_false: {
            advance();
            auto node = std::make_unique<ast::BooleanLiteral>();
            setLocation(node.get(), tok);
            node->value = false;
            return node;
        }

        case TokenKind::KW_null: {
            advance();
            auto node = std::make_unique<ast::NullLiteral>();
            setLocation(node.get(), tok);
            return node;
        }

        case TokenKind::KW_undefined: {
            advance();
            // Produce Identifier("undefined") to match legacy parser behavior.
            // This ensures codegen uses ts_value_is_undefined() for === undefined checks
            // rather than a raw null pointer comparison.
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "undefined";
            return node;
        }

        case TokenKind::KW_this: {
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "this";
            return node;
        }

        case TokenKind::KW_super: {
            advance();
            auto node = std::make_unique<ast::SuperExpression>();
            setLocation(node.get(), tok);
            return node;
        }

        case TokenKind::KW_new:
            return parseNewExpression();

        case TokenKind::OpenParen:
            return parseArrowFunctionOrParenthesized();

        case TokenKind::OpenBracket:
            return parseArrayLiteral();

        case TokenKind::OpenBrace:
            return parseObjectLiteral();

        case TokenKind::KW_function:
            return parseFunctionExpression(false);

        case TokenKind::KW_async: {
            auto saved = saveState();
            advance();
            if (check(TokenKind::KW_function) && !current_.hadNewlineBefore) {
                return parseFunctionExpression(true);
            }
            restoreState(saved);
            // Treat as identifier
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "async";
            return node;
        }

        case TokenKind::KW_class:
            return parseClassExpression();

        case TokenKind::KW_import: {
            // import() or import.meta
            advance();
            if (match(TokenKind::Dot)) {
                // import.meta
                auto meta = std::make_unique<ast::PropertyAccessExpression>();
                setLocation(meta.get(), tok);
                auto importId = std::make_unique<ast::Identifier>();
                importId->name = "import";
                setLocation(importId.get(), tok);
                meta->expression = std::move(importId);
                meta->name = identifierName(); // "meta"
                return meta;
            }
            if (check(TokenKind::OpenParen)) {
                // Dynamic import. ImportCall : import(AssignmentExpression).
                // Empty `import()` and spread `import(...x)` are SyntaxErrors
                // per the grammar. This also covers import.defer() and
                // import.defer(...x) when reached via the property-access
                // path (handled in parseCallExpression — see member-access
                // -> call sequencing).
                advance(); // (
                if (check(TokenKind::CloseParen)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: 'import()' requires a module specifier argument",
                        fileName_, tok.line));
                }
                if (check(TokenKind::DotDotDot)) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: rest/spread is not allowed in import()",
                        fileName_, tok.line));
                }
                auto node = std::make_unique<ast::DynamicImport>();
                setLocation(node.get(), tok);
                node->moduleSpecifier = parseAssignmentExpression();
                expect(TokenKind::CloseParen, "')'");
                return node;
            }
            // Bare `import` outside import-statement / import.meta /
            // import(...) is a SyntaxError per the grammar (covers
            // `typeof import` and similar).
            throw std::runtime_error(fmt::format(
                "{}:{}: SyntaxError: 'import' must be followed by '.', '(', or used as an import statement",
                fileName_, tok.line));
        }

        case TokenKind::DotDotDot: {
            advance();
            auto node = std::make_unique<ast::SpreadElement>();
            setLocation(node.get(), tok);
            node->expression = parseAssignmentExpression();
            return node;
        }

        case TokenKind::Slash: {
            // A '/' reached parsePrimaryExpression — it must be a regex literal.
            // Re-lex from the '/' position with regex mode enabled.
            lexer_->setOffset(tok.offset, tok.line, tok.column);
            lexer_->setRegexAllowed(true);
            current_ = lexer_->nextToken();
            if (current_.kind == TokenKind::RegularExpressionLiteral) {
                auto regTok = current_;
                advance();
                auto node = std::make_unique<ast::RegularExpressionLiteral>();
                setLocation(node.get(), regTok);
                node->text = std::string(regTok.text);
                return node;
            }
            throw std::runtime_error(fmt::format("{}:{}: Unexpected '/'", fileName_, tok.line));
        }

        // Contextual keywords that can be used as identifiers in expression position
        case TokenKind::KW_module:
        case TokenKind::KW_namespace:
        case TokenKind::KW_type:
        case TokenKind::KW_declare:
        case TokenKind::KW_abstract:
        case TokenKind::KW_interface:
        case TokenKind::KW_readonly:
        case TokenKind::KW_override:
        case TokenKind::KW_implements:
        case TokenKind::KW_from:
        case TokenKind::KW_of:
        case TokenKind::KW_as:
        case TokenKind::KW_is:
        case TokenKind::KW_get:
        case TokenKind::KW_set:
        case TokenKind::KW_require:
        case TokenKind::KW_asserts:
        case TokenKind::KW_satisfies:
        case TokenKind::KW_out:
        case TokenKind::KW_keyof:
        case TokenKind::KW_infer:
        case TokenKind::KW_constructor: {
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = std::string(tok.text);
            return node;
        }
        case TokenKind::KW_let: {
            // ES262 13.3.1: `let` is an Identifier in non-strict mode
            // when used as IdentifierReference (e.g., `let = 1;` after
            // `var let;`). Strict mode forbids this; the analyzer/parser
            // reaches here only after a let-as-decl lookahead check
            // determines this is the IdentifierReference path.
            if (strictMode_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'let' cannot be used as an "
                    "identifier in strict mode",
                    fileName_, tok.line));
            }
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "let";
            return node;
        }
        case TokenKind::KW_yield: {
            // ES262 12.6.1: `yield` is a valid IdentifierReference in
            // non-strict, non-generator code. The yield-expression form
            // (`yield expr`) is handled earlier in parseAssignmentExpression
            // when inGenerator_ is true.
            if (inGenerator_ || strictMode_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'yield' is not allowed as an "
                    "identifier inside a generator function or strict mode",
                    fileName_, tok.line));
            }
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "yield";
            return node;
        }
        case TokenKind::KW_public:
        case TokenKind::KW_private:
        case TokenKind::KW_protected:
        case TokenKind::KW_static: {
            // ES262 12.6.1: future-reserved-words `public`, `private`,
            // `protected`, `static` are reserved only in strict mode.
            // In non-strict code they are valid IdentifierReferences.
            if (strictMode_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: '{}' is a reserved word in strict mode",
                    fileName_, tok.line, std::string(tok.text)));
            }
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = std::string(tok.text);
            return node;
        }
        case TokenKind::KW_await: {
            // ES262 13.1.1: `await` is reserved as an IdentifierReference
            // ONLY in async function bodies and modules. Strict mode does
            // NOT reserve `await` — that's reserved by [Await] grammar
            // parameter, not strict-mode early-error rules. Class bodies
            // (which are strict) and other strict-mode contexts allow
            // `await` as an identifier provided we're not in an async
            // function.
            if (inAsync_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: 'await' is not allowed as an "
                    "identifier inside an async function or module",
                    fileName_, tok.line));
            }
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = "await";
            return node;
        }

        default:
            break;
    }

    throw std::runtime_error(fmt::format("{}:{}: Unexpected token '{}' ({})",
        fileName_, tok.line, std::string(tok.text), Lexer::tokenKindToString(tok.kind)));
}

// ============================================================================
// Complex expression types
// ============================================================================

ast::ExprPtr Parser::parseArrowFunctionOrParenthesized() {
    auto startTok = current_;
    bool isAsync = false;

    if (current_.kind == TokenKind::KW_async) {
        isAsync = true;
        advance();
    }

    // Try to parse as arrow function parameters
    // The try-catch only wraps parameter parsing + arrow detection.
    // Once '=>' is confirmed, body parsing errors propagate (they're real errors).
    auto saved = saveState();
    bool isArrow = false;
    std::vector<std::unique_ptr<ast::Parameter>> params;
    try {
        params = parseParameterList();

        // Optional return type annotation
        std::string returnType;
        if (check(TokenKind::Colon)) {
            returnType = parseReturnTypeAnnotation();
        }

        if (check(TokenKind::Arrow) && !current_.hadNewlineBefore) {
            advance(); // =>
            isArrow = true;
        }
    } catch (...) {
        // Not an arrow function - parameter parsing failed
    }

    if (isArrow) {
        // Arrow confirmed - body errors are real parse errors (don't catch)
        auto arrow = std::make_unique<ast::ArrowFunction>();
        setLocation(arrow.get(), startTok);
        arrow->isAsync = isAsync;
        arrow->parameters = std::move(params);

        bool prevAsync = inAsync_;
        StrictModeGuard sg(this);
        inAsync_ = isAsync;
        functionDepth_++;
        int prevIter = iterationDepth_, prevSwitch = switchDepth_;
        iterationDepth_ = 0; switchDepth_ = 0;

        if (check(TokenKind::OpenBrace)) {
            arrow->body = parseBlockStatement();
        } else {
            arrow->body = parseAssignmentExpression();
        }

        functionDepth_--;
        iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
        inAsync_ = prevAsync;
        return arrow;
    }

    restoreState(saved);

    // `async ident => body` — single-identifier async arrow without
    // parens. Per ECMA-262 14.7 AsyncArrowFunction, the param-list may
    // be a single binding identifier in this form. After restoreState,
    // we're at the position right after the leading `async` token (saved
    // was captured post-advance at line 949), so check for ident => here
    // before falling through to the `(`-expecting branch below. This
    // branch sits *before* the "Re-consume 'async'" advance so we don't
    // misalign the cursor.
    if (isAsync &&
        (current_.kind == TokenKind::Identifier ||
         isContextualKeywordAsIdentifier(current_.kind)) &&
        !current_.hadNewlineBefore) {
        auto identTok = current_;
        auto saved2 = saveState();
        advance();
        if (check(TokenKind::Arrow) && !current_.hadNewlineBefore) {
            advance(); // =>
            auto arrow = std::make_unique<ast::ArrowFunction>();
            setLocation(arrow.get(), startTok);
            arrow->isAsync = true;
            auto param = std::make_unique<ast::Parameter>();
            setLocation(param.get(), identTok);
            auto id = std::make_unique<ast::Identifier>();
            id->name = std::string(identTok.text);
            setLocation(id.get(), identTok);
            param->name = std::move(id);
            arrow->parameters.push_back(std::move(param));

            bool prevAsync = inAsync_;
            StrictModeGuard sg(this);
            inAsync_ = true;
            functionDepth_++;
            int prevIter = iterationDepth_, prevSwitch = switchDepth_;
            iterationDepth_ = 0; switchDepth_ = 0;
            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }
            functionDepth_--;
            iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
            inAsync_ = prevAsync;
            return arrow;
        }
        restoreState(saved2);
    }

    if (isAsync) {
        // Re-consume 'async'
        advance(); // async
    }

    // Parse as parenthesized expression
    expect(TokenKind::OpenParen, "'('");
    if (check(TokenKind::CloseParen)) {
        advance();
        // Empty parens - must be () =>
        if (check(TokenKind::Arrow)) {
            advance();
            auto arrow = std::make_unique<ast::ArrowFunction>();
            setLocation(arrow.get(), startTok);
            arrow->isAsync = isAsync;

            bool prevAsync = inAsync_;
            StrictModeGuard sg(this);
            inAsync_ = isAsync;
            functionDepth_++;
            int prevIter = iterationDepth_, prevSwitch = switchDepth_;
            iterationDepth_ = 0; switchDepth_ = 0;

            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }

            functionDepth_--;
            iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
            inAsync_ = prevAsync;
            return arrow;
        }
        throw std::runtime_error(fmt::format("{}:{}: Expected '=>' after '()'",
            fileName_, startTok.line));
    }

    auto expr = parseExpression();
    expect(TokenKind::CloseParen, "')'");
    lexer_->setRegexAllowed(false);
    // The parenthesized expression is transparent (matching AstLoader behavior)
    return expr;
}

ast::ExprPtr Parser::parseObjectLiteral() {
    auto startTok = current_;
    expect(TokenKind::OpenBrace, "'{'");

    auto node = std::make_unique<ast::ObjectLiteralExpression>();
    setLocation(node.get(), startTok);

    // ECMA-262: ObjectLiteral PropertyDefinition uses AssignmentExpression[+In].
    bool prevNoIn = noIn_;
    noIn_ = false;
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        // Spread property: ...expr
        if (check(TokenKind::DotDotDot)) {
            auto spreadTok = current_;
            advance();
            auto spread = std::make_unique<ast::SpreadElement>();
            setLocation(spread.get(), spreadTok);
            spread->expression = parseAssignmentExpression();
            node->properties.push_back(std::move(spread));
        }
        // Method definition or property
        else {
            bool isAsync = false;
            bool isGenerator = false;
            bool isGetter = false;
            bool isSetter = false;

            // async
            if (current_.kind == TokenKind::KW_async && !current_.hadNewlineBefore) {
                auto saved = saveState();
                advance();
                if (isIdentifierOrKeyword() || check(TokenKind::OpenBracket) ||
                    check(TokenKind::Star) || check(TokenKind::StringLiteral) ||
                    check(TokenKind::NumericLiteral)) {
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
                if (isIdentifierOrKeyword() || check(TokenKind::OpenBracket) ||
                    check(TokenKind::StringLiteral) || check(TokenKind::NumericLiteral)) {
                    if (isGet) isGetter = true;
                    else isSetter = true;
                } else {
                    restoreState(saved);
                }
            }

            // Property name
            std::string name;
            ast::NodePtr nameNode;
            // Capture before identifierName() advances past the token —
            // shorthand `{ break }` (where break is an escape-encoded
            // reserved word) is a SyntaxError because the shorthand value
            // is an IdentifierReference, but `{ break: x }` is fine
            // because the property name is an IdentifierName position.
            bool nameEscapedReserved = false;
            bool nameIsIdentifier = false;  // true => eligible for shorthand form
            int nameLine = current_.line;

            if (check(TokenKind::OpenBracket)) {
                // Computed property name — AssignmentExpression[+In].
                advance();
                auto cpn = std::make_unique<ast::ComputedPropertyName>();
                setLocation(cpn.get(), previous_);
                bool prevNoIn = noIn_;
                noIn_ = false;
                cpn->expression = parseAssignmentExpression();
                noIn_ = prevNoIn;
                expect(TokenKind::CloseBracket, "']'");
                name = "[computed]";
                nameNode = std::move(cpn);
            } else if (check(TokenKind::StringLiteral)) {
                name = Lexer::getStringValue(current_.text);
                auto lit = std::make_unique<ast::StringLiteral>();
                lit->value = name;
                setLocation(lit.get(), current_);
                nameNode = std::move(lit);
                advance();
            } else if (check(TokenKind::NumericLiteral)) {
                name = Parser::canonicalNumericPropertyName(current_.text);
                advance();
            } else if (check(TokenKind::BigIntLiteral)) {
                // ECMA-262: BigIntLiteral as PropertyName converts to its
                // decimal-string representation (the 'n' suffix is stripped).
                std::string lex(current_.text);
                if (!lex.empty() && lex.back() == 'n') lex.pop_back();
                name = lex;
                advance();
            } else {
                nameEscapedReserved = current_.escapedReservedWord;
                name = identifierName();
                nameIsIdentifier = true;
            }

            // Method: name(...) { }
            if (check(TokenKind::OpenParen) || check(TokenKind::LessThan) || isAsync || isGenerator || isGetter || isSetter) {
                auto method = parseMethodDefinition(name, std::move(nameNode),
                    false, false, isAsync, isGenerator, isGetter, isSetter,
                    ts::AccessModifier::Public, {});
                node->properties.push_back(std::move(method));
            }
            // Property: name: value
            else if (match(TokenKind::Colon)) {
                auto prop = std::make_unique<ast::PropertyAssignment>();
                setLocation(prop.get(), previous_);
                prop->name = name;
                prop->nameNode = std::move(nameNode);
                prop->initializer = parseAssignmentExpression();
                node->properties.push_back(std::move(prop));
            }
            // Shorthand property: { name }  or CoverInitializedName: { name = init }
            else {
                // ECMA-262 13.2.5: PropertyDefinition shorthand requires
                // an IdentifierReference. Numeric/string literals and
                // computed names cannot be used as shorthand.
                if (!nameIsIdentifier) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: object literal property "
                        "name must be followed by ':' (only "
                        "IdentifierReference is valid in shorthand form)",
                        fileName_, nameLine));
                }
                // Shorthand acts as IdentifierReference. An escape-encoded
                // reserved word here is a SyntaxError per ES262 12.6.1
                // (e.g. `({ break }) => {}` — the value side of `break` is
                // a reference, not a property name).
                if (nameEscapedReserved) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: identifier resolves to reserved "
                        "word via Unicode escape and cannot be used as a "
                        "shorthand property reference",
                        fileName_, nameLine));
                }
                // ECMA-262 12.6.1.1: strict-mode FutureReservedWords
                // (and `let` / `yield`) are not valid IdentifierReferences
                // in strict-mode code. The shorthand-form binds the same
                // identifier as both PropertyName and reference value, so
                // strict-mode rejection applies.
                if (strictMode_ && (name == "let" || name == "yield" ||
                                    name == "package" || name == "private" ||
                                    name == "protected" || name == "public" ||
                                    name == "interface" || name == "implements" ||
                                    name == "static")) {
                    throw std::runtime_error(fmt::format(
                        "{}:{}: SyntaxError: '{}' is a strict-mode reserved "
                        "word and cannot be used as a shorthand property "
                        "reference",
                        fileName_, nameLine, name));
                }
                auto prop = std::make_unique<ast::ShorthandPropertyAssignment>();
                setLocation(prop.get(), previous_);
                prop->name = name;
                // CoverInitializedName: `{ a = expr }`. Per ES262 13.2.5,
                // this form is part of the AssignmentPattern grammar (used
                // when the object literal is the LHS of an assignment).
                // A bare ObjectLiteral with this form is a Syntax Error,
                // but our parser doesn't know yet whether the literal will
                // become an assignment target — so accept it here and let
                // downstream catch misuse. Most test262 tests use it as a
                // destructuring assignment target.
                if (match(TokenKind::Equals)) {
                    // ES262 12.6.1.1: `eval` and `arguments` are not valid
                    // BindingIdentifiers in strict mode. CoverInitializedName
                    // is destructuring-target-only, and the shorthand acts as
                    // a binding target there, so reject in strict.
                    if (strictMode_ && (name == "eval" || name == "arguments")) {
                        throw std::runtime_error(fmt::format(
                            "{}:{}: SyntaxError: '{}' may not be used as a "
                            "binding identifier in strict mode",
                            fileName_, nameLine, name));
                    }
                    prop->initializer = parseAssignmentExpression();
                }
                node->properties.push_back(std::move(prop));
            }
        }

        if (!check(TokenKind::CloseBrace)) {
            match(TokenKind::Comma);  // Trailing comma is optional
        }
    }
    noIn_ = prevNoIn;

    expect(TokenKind::CloseBrace, "'}'");
    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseArrayLiteral() {
    auto startTok = current_;
    expect(TokenKind::OpenBracket, "'['");

    auto node = std::make_unique<ast::ArrayLiteralExpression>();
    setLocation(node.get(), startTok);

    // ECMA-262: ArrayLiteral elements are AssignmentExpression[+In].
    bool prevNoIn = noIn_;
    noIn_ = false;
    while (!check(TokenKind::CloseBracket) && !isAtEnd()) {
        if (check(TokenKind::Comma)) {
            // Elision (hole in array)
            auto omit = std::make_unique<ast::OmittedExpression>();
            setLocation(omit.get(), current_);
            node->elements.push_back(std::move(omit));
        } else if (check(TokenKind::DotDotDot)) {
            auto spreadTok = current_;
            advance();
            auto spread = std::make_unique<ast::SpreadElement>();
            setLocation(spread.get(), spreadTok);
            spread->expression = parseAssignmentExpression();
            node->elements.push_back(std::move(spread));
        } else {
            node->elements.push_back(parseAssignmentExpression());
        }
        if (!check(TokenKind::CloseBracket)) {
            expect(TokenKind::Comma, "','");
        }
    }
    noIn_ = prevNoIn;

    expect(TokenKind::CloseBracket, "']'");
    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseTemplateLiteral() {
    auto startTok = current_;
    auto node = std::make_unique<ast::TemplateExpression>();
    setLocation(node.get(), startTok);

    // TemplateHead: `text${
    std::string headText(current_.text);
    // Remove leading backtick and trailing ${
    if (headText.size() >= 3) {
        headText = headText.substr(1, headText.size() - 3);
    }
    Lexer::validateLegacyOctalEscapes(
        headText, strictMode_, /*isTemplate=*/true,
        startTok.line, startTok.column);
    node->head = Lexer::processTemplateEscapes(headText);
    advance();

    while (true) {
        // Parse expression inside ${}
        ast::TemplateSpan span;
        span.expression = parseExpression();

        // After parseExpression(), current_ should be TemplateTail or TemplateMiddle
        // because the lexer auto-detects template continuation when } matches template brace depth
        auto contTok = current_;

        if (contTok.kind == TokenKind::TemplateTail) {
            // }text` - remove leading } and trailing backtick
            std::string litText(contTok.text);
            if (litText.size() >= 2) {
                litText = litText.substr(1, litText.size() - 2);
            }
            Lexer::validateLegacyOctalEscapes(
                litText, strictMode_, /*isTemplate=*/true,
                contTok.line, contTok.column);
            span.literal = Lexer::processTemplateEscapes(litText);
            node->spans.push_back(std::move(span));
            advance();
            break;
        } else if (contTok.kind == TokenKind::TemplateMiddle) {
            // }text${ - remove leading } and trailing ${
            std::string litText(contTok.text);
            if (litText.size() >= 3) {
                litText = litText.substr(1, litText.size() - 3);
            }
            Lexer::validateLegacyOctalEscapes(
                litText, strictMode_, /*isTemplate=*/true,
                contTok.line, contTok.column);
            span.literal = Lexer::processTemplateEscapes(litText);
            node->spans.push_back(std::move(span));
            advance();
            continue;
        } else if (contTok.kind == TokenKind::CloseBrace) {
            // Fallback: manually scan continuation if lexer didn't auto-detect
            auto manualTok = lexer_->scanTemplateContinuation();
            current_ = manualTok;
            std::string litText(manualTok.text);
            if (manualTok.kind == TokenKind::TemplateTail) {
                if (litText.size() >= 2) litText = litText.substr(1, litText.size() - 2);
                Lexer::validateLegacyOctalEscapes(
                    litText, strictMode_, /*isTemplate=*/true,
                    manualTok.line, manualTok.column);
                span.literal = Lexer::processTemplateEscapes(litText);
                node->spans.push_back(std::move(span));
                advance();
                break;
            } else if (manualTok.kind == TokenKind::TemplateMiddle) {
                if (litText.size() >= 3) litText = litText.substr(1, litText.size() - 3);
                Lexer::validateLegacyOctalEscapes(
                    litText, strictMode_, /*isTemplate=*/true,
                    manualTok.line, manualTok.column);
                span.literal = Lexer::processTemplateEscapes(litText);
                node->spans.push_back(std::move(span));
                advance();
                continue;
            }
            throw std::runtime_error(fmt::format("{}:{}: Unterminated template literal",
                fileName_, startTok.line));
        } else {
            throw std::runtime_error(fmt::format("{}:{}: Unterminated template literal (got {})",
                fileName_, startTok.line, Lexer::tokenKindToString(contTok.kind)));
        }
    }

    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseTaggedTemplate(ast::ExprPtr tag) {
    auto node = std::make_unique<ast::TaggedTemplateExpression>();
    setLocation(node.get(), tag->line, tag->column);
    node->tag = std::move(tag);

    if (check(TokenKind::NoSubstitutionTemplate)) {
        auto tmpl = std::make_unique<ast::TemplateExpression>();
        setLocation(tmpl.get(), current_);
        std::string text(current_.text);
        if (text.size() >= 2) text = text.substr(1, text.size() - 2);
        tmpl->head = Lexer::processTemplateEscapes(text);
        advance();
        node->templateExpr = std::move(tmpl);
    } else {
        node->templateExpr = parseTemplateLiteral();
    }

    return node;
}

ast::ExprPtr Parser::parseFunctionExpression(bool isAsync) {
    auto startTok = current_;
    expect(TokenKind::KW_function, "'function'");

    auto node = std::make_unique<ast::FunctionExpression>();
    setLocation(node.get(), startTok);
    node->isAsync = isAsync;

    if (match(TokenKind::Star)) {
        node->isGenerator = true;
    }

    // Optional name. Track for the strict-mode early-error check below.
    int nameLine = current_.line;
    if (isIdentifierOrKeyword() && !check(TokenKind::OpenParen)) {
        node->name = identifierName();
    }
    // ECMA-262 14.1.5.3: BindingIdentifier of FunctionExpression must not
    // be 'eval' or 'arguments' in strict-mode code. The outer strict
    // case is checked here; the inner-strict (body has "use strict"
    // directive prologue) case is checked after the body parses, since
    // we only learn the body's strict-ness then.
    if (strictMode_ && (node->name == "eval" || node->name == "arguments")) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: function expression name may not be "
            "'eval' or 'arguments' in strict mode",
            fileName_, nameLine));
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // ECMA-262: parameter list uses the new function's [Await]/[Yield]
    // flags, not the outer context's. Save outer flags, set per-function
    // flags before parseParameterList.
    bool prevAsyncOuter = inAsync_;
    bool prevGenOuter = inGenerator_;
    inAsync_ = node->isAsync;
    inGenerator_ = node->isGenerator;

    // Parameters
    node->parameters = parseParameterList();

    // Return type
    if (check(TokenKind::Colon)) {
        node->returnType = parseReturnTypeAnnotation();
    }

    // Body
    bool prevAsync = inAsync_;
    bool prevGen = inGenerator_;
    StrictModeGuard sg(this);
    inAsync_ = node->isAsync;
    inGenerator_ = node->isGenerator;
    functionDepth_++;
    int prevIter = iterationDepth_, prevSwitch = switchDepth_;
    iterationDepth_ = 0; switchDepth_ = 0;
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

    if (sawUseStrictDirective_ &&
        !isParameterListSimple(node->parameters)) {
        throw std::runtime_error(fmt::format(
            "{}:{}: function with non-simple parameter list may not "
            "declare \"use strict\"",
            current_.line, current_.column));
    }
    // ECMA-262 14.1.5.3: BindingIdentifier of FunctionExpression with
    // a body containing "use strict" directive is also strict-mode
    // code. Re-check name === 'eval'/'arguments' after the body parses.
    if (sawUseStrictDirective_ &&
        (node->name == "eval" || node->name == "arguments")) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: function expression name may not be "
            "'eval' or 'arguments' when body is strict-mode code",
            fileName_, nameLine));
    }
    sawUseStrictDirective_ = prevSawUseStrict;

    functionDepth_--;
    iterationDepth_ = prevIter; switchDepth_ = prevSwitch;
    inAsync_ = prevAsync;
    inGenerator_ = prevGen;
    // Restore outer flags (the param-list scope).
    inAsync_ = prevAsyncOuter;
    inGenerator_ = prevGenOuter;

    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseClassExpression() {
    auto startTok = current_;
    expect(TokenKind::KW_class, "'class'");

    auto node = std::make_unique<ast::ClassExpression>();
    setLocation(node.get(), startTok);

    // Optional name. Class body is strict (ES262 10.2.1) so escape-
    // encoded reserved words including contextual-strict ones (let,
    // static, yield) must be rejected.
    if (isIdentifierOrKeyword() && !check(TokenKind::KW_extends) &&
        !check(TokenKind::KW_implements) && !check(TokenKind::OpenBrace)) {
        if (current_.escapedReservedWord) {
            // ECMA-262: `await` is NOT a reserved word in script-mode class
            // bodies — it's only reserved in modules and async function/
            // method bodies. Class body being strict doesn't change that
            // (strict reserves `yield`, but not `await`).
            bool isAwaitEscape = current_.decodedText == "await";
            if (!isAwaitEscape || inAsync_) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: SyntaxError: identifier resolves to reserved word "
                    "via Unicode escape and cannot be used as a class name",
                    fileName_, current_.line));
            }
        }
        node->name = identifierName();
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends — mirror parseClassDeclaration's complex-LHS handling so that
    // ECMA-262 ClassHeritage : extends LeftHandSideExpression works for
    // class EXPRESSIONS too (`class extends function(){} {}`, `class extends
    // 42 {}`, `class extends Foo.bar() {}`, etc.). The simple-identifier
    // fast-path keeps the legacy node->baseClass string for analyzer
    // lookups; the complex path consumes the LHS and leaves baseClass
    // empty (downstream still registers the class).
    if (match(TokenKind::KW_extends)) {
        bool simple = false;
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
                if (check(TokenKind::LessThan)) skipTypeExpression();
                simple = true;
            } else {
                restoreState(saved);
            }
        }
        if (!simple) {
            bool lhsStart = current_.kind == TokenKind::Identifier ||
                            check(TokenKind::KW_new) ||
                            check(TokenKind::KW_super) ||
                            check(TokenKind::KW_this) ||
                            check(TokenKind::KW_class) ||
                            check(TokenKind::KW_function) ||
                            check(TokenKind::KW_null) ||
                            check(TokenKind::KW_true) ||
                            check(TokenKind::KW_false) ||
                            check(TokenKind::NumericLiteral) ||
                            check(TokenKind::StringLiteral) ||
                            check(TokenKind::TemplateHead) ||
                            check(TokenKind::NoSubstitutionTemplate) ||
                            check(TokenKind::RegularExpressionLiteral) ||
                            check(TokenKind::BigIntLiteral) ||
                            check(TokenKind::OpenParen) ||
                            check(TokenKind::OpenBracket);
            if (lhsStart) (void)parseCallExpression();
        }
    }

    // implements
    if (current_.kind == TokenKind::KW_implements) {
        advance();
        do {
            node->implementsInterfaces.push_back(identifierName());
            if (check(TokenKind::LessThan)) skipTypeExpression();
        } while (match(TokenKind::Comma));
    }

    // Body. ECMA-262 §10.2.1: ClassBody is always strict-mode code.
    StrictModeGuard csg(this);
    strictMode_ = true;
    expect(TokenKind::OpenBrace, "'{'");
    // ECMA-262 15.7.1 Static Semantics: Early Errors — duplicate
    // private name detection. Mirrors parseClassDeclaration's logic;
    // see that site for spec rationale. Static and instance share the
    // private namespace.
    struct PrivateEntryE {
        bool isGetter = false;
        bool isSetter = false;
        bool isOther = false;
        int line = 0;
    };
    std::unordered_map<std::string, PrivateEntryE> privateNamesE;
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto member = parseClassMember();
        if (member) {
            if (auto* m = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                bool methodIsAscii = true;
                for (unsigned char c : m->name) if (c >= 0x80) { methodIsAscii = false; break; }
                if (methodIsAscii && !m->name.empty() && m->name[0] == '#') {
                    auto& e = privateNamesE[m->name];
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
                    auto& e = privateNamesE[p->name];
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
        while (match(TokenKind::Semicolon)) {}
    }
    expect(TokenKind::CloseBrace, "'}'");

    return node;
}

ast::ExprPtr Parser::parseNewExpression() {
    auto startTok = current_;
    expect(TokenKind::KW_new, "'new'");

    // new.target
    if (match(TokenKind::Dot)) {
        auto prop = std::make_unique<ast::PropertyAccessExpression>();
        setLocation(prop.get(), startTok);
        auto newId = std::make_unique<ast::Identifier>();
        newId->name = "new";
        setLocation(newId.get(), startTok);
        prop->expression = std::move(newId);
        prop->name = identifierName();
        return prop;
    }

    // ECMA-262: ImportCall is a CallExpression, not a NewExpression.
    // `new import(x)` is a SyntaxError per the spec grammar — same for
    // `new import.meta` (import.meta is not a constructor) — but we
    // permit `new import.meta.X(...)` style expressions through the
    // member-expression chain below; only `new import(...)` directly
    // is invalid. The check here covers the direct-call case.
    if (current_.kind == TokenKind::KW_import) {
        throw std::runtime_error(fmt::format(
            "{}:{}: SyntaxError: 'new import(...)' is not allowed",
            fileName_, current_.line));
    }

    auto node = std::make_unique<ast::NewExpression>();
    setLocation(node.get(), startTok);

    // Parse the constructor expression (without calls)
    node->expression = parseMemberExpression();

    // Handle member access on the new expression target
    while (check(TokenKind::Dot) || check(TokenKind::OpenBracket)) {
        if (match(TokenKind::Dot)) {
            auto access = std::make_unique<ast::PropertyAccessExpression>();
            setLocation(access.get(), node->expression->line, node->expression->column);
            access->expression = std::move(node->expression);
            access->name = identifierName();
            node->expression = std::move(access);
        } else if (check(TokenKind::OpenBracket)) {
            advance();
            auto access = std::make_unique<ast::ElementAccessExpression>();
            setLocation(access.get(), node->expression->line, node->expression->column);
            access->expression = std::move(node->expression);
            access->argumentExpression = parseExpression();
            expect(TokenKind::CloseBracket, "']'");
            node->expression = std::move(access);
        }
    }

    // Type arguments
    if (check(TokenKind::LessThan)) {
        auto saved = saveState();
        try {
            node->typeArguments = parseTypeArguments();
        } catch (...) {
            restoreState(saved);
        }
    }

    // Arguments (optional for new)
    if (match(TokenKind::OpenParen)) {
        while (!check(TokenKind::CloseParen) && !isAtEnd()) {
            if (check(TokenKind::DotDotDot)) {
                auto spreadTok = current_;
                advance();
                auto spread = std::make_unique<ast::SpreadElement>();
                setLocation(spread.get(), spreadTok);
                spread->expression = parseAssignmentExpression();
                node->arguments.push_back(std::move(spread));
            } else {
                node->arguments.push_back(parseAssignmentExpression());
            }
            if (!check(TokenKind::CloseParen)) {
                expect(TokenKind::Comma, "','");
            }
        }
        expect(TokenKind::CloseParen, "')'");
    }

    lexer_->setRegexAllowed(false);
    return node;
}

} // namespace ts::parser
