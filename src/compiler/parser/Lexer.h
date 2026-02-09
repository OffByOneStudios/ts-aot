#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace ts::parser {

enum class TokenKind {
    // Literals
    NumericLiteral,
    StringLiteral,
    TemplateHead,       // `text${
    TemplateMiddle,     // }text${
    TemplateTail,       // }text`
    NoSubstitutionTemplate, // `text` (no ${})
    RegularExpressionLiteral,
    BigIntLiteral,

    // Identifier
    Identifier,

    // Keywords
    KW_break, KW_case, KW_catch, KW_class, KW_const, KW_continue,
    KW_debugger, KW_default, KW_delete, KW_do, KW_else, KW_enum,
    KW_export, KW_extends, KW_false, KW_finally, KW_for, KW_function,
    KW_if, KW_import, KW_in, KW_instanceof, KW_let, KW_new, KW_null,
    KW_return, KW_super, KW_switch, KW_this, KW_throw, KW_true,
    KW_try, KW_typeof, KW_undefined, KW_var, KW_void, KW_while,
    KW_with, KW_yield,
    // Strict mode / contextual keywords
    KW_async, KW_await, KW_of, KW_from, KW_as, KW_get, KW_set,
    KW_type, KW_interface, KW_declare, KW_abstract, KW_implements,
    KW_readonly, KW_namespace, KW_module, KW_public, KW_private,
    KW_protected, KW_static, KW_constructor, KW_keyof, KW_infer,
    KW_is, KW_asserts, KW_satisfies, KW_override, KW_out, KW_require,

    // Punctuation & Operators
    OpenParen,          // (
    CloseParen,         // )
    OpenBrace,          // {
    CloseBrace,         // }
    OpenBracket,        // [
    CloseBracket,       // ]
    Dot,                // .
    DotDotDot,          // ...
    Semicolon,          // ;
    Comma,              // ,
    Colon,              // :
    QuestionMark,       // ?
    QuestionDot,        // ?.
    Arrow,              // =>
    At,                 // @
    Hash,               // #

    // Arithmetic
    Plus,               // +
    Minus,              // -
    Star,               // *
    Slash,              // /
    Percent,            // %
    StarStar,           // **

    // Bitwise
    Ampersand,          // &
    Pipe,               // |
    Caret,              // ^
    Tilde,              // ~

    // Logical / comparison
    ExclamationMark,    // !
    LessThan,           // <
    GreaterThan,        // >
    LessThanEquals,     // <=
    GreaterThanEquals,  // >=
    EqualsEquals,       // ==
    ExclamationEquals,  // !=
    EqualsEqualsEquals, // ===
    ExclamationEqualsEquals, // !==

    // Increment/Decrement
    PlusPlus,           // ++
    MinusMinus,         // --

    // Shift
    LessThanLessThan,   // <<
    GreaterThanGreaterThan, // >>
    GreaterThanGreaterThanGreaterThan, // >>>

    // Logical operators
    AmpersandAmpersand, // &&
    PipePipe,           // ||
    QuestionQuestion,   // ??

    // Assignment
    Equals,             // =
    PlusEquals,         // +=
    MinusEquals,        // -=
    StarEquals,         // *=
    SlashEquals,        // /=
    PercentEquals,      // %=
    StarStarEquals,     // **=
    AmpersandEquals,    // &=
    PipeEquals,         // |=
    CaretEquals,        // ^=
    LessThanLessThanEquals, // <<=
    GreaterThanGreaterThanEquals, // >>=
    GreaterThanGreaterThanGreaterThanEquals, // >>>=
    AmpersandAmpersandEquals, // &&=
    PipePipeEquals,     // ||=
    QuestionQuestionEquals, // ??=

    // Special
    EndOfFile,
    Error,              // Lexer error token

    COUNT               // Sentinel for array sizing
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string_view text;   // View into source buffer
    int line = 1;            // 1-based
    int column = 1;          // 1-based
    int offset = 0;          // Byte offset in source
    bool hadNewlineBefore = false;  // For ASI: was there a newline before this token?
};

class Lexer {
public:
    explicit Lexer(const std::string& source, const std::string& fileName = "");

    /// Advance to the next token and return it
    Token nextToken();

    /// Peek at the current position without advancing
    int currentOffset() const { return pos_; }

    /// Get source text between two offsets
    std::string_view getSourceRange(int start, int end) const;

    /// Get the full source text
    const std::string& source() const { return source_; }

    /// Tell the lexer whether the next '/' should be regex or division.
    /// Called by the parser based on context.
    void setRegexAllowed(bool allowed) { regexAllowed_ = allowed; }

    /// Reset lexer position for re-lexing (e.g., re-lex '/' as regex)
    void setOffset(int offset, int line, int col) {
        pos_ = offset; line_ = line; column_ = col;
    }

    /// For template literal scanning: resume scanning template after expression
    Token scanTemplateContinuation();

    /// Get the string value from a string literal token (with escape processing)
    static std::string getStringValue(std::string_view rawToken);

    /// Check if a token kind is a keyword
    static bool isKeyword(TokenKind kind);

    /// Get the string name of a keyword token
    static const char* tokenKindToString(TokenKind kind);

    /// Check if identifier text is a contextual keyword
    static TokenKind identifierToKeyword(std::string_view text);

private:
    void skipWhitespaceAndComments();
    Token scanIdentifierOrKeyword();
    Token scanNumericLiteral();
    Token scanStringLiteral(char quote);
    Token scanTemplateLiteral();
    Token scanRegularExpression();
    Token scanPunctuation();

    Token makeToken(TokenKind kind, int start, int length);
    Token makeToken(TokenKind kind, int start);

    char peek() const;
    char peekAt(int offset) const;
    char advance();
    bool isAtEnd() const;
    bool match(char expected);

    static bool isDigit(char c);
    static bool isHexDigit(char c);
    static bool isIdentStart(char c);
    static bool isIdentPart(char c);

    const std::string& source_;
    std::string fileName_;
    int pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    int tokenStartLine_ = 1;
    int tokenStartColumn_ = 1;
    bool regexAllowed_ = true;  // At start of file, regex is allowed
    bool hadNewline_ = false;   // Track newlines between tokens for ASI

    // Template literal brace depth stack
    std::vector<int> templateBraceDepth_;
    int braceDepth_ = 0;

    static const std::unordered_map<std::string_view, TokenKind> keywords_;
};

// Helper: is this token kind one that can start a statement?
bool canStartStatement(TokenKind kind);

// Helper: is this token kind one that indicates a binary operator?
bool isBinaryOperator(TokenKind kind);

} // namespace ts::parser
