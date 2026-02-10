#include "Lexer.h"
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace ts::parser {

const std::unordered_map<std::string_view, TokenKind> Lexer::keywords_ = {
    {"break", TokenKind::KW_break},
    {"case", TokenKind::KW_case},
    {"catch", TokenKind::KW_catch},
    {"class", TokenKind::KW_class},
    {"const", TokenKind::KW_const},
    {"continue", TokenKind::KW_continue},
    {"debugger", TokenKind::KW_debugger},
    {"default", TokenKind::KW_default},
    {"delete", TokenKind::KW_delete},
    {"do", TokenKind::KW_do},
    {"else", TokenKind::KW_else},
    {"enum", TokenKind::KW_enum},
    {"export", TokenKind::KW_export},
    {"extends", TokenKind::KW_extends},
    {"false", TokenKind::KW_false},
    {"finally", TokenKind::KW_finally},
    {"for", TokenKind::KW_for},
    {"function", TokenKind::KW_function},
    {"if", TokenKind::KW_if},
    {"import", TokenKind::KW_import},
    {"in", TokenKind::KW_in},
    {"instanceof", TokenKind::KW_instanceof},
    {"let", TokenKind::KW_let},
    {"new", TokenKind::KW_new},
    {"null", TokenKind::KW_null},
    {"return", TokenKind::KW_return},
    {"super", TokenKind::KW_super},
    {"switch", TokenKind::KW_switch},
    {"this", TokenKind::KW_this},
    {"throw", TokenKind::KW_throw},
    {"true", TokenKind::KW_true},
    {"try", TokenKind::KW_try},
    {"typeof", TokenKind::KW_typeof},
    {"undefined", TokenKind::KW_undefined},
    {"var", TokenKind::KW_var},
    {"void", TokenKind::KW_void},
    {"while", TokenKind::KW_while},
    {"with", TokenKind::KW_with},
    {"yield", TokenKind::KW_yield},
    // Contextual keywords (lexed as keywords, parser decides context)
    {"async", TokenKind::KW_async},
    {"await", TokenKind::KW_await},
    {"of", TokenKind::KW_of},
    {"from", TokenKind::KW_from},
    {"as", TokenKind::KW_as},
    {"get", TokenKind::KW_get},
    {"set", TokenKind::KW_set},
    {"type", TokenKind::KW_type},
    {"interface", TokenKind::KW_interface},
    {"declare", TokenKind::KW_declare},
    {"abstract", TokenKind::KW_abstract},
    {"implements", TokenKind::KW_implements},
    {"readonly", TokenKind::KW_readonly},
    {"namespace", TokenKind::KW_namespace},
    {"module", TokenKind::KW_module},
    {"public", TokenKind::KW_public},
    {"private", TokenKind::KW_private},
    {"protected", TokenKind::KW_protected},
    {"static", TokenKind::KW_static},
    {"constructor", TokenKind::KW_constructor},
    {"keyof", TokenKind::KW_keyof},
    {"infer", TokenKind::KW_infer},
    {"is", TokenKind::KW_is},
    {"asserts", TokenKind::KW_asserts},
    {"satisfies", TokenKind::KW_satisfies},
    {"override", TokenKind::KW_override},
    {"out", TokenKind::KW_out},
    {"require", TokenKind::KW_require},
};

Lexer::Lexer(const std::string& source, const std::string& fileName)
    : source_(source), fileName_(fileName) {}

LexerState Lexer::saveLexerState() const {
    LexerState s;
    s.pos = pos_;
    s.line = line_;
    s.column = column_;
    s.tokenStartLine = tokenStartLine_;
    s.tokenStartColumn = tokenStartColumn_;
    s.regexAllowed = regexAllowed_;
    s.hadNewline = hadNewline_;
    s.templateBraceDepth = templateBraceDepth_;
    s.braceDepth = braceDepth_;
    return s;
}

void Lexer::restoreLexerState(const LexerState& state) {
    pos_ = state.pos;
    line_ = state.line;
    column_ = state.column;
    tokenStartLine_ = state.tokenStartLine;
    tokenStartColumn_ = state.tokenStartColumn;
    regexAllowed_ = state.regexAllowed;
    hadNewline_ = state.hadNewline;
    templateBraceDepth_ = state.templateBraceDepth;
    braceDepth_ = state.braceDepth;
}

char Lexer::peek() const {
    if (pos_ >= (int)source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peekAt(int offset) const {
    int idx = pos_ + offset;
    if (idx < 0 || idx >= (int)source_.size()) return '\0';
    return source_[idx];
}

char Lexer::advance() {
    if (pos_ >= (int)source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else if (c == '\r') {
        if (pos_ < (int)source_.size() && source_[pos_] == '\n') {
            pos_++;  // Consume \n in \r\n
        }
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::isAtEnd() const {
    return pos_ >= (int)source_.size();
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[pos_] != expected) return false;
    advance();
    return true;
}

bool Lexer::isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool Lexer::isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

bool Lexer::isIdentPart(char c) {
    return isIdentStart(c) || isDigit(c);
}

Token Lexer::makeToken(TokenKind kind, int start, int length) {
    Token tok;
    tok.kind = kind;
    tok.text = std::string_view(source_.data() + start, length);
    tok.line = tokenStartLine_;
    tok.column = tokenStartColumn_;
    tok.offset = start;
    tok.hadNewlineBefore = hadNewline_;
    return tok;
}

Token Lexer::makeToken(TokenKind kind, int start) {
    return makeToken(kind, start, pos_ - start);
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\t') {
            advance();
        } else if (c == '\n') {
            hadNewline_ = true;
            advance();
        } else if (c == '\r') {
            hadNewline_ = true;
            advance();
        } else if (c == '/' && peekAt(1) == '/') {
            // Single-line comment
            advance(); advance(); // skip //
            while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
                advance();
            }
        } else if (c == '/' && peekAt(1) == '*') {
            // Multi-line comment
            advance(); advance(); // skip /*
            while (!isAtEnd()) {
                if (peek() == '*' && peekAt(1) == '/') {
                    advance(); advance(); // skip */
                    break;
                }
                if (peek() == '\n' || peek() == '\r') {
                    hadNewline_ = true;
                }
                advance();
            }
        } else if (c == '#' && pos_ == 0 && peekAt(1) == '!') {
            // Hashbang: #! at start of file
            advance(); advance();
            while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::nextToken() {
    hadNewline_ = false;
    skipWhitespaceAndComments();

    if (isAtEnd()) {
        tokenStartLine_ = line_;
        tokenStartColumn_ = column_;
        return makeToken(TokenKind::EndOfFile, pos_, 0);
    }

    // Check if we're resuming a template literal
    if (!templateBraceDepth_.empty() && peek() == '}' && braceDepth_ == templateBraceDepth_.back()) {
        return scanTemplateContinuation();
    }

    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;
    int start = pos_;
    char c = peek();

    // Identifiers and keywords
    if (isIdentStart(c)) {
        return scanIdentifierOrKeyword();
    }

    // Numeric literals
    if (isDigit(c) || (c == '.' && isDigit(peekAt(1)))) {
        return scanNumericLiteral();
    }

    // String literals
    if (c == '\'' || c == '"') {
        return scanStringLiteral(c);
    }

    // Template literals
    if (c == '`') {
        return scanTemplateLiteral();
    }

    // Regex or division - parser decides via setRegexAllowed()
    if (c == '/') {
        if (regexAllowed_ && peekAt(1) != '/' && peekAt(1) != '*') {
            return scanRegularExpression();
        }
        // Fall through to punctuation
    }

    return scanPunctuation();
}

Token Lexer::scanIdentifierOrKeyword() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    while (!isAtEnd() && isIdentPart(peek())) {
        advance();
    }

    std::string_view text(source_.data() + start, pos_ - start);

    // Check if it's a keyword
    auto it = keywords_.find(text);
    if (it != keywords_.end()) {
        return makeToken(it->second, start);
    }

    return makeToken(TokenKind::Identifier, start);
}

Token Lexer::scanNumericLiteral() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    char c = peek();

    // Handle 0x, 0o, 0b prefixes
    if (c == '0') {
        advance();
        char next = peek();
        if (next == 'x' || next == 'X') {
            advance();
            while (!isAtEnd() && (isHexDigit(peek()) || peek() == '_')) advance();
            // Check for BigInt suffix
            if (!isAtEnd() && peek() == 'n') {
                advance();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            return makeToken(TokenKind::NumericLiteral, start);
        }
        if (next == 'o' || next == 'O') {
            advance();
            while (!isAtEnd() && ((peek() >= '0' && peek() <= '7') || peek() == '_')) advance();
            if (!isAtEnd() && peek() == 'n') {
                advance();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            return makeToken(TokenKind::NumericLiteral, start);
        }
        if (next == 'b' || next == 'B') {
            advance();
            while (!isAtEnd() && (peek() == '0' || peek() == '1' || peek() == '_')) advance();
            if (!isAtEnd() && peek() == 'n') {
                advance();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            return makeToken(TokenKind::NumericLiteral, start);
        }
    }

    // Regular decimal number
    while (!isAtEnd() && (isDigit(peek()) || peek() == '_')) {
        advance();
    }

    // Decimal point
    if (!isAtEnd() && peek() == '.' && isDigit(peekAt(1))) {
        advance(); // .
        while (!isAtEnd() && (isDigit(peek()) || peek() == '_')) {
            advance();
        }
    }

    // Exponent
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        advance();
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) advance();
        while (!isAtEnd() && (isDigit(peek()) || peek() == '_')) advance();
    }

    // BigInt suffix
    if (!isAtEnd() && peek() == 'n') {
        advance();
        return makeToken(TokenKind::BigIntLiteral, start);
    }

    return makeToken(TokenKind::NumericLiteral, start);
}

Token Lexer::scanStringLiteral(char quote) {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    advance(); // opening quote

    while (!isAtEnd() && peek() != quote) {
        if (peek() == '\\') {
            advance(); // backslash
            if (!isAtEnd()) advance(); // escaped char
        } else {
            advance();
        }
    }

    if (!isAtEnd()) {
        advance(); // closing quote
    }

    return makeToken(TokenKind::StringLiteral, start);
}

Token Lexer::scanTemplateLiteral() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    advance(); // opening backtick

    while (!isAtEnd()) {
        if (peek() == '\\') {
            advance(); // backslash
            if (!isAtEnd()) advance(); // escaped char
        } else if (peek() == '$' && peekAt(1) == '{') {
            // Template expression starts
            advance(); advance(); // skip ${
            templateBraceDepth_.push_back(braceDepth_);
            return makeToken(TokenKind::TemplateHead, start);
        } else if (peek() == '`') {
            advance(); // closing backtick
            return makeToken(TokenKind::NoSubstitutionTemplate, start);
        } else {
            advance();
        }
    }

    // Unterminated template literal
    return makeToken(TokenKind::NoSubstitutionTemplate, start);
}

Token Lexer::scanTemplateContinuation() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    advance(); // skip the closing }
    templateBraceDepth_.pop_back();

    while (!isAtEnd()) {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) advance();
        } else if (peek() == '$' && peekAt(1) == '{') {
            advance(); advance(); // skip ${
            templateBraceDepth_.push_back(braceDepth_);
            return makeToken(TokenKind::TemplateMiddle, start);
        } else if (peek() == '`') {
            advance(); // closing backtick
            return makeToken(TokenKind::TemplateTail, start);
        } else {
            advance();
        }
    }

    // Unterminated
    return makeToken(TokenKind::TemplateTail, start);
}

