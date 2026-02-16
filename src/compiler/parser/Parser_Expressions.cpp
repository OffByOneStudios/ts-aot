#include "Parser.h"
#include <stdexcept>
#include <fmt/format.h>
#include <cstdlib>
#include <cmath>

namespace ts::parser {

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
        if (current_.kind == TokenKind::Identifier && !current_.hadNewlineBefore) {
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
    if (current_.kind == TokenKind::Identifier) {
        auto saved = saveState();
        std::string name(current_.text);
        int line = current_.line, col = current_.column;
        advance();
        if (check(TokenKind::Arrow) && !current_.hadNewlineBefore) {
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
            inAsync_ = false;
            functionDepth_++;

            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }

            functionDepth_--;
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
        cond->whenTrue = parseAssignmentExpression();
        expect(TokenKind::Colon, "':'");
        cond->whenFalse = parseAssignmentExpression();
        return cond;
    }

    // Assignment operators
    if (isAssignmentOperator(current_.kind)) {
        auto opTok = current_;
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
            return node;
        }
        case TokenKind::KW_typeof: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = "typeof";
            node->operand = parseUnaryExpression();
            return node;
        }
        case TokenKind::KW_void: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::PrefixUnaryExpression>();
            setLocation(node.get(), tok);
            node->op = "void";
            node->operand = parseUnaryExpression();
            return node;
        }
        case TokenKind::KW_delete: {
            auto tok = current_;
            advance();
            auto node = std::make_unique<ast::DeleteExpression>();
            setLocation(node.get(), tok);
            node->expression = parseUnaryExpression();
            return node;
        }
        case TokenKind::KW_await: {
            if (inAsync_ || functionDepth_ == 0) {
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
            auto node = std::make_unique<ast::PostfixUnaryExpression>();
            setLocation(node.get(), expr->line, expr->column);
            node->op = "++";
            node->operand = std::move(expr);
            return node;
        }
        if (current_.kind == TokenKind::MinusMinus) {
            auto tok = current_;
            advance();
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
            advance();
            auto node = std::make_unique<ast::Identifier>();
            setLocation(node.get(), tok);
            node->name = std::string(tok.text);
            return node;
        }

        case TokenKind::NumericLiteral: {
            advance();
            auto node = std::make_unique<ast::NumericLiteral>();
            setLocation(node.get(), tok);
            std::string text(tok.text);
            // Handle hex, octal, binary
            if (text.size() > 1 && text[0] == '0') {
                if (text[1] == 'x' || text[1] == 'X') {
                    node->value = static_cast<double>(std::stoull(text, nullptr, 16));
                } else if (text[1] == 'o' || text[1] == 'O') {
                    node->value = static_cast<double>(std::stoull(text, nullptr, 8));
                } else if (text[1] == 'b' || text[1] == 'B') {
                    node->value = static_cast<double>(std::stoull(text, nullptr, 2));
                } else {
                    // Remove underscores for numeric separators
                    std::string clean;
                    for (char c : text) if (c != '_') clean += c;
                    node->value = std::stod(clean);
                }
            } else {
                std::string clean;
                for (char c : text) if (c != '_') clean += c;
                node->value = std::stod(clean);
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
            node->head = text;
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
                // Dynamic import
                advance(); // (
                auto node = std::make_unique<ast::DynamicImport>();
                setLocation(node.get(), tok);
                node->moduleSpecifier = parseAssignmentExpression();
                expect(TokenKind::CloseParen, "')'");
                return node;
            }
            // Fallback
            auto id = std::make_unique<ast::Identifier>();
            id->name = "import";
            setLocation(id.get(), tok);
            return id;
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
    auto saved = saveState();
    try {
        auto params = parseParameterList();

        // Optional return type annotation
        std::string returnType;
        if (check(TokenKind::Colon)) {
            returnType = parseReturnTypeAnnotation();
        }

        if (check(TokenKind::Arrow) && !current_.hadNewlineBefore) {
            advance(); // =>

            auto arrow = std::make_unique<ast::ArrowFunction>();
            setLocation(arrow.get(), startTok);
            arrow->isAsync = isAsync;
            arrow->parameters = std::move(params);

            bool prevAsync = inAsync_;
            inAsync_ = isAsync;
            functionDepth_++;

            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }

            functionDepth_--;
            inAsync_ = prevAsync;
            return arrow;
        }
    } catch (...) {
        // Not an arrow function
    }

    restoreState(saved);
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
            inAsync_ = isAsync;
            functionDepth_++;

            if (check(TokenKind::OpenBrace)) {
                arrow->body = parseBlockStatement();
            } else {
                arrow->body = parseAssignmentExpression();
            }

            functionDepth_--;
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

            if (check(TokenKind::OpenBracket)) {
                // Computed property name
                advance();
                auto cpn = std::make_unique<ast::ComputedPropertyName>();
                setLocation(cpn.get(), previous_);
                cpn->expression = parseAssignmentExpression();
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
                name = std::string(current_.text);
                advance();
            } else {
                name = identifierName();
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
            // Shorthand property: { name }
            else {
                auto prop = std::make_unique<ast::ShorthandPropertyAssignment>();
                setLocation(prop.get(), previous_);
                prop->name = name;
                node->properties.push_back(std::move(prop));
            }
        }

        if (!check(TokenKind::CloseBrace)) {
            match(TokenKind::Comma);  // Trailing comma is optional
        }
    }

    expect(TokenKind::CloseBrace, "'}'");
    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseArrayLiteral() {
    auto startTok = current_;
    expect(TokenKind::OpenBracket, "'['");

    auto node = std::make_unique<ast::ArrayLiteralExpression>();
    setLocation(node.get(), startTok);

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
    node->head = headText;
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
            span.literal = litText;
            node->spans.push_back(std::move(span));
            advance();
            break;
        } else if (contTok.kind == TokenKind::TemplateMiddle) {
            // }text${ - remove leading } and trailing ${
            std::string litText(contTok.text);
            if (litText.size() >= 3) {
                litText = litText.substr(1, litText.size() - 3);
            }
            span.literal = litText;
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
                span.literal = litText;
                node->spans.push_back(std::move(span));
                advance();
                break;
            } else if (manualTok.kind == TokenKind::TemplateMiddle) {
                if (litText.size() >= 3) litText = litText.substr(1, litText.size() - 3);
                span.literal = litText;
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
        tmpl->head = text;
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

    // Optional name
    if (isIdentifierOrKeyword() && !check(TokenKind::OpenParen)) {
        node->name = identifierName();
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
    bool prevAsync = inAsync_;
    bool prevGen = inGenerator_;
    inAsync_ = node->isAsync;
    inGenerator_ = node->isGenerator;
    functionDepth_++;

    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->body.push_back(std::move(stmt));
    }
    expect(TokenKind::CloseBrace, "'}'");

    functionDepth_--;
    inAsync_ = prevAsync;
    inGenerator_ = prevGen;

    lexer_->setRegexAllowed(false);
    return node;
}

ast::ExprPtr Parser::parseClassExpression() {
    auto startTok = current_;
    expect(TokenKind::KW_class, "'class'");

    auto node = std::make_unique<ast::ClassExpression>();
    setLocation(node.get(), startTok);

    // Optional name
    if (isIdentifierOrKeyword() && !check(TokenKind::KW_extends) &&
        !check(TokenKind::KW_implements) && !check(TokenKind::OpenBrace)) {
        node->name = identifierName();
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends
    if (match(TokenKind::KW_extends)) {
        node->baseClass = identifierName();
        if (check(TokenKind::LessThan)) skipTypeExpression();
    }

    // implements
    if (current_.kind == TokenKind::KW_implements) {
        advance();
        do {
            node->implementsInterfaces.push_back(identifierName());
            if (check(TokenKind::LessThan)) skipTypeExpression();
        } while (match(TokenKind::Comma));
    }

    // Body
    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto member = parseClassMember();
        if (member) node->members.push_back(std::move(member));
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
