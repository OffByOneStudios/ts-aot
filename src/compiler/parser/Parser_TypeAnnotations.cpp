#include "Parser.h"
#include <stdexcept>
#include <fmt/format.h>

namespace ts::parser {

// ============================================================================
// Type annotation parsing
//
// TypeScript type annotations are stored as opaque strings in the AST
// (matching what dump_ast.js produces). These methods scan and collect
// the raw source text of type expressions, balancing brackets as needed.
// ============================================================================

/// Scan a complete type expression and return its source text.
/// This is the core routine used by all type annotation parsing.
///
/// Stops at (when depth == 0):
///   , ) } ] ; => = (unless in a mapped/conditional type)
///   { only stops if not inside a type literal
///
/// Balances: < > ( ) [ ] { }
std::string Parser::scanTypeExpression() {
    int startOffset = current_.offset;
    int depth = 0;        // Combined bracket depth
    int angleDepth = 0;   // < > depth
    int parenDepth = 0;   // ( ) depth
    int bracketDepth = 0; // [ ] depth
    int braceDepth = 0;   // { } depth
    TokenKind lastTokenKind = TokenKind::EndOfFile;

    while (!isAtEnd()) {
        auto kind = current_.kind;

        // At top level (no nesting), these tokens end the type
        if (depth == 0 && angleDepth == 0) {
            if (kind == TokenKind::Arrow) {
                // => is part of a function type if preceded by ')' at top level
                // e.g., () => void, (a: number, b: string) => boolean
                if (lastTokenKind == TokenKind::CloseParen) {
                    // Function type arrow: continue scanning the return type
                } else {
                    break;
                }
            }
            if (kind == TokenKind::Comma ||
                kind == TokenKind::CloseParen ||
                kind == TokenKind::CloseBrace ||
                kind == TokenKind::CloseBracket ||
                kind == TokenKind::Semicolon ||
                kind == TokenKind::Equals ||
                kind == TokenKind::GreaterThan ||
                kind == TokenKind::GreaterThanGreaterThan ||
                kind == TokenKind::GreaterThanGreaterThanGreaterThan) {
                break;
            }
            // OpenBrace at depth 0: if we've already consumed tokens,
            // this is a function/class body, not a type literal.
            // If it's the FIRST token, it starts a type literal like { x: number }.
            // Exception: after & or | it's an intersection/union member like { a: number } & { b: string }
            if (kind == TokenKind::OpenBrace && current_.offset > startOffset) {
                if (lastTokenKind != TokenKind::Ampersand && lastTokenKind != TokenKind::Pipe) {
                    break;
                }
            }
        }

        // At bracket depth 0, but inside angle brackets only,
        // comma still separates type arguments
        // Let those be consumed by the parent (parseTypeArguments or parameter list)

        switch (kind) {
            case TokenKind::LessThan:
                angleDepth++;
                depth++;
                break;
            case TokenKind::GreaterThan:
                if (angleDepth > 0) {
                    angleDepth--;
                    depth--;
                }
                // If angleDepth == 0, this > might be a comparison
                // But in type context we treat it as closing a type arg
                if (angleDepth < 0) angleDepth = 0;
                if (depth < 0) depth = 0;
                break;
            case TokenKind::GreaterThanGreaterThan:
                // >> can close two levels of type params
                if (angleDepth >= 2) {
                    angleDepth -= 2;
                    depth -= 2;
                }
                if (angleDepth < 0) angleDepth = 0;
                if (depth < 0) depth = 0;
                break;
            case TokenKind::OpenParen:
                parenDepth++;
                depth++;
                break;
            case TokenKind::CloseParen:
                if (parenDepth > 0) {
                    parenDepth--;
                    depth--;
                } else {
                    // Unmatched ) — end of type
                    goto done;
                }
                break;
            case TokenKind::OpenBracket:
                bracketDepth++;
                depth++;
                break;
            case TokenKind::CloseBracket:
                if (bracketDepth > 0) {
                    bracketDepth--;
                    depth--;
                } else {
                    goto done;
                }
                break;
            case TokenKind::OpenBrace:
                braceDepth++;
                depth++;
                break;
            case TokenKind::CloseBrace:
                if (braceDepth > 0) {
                    braceDepth--;
                    depth--;
                } else {
                    goto done;
                }
                break;
            default:
                break;
        }

        lastTokenKind = kind;
        advance();
    }

done:
    int endOffset = current_.offset;

    // Extract source text between start and end
    if (endOffset <= startOffset) return "";

    auto text = lexer_->getSourceRange(startOffset, endOffset);
    // Trim whitespace from both ends
    std::string result(text);
    size_t start = result.find_first_not_of(" \t\r\n");
    size_t end = result.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

/// Skip over a type expression without collecting text.
/// Used for skipping generic type arguments on base classes, etc.
void Parser::skipTypeExpression() {
    int depth = 0;

    while (!isAtEnd()) {
        auto kind = current_.kind;

        if (depth == 0) {
            if (kind == TokenKind::Comma ||
                kind == TokenKind::CloseParen ||
                kind == TokenKind::CloseBrace ||
                kind == TokenKind::CloseBracket ||
                kind == TokenKind::Semicolon ||
                kind == TokenKind::Arrow ||
                kind == TokenKind::Equals ||
                kind == TokenKind::OpenBrace) {
                break;
            }
        }

        switch (kind) {
            case TokenKind::LessThan:
                depth++;
                break;
            case TokenKind::GreaterThan:
                depth--;
                if (depth < 0) { depth = 0; goto skip_done; }
                break;
            case TokenKind::GreaterThanGreaterThan:
                depth -= 2;
                if (depth < 0) { depth = 0; goto skip_done; }
                break;
            case TokenKind::OpenParen:
            case TokenKind::OpenBracket:
                depth++;
                break;
            case TokenKind::CloseParen:
            case TokenKind::CloseBracket:
                depth--;
                if (depth < 0) { depth = 0; goto skip_done; }
                break;
            default:
                break;
        }

        advance();
    }

skip_done:
    return;
}

/// Parse a type annotation after ':' (e.g., in variable declarations, parameters).
/// Consumes the ':' token and returns the type as a string.
std::string Parser::parseTypeAnnotation() {
    expect(TokenKind::Colon, "':'");
    return scanTypeExpression();
}

/// Parse a return type annotation after ':'.
/// Handles type predicates like ': this is Foo' and ': asserts x is Foo'.
std::string Parser::parseReturnTypeAnnotation() {
    expect(TokenKind::Colon, "':'");
    return scanTypeExpression();
}

/// Parse type arguments: <Type1, Type2, ...>
/// Returns a vector of type strings.
/// Used for generic call expressions like foo<T, U>(args).
std::vector<std::string> Parser::parseTypeArguments() {
    std::vector<std::string> args;

    if (!check(TokenKind::LessThan)) return args;
    advance(); // <

    while (!check(TokenKind::GreaterThan) && !isAtEnd()) {
        args.push_back(scanTypeExpression());
        if (!check(TokenKind::GreaterThan)) {
            expect(TokenKind::Comma, "','");
        }
    }

    expect(TokenKind::GreaterThan, "'>'");
    return args;
}

} // namespace ts::parser