Token Lexer::scanRegularExpression() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    advance(); // opening /

    bool inCharClass = false;
    while (!isAtEnd()) {
        char c = peek();
        if (c == '\\') {
            advance();
            if (!isAtEnd()) advance(); // escaped char
        } else if (c == '[') {
            inCharClass = true;
            advance();
        } else if (c == ']') {
            inCharClass = false;
            advance();
        } else if (c == '/' && !inCharClass) {
            advance(); // closing /
            break;
        } else if (c == '\n' || c == '\r') {
            break; // Unterminated regex on this line
        } else {
            advance();
        }
    }

    // Scan flags: g, i, m, s, u, v, y, d
    while (!isAtEnd() && isIdentPart(peek())) {
        advance();
    }

    return makeToken(TokenKind::RegularExpressionLiteral, start);
}

Token Lexer::scanPunctuation() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    char c = advance();

    switch (c) {
    case '(': return makeToken(TokenKind::OpenParen, start);
    case ')': return makeToken(TokenKind::CloseParen, start);
    case '{':
        braceDepth_++;
        return makeToken(TokenKind::OpenBrace, start);
    case '}':
        braceDepth_--;
        return makeToken(TokenKind::CloseBrace, start);
    case '[': return makeToken(TokenKind::OpenBracket, start);
    case ']': return makeToken(TokenKind::CloseBracket, start);
    case ';': return makeToken(TokenKind::Semicolon, start);
    case ',': return makeToken(TokenKind::Comma, start);
    case '~': return makeToken(TokenKind::Tilde, start);
    case '@': return makeToken(TokenKind::At, start);
    case '#': return makeToken(TokenKind::Hash, start);

    case '.':
        if (peek() == '.' && peekAt(1) == '.') {
            advance(); advance();
            return makeToken(TokenKind::DotDotDot, start);
        }
        return makeToken(TokenKind::Dot, start);

    case ':': return makeToken(TokenKind::Colon, start);

    case '?':
        if (peek() == '.') {
            // Check it's not ?. followed by a digit (that would be ternary + decimal literal)
            if (!isDigit(peekAt(1))) {
                advance();
                return makeToken(TokenKind::QuestionDot, start);
            }
        }
        if (peek() == '?') {
            advance();
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::QuestionQuestionEquals, start);
            }
            return makeToken(TokenKind::QuestionQuestion, start);
        }
        return makeToken(TokenKind::QuestionMark, start);

    case '+':
        if (peek() == '+') { advance(); return makeToken(TokenKind::PlusPlus, start); }
        if (peek() == '=') { advance(); return makeToken(TokenKind::PlusEquals, start); }
        return makeToken(TokenKind::Plus, start);

    case '-':
        if (peek() == '-') { advance(); return makeToken(TokenKind::MinusMinus, start); }
        if (peek() == '=') { advance(); return makeToken(TokenKind::MinusEquals, start); }
        return makeToken(TokenKind::Minus, start);

    case '*':
        if (peek() == '*') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::StarStarEquals, start); }
            return makeToken(TokenKind::StarStar, start);
        }
        if (peek() == '=') { advance(); return makeToken(TokenKind::StarEquals, start); }
        return makeToken(TokenKind::Star, start);

    case '/':
        if (peek() == '=') { advance(); return makeToken(TokenKind::SlashEquals, start); }
        return makeToken(TokenKind::Slash, start);

    case '%':
        if (peek() == '=') { advance(); return makeToken(TokenKind::PercentEquals, start); }
        return makeToken(TokenKind::Percent, start);

    case '&':
        if (peek() == '&') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::AmpersandAmpersandEquals, start); }
            return makeToken(TokenKind::AmpersandAmpersand, start);
        }
        if (peek() == '=') { advance(); return makeToken(TokenKind::AmpersandEquals, start); }
        return makeToken(TokenKind::Ampersand, start);

    case '|':
        if (peek() == '|') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::PipePipeEquals, start); }
            return makeToken(TokenKind::PipePipe, start);
        }
        if (peek() == '=') { advance(); return makeToken(TokenKind::PipeEquals, start); }
        return makeToken(TokenKind::Pipe, start);

    case '^':
        if (peek() == '=') { advance(); return makeToken(TokenKind::CaretEquals, start); }
        return makeToken(TokenKind::Caret, start);

    case '!':
        if (peek() == '=') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::ExclamationEqualsEquals, start); }
            return makeToken(TokenKind::ExclamationEquals, start);
        }
        return makeToken(TokenKind::ExclamationMark, start);

    case '=':
        if (peek() == '>') { advance(); return makeToken(TokenKind::Arrow, start); }
        if (peek() == '=') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::EqualsEqualsEquals, start); }
            return makeToken(TokenKind::EqualsEquals, start);
        }
        return makeToken(TokenKind::Equals, start);

    case '<':
        if (peek() == '<') {
            advance();
            if (peek() == '=') { advance(); return makeToken(TokenKind::LessThanLessThanEquals, start); }
            return makeToken(TokenKind::LessThanLessThan, start);
        }
        if (peek() == '=') { advance(); return makeToken(TokenKind::LessThanEquals, start); }
        return makeToken(TokenKind::LessThan, start);

    case '>':
        if (peek() == '>') {
            advance();
            if (peek() == '>') {
                advance();
                if (peek() == '=') { advance(); return makeToken(TokenKind::GreaterThanGreaterThanGreaterThanEquals, start); }
                return makeToken(TokenKind::GreaterThanGreaterThanGreaterThan, start);
            }
            if (peek() == '=') { advance(); return makeToken(TokenKind::GreaterThanGreaterThanEquals, start); }
            return makeToken(TokenKind::GreaterThanGreaterThan, start);
        }
        if (peek() == '=') { advance(); return makeToken(TokenKind::GreaterThanEquals, start); }
        return makeToken(TokenKind::GreaterThan, start);

    default:
        // Unknown character - skip it
        return makeToken(TokenKind::Error, start);
    }
}

