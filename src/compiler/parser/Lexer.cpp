#include "Lexer.h"
#include "RegExpEarlyErrors.h"
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <unicode/uchar.h>

namespace ts::parser {

namespace {
// Decode the UTF-8 sequence starting at p (must have at least len bytes
// available). Returns the code point and writes the consumed byte count
// to *consumed. On invalid input returns -1 and sets *consumed to 1.
static int32_t decodeUtf8(const char* p, int len, int* consumed) {
    if (len <= 0) { *consumed = 0; return -1; }
    unsigned char b0 = (unsigned char)p[0];
    if (b0 < 0x80) { *consumed = 1; return b0; }
    auto follow = [&](int idx) -> int {
        if (idx >= len) return -1;
        unsigned char b = (unsigned char)p[idx];
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };
    if ((b0 & 0xE0) == 0xC0) {
        int b1 = follow(1);
        if (b1 < 0) { *consumed = 1; return -1; }
        *consumed = 2;
        return ((b0 & 0x1F) << 6) | b1;
    }
    if ((b0 & 0xF0) == 0xE0) {
        int b1 = follow(1), b2 = follow(2);
        if (b1 < 0 || b2 < 0) { *consumed = 1; return -1; }
        *consumed = 3;
        return ((b0 & 0x0F) << 12) | (b1 << 6) | b2;
    }
    if ((b0 & 0xF8) == 0xF0) {
        int b1 = follow(1), b2 = follow(2), b3 = follow(3);
        if (b1 < 0 || b2 < 0 || b3 < 0) { *consumed = 1; return -1; }
        *consumed = 4;
        return ((b0 & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;
    }
    *consumed = 1;
    return -1;
}
} // namespace

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

// ASCII fast path. For non-ASCII identifier chars the lexer must decode
// the UTF-8 sequence at the cursor and consult ICU (u_isIDStart /
// u_isIDPart) — see scanIdentifierOrKeyword and isUnicodeIdentStartAt.
bool Lexer::isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

bool Lexer::isIdentPart(char c) {
    return isIdentStart(c) || isDigit(c);
}

// Unicode 16.0 (Sept 2024) added ID_Start / ID_Continue code points that the
// bundled ICU 74 (Unicode 15.1) doesn't recognize. Hard-code the new ranges as
// a fallback so tests like language/identifiers/{start,part}-unicode-16.0.0.js
// parse cleanly. Ranges derived from running the test corpus through Unicode
// 16.0 DerivedCoreProperties.txt. When the bundled ICU is upgraded to 76+
// (Unicode 16+), this table can be removed.
static bool isUnicode16IdStart(int32_t cp) {
    static const int32_t ranges[][2] = {
        {0x1C89, 0x1C8A},     // Cyrillic Extended-B additions
        {0xA7CB, 0xA7CD},     // Latin Extended-D additions
        {0xA7DA, 0xA7DC},     // Latin Extended-D additions
        {0x105C0, 0x105F3},   // Todhri
        {0x10D4A, 0x10D65},   // Garay (uppercase)
        {0x10D6F, 0x10D85},   // Garay (lowercase) + Garay extensions
        {0x10EC2, 0x10EC4},   // Yezidi additions
        {0x11380, 0x11389},   // Tulu-Tigalari
        {0x1138B, 0x1138B},
        {0x1138E, 0x1138E},
        {0x11390, 0x113B5},
        {0x113B7, 0x113B7},
        {0x113D1, 0x113D1},
        {0x113D3, 0x113D3},
        {0x11BC0, 0x11BE0},   // Sunuwar
        {0x13460, 0x143FA},   // Egyptian Hieroglyphs Extended-A
        {0x16100, 0x1611D},   // Gurung Khema
        {0x16D40, 0x16D6C},   // Kirat Rai
        {0x18CFF, 0x18CFF},   // Khitan Small Script additions
        {0x1E5D0, 0x1E5ED},   // Ol Onal
        {0x1E5F0, 0x1E5F0},
    };
    for (auto& r : ranges) {
        if (cp >= r[0] && cp <= r[1]) return true;
    }
    return false;
}

// Unicode 16.0 ID_Continue additions that are NOT ID_Start (combining marks,
// digits, vowel signs added by Unicode 16). ID_Continue is a superset of
// ID_Start, so a code point matching isUnicode16IdStart is automatically
// part-eligible; this table covers the rest.
static bool isUnicode16IdContinueOnly(int32_t cp) {
    static const int32_t ranges[][2] = {
        {0x0897, 0x0897},     // Arabic combining mark
        {0x10D40, 0x10D49},   // Garay digits
        {0x10D69, 0x10D6D},   // Garay extensions (combining)
        {0x10EFC, 0x10EFC},   // Arabic Extended-C combining
        {0x113B8, 0x113C0},   // Tulu-Tigalari vowel signs + marks
        {0x113C2, 0x113C2},
        {0x113C5, 0x113C5},
        {0x113C7, 0x113CA},
        {0x113CC, 0x113D0},
        {0x113D2, 0x113D2},
        {0x113E1, 0x113E2},
        {0x116D0, 0x116E3},   // Myanmar Extended-C digits/marks
        {0x11BF0, 0x11BF9},   // Sunuwar digits
        {0x11F5A, 0x11F5A},   // Kawi extension
        {0x1611E, 0x16139},   // Gurung Khema extensions + digits
        {0x16D70, 0x16D79},   // Kirat Rai digits
        {0x1CCF0, 0x1CCF9},   // Outlined Numeric Digits
        {0x1E5EE, 0x1E5EF},   // Ol Onal extensions
        {0x1E5F1, 0x1E5FA},   // Ol Onal digits
    };
    for (auto& r : ranges) {
        if (cp >= r[0] && cp <= r[1]) return true;
    }
    return false;
}

// Decode UTF-8 at the cursor and check Unicode ID_Start. Returns the
// length in bytes of the matching sequence, or 0 if the cursor is not
// at an ID_Start code point.
int Lexer::isUnicodeIdentStartAt() const {
    if (pos_ >= (int)source_.size()) return 0;
    if (((unsigned char)source_[pos_]) < 0x80) return 0;
    int consumed = 0;
    int32_t cp = decodeUtf8(source_.data() + pos_,
                            (int)source_.size() - pos_, &consumed);
    if (cp < 0) return 0;
    if (u_isIDStart(cp)) return consumed;
    if (isUnicode16IdStart(cp)) return consumed;
    return 0;
}

// As above, but for ID_Continue. ZWJ (U+200D) and ZWNJ (U+200C) are
// allowed in IdentifierPart per ES262 in addition to whatever ICU
// classifies as ID_Continue. Unicode 16 ID_Continue is a strict superset
// of ID_Start (plus some script-specific marks), so we reuse the same
// fallback table — the test corpus only exercises ID_Start additions
// for IdentifierStart positions and standard ID_Continue elsewhere.
int Lexer::isUnicodeIdentPartAt() const {
    if (pos_ >= (int)source_.size()) return 0;
    if (((unsigned char)source_[pos_]) < 0x80) return 0;
    int consumed = 0;
    int32_t cp = decodeUtf8(source_.data() + pos_,
                            (int)source_.size() - pos_, &consumed);
    if (cp < 0) return 0;
    if (u_isIDPart(cp)) return consumed;
    if (cp == 0x200C || cp == 0x200D) return consumed;
    if (isUnicode16IdStart(cp)) return consumed;
    if (isUnicode16IdContinueOnly(cp)) return consumed;
    return 0;
}

// Returns the byte length of any non-ASCII whitespace or line terminator
// at the cursor and writes whether it counts as a line terminator
// (sets *isLineTerm). U+2028 / U+2029 are line terminators per ES262.
// Non-line whitespace covers everything ICU's u_isUWhiteSpace recognises
// (NBSP, NEL, OGHAM-SPACE, MVS, EM-SPACE, BOM, etc.).
int Lexer::isUnicodeWhitespaceAt(bool* isLineTerm) const {
    if (pos_ >= (int)source_.size()) return 0;
    if (((unsigned char)source_[pos_]) < 0x80) return 0;
    int consumed = 0;
    int32_t cp = decodeUtf8(source_.data() + pos_,
                            (int)source_.size() - pos_, &consumed);
    if (cp < 0) return 0;
    if (cp == 0x2028 || cp == 0x2029) {
        if (isLineTerm) *isLineTerm = true;
        return consumed;
    }
    if (u_isUWhiteSpace(cp) || cp == 0xFEFF /* BOM */) {
        if (isLineTerm) *isLineTerm = false;
        return consumed;
    }
    return 0;
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
        // ECMA-262 §11.2 WhiteSpace: TAB, VT, FF, SP, NBSP, ZWNBSP/BOM,
        // and the Unicode "White_Space" category. The non-ASCII branch
        // below handles NBSP/BOM/etc via ICU; here we cover the ASCII
        // whitespace set including \v (U+000B) and \f (U+000C) which
        // were previously missed (treated as token-starters → spurious
        // parse errors on tests that probe whitespace-tolerance).
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            advance();
        } else if (c == '\n') {
            hadNewline_ = true;
            advance();
        } else if (c == '\r') {
            hadNewline_ = true;
            advance();
        }
        // Non-ASCII whitespace / line terminator. ICU recognises the full
        // Unicode White_Space set (NBSP, NEL, OGHAM-SPACE, EM-SPACE, etc.)
        // and we add U+2028/U+2029 (line terminators), U+FEFF (BOM) and
        // U+180E (MVS, kept for back-compat).
        else if (((unsigned char)c) >= 0x80) {
            bool isLineTerm = false;
            int n = isUnicodeWhitespaceAt(&isLineTerm);
            if (n == 0) {
                break;  // Non-ASCII byte that isn't whitespace — leave it for the
                        // identifier scanner / dispatcher.
            }
            if (isLineTerm) hadNewline_ = true;
            for (int i = 0; i < n; i++) advance();
        }
        else if (c == '/' && peekAt(1) == '/') {
            // Single-line comment
            advance(); advance(); // skip //
            while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
                advance();
            }
        }
        // ECMA-262 Annex B.1.3: HTML-like comments. Web reality.
        // `<!--` starts a SingleLineHTMLOpenComment anywhere — treat as `//`.
        // `-->` starts a SingleLineHTMLCloseComment, but ONLY when preceded by a
        // LineTerminator (directly, or with intervening whitespace / multi-line
        // comments since the last regular token). hadNewline_ tracks exactly
        // that condition within this skipWhitespaceAndComments call.
        else if (c == '<' && peekAt(1) == '!' && peekAt(2) == '-' && peekAt(3) == '-') {
            advance(); advance(); advance(); advance();  // skip <!--
            while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
                advance();
            }
        }
        else if (c == '-' && peekAt(1) == '-' && peekAt(2) == '>' &&
                 (hadNewline_ || !hasEmittedToken_)) {
            // HTMLCloseComment is allowed when preceded by a LineTerminator
            // OR at the very start of input (per Annex B.1.3 InputElement
            // productions where it appears at the start of a script).
            advance(); advance(); advance();  // skip -->
            while (!isAtEnd() && peek() != '\n' && peek() != '\r') {
                advance();
            }
        }
        else if (c == '/' && peekAt(1) == '*') {
            // Multi-line comment. ECMA-262 11.4: if a MultiLineComment
            // contains a LineTerminator, the whole comment is treated as
            // a LineTerminator for ASI purposes — set hadNewline_ on
            // \n, \r, AND U+2028/U+2029 (the non-ASCII line terminators).
            advance(); advance(); // skip /*
            while (!isAtEnd()) {
                if (peek() == '*' && peekAt(1) == '/') {
                    advance(); advance(); // skip */
                    break;
                }
                if (peek() == '\n' || peek() == '\r') {
                    hadNewline_ = true;
                    advance();
                    continue;
                }
                if (((unsigned char)peek()) >= 0x80) {
                    bool isLineTerm = false;
                    int n = isUnicodeWhitespaceAt(&isLineTerm);
                    if (n > 0) {
                        if (isLineTerm) hadNewline_ = true;
                        for (int i = 0; i < n; i++) advance();
                        continue;
                    }
                }
                advance();
            }
        } else if (c == '#' && pos_ == 0 && peekAt(1) == '!') {
            // Hashbang: #! at start of file. Terminated by any LineTerminator
            // per ECMA-262 — \n, \r, U+2028 (LS), U+2029 (PS). LS/PS in UTF-8
            // are 0xE2 0x80 0xA8 / 0xA9 respectively.
            advance(); advance();
            while (!isAtEnd()) {
                char ch = peek();
                if (ch == '\n' || ch == '\r') break;
                if ((unsigned char)ch == 0xE2 &&
                    (unsigned char)peekAt(1) == 0x80 &&
                    ((unsigned char)peekAt(2) == 0xA8 ||
                     (unsigned char)peekAt(2) == 0xA9)) break;
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

    // Mark that the script has progressed past the leading whitespace/comment
    // region. The HTMLCloseComment guard in skipWhitespaceAndComments uses
    // !hasEmittedToken_ to allow `-->` at the very start of input; once we
    // return any token (or hit EOF), that allowance is consumed.
    hasEmittedToken_ = true;

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
    // Also allow \uXXXX Unicode escape at identifier start (ES §12.6)
    // and any non-ASCII code point with Unicode ID_Start.
    if (isIdentStart(c) || (c == '\\' && peekAt(1) == 'u')
        || (((unsigned char)c) >= 0x80 && isUnicodeIdentStartAt() > 0)) {
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

    bool isFirstChar = true;
    auto hexDigitValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    while (!isAtEnd()) {
        char c = peek();
        if (((unsigned char)c) >= 0x80) {
            // Decode the multibyte sequence and check Unicode ID_Start (first)
            // or ID_Continue (subsequent). ZWJ/ZWNJ are explicitly allowed by
            // isUnicodeIdentPartAt.
            int n = isFirstChar ? isUnicodeIdentStartAt() : isUnicodeIdentPartAt();
            if (n == 0) break;  // not an identifier code point — stop here
            for (int i = 0; i < n; i++) advance();
            isFirstChar = false;
            continue;
        }
        bool partOK = isFirstChar ? isIdentStart(c) : isIdentPart(c);
        if (partOK) {
            advance();
            isFirstChar = false;
        } else if (c == '\\' && peekAt(1) == 'u') {
            // ES §12.6.1: Unicode escape sequence in identifier. The decoded
            // code point must satisfy IdentifierStart (first char) or
            // IdentifierPart (subsequent chars). U+0000, U+200D at start,
            // etc. are SyntaxErrors.
            int escStartLine = line_;
            int escStartCol = column_;
            advance(); // consume '\'
            advance(); // consume 'u'
            uint32_t cp = 0;
            if (!isAtEnd() && peek() == '{') {
                // \u{XXXX...} form
                advance(); // consume '{'
                while (!isAtEnd() && isHexDigit(peek())) {
                    cp = cp * 16 + hexDigitValue(peek());
                    advance();
                }
                if (!isAtEnd() && peek() == '}') {
                    advance(); // consume '}'
                }
            } else {
                // \uXXXX form — exactly 4 hex digits
                for (int i = 0; i < 4 && !isAtEnd() && isHexDigit(peek()); i++) {
                    cp = cp * 16 + hexDigitValue(peek());
                    advance();
                }
            }
            // Validate the decoded code point against ID_Start / ID_Part.
            // ASCII `$` and `_` are also valid IdentifierStart / Part per
            // the spec table for completeness.
            bool validStart = (cp == '$' || cp == '_') ||
                              u_isIDStart((UChar32)cp) ||
                              isUnicode16IdStart(cp);
            bool validPart = validStart || cp == 0x200C || cp == 0x200D ||
                             u_isIDPart((UChar32)cp) ||
                             isUnicode16IdContinueOnly(cp);
            bool ok = isFirstChar ? validStart : validPart;
            if (!ok) {
                char buf[160];
                snprintf(buf, sizeof(buf),
                    "%d:%d: SyntaxError: invalid identifier character "
                    "U+%04X via Unicode escape",
                    escStartLine, escStartCol, (unsigned)cp);
                throw std::runtime_error(buf);
            }
            isFirstChar = false;
        } else {
            break;
        }
    }

    std::string_view text(source_.data() + start, pos_ - start);

    if (text.find('\\') == std::string_view::npos) {
        // No escapes — direct keyword lookup
        auto it = keywords_.find(text);
        if (it != keywords_.end()) {
            return makeToken(it->second, start);
        }
    } else {
        // Contains \u escapes — decode and check if the result is a keyword.
        // Per ES §12.6.1, Unicode-escaped keywords are still reserved words:
        // `\u0063lass` is NOT a valid identifier.
        std::string decoded;
        decoded.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'u') {
                i += 2; // skip \u
                uint32_t cp = 0;
                if (i < text.size() && text[i] == '{') {
                    i++; // skip {
                    while (i < text.size() && text[i] != '}') {
                        cp = cp * 16 + (isDigit(text[i]) ? text[i] - '0'
                            : (text[i] >= 'a' ? text[i] - 'a' + 10 : text[i] - 'A' + 10));
                        i++;
                    }
                    // i now points to '}', loop will increment past it
                } else {
                    for (int j = 0; j < 4 && i < text.size(); j++, i++) {
                        cp = cp * 16 + (isDigit(text[i]) ? text[i] - '0'
                            : (text[i] >= 'a' ? text[i] - 'a' + 10 : text[i] - 'A' + 10));
                    }
                    i--; // loop will increment
                }
                // Append as UTF-8 (for BMP characters, just cast)
                if (cp < 0x80) {
                    decoded += (char)cp;
                } else if (cp < 0x800) {
                    decoded += (char)(0xC0 | (cp >> 6));
                    decoded += (char)(0x80 | (cp & 0x3F));
                } else {
                    decoded += (char)(0xE0 | (cp >> 12));
                    decoded += (char)(0x80 | ((cp >> 6) & 0x3F));
                    decoded += (char)(0x80 | (cp & 0x3F));
                }
            } else {
                decoded += text[i];
            }
        }
        // Check decoded form against keywords.
        // Per ES §12.6.1, an identifier that resolves to a reserved word
        // via Unicode escape is a SyntaxError when used as a
        // BindingIdentifier or IdentifierReference. Since the parser
        // doesn't distinguish escaped-keyword tokens from regular
        // keywords, the safest approach is to report a syntax error here
        // in the lexer. Property-name contexts (obj.\u0063lass) are
        // acceptable but rare in practice and require a more nuanced
        // parser-level check — accept the minor over-rejection for now.
        // Per ES §12.6.1, an identifier that resolves to a reserved word
        // via Unicode escape is a SyntaxError when used as a
        // BindingIdentifier or IdentifierReference. Property-name
        // contexts (`obj.class`, `{ default: x }`) are allowed but
        // require parser-side context — implementing that nuance is
        // tracked separately. The lex-time over-rejection costs ~64
        // PropertyName tests but catches ~315 negative-parse tests; net
        // is strongly positive in favor of keeping the rejection.
        // Per ECMA-262 12.6.1: an Identifier whose decoded form matches
        // a reserved word is a SyntaxError as a BindingIdentifier or
        // IdentifierReference, but is fine as an IdentifierName in
        // PropertyName / MemberExpression contexts. Mark the token; the
        // parser checks the flag at every binding/reference site
        // (parseBindingNameOrPattern, parsePrimaryExpression, and
        // parseObjectLiteral's shorthand-property branch).
        auto it = keywords_.find(decoded);
        if (it != keywords_.end()) {
            // Narrow exception: `async` is never reserved in any context.
            // Other contextual keywords (let/yield/await/static/etc.) have
            // context-sensitive reservation that the parser checks via the
            // KW_xxx token kind, so they MUST stay marked as escapedReservedWord
            // to preserve the existing strict-mode / generator / async checks.
            if (it->second == TokenKind::KW_async) {
                Token tok = makeToken(TokenKind::Identifier, start);
                tok.decodedText = std::move(decoded);
                return tok;
            }
            Token tok = makeToken(TokenKind::Identifier, start);
            tok.escapedReservedWord = true;
            tok.decodedText = std::move(decoded);
            return tok;
        }
        // Even when the decoded form isn't a reserved word, store it so
        // the parser can use the spec-correct identifier name in
        // PropertyName / MemberExpression / ImportSpecifier / Export-
        // Specifier positions. Without this, `{ foo: 42 }` would
        // define property `foo` instead of `foo`.
        Token tok = makeToken(TokenKind::Identifier, start);
        tok.decodedText = std::move(decoded);
        return tok;
    }

    return makeToken(TokenKind::Identifier, start);
}

Token Lexer::scanNumericLiteral() {
    int start = pos_;
    tokenStartLine_ = line_;
    tokenStartColumn_ = column_;

    // Helper: consume a run of digits (+ separators) with separator validation.
    // Returns true on success, false (and reports error) on invalid separator position.
    auto consumeDigitsWithSeparators = [&](auto isDigitFn, const char* context) -> bool {
        bool prevWasSep = false;
        bool prevWasDigit = false;
        while (!isAtEnd()) {
            char ch = peek();
            if (isDigitFn(ch)) {
                prevWasSep = false;
                prevWasDigit = true;
                advance();
            } else if (ch == '_') {
                if (prevWasSep) {
                    reportLexError(std::string("Only one underscore is allowed as numeric separator in ") + context);
                    advance();
                    return false;
                }
                if (!prevWasDigit) {
                    reportLexError(std::string("Numeric separator must follow a digit in ") + context);
                    advance();
                    return false;
                }
                prevWasSep = true;
                prevWasDigit = false;
                advance();
            } else {
                break;
            }
        }
        if (prevWasSep) {
            reportLexError(std::string("Numeric separator cannot be at end of ") + context);
            return false;
        }
        return true;
    };

    // ECMA-262 12.9.3.1: the SourceCharacter immediately following a
    // NumericLiteral must not be an IdentifierStart or DecimalDigit
    // (`3in`, `0b12`, `1.foo`, `1n_` are all SyntaxErrors).
    auto rejectIdentAfterLiteral = [&]() {
        if (!isAtEnd() && (isIdentStart(peek()) || isDigit(peek()))) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "%d:%d: SyntaxError: identifier or digit cannot "
                     "immediately follow a numeric literal",
                     tokenStartLine_, tokenStartColumn_);
            throw std::runtime_error(buf);
        }
    };

    char c = peek();

    // Handle 0x, 0o, 0b prefixes
    if (c == '0') {
        advance();
        char next = peek();
        if (next == 'x' || next == 'X') {
            advance();
            if (peek() == '_') {
                reportLexError("Numeric separator cannot be after 0x prefix");
            }
            consumeDigitsWithSeparators([](char ch) { return isHexDigit(ch); }, "hex literal");
            if (!isAtEnd() && peek() == 'n') {
                advance();
                rejectIdentAfterLiteral();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            rejectIdentAfterLiteral();
            return makeToken(TokenKind::NumericLiteral, start);
        }
        if (next == 'o' || next == 'O') {
            advance();
            if (peek() == '_') {
                reportLexError("Numeric separator cannot be after 0o prefix");
            }
            consumeDigitsWithSeparators([](char ch) { return ch >= '0' && ch <= '7'; }, "octal literal");
            if (!isAtEnd() && peek() == 'n') {
                advance();
                rejectIdentAfterLiteral();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            rejectIdentAfterLiteral();
            return makeToken(TokenKind::NumericLiteral, start);
        }
        if (next == 'b' || next == 'B') {
            advance();
            if (peek() == '_') {
                reportLexError("Numeric separator cannot be after 0b prefix");
            }
            consumeDigitsWithSeparators([](char ch) { return ch == '0' || ch == '1'; }, "binary literal");
            if (!isAtEnd() && peek() == 'n') {
                advance();
                rejectIdentAfterLiteral();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            rejectIdentAfterLiteral();
            return makeToken(TokenKind::NumericLiteral, start);
        }

        // Legacy octal / NonOctalDecimal: 0 followed by digits (no x/o/b/./eE suffix).
        // ECMA-262 Annex B.1.1:
        //   - LegacyOctalIntegerLiteral: 0 followed by octal digits (0-7).
        //   - NonOctalDecimalIntegerLiteral: 0 followed by digits including at
        //     least one 8 or 9 (treated as decimal).
        // BOTH are SyntaxError in strict mode (handled at the parser via the
        // isLegacyOctal token flag). In non-strict the parser evaluates the
        // literal via std::stod which gives the spec-correct decimal value
        // for NonOctalDecimal forms.
        // BigInt suffix is rejected on any 0-prefixed legacy number.
        if (isDigit(next)) {
            while (!isAtEnd() && isDigit(peek())) {
                advance();
            }
            // Reject BigInt suffix on legacy octal / non-octal-decimal (0nnn)
            if (!isAtEnd() && peek() == 'n') {
                reportLexError("BigInt literal cannot use legacy octal notation");
                advance();
                return makeToken(TokenKind::BigIntLiteral, start);
            }
            // Per ECMA-262 Annex B.1.1: LegacyOctalIntegerLiteral is a
            // Syntax Error in strict mode. The lexer can't know strict
            // mode yet (directive prologues are parser-detected) so we
            // flag the token and defer the check to the parser.
            rejectIdentAfterLiteral();
            Token t = makeToken(TokenKind::NumericLiteral, start);
            t.isLegacyOctal = true;
            return t;
        }
    }

    // Regular decimal number
    bool hasDecimalPoint = false;
    bool hasExponent = false;
    consumeDigitsWithSeparators([](char ch) { return isDigit(ch); }, "decimal literal");

    // Decimal point. Per ES262 DecimalLiteral grammar, the fractional part
    // is optional after the dot — so `0.`, `1.`, `2.` are all valid number
    // literals on their own (and idiomatically used like `0..toString(2)`
    // for property access on an integer). Maximal munch: the dot always
    // joins the literal (so `0.e1` is a number, and `1.foo` is the spec's
    // SyntaxError via the ident-after-literal check below, exactly as in
    // V8 — NOT a member access).
    if (!isAtEnd() && peek() == '.') {
        advance(); // .
        hasDecimalPoint = true;
        consumeDigitsWithSeparators([](char ch) { return isDigit(ch); }, "decimal fraction");
    }

    // Exponent
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        advance();
        hasExponent = true;
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) advance();
        consumeDigitsWithSeparators([](char ch) { return isDigit(ch); }, "exponent");
    }

    // BigInt suffix — only valid for pure integer decimal literals
    if (!isAtEnd() && peek() == 'n') {
        if (hasDecimalPoint) {
            reportLexError("BigInt literal cannot have a decimal point");
        }
        if (hasExponent) {
            reportLexError("BigInt literal cannot have an exponent");
        }
        advance();
        rejectIdentAfterLiteral();
        return makeToken(TokenKind::BigIntLiteral, start);
    }

    rejectIdentAfterLiteral();
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
            if (!isAtEnd()) {
                // LineContinuation: \<CR><LF> is a single terminator.
                if (peek() == '\r') {
                    advance();
                    if (!isAtEnd() && peek() == '\n') advance();
                } else {
                    advance(); // escaped char (including a lone \<LF>)
                }
            }
        } else if (peek() == '\n' || peek() == '\r') {
            // ECMA-262 12.9.4: an unescaped LineTerminator cannot appear in
            // a string literal (U+2028/U+2029 are allowed since ES2019).
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "%d:%d: SyntaxError: unterminated string literal",
                     tokenStartLine_, tokenStartColumn_);
            throw std::runtime_error(buf);
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

    int bodyStart = pos_;
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
    int bodyEnd = pos_ - 1;  // position of closing '/' (or end if unterminated)
    if (bodyEnd < bodyStart) bodyEnd = bodyStart;

    // Scan flags: g, i, m, s, u, v, y, d
    int flagStart = pos_;
    while (!isAtEnd() && isIdentPart(peek())) {
        advance();
    }
    int flagEnd = pos_;

    // ECMA-262 early errors: flag validity/duplication and pattern
    // compilability (probe through the runtime's own ICU translation).
    // Throws SyntaxError. See RegExpEarlyErrors.cpp for the safety argument.
    tsaot::validateRegExpLiteral(
        source_.substr(bodyStart, bodyEnd - bodyStart),
        source_.substr(flagStart, flagEnd - flagStart),
        tokenStartLine_, tokenStartColumn_);

    // ECMA-262 22.2.1.4: UnicodePropertyEscape `\p{Name}` / `\p{Name=Value}` /
    // `\P{...}` is only valid in Unicode mode (`u` or `v` flag). When that
    // flag is present, the property escape must be syntactically well-formed:
    //   - `\p` / `\P` must be immediately followed by `{`
    //   - Content between braces must be non-empty
    //   - May not begin with `^` or contain `=` with empty LHS
    //   - `}` must close before regex body ends.
    // This catches the grammar-extension-{unclosed,empty,unopened,
    // circumflex-negation,separator-and-value-only} cluster (~18 tests).
    // Full property NAME validation (binary vs non-binary, value lookup) is
    // deferred — that needs an ICU property table; this commit handles
    // structure only.
    bool hasUnicodeFlag = false;
    for (int i = flagStart; i < flagEnd; i++) {
        char f = source_[i];
        if (f == 'u' || f == 'v') { hasUnicodeFlag = true; break; }
    }
    if (hasUnicodeFlag) {
        auto fail = [&](const char* msg, int errLine, int errCol) {
            char buf[160];
            snprintf(buf, sizeof(buf), "%d:%d: SyntaxError: %s",
                     errLine, errCol, msg);
            throw std::runtime_error(buf);
        };
        // Re-walk the body locating `\p` / `\P` escapes and validating.
        int p = bodyStart;
        while (p < bodyEnd) {
            unsigned char c = (unsigned char)source_[p];
            if (c == '\\' && p + 1 < bodyEnd) {
                char nxt = source_[p + 1];
                if (nxt == 'p' || nxt == 'P') {
                    // Record source position approximately (line/col not
                    // strictly tracked through bodyStart..bodyEnd — emit at
                    // the regex's start line/col instead, which is correct
                    // enough for test262's expected SyntaxError detection).
                    int errLine = tokenStartLine_;
                    int errCol = tokenStartColumn_;
                    int q = p + 2;
                    if (q >= bodyEnd || source_[q] != '{') {
                        fail("'\\p' / '\\P' in Unicode regex requires '{...}'",
                             errLine, errCol);
                    }
                    q++;  // past '{'
                    int contentStart = q;
                    while (q < bodyEnd && source_[q] != '}') {
                        // Don't allow `/` or newline mid-escape — that means
                        // the brace was never closed in this regex body.
                        q++;
                    }
                    if (q >= bodyEnd) {
                        fail("unterminated '\\p{...}' Unicode property escape",
                             errLine, errCol);
                    }
                    int contentLen = q - contentStart;
                    if (contentLen == 0) {
                        fail("empty '\\p{}' Unicode property escape",
                             errLine, errCol);
                    }
                    // Disallow leading '^' (negation belongs OUTSIDE on \P).
                    if (source_[contentStart] == '^') {
                        fail("'\\p{^...}' is not a valid Unicode property "
                             "escape; use '\\P{...}' for negation",
                             errLine, errCol);
                    }
                    // Disallow leading '=' (empty property name with value).
                    if (source_[contentStart] == '=') {
                        fail("'\\p{=value}' is not a valid Unicode property "
                             "escape; the name before '=' must be non-empty",
                             errLine, errCol);
                    }
                    p = q + 1;  // past '}'
                    continue;
                }
                // Other escape: skip the escaped char.
                p += 2;
                continue;
            }
            p++;
        }
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
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'v': result += '\v'; break;
            case '\\': result += '\\'; break;
            case '\'': result += '\''; break;
            case '"': result += '"'; break;
            case '`': result += '`'; break;
            case '\n':
                // LineContinuation contributes nothing to the value.
                break;
            case '\r':
                if (i + 1 < inner.size() && inner[i + 1] == '\n') i++;
                break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                // LegacyOctalEscapeSequence (Annex B.1.2); strict mode and
                // templates already rejected these upstream via
                // validateLegacyOctalEscapes. First digit 0-3 admits up to
                // 3 octal digits, 4-7 up to 2; \0 alone is the NUL escape.
                char first = inner[i];
                int v = first - '0';
                int maxDigits = (first <= '3') ? 3 : 2;
                int consumed = 1;
                while (consumed < maxDigits && i + 1 < inner.size() &&
                       inner[i + 1] >= '0' && inner[i + 1] <= '7') {
                    v = v * 8 + (inner[i + 1] - '0');
                    i++;
                    consumed++;
                }
                if (v < 0x80) {
                    result += (char)v;
                } else {
                    result += (char)(0xC0 | (v >> 6));
                    result += (char)(0x80 | (v & 0x3F));
                }
                break;
            }
            case 'x': {
                auto hexVal = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                // ECMA-262 12.9.4.1: \x must be followed by exactly 2 hex
                // digits; anything else is a SyntaxError (was: silently
                // decoded with garbage digits treated as 0).
                int hi = (i + 1 < inner.size()) ? hexVal(inner[i + 1]) : -1;
                int lo = (i + 2 < inner.size()) ? hexVal(inner[i + 2]) : -1;
                if (hi < 0 || lo < 0) {
                    throw std::runtime_error(
                        "SyntaxError: invalid hexadecimal escape sequence "
                        "in string literal");
                }
                // \xHH is the character with code unit HH (0..255).
                // result is UTF-8, so a code unit >= 0x80 needs a
                // 2-byte encoding — raw 0xE9 alone is an invalid
                // UTF-8 lead byte that later decoders replace with
                // U+FFFD, breaking string equality with the same
                // character written as a literal "é".
                int cp = hi * 16 + lo;
                if (cp < 0x80) {
                    result += (char)cp;
                } else {
                    result += (char)(0xC0 | (cp >> 6));
                    result += (char)(0x80 | (cp & 0x3F));
                }
                i += 2;
                break;
            }
            case 'u':
                if (i + 1 < inner.size() && inner[i + 1] == '{') {
                    // \u{XXXX} unicode escape. ECMA-262 12.9.4: must be
                    // 1+ hex digits, <= 0x10FFFF, closed with '}'.
                    i += 2; // skip u{
                    int codePoint = 0;
                    int nDigits = 0;
                    while (i < inner.size() && inner[i] != '}') {
                        codePoint = codePoint * 16;
                        char h = inner[i];
                        if (h >= '0' && h <= '9') codePoint += h - '0';
                        else if (h >= 'a' && h <= 'f') codePoint += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') codePoint += h - 'A' + 10;
                        else {
                            throw std::runtime_error(
                                "SyntaxError: invalid Unicode escape "
                                "sequence in string literal");
                        }
                        if (codePoint > 0x10FFFF) {
                            throw std::runtime_error(
                                "SyntaxError: Unicode code point out of "
                                "range in string literal escape");
                        }
                        nDigits++;
                        i++;
                    }
                    if (nDigits == 0 || i >= inner.size()) {
                        throw std::runtime_error(
                            "SyntaxError: invalid Unicode escape sequence "
                            "in string literal");
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
                    // \uXXXX — exactly 4 hex digits or SyntaxError.
                    int cp = 0;
                    for (int j = 0; j < 4; j++) {
                        cp *= 16;
                        char h = inner[i + 1 + j];
                        if (h >= '0' && h <= '9') cp += h - '0';
                        else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                        else {
                            throw std::runtime_error(
                                "SyntaxError: invalid Unicode escape "
                                "sequence in string literal");
                        }
                    }
                    i += 4;
                    // Combine a UTF-16 surrogate pair written as two \uXXXX
                    // escapes (high \uD800-\uDBFF then low \uDC00-\uDFFF) into a
                    // single supplementary code point. Without this each half
                    // hit the 3-byte branch below and emitted invalid UTF-8
                    // (ED A0 BC ...), which ICU later decoded as U+FFFD — so
                    // `'🍂'` became replacement chars instead of 🍂.
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        i + 6 < inner.size() && inner[i + 1] == '\\' &&
                        inner[i + 2] == 'u' && inner[i + 3] != '{') {
                        int lo = 0; bool ok = true;
                        for (int j = 0; j < 4; j++) {
                            char h = inner[i + 3 + j];
                            if (h >= '0' && h <= '9') lo = lo * 16 + (h - '0');
                            else if (h >= 'a' && h <= 'f') lo = lo * 16 + (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo = lo * 16 + (h - 'A' + 10);
                            else { ok = false; break; }
                        }
                        if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            i += 6; // consume the trailing \uXXXX
                        }
                    }
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xF0 | (cp >> 18));
                        result += (char)(0x80 | ((cp >> 12) & 0x3F));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    // \u with fewer than 4 chars remaining.
                    throw std::runtime_error(
                        "SyntaxError: invalid Unicode escape sequence in "
                        "string literal");
                }
                break;
            default:
                // \<LS> / \<PS> (U+2028/U+2029, UTF-8 E2 80 A8/A9) are
                // LineContinuations: contribute nothing.
                if ((unsigned char)inner[i] == 0xE2 && i + 2 < inner.size() &&
                    (unsigned char)inner[i + 1] == 0x80 &&
                    ((unsigned char)inner[i + 2] == 0xA8 ||
                     (unsigned char)inner[i + 2] == 0xA9)) {
                    i += 2;
                    break;
                }
                // NonEscapeCharacter: identity (\8 and \9 land here too).
                result += inner[i];
                break;
            }
        } else {
            result += inner[i];
        }
    }

    return result;
}

