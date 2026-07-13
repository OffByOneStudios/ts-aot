#include "Parser.h"
#include <stdexcept>
#include <fmt/format.h>

namespace ts::parser {

// ============================================================================
// Type annotation parsing — STRUCTURAL recognizer (TSCONF plan, 2026-07-13).
//
// Type annotations are still stored as opaque strings in the AST (that
// contract is unchanged), but the string boundaries are now found by walking
// the actual TypeScript type grammar instead of the old bracket-depth
// heuristic scanner. The heuristic scanner could not tell where a type ENDED
// in ambiguous positions (`foo: { bar: T }` followed by a class member
// swallowed the member; every fix there bred a new heuristic). The
// recognizer consumes exactly one Type production and stops.
//
// Grammar coverage (permissive superset — we ACCEPT, the .errors.txt
// negative axis is scored separately):
//   Type            = [asserts-predicate] NonConditional
//                     [ 'extends' NonConditional '?' Type ':' Type ]
//                     [ 'is' Type ]                 (return-type predicates)
//   NonConditional  = ['|'|'&'] Operand { ('|'|'&') Operand }
//   Operand         = { 'keyof' 'readonly' 'unique' } PrefixForm { Postfix }
//   PrefixForm      = 'typeof' Entity | 'infer' Name | ['abstract'] 'new'
//                     FnType | Primary
//   Postfix         = '[' [Type] ']'                (array / indexed access)
//   Primary         = Entity [TypeArgs] | literal types | template type
//                   | '(' paren-or-function | '{' members '}' | '[' tuple ']'
//                   | '<'TypeParams'>' '(' params ')' '=>' Type
//                   | 'import' '(' string ')' ['.' Entity] [TypeArgs]
// ============================================================================

namespace {
bool isCloseAngleFamily(TokenKind k) {
    return k == TokenKind::GreaterThan ||
           k == TokenKind::GreaterThanGreaterThan ||
           k == TokenKind::GreaterThanGreaterThanGreaterThan ||
           k == TokenKind::GreaterThanEquals;
}
}  // namespace

// Consume one '>' out of the current close-angle-family token: a plain '>'
// advances; compound tokens ('>>', '>>>', '>=') are SPLIT by rewriting the
// current token in place (offset/column shift by one). This is how nested
// type arguments `Foo<Bar<T>>` close without lexer cooperation.
void Parser::typeExpectCloseAngle() {
    switch (current_.kind) {
        case TokenKind::GreaterThan:
            advance();
            return;
        case TokenKind::GreaterThanGreaterThan:
            current_.kind = TokenKind::GreaterThan;
            break;
        case TokenKind::GreaterThanGreaterThanGreaterThan:
            current_.kind = TokenKind::GreaterThanGreaterThan;
            break;
        case TokenKind::GreaterThanEquals:
            current_.kind = TokenKind::Equals;
            break;
        default:
            throw std::runtime_error(fmt::format(
                "{}:{}: Expected '>' in type but got '{}'",
                fileName_, current_.line, std::string(current_.text)));
    }
    current_.offset += 1;
    current_.column += 1;
    current_.text.remove_prefix(1);
}

// Name position inside a type: identifiers and (almost) every keyword are
// legal type names / member names (`string`, `any`, `catch`, ...).
void Parser::typeName() {
    if (current_.kind == TokenKind::Identifier || Lexer::isKeyword(current_.kind)) {
        advance();
        return;
    }
    throw std::runtime_error(fmt::format(
        "{}:{}: Expected a type name but got '{}'",
        fileName_, current_.line, std::string(current_.text)));
}

// Dotted entity name: A.B.C (each segment may be a keyword).
void Parser::typeEntityName() {
    typeName();
    while (check(TokenKind::Dot)) {
        advance();
        typeName();
    }
}

// Optional type arguments `<T, U>`; empty `<>` tolerated permissively.
void Parser::typeArgumentsIfPresent() {
    if (!check(TokenKind::LessThan)) return;
    advance();  // <
    while (!isCloseAngleFamily(current_.kind) && !isAtEnd()) {
        typeExpr();
        if (!isCloseAngleFamily(current_.kind)) {
            expect(TokenKind::Comma, "','");
        }
    }
    typeExpectCloseAngle();
}

// Balanced skip for a destructuring BINDING PATTERN inside function-type
// parameters (`({a, b}: T) => U`). Pattern internals are bindings, not
// types, so a structural skip is correct here.
void Parser::typeSkipBalanced(TokenKind open, TokenKind close) {
    expect(open, "opening bracket");
    int depth = 1;
    while (depth > 0 && !isAtEnd()) {
        if (current_.kind == open) depth++;
        else if (current_.kind == close) depth--;
        advance();
    }
}

// Function-type parameter list, already positioned ON '('. Throws when the
// contents are not parameter-shaped (caller treats that as "not a function
// type" and reparses as a parenthesized type).
void Parser::typeFunctionParams() {
    expect(TokenKind::OpenParen, "'('");
    while (!check(TokenKind::CloseParen) && !isAtEnd()) {
        if (check(TokenKind::DotDotDot)) advance();
        if (check(TokenKind::OpenBrace)) {
            typeSkipBalanced(TokenKind::OpenBrace, TokenKind::CloseBrace);
        } else if (check(TokenKind::OpenBracket)) {
            typeSkipBalanced(TokenKind::OpenBracket, TokenKind::CloseBracket);
        } else if (current_.kind == TokenKind::KW_this) {
            advance();
        } else {
            typeName();
        }
        if (match(TokenKind::QuestionMark)) { /* optional param */ }
        if (match(TokenKind::Colon)) typeExpr();
        // Defaults are illegal in function TYPES; their presence means this
        // '(' was an expression, so throwing here correctly aborts the
        // speculative function-type parse.
        if (!check(TokenKind::CloseParen)) {
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::CloseParen, "')'");
}

// '(' in type position: function type `(a: T) => U` or parenthesized type
// `(T | U)`. Resolved by a speculative parameter parse + '=>' confirmation,
// same cover-grammar approach as arrow functions in expressions.
void Parser::typeParenOrFunction() {
    auto saved = saveState();
    try {
        typeFunctionParams();
        if (check(TokenKind::Arrow)) {
            advance();
            typeExpr();  // return type
            return;
        }
    } catch (...) {
        // fall through to parenthesized
    }
    restoreState(saved);
    expect(TokenKind::OpenParen, "'('");
    typeExpr();
    expect(TokenKind::CloseParen, "')'");
}

// Template literal type: `prefix${T}suffix`. Mirrors the expression
// template-continuation handling (the lexer needs a manual rescan when the
// '}' after the embedded type was lexed as CloseBrace).
void Parser::typeTemplate() {
    if (current_.kind == TokenKind::NoSubstitutionTemplate) {
        advance();
        return;
    }
    expect(TokenKind::TemplateHead, "template head");
    while (true) {
        typeExpr();
        if (current_.kind == TokenKind::TemplateTail) {
            advance();
            return;
        }
        if (current_.kind == TokenKind::TemplateMiddle) {
            advance();
            continue;
        }
        if (current_.kind == TokenKind::CloseBrace) {
            auto manualTok = lexer_->scanTemplateContinuation();
            current_ = manualTok;
            if (manualTok.kind == TokenKind::TemplateTail) {
                advance();
                return;
            }
            if (manualTok.kind == TokenKind::TemplateMiddle) {
                advance();
                continue;
            }
        }
        throw std::runtime_error(fmt::format(
            "{}:{}: Unterminated template literal type", fileName_, current_.line));
    }
}

// Object type body, positioned ON '{'. Covers property/method signatures,
// call/construct signatures, index signatures, mapped types, and get/set
// accessors; separators ';' ',' or newline (ASI).
void Parser::typeObjectBody() {
    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        int memberStart = current_.offset;

        // Mapped-type / readonly modifiers: [+|-] readonly
        if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            advance();
            expect(TokenKind::KW_readonly, "'readonly'");
        } else if (check(TokenKind::KW_readonly)) {
            // `readonly` may itself be a property NAME (`readonly: T`) —
            // only treat as modifier when a member follows.
            auto saved = saveState();
            advance();
            if (check(TokenKind::Colon) || check(TokenKind::QuestionMark) ||
                check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
                restoreState(saved);  // it was the member name
            }
        }

        if (check(TokenKind::OpenBracket)) {
            // Index signature `[k: string]: T`, mapped type `[K in T as U]`,
            // or computed name `[Symbol.iterator]() {...}`.
            advance();
            bool handled = false;
            if (current_.kind == TokenKind::Identifier || Lexer::isKeyword(current_.kind)) {
                auto saved = saveState();
                typeName();
                if (check(TokenKind::KW_in)) {
                    advance();
                    typeExpr();
                    if (check(TokenKind::KW_as)) { advance(); typeExpr(); }
                    expect(TokenKind::CloseBracket, "']'");
                    // optional-modifier: [+|-]?
                    if (check(TokenKind::Plus) || check(TokenKind::Minus)) advance();
                    match(TokenKind::QuestionMark);
                    if (match(TokenKind::Colon)) typeExpr();
                    handled = true;
                } else if (check(TokenKind::Colon)) {
                    advance();
                    typeExpr();
                    expect(TokenKind::CloseBracket, "']'");
                    match(TokenKind::QuestionMark);
                    expect(TokenKind::Colon, "':'");
                    typeExpr();
                    handled = true;
                } else {
                    restoreState(saved);
                }
            }
            if (!handled) {
                // Computed property name: an expression in brackets.
                parseAssignmentExpression();
                expect(TokenKind::CloseBracket, "']'");
                match(TokenKind::QuestionMark);
                if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
                    if (check(TokenKind::LessThan)) parseTypeParameterList();
                    typeFunctionParams();
                    if (match(TokenKind::Colon)) typeExpr();
                } else if (match(TokenKind::Colon)) {
                    typeExpr();
                }
            }
        } else if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
            // Call signature.
            if (check(TokenKind::LessThan)) parseTypeParameterList();
            typeFunctionParams();
            if (match(TokenKind::Colon)) typeExpr();
        } else if (check(TokenKind::KW_new)) {
            // Construct signature `new (args): T` — but `new` can also be a
            // property NAME (`new: T`).
            auto saved = saveState();
            advance();
            if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
                if (check(TokenKind::LessThan)) parseTypeParameterList();
                typeFunctionParams();
                if (match(TokenKind::Colon)) typeExpr();
            } else {
                restoreState(saved);
                typeObjectMemberNamed();
            }
        } else {
            typeObjectMemberNamed();
        }

        // Separator: ';' ',' or ASI (next member on a new line).
        if (!match(TokenKind::Semicolon) && !match(TokenKind::Comma)) {
            if (check(TokenKind::CloseBrace)) break;
            if (!current_.hadNewlineBefore || current_.offset == memberStart) {
                throw std::runtime_error(fmt::format(
                    "{}:{}: Expected ';' or ',' in type literal but got '{}'",
                    fileName_, current_.line, std::string(current_.text)));
            }
        }
    }
    expect(TokenKind::CloseBrace, "'}'");
}