std::string_view Lexer::getSourceRange(int start, int end) const {
    if (start < 0) start = 0;
    if (end > (int)source_.size()) end = (int)source_.size();
    if (start >= end) return {};
    return std::string_view(source_.data() + start, end - start);
}

std::string Lexer::getStringValue(std::string_view rawToken) {
    if (rawToken.size() < 2) return std::string(rawToken);

    char quote = rawToken[0];
    // Strip quotes
    std::string_view inner = rawToken.substr(1, rawToken.size() - 2);

    std::string result;
    result.reserve(inner.size());

    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '\\' && i + 1 < inner.size()) {
            i++;
            switch (inner[i]) {
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case '\\': result += '\\'; break;
            case '\'': result += '\''; break;
            case '"': result += '"'; break;
            case '`': result += '`'; break;
            case '0': result += '\0'; break;
            case 'x':
                if (i + 2 < inner.size()) {
                    char hi = inner[i + 1];
                    char lo = inner[i + 2];
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return 0;
                    };
                    result += (char)(hexVal(hi) * 16 + hexVal(lo));
                    i += 2;
                }
                break;
            case 'u':
                if (i + 1 < inner.size() && inner[i + 1] == '{') {
                    // \u{XXXX} unicode escape
                    i += 2; // skip u{
                    int codePoint = 0;
                    while (i < inner.size() && inner[i] != '}') {
                        codePoint = codePoint * 16;
                        char h = inner[i];
                        if (h >= '0' && h <= '9') codePoint += h - '0';
                        else if (h >= 'a' && h <= 'f') codePoint += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') codePoint += h - 'A' + 10;
                        i++;
                    }
                    // Encode as UTF-8
                    if (codePoint < 0x80) {
                        result += (char)codePoint;
                    } else if (codePoint < 0x800) {
                        result += (char)(0xC0 | (codePoint >> 6));
                        result += (char)(0x80 | (codePoint & 0x3F));
                    } else if (codePoint < 0x10000) {
                        result += (char)(0xE0 | (codePoint >> 12));
                        result += (char)(0x80 | ((codePoint >> 6) & 0x3F));
                        result += (char)(0x80 | (codePoint & 0x3F));
                    } else {
                        result += (char)(0xF0 | (codePoint >> 18));
                        result += (char)(0x80 | ((codePoint >> 12) & 0x3F));
                        result += (char)(0x80 | ((codePoint >> 6) & 0x3F));
                        result += (char)(0x80 | (codePoint & 0x3F));
                    }
                } else if (i + 4 < inner.size()) {
                    // \uXXXX
                    int cp = 0;
                    for (int j = 0; j < 4; j++) {
                        cp *= 16;
                        char h = inner[i + 1 + j];
                        if (h >= '0' && h <= '9') cp += h - '0';
                        else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                    }
                    i += 4;
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                }
                break;
            default:
                result += inner[i];
                break;
            }
        } else {
            result += inner[i];
        }
    }

    return result;
}