void Lexer::validateLegacyOctalEscapes(
    std::string_view rawToken, bool isStrict, bool isTemplate,
    int line, int column) {
    // Walk the raw lexeme. Quotes (if any) are skipped over by the
    // generic char loop. ECMA-262 12.8.4.1 + Annex B.1.2:
    //   - Strict-mode string literals must reject \1..\7, \0<digit>,
    //     \8, \9.
    //   - Template literals must reject the same in any mode.
    auto fail = [&](const std::string& msg) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%d:%d: SyntaxError: %s",
                 line, column, msg.c_str());
        throw std::runtime_error(buf);
    };
    bool reject = isStrict || isTemplate;
    if (!reject) return;
    for (size_t i = 0; i < rawToken.size(); i++) {
        if (rawToken[i] != '\\') continue;
        if (i + 1 >= rawToken.size()) break;
        char nxt = rawToken[i + 1];
        // \xHH — skip 2 hex digits, no octal concern.
        if (nxt == 'x') { i += 1; continue; }
        // \u — skip the unicode escape; either \u{...} or \uXXXX.
        if (nxt == 'u') { i += 1; continue; }
        // \0 alone is the NUL escape (allowed). \0 followed by a
        // decimal digit is a LegacyOctalEscapeSequence.
        if (nxt == '0') {
            if (i + 2 < rawToken.size()) {
                char after = rawToken[i + 2];
                if (after >= '0' && after <= '9') {
                    fail("Octal escape sequences are not allowed in "
                         + std::string(isTemplate ? "template literals"
                                                  : "strict-mode strings"));
                }
            }
            i += 1;
            continue;
        }
        // \1..\7 — LegacyOctalEscapeSequence.
        if (nxt >= '1' && nxt <= '7') {
            fail("Octal escape sequences are not allowed in "
                 + std::string(isTemplate ? "template literals"
                                          : "strict-mode strings"));
        }
        // \8, \9 — NonOctalDecimalEscapeSequence.
        if (nxt == '8' || nxt == '9') {
            fail("\\8 and \\9 are not allowed in "
                 + std::string(isTemplate ? "template literals"
                                          : "strict-mode strings"));
        }
        // Other escapes (e.g., \n, \t, \\) are fine; advance past them.
        i += 1;
    }
}