// A named object-type member (property / method / accessor signature).
void Parser::typeObjectMemberNamed() {
    // get/set accessor signatures look like `get name(): T`.
    if (check(TokenKind::KW_get) || check(TokenKind::KW_set)) {
        auto saved = saveState();
        advance();
        bool accessor = current_.kind == TokenKind::Identifier ||
                        Lexer::isKeyword(current_.kind) ||
                        current_.kind == TokenKind::StringLiteral ||
                        current_.kind == TokenKind::NumericLiteral ||
                        current_.kind == TokenKind::OpenBracket;
        if (!accessor) restoreState(saved);
    }
    // Member name: identifier/keyword/string/number/computed.
    if (check(TokenKind::StringLiteral) || check(TokenKind::NumericLiteral) ||
        check(TokenKind::BigIntLiteral)) {
        advance();
    } else if (check(TokenKind::OpenBracket)) {
        advance();
        parseAssignmentExpression();
        expect(TokenKind::CloseBracket, "']'");
    } else {
        typeName();
    }
    match(TokenKind::QuestionMark);
    if (check(TokenKind::OpenParen) || check(TokenKind::LessThan)) {
        // Method signature.
        if (check(TokenKind::LessThan)) parseTypeParameterList();
        typeFunctionParams();
        if (match(TokenKind::Colon)) typeExpr();
    } else if (match(TokenKind::Colon)) {
        typeExpr();
    }
}

