#include "Parser.h"
#include <stdexcept>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace ts::parser {

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

    while (!isAtEnd()) {
        auto stmts = parseDeclarationOrStatement();
        if (stmts) {
            program->body.push_back(std::move(stmts));
        }
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
        std::string name(current_.text);
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
    if (match(TokenKind::Equals)) {
        param->initializer = parseAssignmentExpression();
    }

    return param;
}

std::vector<std::unique_ptr<ast::Parameter>> Parser::parseParameterList() {
    std::vector<std::unique_ptr<ast::Parameter>> params;
    expect(TokenKind::OpenParen, "'('");
    while (!check(TokenKind::CloseParen) && !isAtEnd()) {
        params.push_back(parseParameter());
        if (!check(TokenKind::CloseParen)) {
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
    // Simple identifier
    auto id = std::make_unique<ast::Identifier>();
    setLocation(id.get(), current_);
    // Handle # prefix for private fields
    if (match(TokenKind::Hash)) {
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
        } else {
            // propertyName: binding or just binding
            // We need to look ahead: if there's a ':', it's propertyName: binding
            std::string propName = identifierName();

            if (match(TokenKind::Colon)) {
                elem->propertyName = propName;
                elem->name = parseBindingNameOrPattern();
            } else {
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
            // Wrap in block
            auto block = std::make_unique<ast::BlockStatement>();
            for (auto& s : stmts) block->statements.push_back(std::move(s));
            return block;
        }
        if (current_.kind == TokenKind::KW_namespace || current_.kind == TokenKind::KW_module) {
            // Skip namespace/module declarations
            advance();
            identifierName(); // name
            if (check(TokenKind::OpenBrace)) {
                // Skip block
                int depth = 0;
                advance(); // {
                depth++;
                while (depth > 0 && !isAtEnd()) {
                    if (current_.kind == TokenKind::OpenBrace) depth++;
                    else if (current_.kind == TokenKind::CloseBrace) depth--;
                    advance();
                }
            }
            // Return empty expression statement as placeholder
            auto es = std::make_unique<ast::ExpressionStatement>();
            auto id = std::make_unique<ast::UndefinedLiteral>();
            es->expression = std::move(id);
            return es;
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
            auto stmts = parseVariableDeclarationList(false);
            if (stmts.size() == 1) {
                result = std::move(stmts[0]);
            } else {
                auto block = std::make_unique<ast::BlockStatement>();
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
        inAsync_ = node->isAsync;
        inGenerator_ = node->isGenerator;

        expect(TokenKind::OpenBrace, "'{'");
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) node->body.push_back(std::move(stmt));
        }
        expect(TokenKind::CloseBrace, "'}'");

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

        // Name or binding pattern
        decl->name = parseBindingNameOrPattern();

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

    // Name (optional for expressions)
    if (isIdentifierOrKeyword() && !check(TokenKind::KW_extends) && !check(TokenKind::KW_implements) && !check(TokenKind::OpenBrace)) {
        node->name = identifierName();
    }

    // Type parameters
    node->typeParameters = parseTypeParameterList();

    // extends
    if (match(TokenKind::KW_extends)) {
        node->baseClass = identifierName();
        // Skip generic type args on base class
        if (check(TokenKind::LessThan)) {
            skipTypeExpression();
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

    // Body
    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto member = parseClassMember();
        if (member) node->members.push_back(std::move(member));
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
    if (current_.kind == TokenKind::KW_async && !current_.hadNewlineBefore) {
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
        name = std::string(current_.text);
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

    // Optional marker
    if (match(TokenKind::QuestionMark)) {}
    // Definite assignment assertion
    if (match(TokenKind::ExclamationMark)) {}

    // Type annotation
    if (check(TokenKind::Colon)) {
        prop->type = parseTypeAnnotation();
    }

    // Initializer
    if (match(TokenKind::Equals)) {
        prop->initializer = parseAssignmentExpression();
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

    // Body (or abstract/declaration without body)
    if (check(TokenKind::OpenBrace)) {
        method->hasBody = true;
        bool prevAsync = inAsync_;
        bool prevGen = inGenerator_;
        inAsync_ = method->isAsync;
        inGenerator_ = method->isGenerator;

        expect(TokenKind::OpenBrace, "'{'");
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) method->body.push_back(std::move(stmt));
        }
        expect(TokenKind::CloseBrace, "'}'");

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
    node->thenStatement = parseDeclarationOrStatement();

    if (match(TokenKind::KW_else)) {
        node->elseStatement = parseDeclarationOrStatement();
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
    if (current_.kind == TokenKind::KW_var || current_.kind == TokenKind::KW_let ||
        current_.kind == TokenKind::KW_const) {
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
            firstDecl->initializer = parseAssignmentExpression();
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
    return node;
}

ast::StmtPtr Parser::parseTryStatement() {
    auto startTok = current_;
    expect(TokenKind::KW_try, "'try'");

    auto node = std::make_unique<ast::TryStatement>();
    setLocation(node.get(), startTok);

    // try block
    expect(TokenKind::OpenBrace, "'{'");
    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->tryBlock.push_back(std::move(stmt));
    }
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
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) node->catchClause->block.push_back(std::move(stmt));
        }
        expect(TokenKind::CloseBrace, "'}'");
    }

    // finally clause
    if (match(TokenKind::KW_finally)) {
        expect(TokenKind::OpenBrace, "'{'");
        while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
            auto stmt = parseDeclarationOrStatement();
            if (stmt) node->finallyBlock.push_back(std::move(stmt));
        }
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

    while (!check(TokenKind::CloseBrace) && !isAtEnd()) {
        auto stmt = parseDeclarationOrStatement();
        if (stmt) node->statements.push_back(std::move(stmt));
    }
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
        advance();
        if (match(TokenKind::Colon)) {
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

    auto node = std::make_unique<ast::ImportDeclaration>();
    setLocation(node.get(), startTok);

    // import type { ... } from '...' (skip 'type' keyword)
    bool isTypeOnly = false;
    if (current_.kind == TokenKind::KW_type) {
        auto saved = saveState();
        advance();
        if (check(TokenKind::OpenBrace) || check(TokenKind::Star) || current_.kind == TokenKind::Identifier) {
            isTypeOnly = true;
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
            if (current_.kind == TokenKind::KW_type) {
                auto saved = saveState();
                advance();
                if (isIdentifierOrKeyword() && !check(TokenKind::Comma) && !check(TokenKind::CloseBrace)) {
                    // This was 'type Foo' - a type-only import
                } else {
                    restoreState(saved);
                }
            }
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
