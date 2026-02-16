#pragma once

#include "Lexer.h"
#include "../ast/AstNodes.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace ts::parser {

class Parser {
public:
    /// Parse a TypeScript/JavaScript source file into an AST Program
    std::unique_ptr<ast::Program> parse(const std::string& source,
                                         const std::string& fileName);

private:
    // --- Token manipulation ---
    Token advance();
    Token peek() const { return current_; }
    bool check(TokenKind kind) const { return current_.kind == kind; }
    bool match(TokenKind kind);
    Token expect(TokenKind kind, const char* msg);
    bool isAtEnd() const { return current_.kind == TokenKind::EndOfFile; }

    // Check for contextual keyword (an identifier with a specific text)
    bool checkContextual(const char* keyword) const;
    bool matchContextual(const char* keyword);

    // --- ASI (Automatic Semicolon Insertion) ---
    bool canInsertSemicolon() const;
    void expectSemicolon();

    // --- Source location ---
    void setLocation(ast::Node* node, const Token& tok);
    void setLocation(ast::Node* node, int line, int col);

    // --- Statement parsers (Parser.cpp) ---
    ast::StmtPtr parseStatement();
    ast::StmtPtr parseDeclarationOrStatement();
    ast::StmtPtr parseFunctionDeclaration(bool isAsync, bool isExported, bool isDefaultExport);
    std::vector<ast::StmtPtr> parseVariableDeclarationList(bool isExported);
    ast::StmtPtr parseClassDeclaration(bool isAbstract, bool isExported, bool isDefaultExport);
    ast::StmtPtr parseIfStatement();
    ast::StmtPtr parseWhileStatement();
    ast::StmtPtr parseDoWhileStatement();
    ast::StmtPtr parseForStatement();
    ast::StmtPtr parseSwitchStatement();
    ast::StmtPtr parseTryStatement();
    ast::StmtPtr parseReturnStatement();
    ast::StmtPtr parseThrowStatement();
    ast::StmtPtr parseBlockStatement();
    ast::StmtPtr parseExpressionStatement();
    ast::StmtPtr parseImportDeclaration();
    ast::StmtPtr parseExportDeclaration();
    ast::StmtPtr parseLabeledOrExpressionStatement();
    ast::StmtPtr parseBreakStatement();
    ast::StmtPtr parseContinueStatement();
    ast::StmtPtr parseDebuggerStatement();
    ast::StmtPtr parseInterfaceDeclaration(bool isExported, bool isDefaultExport);
    ast::StmtPtr parseTypeAliasDeclaration(bool isExported);
    ast::StmtPtr parseEnumDeclaration(bool isExported, bool isDeclare);

    // --- Class/interface members ---
    ast::NodePtr parseClassMember();
    std::unique_ptr<ast::MethodDefinition> parseMethodDefinition(
        const std::string& name, ast::NodePtr nameNode,
        bool isStatic, bool isAbstract, bool isAsync, bool isGenerator,
        bool isGetter, bool isSetter, ts::AccessModifier access,
        std::vector<ast::Decorator> decorators);

    // --- Parameter parsing ---
    std::unique_ptr<ast::Parameter> parseParameter();
    std::vector<std::unique_ptr<ast::Parameter>> parseParameterList();

    // --- Type parameter parsing ---
    std::unique_ptr<ast::TypeParameter> parseTypeParameter();
    std::vector<std::unique_ptr<ast::TypeParameter>> parseTypeParameterList();

    // --- Expression parsers (Parser_Expressions.cpp) ---
    ast::ExprPtr parseExpression();
    ast::ExprPtr parseAssignmentExpression();
    ast::ExprPtr parsePrecedenceExpression(int minPrec);
    ast::ExprPtr parseUnaryExpression();
    ast::ExprPtr parsePostfixExpression();
    ast::ExprPtr parseCallExpression();
    ast::ExprPtr parseMemberExpression();
    ast::ExprPtr parsePrimaryExpression();

    // Expression helpers
    ast::ExprPtr parseArrowFunctionOrParenthesized();
    ast::ExprPtr parseObjectLiteral();
    ast::ExprPtr parseArrayLiteral();
    ast::ExprPtr parseTemplateLiteral();
    ast::ExprPtr parseTaggedTemplate(ast::ExprPtr tag);
    ast::ExprPtr parseFunctionExpression(bool isAsync);
    ast::ExprPtr parseClassExpression();
    ast::ExprPtr parseNewExpression();

    // Precedence table
    int getBinaryPrecedence(TokenKind kind) const;
    static std::string tokenToOperator(TokenKind kind);
    static bool isAssignmentOperator(TokenKind kind);
    static bool isRightAssociative(TokenKind kind);

    // --- Type annotation parsing (Parser_TypeAnnotations.cpp) ---
    std::string parseTypeAnnotation();       // After ':'
    std::string parseReturnTypeAnnotation(); // After ':' for return types
    std::string scanTypeExpression();        // Scan a complete type expression
    void skipTypeExpression();               // Skip over type expression tokens
    std::vector<std::string> parseTypeArguments(); // <T, U> in call expressions

    // --- Binding pattern parsing ---
    ast::NodePtr parseBindingPattern();
    ast::NodePtr parseObjectBindingPattern();
    ast::NodePtr parseArrayBindingPattern();
    ast::NodePtr parseBindingNameOrPattern();

    // --- Decorator parsing ---
    std::vector<ast::Decorator> parseDecorators();
    ast::Decorator parseDecorator();

    // --- Triple-slash references ---
    std::vector<ast::TripleSlashReference> parseTripleSlashReferences();

    // --- Helpers ---
    bool isStartOfExpression() const;
    bool isStartOfStatement() const;
    bool isIdentifierOrKeyword() const;
    std::string identifierName(); // Get identifier text, even from keywords used as identifiers

    // Save/restore state for speculative parsing
    struct SavedState {
        Token current;
        Token previous;
        LexerState lexerState;
    };
    SavedState saveState() const;
    void restoreState(const SavedState& state);

    // --- Data ---
    std::unique_ptr<Lexer> lexer_;
    Token current_;
    Token previous_;
    std::string fileName_;
    const std::string* source_ = nullptr;
    bool inAsync_ = false;      // Inside async function?
    bool inGenerator_ = false;  // Inside generator function?
    bool noIn_ = false;         // Suppress 'in' as binary operator (for-loop initializers)
    int functionDepth_ = 0;    // 0 = top-level, >0 = inside function
};

} // namespace ts::parser