// Tuple type body, positioned ON '['. Elements may be optional (`T?`),
// rest (`...T`), or named (`name?: T`).
void Parser::typeTupleBody() {
    expect(TokenKind::OpenBracket, "'['");
    while (!check(TokenKind::CloseBracket) && !isAtEnd()) {
        if (check(TokenKind::DotDotDot)) advance();
        // Named tuple member lookahead: NAME [?] ':'
        if (current_.kind == TokenKind::Identifier || Lexer::isKeyword(current_.kind)) {
            auto saved = saveState();
            typeName();
            match(TokenKind::QuestionMark);
            if (check(TokenKind::Colon)) {
                advance();  // ':'
            } else {
                restoreState(saved);
            }
        }
        typeExpr();
        match(TokenKind::QuestionMark);  // optional element `T?`
        if (!check(TokenKind::CloseBracket)) {
            expect(TokenKind::Comma, "','");
        }
    }
    expect(TokenKind::CloseBracket, "']'");
}

// Primary type forms.
void Parser::typePrimary() {
    switch (current_.kind) {
        case TokenKind::OpenParen:
            typeParenOrFunction();
            return;
        case TokenKind::OpenBrace:
            typeObjectBody();
            return;
        case TokenKind::OpenBracket:
            typeTupleBody();
            return;
        case TokenKind::NoSubstitutionTemplate:
        case TokenKind::TemplateHead:
            typeTemplate();
            return;
        case TokenKind::StringLiteral:
        case TokenKind::NumericLiteral:
        case TokenKind::BigIntLiteral:
        case TokenKind::KW_null:
        case TokenKind::KW_undefined:
        case TokenKind::KW_true:
        case TokenKind::KW_false:
        case TokenKind::KW_void:
        case TokenKind::KW_this:
            advance();
            return;
        case TokenKind::Minus:
            // Negative literal type: -1
            advance();
            expect(TokenKind::NumericLiteral, "number");
            return;
        case TokenKind::LessThan: {
            // Generic function type: <T>(x: T) => T
            parseTypeParameterList();
            typeFunctionParams();
            expect(TokenKind::Arrow, "'=>'");
            typeExpr();
            return;
        }
        case TokenKind::KW_import: {
            // import("module").Entity<Args>
            advance();
            expect(TokenKind::OpenParen, "'('");
            expect(TokenKind::StringLiteral, "module string");
            expect(TokenKind::CloseParen, "')'");
            if (check(TokenKind::Dot)) {
                advance();
                typeEntityName();
            }
            typeArgumentsIfPresent();
            return;
        }
        default:
            // Type reference: dotted entity name + optional type args.
            typeEntityName();
            typeArgumentsIfPresent();
            return;
    }
}