bool Lexer::isKeyword(TokenKind kind) {
    return kind >= TokenKind::KW_break && kind <= TokenKind::KW_require;
}

TokenKind Lexer::identifierToKeyword(std::string_view text) {
    auto it = keywords_.find(text);
    if (it != keywords_.end()) return it->second;
    return TokenKind::Identifier;
}

const char* Lexer::tokenKindToString(TokenKind kind) {
    switch (kind) {
    case TokenKind::NumericLiteral: return "NumericLiteral";
    case TokenKind::StringLiteral: return "StringLiteral";
    case TokenKind::TemplateHead: return "TemplateHead";
    case TokenKind::TemplateMiddle: return "TemplateMiddle";
    case TokenKind::TemplateTail: return "TemplateTail";
    case TokenKind::NoSubstitutionTemplate: return "NoSubstitutionTemplate";
    case TokenKind::RegularExpressionLiteral: return "RegularExpressionLiteral";
    case TokenKind::BigIntLiteral: return "BigIntLiteral";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::KW_break: return "break";
    case TokenKind::KW_case: return "case";
    case TokenKind::KW_catch: return "catch";
    case TokenKind::KW_class: return "class";
    case TokenKind::KW_const: return "const";
    case TokenKind::KW_continue: return "continue";
    case TokenKind::KW_debugger: return "debugger";
    case TokenKind::KW_default: return "default";
    case TokenKind::KW_delete: return "delete";
    case TokenKind::KW_do: return "do";
    case TokenKind::KW_else: return "else";
    case TokenKind::KW_enum: return "enum";
    case TokenKind::KW_export: return "export";
    case TokenKind::KW_extends: return "extends";
    case TokenKind::KW_false: return "false";
    case TokenKind::KW_finally: return "finally";
    case TokenKind::KW_for: return "for";
    case TokenKind::KW_function: return "function";
    case TokenKind::KW_if: return "if";
    case TokenKind::KW_import: return "import";
    case TokenKind::KW_in: return "in";
    case TokenKind::KW_instanceof: return "instanceof";
    case TokenKind::KW_let: return "let";
    case TokenKind::KW_new: return "new";
    case TokenKind::KW_null: return "null";
    case TokenKind::KW_return: return "return";
    case TokenKind::KW_super: return "super";
    case TokenKind::KW_switch: return "switch";
    case TokenKind::KW_this: return "this";
    case TokenKind::KW_throw: return "throw";
    case TokenKind::KW_true: return "true";
    case TokenKind::KW_try: return "try";
    case TokenKind::KW_typeof: return "typeof";
    case TokenKind::KW_undefined: return "undefined";
    case TokenKind::KW_var: return "var";
    case TokenKind::KW_void: return "void";
    case TokenKind::KW_while: return "while";
    case TokenKind::KW_with: return "with";
    case TokenKind::KW_yield: return "yield";
    case TokenKind::KW_async: return "async";
    case TokenKind::KW_await: return "await";
    case TokenKind::KW_of: return "of";
    case TokenKind::KW_from: return "from";
    case TokenKind::KW_as: return "as";
    case TokenKind::KW_get: return "get";
    case TokenKind::KW_set: return "set";
    case TokenKind::KW_type: return "type";
    case TokenKind::KW_interface: return "interface";
    case TokenKind::KW_declare: return "declare";
    case TokenKind::KW_abstract: return "abstract";
    case TokenKind::KW_implements: return "implements";
    case TokenKind::KW_readonly: return "readonly";
    case TokenKind::KW_namespace: return "namespace";
    case TokenKind::KW_module: return "module";
    case TokenKind::KW_public: return "public";
    case TokenKind::KW_private: return "private";
    case TokenKind::KW_protected: return "protected";
    case TokenKind::KW_static: return "static";
    case TokenKind::KW_constructor: return "constructor";
    case TokenKind::KW_keyof: return "keyof";
    case TokenKind::KW_infer: return "infer";
    case TokenKind::KW_is: return "is";
    case TokenKind::KW_asserts: return "asserts";
    case TokenKind::KW_satisfies: return "satisfies";
    case TokenKind::KW_override: return "override";
    case TokenKind::KW_out: return "out";
    case TokenKind::KW_require: return "require";
    case TokenKind::OpenParen: return "(";
    case TokenKind::CloseParen: return ")";
    case TokenKind::OpenBrace: return "{";
    case TokenKind::CloseBrace: return "}";
    case TokenKind::OpenBracket: return "[";
    case TokenKind::CloseBracket: return "]";
    case TokenKind::Dot: return ".";
    case TokenKind::DotDotDot: return "...";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Comma: return ",";
    case TokenKind::Colon: return ":";
    case TokenKind::QuestionMark: return "?";
    case TokenKind::QuestionDot: return "?.";
    case TokenKind::Arrow: return "=>";
    case TokenKind::At: return "@";
    case TokenKind::Hash: return "#";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::StarStar: return "**";
    case TokenKind::Ampersand: return "&";
    case TokenKind::Pipe: return "|";
    case TokenKind::Caret: return "^";
    case TokenKind::Tilde: return "~";
    case TokenKind::ExclamationMark: return "!";
    case TokenKind::LessThan: return "<";
    case TokenKind::GreaterThan: return ">";
    case TokenKind::LessThanEquals: return "<=";
    case TokenKind::GreaterThanEquals: return ">=";
    case TokenKind::EqualsEquals: return "==";
    case TokenKind::ExclamationEquals: return "!=";
    case TokenKind::EqualsEqualsEquals: return "===";
    case TokenKind::ExclamationEqualsEquals: return "!==";
    case TokenKind::PlusPlus: return "++";
    case TokenKind::MinusMinus: return "--";
    case TokenKind::LessThanLessThan: return "<<";
    case TokenKind::GreaterThanGreaterThan: return ">>";
    case TokenKind::GreaterThanGreaterThanGreaterThan: return ">>>";
    case TokenKind::AmpersandAmpersand: return "&&";
    case TokenKind::PipePipe: return "||";
    case TokenKind::QuestionQuestion: return "??";
    case TokenKind::Equals: return "=";
    case TokenKind::PlusEquals: return "+=";
    case TokenKind::MinusEquals: return "-=";
    case TokenKind::StarEquals: return "*=";
    case TokenKind::SlashEquals: return "/=";
    case TokenKind::PercentEquals: return "%=";
    case TokenKind::StarStarEquals: return "**=";
    case TokenKind::AmpersandEquals: return "&=";
    case TokenKind::PipeEquals: return "|=";
    case TokenKind::CaretEquals: return "^=";
    case TokenKind::LessThanLessThanEquals: return "<<=";
    case TokenKind::GreaterThanGreaterThanEquals: return ">>=";
    case TokenKind::GreaterThanGreaterThanGreaterThanEquals: return ">>>=";
    case TokenKind::AmpersandAmpersandEquals: return "&&=";
    case TokenKind::PipePipeEquals: return "||=";
    case TokenKind::QuestionQuestionEquals: return "??=";
    case TokenKind::EndOfFile: return "EOF";
    case TokenKind::Error: return "Error";
    default: return "<unknown>";
    }
}