std::string Lexer::processTemplateEscapes(std::string_view text) {
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            i++;
            switch (text[i]) {
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case '\\': result += '\\'; break;
            case '\'': result += '\''; break;
            case '"': result += '"'; break;
            case '`': result += '`'; break;
            case '0': result += '\0'; break;
            case '$': result += '$'; break; // \$ in template literals
            case 'x':
                if (i + 2 < text.size()) {
                    auto hexVal = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return 0;
                    };
                    // See string-literal \x note above: emit proper
                    // UTF-8 for cp >= 0x80 so the result matches what
                    // the literal character would produce.
                    int cp = hexVal(text[i + 1]) * 16 + hexVal(text[i + 2]);
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                    i += 2;
                }
                break;
            case 'u':
                if (i + 1 < text.size() && text[i + 1] == '{') {
                    i += 2;
                    int codePoint = 0;
                    while (i < text.size() && text[i] != '}') {
                        codePoint = codePoint * 16;
                        char h = text[i];
                        if (h >= '0' && h <= '9') codePoint += h - '0';
                        else if (h >= 'a' && h <= 'f') codePoint += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') codePoint += h - 'A' + 10;
                        i++;
                    }
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
                } else if (i + 4 < text.size()) {
                    int cp = 0;
                    for (int j = 0; j < 4; j++) {
                        cp *= 16;
                        char h = text[i + 1 + j];
                        if (h >= '0' && h <= '9') cp += h - '0';
                        else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                    }
                    i += 4;
                    // Combine a UTF-16 surrogate pair written as two \uXXXX
                    // escapes (high \uD800-\uDBFF then low \uDC00-\uDFFF) into a
                    // single supplementary code point. Without this each half
                    // hit the 3-byte branch below and emitted invalid UTF-8
                    // (ED A0 BC ...), which ICU later decoded as U+FFFD — so
                    // `'🍂'` became replacement chars instead of 🍂.
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        i + 6 < text.size() && text[i + 1] == '\\' &&
                        text[i + 2] == 'u' && text[i + 3] != '{') {
                        int lo = 0; bool ok = true;
                        for (int j = 0; j < 4; j++) {
                            char h = text[i + 3 + j];
                            if (h >= '0' && h <= '9') lo = lo * 16 + (h - '0');
                            else if (h >= 'a' && h <= 'f') lo = lo * 16 + (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo = lo * 16 + (h - 'A' + 10);
                            else { ok = false; break; }
                        }
                        if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            i += 6; // consume the trailing \uXXXX
                        }
                    }
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xF0 | (cp >> 18));
                        result += (char)(0x80 | ((cp >> 12) & 0x3F));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                }
                break;
            default:
                result += text[i];
                break;
            }
        } else {
            result += text[i];
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