// Operand: prefix operators, a primary, then postfix array/indexed forms.
void Parser::typeOperand() {
    // Prefix operators (stackable).
    while (true) {
        if (check(TokenKind::KW_keyof) || check(TokenKind::KW_readonly)) {
            advance();
            continue;
        }
        if (current_.kind == TokenKind::Identifier && current_.text == "unique") {
            advance();
            continue;
        }
        break;
    }
    if (check(TokenKind::KW_typeof)) {
        advance();
        if (check(TokenKind::KW_import)) {
            typePrimary();  // typeof import("...")
        } else {
            typeEntityName();
            typeArgumentsIfPresent();
        }
    } else if (check(TokenKind::KW_infer)) {
        advance();
        typeName();
    } else if (check(TokenKind::KW_new) || check(TokenKind::KW_abstract)) {
        // Constructor type: [abstract] new (args) => T
        if (check(TokenKind::KW_abstract)) advance();
        expect(TokenKind::KW_new, "'new'");
        if (check(TokenKind::LessThan)) parseTypeParameterList();
        typeFunctionParams();
        expect(TokenKind::Arrow, "'=>'");
        typeExpr();
    } else {
        typePrimary();
    }
    // Postfix: array `T[]` and indexed access `T[K]` (chainable).
    while (check(TokenKind::OpenBracket)) {
        // `[` on a NEW LINE never continues a type annotation in statement
        // position (`var x: T` then `[1].forEach(...)` — ASI boundary).
        if (current_.hadNewlineBefore) break;
        advance();
        if (check(TokenKind::CloseBracket)) {
            advance();
            continue;
        }
        typeExpr();
        expect(TokenKind::CloseBracket, "']'");
    }
}