bool canStartStatement(TokenKind kind) {
    switch (kind) {
    case TokenKind::KW_var:
    case TokenKind::KW_let:
    case TokenKind::KW_const:
    case TokenKind::KW_function:
    case TokenKind::KW_class:
    case TokenKind::KW_if:
    case TokenKind::KW_while:
    case TokenKind::KW_for:
    case TokenKind::KW_do:
    case TokenKind::KW_switch:
    case TokenKind::KW_try:
    case TokenKind::KW_throw:
    case TokenKind::KW_return:
    case TokenKind::KW_break:
    case TokenKind::KW_continue:
    case TokenKind::KW_import:
    case TokenKind::KW_export:
    case TokenKind::KW_enum:
    case TokenKind::KW_interface:
    case TokenKind::KW_type:
    case TokenKind::KW_abstract:
    case TokenKind::KW_declare:
    case TokenKind::KW_async:
    case TokenKind::KW_debugger:
    case TokenKind::OpenBrace:
    case TokenKind::Semicolon:
    case TokenKind::At: // decorator
        return true;
    default:
        return false;
    }
}

bool isBinaryOperator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
    case TokenKind::StarStar:
    case TokenKind::Ampersand:
    case TokenKind::Pipe:
    case TokenKind::Caret:
    case TokenKind::LessThan:
    case TokenKind::GreaterThan:
    case TokenKind::LessThanEquals:
    case TokenKind::GreaterThanEquals:
    case TokenKind::EqualsEquals:
    case TokenKind::ExclamationEquals:
    case TokenKind::EqualsEqualsEquals:
    case TokenKind::ExclamationEqualsEquals:
    case TokenKind::LessThanLessThan:
    case TokenKind::GreaterThanGreaterThan:
    case TokenKind::GreaterThanGreaterThanGreaterThan:
    case TokenKind::AmpersandAmpersand:
    case TokenKind::PipePipe:
    case TokenKind::QuestionQuestion:
    case TokenKind::KW_in:
    case TokenKind::KW_instanceof:
    case TokenKind::KW_as:
        return true;
    default:
        return false;
    }
}

} // namespace ts::parser
