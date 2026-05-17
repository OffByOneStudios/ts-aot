#pragma once

#include "Lexer.h"
#include "../ast/AstNodes.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace ts::parser {

class Parser {
public:
    /// Parse a TypeScript/JavaScript source file into an AST Program
    std::unique_ptr<ast::Program> parse(const std::string& source,
                                         const std::string& fileName);

    /// Canonicalize a NumericLiteral lexeme to its ECMA-262 property-name
    /// form: ToString(ToNumber(lexeme)). For example `0b10` → "2",
    /// `0xFF` → "255", `1.0` → "1". Used wherever a numeric literal
    /// appears as a property key (class member name, object literal,
    /// binding pattern), since property keys must compare against the
    /// canonical string per spec.
    static std::string canonicalNumericPropertyName(std::string_view lexeme);

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
    // Body of if/while/for/do-while/etc — only Statement is allowed,
    // not Declaration (per ECMA-262 13.1). Rejects let/const/class,
    // generators, and async functions; honors Annex B.3.2 for plain
    // FunctionDeclaration in non-strict mode when allowAnnexBFunction
    // is true (if-body and labeled-stmt-body sites). Loop bodies pass
    // false: plain function-declaration is always rejected there.
    ast::StmtPtr parseStatementOnly(bool allowAnnexBFunction = false);
    // Like parseStatementOnly() but additionally bumps iterationDepth_
    // around the body parse so unlabeled break/continue inside know
    // they're in a loop.
    ast::StmtPtr parseLoopBody();
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
    // checkDuplicates=false suppresses the ECMA-262 14.1.2/14.3.1
    // duplicate-BoundNames sweep so callers performing speculative cover-
    // grammar parsing (parseArrowFunctionOrParenthesized) can defer the
    // check until after `=>` is confirmed and re-run it themselves.
    std::vector<std::unique_ptr<ast::Parameter>> parseParameterList(
        bool checkDuplicates = true);

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

    // --- Lexical scope tracking for redeclaration detection ---
    // Each scope maps names to their declaration kind
    enum class PDeclKind { Var, Let, Const, Function };
    struct LexicalScope {
        std::unordered_map<std::string, PDeclKind> names;
    };
    std::vector<LexicalScope> lexicalScopes_;
    void pushLexicalScope();
    void popLexicalScope();
    // Returns false if redeclaration conflict. Emits error if conflict.
    bool declareLexicalName(const std::string& name, PDeclKind kind);

    // --- Data ---
    std::unique_ptr<Lexer> lexer_;
    Token current_;
    Token previous_;
    std::string fileName_;
    const std::string* source_ = nullptr;
    bool inAsync_ = false;      // Inside async function?
    bool inGenerator_ = false;  // Inside generator function?
    bool noIn_ = false;         // Suppress 'in' as binary operator (for-loop initializers)
    bool strictMode_ = false;   // Effective strict mode (set after "use strict" prologue)
    bool sawUseStrictDirective_ = false;  // Did the most-recently-parsed body contain a "use strict" directive?
    int functionDepth_ = 0;    // 0 = top-level, >0 = inside function
    int iterationDepth_ = 0;   // Inside for/while/do-while body (break + continue allowed)
    int switchDepth_ = 0;      // Inside switch body (break allowed, continue not)
    int errorCount_ = 0;       // Parse-time errors (redeclaration, etc.)
    // ECMA-262 15.7.1: HasDirectSuper counter scoped to the immediate
    // MethodDefinition body. Incremented in parseCallExpression when the
    // callee is a SuperExpression; saved+reset around each method body.
    int directSuperCount_ = 0;
    // Tracks whether the enclosing class has a ClassHeritage clause.
    // Used in parseMethodDefinition to validate the constructor's
    // HasDirectSuper invariant.
    bool currentClassHasHeritage_ = false;
    // ECMA-262 13.3.7.1: SuperReference is only valid in a context with
    // an associated [HomeObject]: class methods, class field initializers,
    // and object literal methods. Plain FunctionDeclaration /
    // FunctionExpression bodies do NOT have [HomeObject]. ArrowFunction
    // inherits the surrounding super-binding lexically, so we don't toggle
    // for arrows. Saved+restored across function-kind boundaries.
    bool superAllowed_ = false;
    // ECMA-262 14.13 / 14.14: stack of active label names so that
    // `break LABEL` / `continue LABEL` can verify LABEL is in scope.
    // Each entry: { name, isIteration }. continue requires isIteration=true.
    struct ActiveLabel {
        std::string name;
        bool isIteration;
    };
    std::vector<ActiveLabel> activeLabels_;
    // ECMA-262 15.7.1 / 15.7.2 Static Semantics: AllPrivateIdentifiersValid.
    // Each entry is a single class body's PrivateBoundNames plus the
    // unresolved `#x` references seen inside it. Class boundaries push
    // and pop. On pop, unresolved refs are validated against the entire
    // stack (inner classes inherit outer #names) and SyntaxError if any
    // ref doesn't resolve.
    struct ClassPrivateScope {
        std::unordered_set<std::string> declared;
        std::vector<std::pair<std::string, int>> unresolved;  // name, line
    };
    std::vector<ClassPrivateScope> classPrivateScopes_;

    // Strict-mode helpers. Function/class bodies push the parent's
    // strictMode_ via StrictModeGuard; class bodies always force-elevate
    // to true; function bodies start at the parent value and may elevate
    // when their directive prologue contains "use strict". Class
    // bodies are always strict per ECMA-262 §10.2.1; modules are always
    // strict per §16.1; arrow function bodies inherit the surrounding
    // strict-mode and may further elevate via prologue (in their
    // FunctionBody form).
    struct StrictModeGuard {
        Parser* p_;
        bool prev_;
        explicit StrictModeGuard(Parser* p) : p_(p), prev_(p->strictMode_) {}
        ~StrictModeGuard() { p_->strictMode_ = prev_; }
    };
    // Returns true iff the just-parsed statement is part of a directive
    // prologue (i.e., an ExpressionStatement wrapping a single
    // StringLiteral). Side effect: if the literal text is "use strict",
    // sets strictMode_ = true.
    bool processPrologueDirective(const ast::StmtPtr& stmt);
    // Returns true iff `params` is a SimpleParameterList per ECMA-262 14.1
    // (single binding-identifier per param, no rest, no default, no
    // destructuring, no optional `?`).
    bool isParameterListSimple(
        const std::vector<std::unique_ptr<ast::Parameter>>& params) const;

    // Validate that `expr` is a valid AssignmentTarget per ECMA-262
    // 12.15.5 (IsValidSimpleAssignmentTarget). Throws SyntaxError when
    // the expression cannot legally appear on the LHS of `=`/`+=`/etc.
    // `forCompoundAssign` excludes destructuring patterns
    // (Array/ObjectLiteralExpression), which are only permitted with
    // plain `=`.
    void validateAssignmentTarget(const ast::Node* expr,
                                  bool forCompoundAssign) const;
public:
    int getErrorCount() const {
        // Include lexer errors if lexer exists
        int lexerErrs = lexer_ ? lexer_->getErrorCount() : 0;
        return errorCount_ + lexerErrs;
    }
};

} // namespace ts::parser