// Union / intersection level; tolerates a leading '|' or '&'.
void Parser::typeNonConditional() {
    if (check(TokenKind::Pipe) || check(TokenKind::Ampersand)) advance();
    typeOperand();
    while (check(TokenKind::Pipe) || check(TokenKind::Ampersand)) {
        advance();
        typeOperand();
    }
}

// Full Type: predicates + conditional.
void Parser::typeExpr() {
    // `asserts x [is T]` / `asserts this [is T]` (return-type position).
    if (check(TokenKind::KW_asserts)) {
        auto saved = saveState();
        advance();
        if (current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::KW_this) {
            advance();
            if (check(TokenKind::KW_is)) {
                advance();
                typeExpr();
            }
            return;
        }
        restoreState(saved);  // `asserts` used as a plain type name
    }

    typeNonConditional();

    // Conditional type: T extends U ? A : B (right-associative).
    if (check(TokenKind::KW_extends)) {
        advance();
        typeNonConditional();
        expect(TokenKind::QuestionMark, "'?'");
        typeExpr();
        expect(TokenKind::Colon, "':'");
        typeExpr();
    }

    // Type predicate: `x is T` (x parsed as a type reference above).
    if (check(TokenKind::KW_is)) {
        advance();
        typeExpr();
    }
}

// ============================================================================
// Public surface (contracts unchanged: types are stored as source slices)
// ============================================================================

/// Scan a complete type expression and return its source text.
std::string Parser::scanTypeExpression() {
    int startOffset = current_.offset;
    typeExpr();
    int endOffset = previous_.offset + (int)previous_.text.size();

    if (endOffset <= startOffset) return "";
    auto text = lexer_->getSourceRange(startOffset, endOffset);
    std::string result(text);
    size_t start = result.find_first_not_of(" \t\r\n");
    size_t end = result.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

/// Skip over a type expression without collecting text.
void Parser::skipTypeExpression() {
    typeExpr();
}

/// Parse a type annotation after ':' (variable declarations, parameters).
std::string Parser::parseTypeAnnotation() {
    expect(TokenKind::Colon, "':'");
    return scanTypeExpression();
}

/// Parse a return type annotation after ':' (includes type predicates).
std::string Parser::parseReturnTypeAnnotation() {
    expect(TokenKind::Colon, "':'");
    return scanTypeExpression();
}

/// Parse type arguments: <Type1, Type2, ...> returning source slices.
std::vector<std::string> Parser::parseTypeArguments() {
    std::vector<std::string> args;
    if (!check(TokenKind::LessThan)) return args;
    advance();  // <

    while (!isCloseAngleFamily(current_.kind) && !isAtEnd()) {
        int startOffset = current_.offset;
        typeExpr();
        int endOffset = previous_.offset + (int)previous_.text.size();
        std::string result(lexer_->getSourceRange(startOffset, endOffset));
        size_t s = result.find_first_not_of(" \t\r\n");
        size_t e = result.find_last_not_of(" \t\r\n");
        args.push_back(s == std::string::npos ? "" : result.substr(s, e - s + 1));
        if (!isCloseAngleFamily(current_.kind)) {
            expect(TokenKind::Comma, "','");
        }
    }
    typeExpectCloseAngle();
    return args;
}

} // namespace ts::parser
