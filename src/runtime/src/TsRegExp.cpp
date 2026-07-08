#include "TsRegExp.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include "TsConsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsRuntime.h"
#include <unicode/unistr.h>
#include <unicode/regex.h>
#include <unicode/uniset.h>
#include <unicode/usetiter.h>
#include <regex>

extern "C" void* ts_alloc(size_t size);
#include "TsObject.h"
#include "TsString.h"
#include "TsTyped.h"
#include "TsGC.h"
// TsString tag is enrolled in TsString.h.

// JS exception machinery (defined elsewhere in the runtime). Declared at file
// scope per runtime-safety rules (block-scope extern "C" is illegal).
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

// ECMA-262 22.2.3.4 (RegExpInitialize) step 1: validate the flags string.
// Each code unit must be one of d g i m s u v y, with NO duplicates; otherwise
// throw a SyntaxError. Returns true if flags are valid (or null).
static bool validateRegExpFlags(const char* flags) {
    if (!flags) return true;
    unsigned int seen = 0;  // bitmask over the 8 valid flag letters
    for (const char* p = flags; *p; ++p) {
        int bit;
        switch (*p) {
            case 'd': bit = 0; break;
            case 'g': bit = 1; break;
            case 'i': bit = 2; break;
            case 'm': bit = 3; break;
            case 's': bit = 4; break;
            case 'u': bit = 5; break;
            case 'v': bit = 6; break;
            case 'y': bit = 7; break;
            default:
                return false;  // invalid flag character
        }
        unsigned int mask = 1u << bit;
        if (seen & mask) return false;  // duplicate flag
        seen |= mask;
    }
    // ES 22.2.3.1: `u` and `v` are mutually exclusive.
    if ((seen & (1u << 5)) && (seen & (1u << 6))) return false;
    return true;
}

// TsRegExpMatchArray implementation
TsRegExpMatchArray* TsRegExpMatchArray::Create(TsArray* source, int64_t matchIndex, TsString* input) {
    void* mem = ts_gc_alloc_old_gen(sizeof(TsRegExpMatchArray));
    return new(mem) TsRegExpMatchArray(source, matchIndex, input);
}

TsRegExpMatchArray::TsRegExpMatchArray(TsArray* source, int64_t matchIndex, TsString* input)
    : matchIndex(matchIndex), input(input) {
    // Copy the array's data pointers so that inline codegen struct access works
    if (source) {
        elements = source->GetElementsPtr();
        length = source->Length();
        // We don't have direct access to capacity, but we can set it equal to length
        capacity = length;
    }
}

void* TsRegExpMatchArray::Get(size_t idx) const {
    if (idx >= length) return nullptr;
    return (void*)((int64_t*)elements)[idx];
}

TsRegExp* TsRegExp::Create(const char* pattern, const char* flags) {
    // Allocate in old-gen: TsRegExp objects are often stored in long-lived
    // module-level arrays (e.g. semver's re[] array) and contain complex
    // C++ objects (ICU RegexMatcher, std::vector). Nursery promotion would
    // require forwarding pointer updates in all referencing arrays.
    // ECMA-262 22.2.3.4: validate flags (allowed set, no duplicates) BEFORE
    // constructing, so an invalid/duplicate flag surfaces as a JS SyntaxError
    // rather than silently building a regex that ignores the bad flag.
    if (!validateRegExpFlags(flags)) {
        // Do NOT ts_throw from THIS frame: TsRegExp holds a
        // std::vector<pair<string,int>> (named-group state) and longjmp-based
        // ts_throw unwinding out of a std-container frame corrupts the MSVC
        // unwinder (GS failure). Callers pre-validate and throw the SyntaxError
        // from a std-container-free frame. See longjmp-stdstring-frame-crash.
        return nullptr;
    }
    void* mem = ts_gc_alloc_old_gen(sizeof(TsRegExp));
    // NOTE: a pattern-compile failure is intentionally NOT turned into a JS
    // SyntaxError here. ICU 74 rejects some patterns that are VALID JS regexes
    // (annexB legacy escapes, newer-Unicode \p{Script=...}), so a matcher==null
    // throw regressed ~24 RegExp/property-escapes + annexB tests. Flag
    // validation above is exact and kept; pattern-level SyntaxError would need a
    // real JS-grammar check (not "ICU couldn't compile it") — deferred.
    return new(mem) TsRegExp(pattern, flags);
}

// Parse pattern to extract named capture groups (?<name>...)
// Returns a vector of (name, groupNumber) pairs
void TsRegExp::parseNamedGroups() {
    std::string patternUtf8;
    patternStr.toUTF8String(patternUtf8);

    // Track group numbers - named and unnamed groups are numbered in order of opening paren
    int32_t groupNumber = 0;
    size_t i = 0;

    while (i < patternUtf8.size()) {
        // Skip escaped characters
        if (patternUtf8[i] == '\\' && i + 1 < patternUtf8.size()) {
            i += 2;
            continue;
        }

        // Skip character classes
        if (patternUtf8[i] == '[') {
            i++;
            while (i < patternUtf8.size()) {
                if (patternUtf8[i] == '\\' && i + 1 < patternUtf8.size()) {
                    i += 2;
                    continue;
                }
                if (patternUtf8[i] == ']') {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        // Check for groups
        if (patternUtf8[i] == '(' && i + 1 < patternUtf8.size()) {
            if (patternUtf8[i + 1] == '?') {
                // Non-capturing group or special construct
                if (i + 2 < patternUtf8.size() && patternUtf8[i + 2] == '<') {
                    // Could be named group (?<name>...) or lookbehind (?<!...) or (?<=...)
                    if (i + 3 < patternUtf8.size() && patternUtf8[i + 3] != '=' && patternUtf8[i + 3] != '!') {
                        // Named group: extract name
                        groupNumber++;
                        size_t nameStart = i + 3;
                        size_t nameEnd = patternUtf8.find('>', nameStart);
                        if (nameEnd != std::string::npos) {
                            std::string name = patternUtf8.substr(nameStart, nameEnd - nameStart);
                            namedGroups.push_back({name, groupNumber});
                            i = nameEnd + 1;
                            continue;
                        }
                    }
                }
                // Other non-capturing constructs: (?:...), (?=...), (?!...), (?<=...), (?<!...)
                // These don't create capturing groups
                i += 2;
                continue;
            } else {
                // Regular capturing group
                groupNumber++;
            }
        }
        i++;
    }
}

// ---------------------------------------------------------------------------
// JS surrogate-half / \u{...} escape normalization for ICU.
//
// ICU's regex engine matches on CODE POINTS: a valid surrogate pair in the
// subject is one supplementary code point, so JS patterns written with UTF-16
// surrogate-half escapes (V8 legacy semantics) can never match. ICU also does
// not understand JS's ES6 \u{...} extended escape. lodash's reUnicode family
// is built entirely from surrogate-half constructs:
//   \ud83c[\udde6-\uddff]              (regional indicator high + low range)
//   [\ud800-\udbff][\udc00-\udfff]     (any surrogate pair)
//   [...\ud800-\udfff...]              (reHasUnicode "has a surrogate" detector)
// This pass rewrites exactly those constructs (plus \u{...}) into ICU's
// \x{...} code-point escape, matching V8 semantics. Everything else is copied
// through byte-for-byte so existing patterns are unaffected.
// See memory/icu-regex-codepoint-vs-codeunit.md.

static inline int reHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse a \uHHHH or \u{H..H} escape at s[i] (s[i] must be '\\'). On success
// returns the code point (>= 0) and sets *len to total chars consumed
// (including the backslash); returns -1 if s[i..] is not a \u escape.
static long reParseUnicodeEscape(const std::string& s, size_t i, size_t* len) {
    if (i + 1 >= s.size() || s[i] != '\\' || s[i + 1] != 'u') return -1;
    size_t j = i + 2;
    if (j < s.size() && s[j] == '{') {
        long v = 0; int n = 0; size_t k = j + 1;
        for (; k < s.size() && s[k] != '}'; ++k) {
            int d = reHexDigit(s[k]); if (d < 0) return -1;
            v = v * 16 + d; if (v > 0x10FFFF) return -1; ++n;
        }
        if (n == 0 || k >= s.size() || s[k] != '}') return -1;
        *len = (k + 1) - i; return v;
    }
    if (j + 4 > s.size()) return -1;
    long v = 0;
    for (int t = 0; t < 4; ++t) { int d = reHexDigit(s[j + t]); if (d < 0) return -1; v = v * 16 + d; }
    *len = (j + 4) - i; return v;
}

static inline bool reIsHighSurr(long v) { return v >= 0xD800 && v <= 0xDBFF; }
static inline bool reIsLowSurr(long v)  { return v >= 0xDC00 && v <= 0xDFFF; }
static inline long reCombine(long hi, long lo) {
    return 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
}
static void reAppendCp(std::string& out, long cp) {
    char buf[16]; snprintf(buf, sizeof(buf), "\\x{%lX}", cp); out += buf;
}

// Parse a low-surrogate range class "[\uLO1-\uLO2]" (or single "[\uLO]") at
// s[i]. On success returns true with lo1/lo2 and *len = chars consumed.
static bool reParseLowSurrClass(const std::string& s, size_t i, long* lo1, long* lo2, size_t* len) {
    if (i >= s.size() || s[i] != '[') return false;
    size_t p = i + 1, l1;
    long a = reParseUnicodeEscape(s, p, &l1);
    if (!reIsLowSurr(a)) return false;
    p += l1; long b = a;
    if (p < s.size() && s[p] == '-') {
        size_t l2; long c = reParseUnicodeEscape(s, p + 1, &l2);
        if (!reIsLowSurr(c)) return false;
        b = c; p += 1 + l2;
    }
    if (p >= s.size() || s[p] != ']') return false;
    *lo1 = a; *lo2 = b; *len = (p + 1) - i; return true;
}

// At s[i]=='[', try to match [\uHI1-\uHI2][\uLO1-\uLO2] (any surrogate pair) and
// emit the code-point range class. Returns chars consumed, or 0 if no match.
static size_t reTrySurrPairClass(const std::string& s, size_t i, std::string& out) {
    size_t hp = i + 1, hl1;
    long h1 = reParseUnicodeEscape(s, hp, &hl1);
    if (!reIsHighSurr(h1)) return 0;
    size_t q = hp + hl1; long h2 = h1;
    if (q < s.size() && s[q] == '-') {
        size_t hl2; long hh = reParseUnicodeEscape(s, q + 1, &hl2);
        if (!reIsHighSurr(hh)) return 0;
        h2 = hh; q += 1 + hl2;
    }
    if (q >= s.size() || s[q] != ']') return 0;
    long lo1, lo2; size_t lowLen;
    if (!reParseLowSurrClass(s, q + 1, &lo1, &lo2, &lowLen)) return 0;
    out += '['; reAppendCp(out, reCombine(h1, lo1)); out += '-';
    reAppendCp(out, reCombine(h2, lo2)); out += ']';
    return (q + 1 + lowLen) - i;
}

// Handle one \u escape (value v, length len) at s[i]. Returns chars consumed.
static size_t reEmitUnicodeEscape(const std::string& s, size_t i, size_t len,
                                  long v, bool inClass, std::string& out) {
    bool extended = (i + 2 < s.size() && s[i + 2] == '{');
    if (extended) { reAppendCp(out, v); return len; }      // \u{..} -> \x{..}
    if (reIsHighSurr(v)) {
        // \uHI\uLO  -> single supplementary code point
        size_t l2; long lo = reParseUnicodeEscape(s, i + len, &l2);
        if (reIsLowSurr(lo)) { reAppendCp(out, reCombine(v, lo)); return len + l2; }
        // \uHI[\uLO1-\uLO2]  -> code-point range class
        long lo1, lo2; size_t lowLen;
        if (reParseLowSurrClass(s, i + len, &lo1, &lo2, &lowLen)) {
            out += '['; reAppendCp(out, reCombine(v, lo1)); out += '-';
            reAppendCp(out, reCombine(v, lo2)); out += ']';
            return len + lowLen;
        }
        // inside a class, "\uHI-\uLO" spans the whole surrogate block: that is
        // the reHasUnicode "has a surrogate code unit" detector — match lone
        // surrogates AND every supplementary code point.
        if (inClass && i + len < s.size() && s[i + len] == '-') {
            size_t l3; long hi2 = reParseUnicodeEscape(s, i + len + 1, &l3);
            if (reIsLowSurr(hi2)) {
                reAppendCp(out, v); out += '-'; reAppendCp(out, hi2);
                out += "\\x{10000}-\\x{10FFFF}";
                return len + 1 + l3;
            }
        }
        // lone high surrogate (or high-high range): pass through unchanged.
        out.append(s, i, len); return len;
    }
    // BMP escape or lone low surrogate: leave verbatim (ICU handles \uHHHH).
    out.append(s, i, len); return len;
}

static std::string rewriteUnicodeForIcu(const std::string& pat) {
    std::string out; out.reserve(pat.size() + 16);
    bool inClass = false;
    for (size_t i = 0; i < pat.size();) {
        char c = pat[i];
        if (c == '[' && !inClass) {
            size_t consumed = reTrySurrPairClass(pat, i, out);
            if (consumed) { i += consumed; continue; }
            inClass = true; out += c; i++; continue;
        }
        if (c == ']' && inClass) { inClass = false; out += c; i++; continue; }
        if (c == '\\') {
            if (i + 1 < pat.size() && pat[i + 1] == '\\') { out += "\\\\"; i += 2; continue; }
            size_t len; long v = reParseUnicodeEscape(pat, i, &len);
            if (v >= 0) { i += reEmitUnicodeEscape(pat, i, len, v, inClass, out); continue; }
            out += pat[i]; if (i + 1 < pat.size()) out += pat[i + 1]; i += 2; continue;
        }
        out += c; i++;
    }
    return out;
}

// Transform a JavaScript regex pattern into an ICU-compatible pattern.
// JS allows literal '[' inside character classes; ICU treats '[' inside a class
// as a nested set operation. Escape unescaped '[' inside character classes.
// /v strings-in-sets support (ES2024 unicodeSets): a class may contain
// multi-code-point STRINGS — via \q{a|bc|...} literals or properties of
// strings like \p{RGI_Emoji}. ICU's RegexMatcher character classes match
// single code points only, but icu::UnicodeSet holds string elements and
// knows the properties-of-strings. Each top-level class is evaluated here
// as ES set algebra (union / `--` difference / `&&` intersection) over
// icu::UnicodeSet values, then re-emitted either as a plain code-point
// class or, when strings are present, as a non-capturing alternation:
//   [\q{ab|c}\p{X}]  ->  (?:ab|c|[<remaining single-char set>])
// Strings sort longest-first so the alternation prefers the longest match.
// A NEGATED class containing strings is an ES early error (SyntaxError).
static void vmode_escape_regex_literal(const icu::UnicodeString& s,
                                       icu::UnicodeString& out) {
    for (int32_t i = 0; i < s.length(); ) {
        UChar32 c = s.char32At(i);
        if (c == u'\\' || c == u'(' || c == u')' || c == u'[' || c == u']' ||
            c == u'{' || c == u'}' || c == u'.' || c == u'*' || c == u'+' ||
            c == u'?' || c == u'^' || c == u'$' || c == u'|' || c == u'/')
            out.append(u'\\');
        out.append(c);
        i += U16_LENGTH(c);
    }
}

// Translate ES \q{a|b|...} string literals into ICU UnicodeSet {a}{b} string
// elements (in place, within one class body).
static std::string vmode_translate_q(const std::string& cls, bool* bad) {
    std::string out;
    for (size_t i = 0; i < cls.size(); i++) {
        if (cls[i] == '\\' && i + 2 < cls.size() && cls[i + 1] == 'q' &&
            cls[i + 2] == '{') {
            size_t j = i + 3;
            std::string cur;
            std::string elems;
            int depth = 1;
            for (; j < cls.size(); j++) {
                char ch = cls[j];
                if (ch == '\\' && j + 1 < cls.size()) { cur += ch; cur += cls[++j]; continue; }
                if (ch == '{') depth++;
                if (ch == '}') { depth--; if (depth == 0) break; }
                if (ch == '|' && depth == 1) { elems += "{" + cur + "}"; cur.clear(); continue; }
                cur += ch;
            }
            if (j >= cls.size()) { *bad = true; return cls; }
            elems += "{" + cur + "}";
            out += elems;
            i = j;  // skip past }
            continue;
        }
        out += cls[i];
    }
    return out;
}

// UnicodeSet does not know regex shorthand escapes (\d is a literal 'd' to
// it) — expand them to explicit nested sets with ES semantics.
static void vmode_expand_shorthand(const std::string& in, std::string& out) {
    static const char* WS_SET =
        "[\\u0009-\\u000D\\u0020\\u00A0\\u1680\\u2000-\\u200A"
        "\\u2028\\u2029\\u202F\\u205F\\u3000\\uFEFF]";
    static const char* WS_SET_NEG =
        "[^\\u0009-\\u000D\\u0020\\u00A0\\u1680\\u2000-\\u200A"
        "\\u2028\\u2029\\u202F\\u205F\\u3000\\uFEFF]";
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char* rep = nullptr;
            switch (in[i + 1]) {
                case 'd': rep = "[0-9]"; break;
                case 'D': rep = "[^0-9]"; break;
                case 'w': rep = "[0-9A-Za-z_]"; break;
                case 'W': rep = "[^0-9A-Za-z_]"; break;
                case 's': rep = WS_SET; break;
                case 'S': rep = WS_SET_NEG; break;
                default: break;
            }
            if (rep) out += rep;
            else { out += in[i]; out += in[i + 1]; }
            i++;
            continue;
        }
        out += in[i];
    }
}

// Split a class body at depth-0 `--` (difference) or `&&` (intersection).
// ES v-mode forbids mixing the two in one class. op stays 0 for pure union.
static bool vmode_split_operands(const std::string& body, char& op,
                                 std::vector<std::string>& operands) {
    op = 0;
    std::string cur;
    int depth = 0;
    for (size_t i = 0; i < body.size(); i++) {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size()) { cur += c; cur += body[i + 1]; i++; continue; }
        if (c == '[' || c == '{') { depth++; cur += c; continue; }
        if (c == ']' || c == '}') { depth--; cur += c; continue; }
        if (depth == 0 && (c == '-' || c == '&') && i + 1 < body.size() &&
            body[i + 1] == c) {
            char newOp = (c == '-') ? '-' : '&';
            if (op && op != newOp) return false;  // mixed set operators
            op = newOp;
            operands.push_back(cur);
            cur.clear();
            i++;
            continue;
        }
        cur += c;
    }
    operands.push_back(cur);
    return true;
}

// Emit a computed UnicodeSet back into ICU regex syntax. No strings ->
// plain (possibly negated) code-point class; strings -> (?:s1|s2|[chars])
// alternation (longest string first). Negated-with-strings is an ES early
// error: emit an unterminated class so the ICU compile fails -> SyntaxError.
static bool vmode_emit_set(const icu::UnicodeSet& uset, bool negated,
                           std::string& out) {
    if (negated && uset.hasStrings()) { out = "["; return true; }
    std::vector<icu::UnicodeString> strings;
    icu::UnicodeSetIterator it(uset);
    while (it.next()) {
        if (it.isString()) strings.push_back(it.getString());
    }
    icu::UnicodeSet chars(uset);
    chars.removeAllStrings();
    if (negated) chars.complement();
    bool hasChars = !chars.isEmpty();
    if (!hasChars && strings.empty()) { out = "(?!)"; return true; }
    icu::UnicodeString ranges;
    if (hasChars) {
        for (int32_t r = 0; r < chars.getRangeCount(); r++) {
            UChar32 a = chars.getRangeStart(r), b = chars.getRangeEnd(r);
            char buf[32];
            snprintf(buf, sizeof(buf), "\\x{%X}", (unsigned)a);
            ranges += icu::UnicodeString::fromUTF8(buf);
            if (b != a) {
                snprintf(buf, sizeof(buf), "-\\x{%X}", (unsigned)b);
                ranges += icu::UnicodeString::fromUTF8(buf);
            }
        }
    }
    if (strings.empty()) {
        icu::UnicodeString cls;
        cls += (UChar)u'[';
        cls += ranges;
        cls += (UChar)u']';
        std::string clsU8;
        cls.toUTF8String(clsU8);
        out = clsU8;
        return true;
    }
    std::sort(strings.begin(), strings.end(),
              [](const icu::UnicodeString& a, const icu::UnicodeString& b) {
                  return a.length() > b.length();
              });
    icu::UnicodeString alt = icu::UnicodeString::fromUTF8("(?:");
    bool first = true;
    for (auto& s : strings) {
        if (!first) alt += (UChar)u'|';
        vmode_escape_regex_literal(s, alt);
        first = false;
    }
    if (hasChars) {
        if (!first) alt += (UChar)u'|';
        alt += (UChar)u'[';
        alt += ranges;
        alt += (UChar)u']';
    }
    alt += (UChar)u')';
    std::string altU8;
    alt.toUTF8String(altU8);
    out = altU8;
    return true;
}

// Evaluate one v-mode class body (WITHOUT the outer brackets) as ES set
// algebra and emit ICU syntax. Returns false when the class can't be
// evaluated (caller falls back to the untransformed pattern).
static bool vmode_rewrite_class(const std::string& body, bool negated,
                                std::string& out) {
    bool bad = false;
    std::string translated = vmode_translate_q(body, &bad);
    if (bad) return false;
    std::string expanded;
    vmode_expand_shorthand(translated, expanded);
    char op = 0;
    std::vector<std::string> operands;
    if (!vmode_split_operands(expanded, op, operands)) return false;
    icu::UnicodeSet uset;
    bool first = true;
    for (auto& o : operands) {
        UErrorCode st = U_ZERO_ERROR;
        icu::UnicodeString ps =
            icu::UnicodeString::fromUTF8(std::string("[") + o + "]");
        icu::UnicodeSet os(ps, st);
        if (U_FAILURE(st)) return false;
        if (first) { uset = os; first = false; }
        else if (op == '-') uset.removeAll(os);
        else uset.retainAll(os);
    }
    return vmode_emit_set(uset, negated, out);
}

// Scan a v-mode pattern, rewriting every TOP-LEVEL class through
// vmode_rewrite_class (nested classes are consumed via depth tracking)
// and every bare \p{...} that names a property of STRINGS (RGI_Emoji etc.)
// into the equivalent alternation. \p over plain code points stays as-is
// for ICU to handle natively.
static bool vmode_rewrite_pattern(const std::string& pat, std::string& out) {
    out.clear();
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            if (pat[i + 1] == 'p' && i + 2 < pat.size() && pat[i + 2] == '{') {
                size_t j = pat.find('}', i + 3);
                if (j != std::string::npos) {
                    std::string prop = pat.substr(i, j - i + 1);
                    UErrorCode st = U_ZERO_ERROR;
                    icu::UnicodeString ps =
                        icu::UnicodeString::fromUTF8("[" + prop + "]");
                    icu::UnicodeSet os(ps, st);
                    if (U_SUCCESS(st) && os.hasStrings()) {
                        std::string rewritten;
                        if (!vmode_emit_set(os, false, rewritten)) return false;
                        out += rewritten;
                        i = j;
                        continue;
                    }
                }
            }
            out += pat[i]; out += pat[i + 1]; i++;
            continue;
        }
        if (pat[i] != '[') { out += pat[i]; continue; }
        size_t j = i + 1;
        bool negated = false;
        if (j < pat.size() && pat[j] == '^') { negated = true; j++; }
        size_t bodyStart = j;
        int depth = 1;
        for (; j < pat.size(); j++) {
            if (pat[j] == '\\' && j + 1 < pat.size()) { j++; continue; }
            if (pat[j] == '[') depth++;
            else if (pat[j] == ']') { depth--; if (depth == 0) break; }
        }
        if (j >= pat.size()) return false;  // unterminated
        std::string body = pat.substr(bodyStart, j - bodyStart);
        std::string rewritten;
        if (!vmode_rewrite_class(body, negated, rewritten)) return false;
        out += rewritten;
        i = j;
    }
    return true;
}

// ECMA-262 22.2.2.9 CompileToCharSet: JS \d/\w are ASCII-ONLY and \s is the
// WhiteSpace + LineTerminator set, in EVERY mode. ICU's shorthands are
// Unicode-wide (\d = Nd so /\d/ matched Arabic-Indic digits; \w = Unicode
// word; \s lacks U+FEFF). Rewrite the shorthands into explicit classes
// AFTER transformJsPatternForIcu (so the emitted \x{...} escapes are not
// re-processed). Inside [...] the set body is spliced; negated forms nest a
// class, which ICU set syntax supports ([abc[^...]]).
static const char* kJsDigitBody = "0-9";
static const char* kJsWordBody  = "A-Za-z0-9_";
static const char* kJsWsBody =
    // Pure \x{...} escapes: valid in BOTH ICU regex classes and the
    // v-mode UnicodeSet rewriter (a literal space or \t was lost by the
    // UnicodeSet pattern parser, breaking /\s/v).
    "\\x{9}\\x{B}\\x{C}\\x{20}\\x{A0}\\x{FEFF}\\x{1680}"
    "\\x{2000}-\\x{200A}\\x{202F}\\x{205F}\\x{3000}"
    "\\x{A}\\x{D}\\x{2028}\\x{2029}";
static std::string rewriteJsShorthandClasses(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 32);
    bool inClass = false;
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '\\' && i + 1 < in.size()) {
            char n = in[i + 1];
            const char* body = nullptr;
            bool neg = false;
            switch (n) {
                case 'd': body = kJsDigitBody; break;
                case 'D': body = kJsDigitBody; neg = true; break;
                case 'w': body = kJsWordBody; break;
                case 'W': body = kJsWordBody; neg = true; break;
                case 's': body = kJsWsBody; break;
                case 'S': body = kJsWsBody; neg = true; break;
                default: out += c; out += n; i++; continue;
            }
            if (!inClass) {
                out += neg ? "[^" : "[";
                out += body;
                out += ']';
            } else if (neg) {
                out += "[^";
                out += body;
                out += ']';
            } else {
                out += body;
            }
            i++;
            continue;
        }
        if (c == '[' && !inClass) { inClass = true; out += c; continue; }
        if (c == ']' && inClass)  { inClass = false; out += c; continue; }
        out += c;
    }
    return out;
}

static std::string transformJsPatternForIcu(const std::string& pat,
                                             bool vMode = false) {
    std::string result;
    result.reserve(pat.size() + 8);
    bool inClass = false;
    for (size_t i = 0; i < pat.size(); i++) {
        // Escaped character - pass through as-is
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            // JS defines \b INSIDE a character class as U+0008 (backspace);
            // ICU keeps it a word-boundary there, so [\b] / [^\b] matched
            // nothing sensible. Rewrite to an explicit \x08.
            if (inClass && pat[i + 1] == 'b') {
                result += "\\x08";
                i++;
                continue;
            }
            result += pat[i];
            result += pat[i + 1];
            i++;
            continue;
        }
        if (!inClass && pat[i] == '[') {
            inClass = true;
            result += pat[i];
            // Handle negation [^
            if (i + 1 < pat.size() && pat[i + 1] == '^') {
                result += pat[++i];
            }
            // Handle ] as first char in class (literal ])
            if (i + 1 < pat.size() && pat[i + 1] == ']') {
                result += pat[++i];
            }
            continue;
        }
        if (inClass && pat[i] == ']') {
            inClass = false;
            result += pat[i];
            continue;
        }
        if (inClass && pat[i] == '[') {
            if (vMode) {
                // /v (unicodeSets): nested classes are STRUCTURAL and ICU's
                // UnicodeSet syntax supports them (plus the -- difference and
                // && intersection operators) natively — pass through.
                result += pat[i];
                continue;
            }
            // Escape [ inside character class for ICU compatibility
            result += "\\[";
            continue;
        }
        result += pat[i];
    }
    return result;
}

TsRegExp::TsRegExp(const char* pattern, const char* flags) {
    Recompile(pattern, flags);
}

void TsRegExp::Recompile(const char* pattern, const char* flags) {
    UErrorCode status = U_ZERO_ERROR;
    if (!pattern) pattern = "";

    // RegExp.prototype.compile reuses this on an existing object: drop the old
    // matcher and reset all flag-derived state before rebuilding.
    delete matcher; matcher = nullptr;
    ignoreCase = false; multiline = false; global = false; sticky = false; hasIndices = false;
    lastIndex = 0;

    // Store original pattern for GetSource() and parseNamedGroups()
    patternStr = icu::UnicodeString::fromUTF8(pattern);
    flagsStr = flags ? flags : "";

    // Transform JS regex pattern for ICU compatibility: first normalize JS
    // surrogate-half / \u{...} escapes into ICU \x{...} code-point escapes
    // (ICU matches on code points), then escape [ inside char classes.
    bool vMode = flags && std::string(flags).find('v') != std::string::npos;
    std::string transformed =
        transformJsPatternForIcu(rewriteUnicodeForIcu(pattern), vMode);
    // JS ASCII/whitespace shorthand semantics (\d \w \s and negations) —
    // see rewriteJsShorthandClasses.
    transformed = rewriteJsShorthandClasses(transformed);
    if (vMode) {
        std::string rewritten;
        if (vmode_rewrite_pattern(transformed, rewritten)) {
            transformed = rewritten;
        }
    }
    icu::UnicodeString icuPatternStr = icu::UnicodeString::fromUTF8(transformed);

    uint32_t icuFlags = 0;
    if (flags) {
        std::string f(flags);
        if (f.find('i') != std::string::npos) {
            icuFlags |= UREGEX_CASE_INSENSITIVE;
            ignoreCase = true;
        }
        if (f.find('m') != std::string::npos) {
            icuFlags |= UREGEX_MULTILINE;
            multiline = true;
        }
        if (f.find('s') != std::string::npos) icuFlags |= UREGEX_DOTALL;
        if (f.find('u') != std::string::npos) icuFlags |= UREGEX_UWORD;
        if (f.find('x') != std::string::npos) icuFlags |= UREGEX_COMMENTS;

        if (f.find('g') != std::string::npos) global = true;
        if (f.find('y') != std::string::npos) sticky = true;
        if (f.find('d') != std::string::npos) hasIndices = true;
    }

    if (vMode) icuFlags |= UREGEX_UWORD;   // v implies unicode semantics

    matcher = new icu::RegexMatcher(icuPatternStr, icuFlags, status);
    if (U_FAILURE(status)) {
        delete matcher;
        matcher = nullptr;
    }

    // Parse pattern to extract named capture groups
    parseNamedGroups();
}

TsRegExp::~TsRegExp() {
    delete matcher;
    delete subjectStr;
}

TsString* TsRegExp::GetSource() const {
    std::string utf8;
    patternStr.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsRegExp::GetFlags() const {
    return TsString::Create(flagsStr.c_str());
}

bool TsRegExp::Test(TsString* str) {
    if (!matcher) return false;

    UErrorCode status = U_ZERO_ERROR;
    // Assign into stable heap storage: matcher->reset() KEEPS A REFERENCE to
    // the subject (see subjectStr declaration) — a local is a use-after-free.
    if (!subjectStr) subjectStr = new icu::UnicodeString();
    *subjectStr = str->ToUnicodeString();
    const icu::UnicodeString& input = *subjectStr;
    matcher->reset(input);

    if (global || sticky) {
        matcher->region(lastIndex, input.length(), status);
    }

    bool found = matcher->find();
    
    if (global || sticky) {
        if (found) {
            lastIndex = matcher->end(status);
        } else {
            lastIndex = 0;
        }
    }
    
    return found;
}

void* TsRegExp::Exec(TsString* str) {
    if (!matcher) return nullptr;

    UErrorCode status = U_ZERO_ERROR;
    // Stable heap storage — matcher->reset() keeps a reference (see Test).
    if (!subjectStr) subjectStr = new icu::UnicodeString();
    *subjectStr = str->ToUnicodeString();
    const icu::UnicodeString& input = *subjectStr;
    matcher->reset(input);

    if (global || sticky) {
        matcher->region(lastIndex, input.length(), status);
    }

    if (matcher->find()) {
        if (sticky && matcher->start(status) != lastIndex) {
            lastIndex = 0;
            return nullptr;
        }

        TsArray* matches = TsArray::Create();
        int32_t count = matcher->groupCount();
        int64_t matchIndex = matcher->start(status);  // Index of full match

        // Build array of match strings
        for (int32_t i = 0; i <= count; ++i) {
            icu::UnicodeString group = matcher->group(i, status);
            int32_t groupStart = matcher->start(i, status);
            if (groupStart == -1) {
                matches->Push((int64_t)ts_value_make_undefined());
            } else {
                std::string utf8;
                group.toUTF8String(utf8);
                matches->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
            }
        }

        // Create the match array wrapper with index and input
        TsRegExpMatchArray* result = TsRegExpMatchArray::Create(matches, matchIndex, str);

        // If d flag (hasIndices) is set, build the indices array
        if (hasIndices) {
            TsArray* indices = TsArray::Create();

            for (int32_t i = 0; i <= count; ++i) {
                int32_t start = matcher->start(i, status);
                int32_t end = matcher->end(i, status);

                if (start == -1) {
                    // Group did not participate in match
                    indices->Push((int64_t)ts_value_make_undefined());
                } else {
                    // Create [start, end] pair as a 2-element array
                    TsArray* pair = TsArray::Create(2);
                    pair->Push((int64_t)ts_value_make_int(start));
                    pair->Push((int64_t)ts_value_make_int(end));
                    indices->Push((int64_t)ts_value_make_object(pair));
                }
            }

            result->SetIndices(indices);
        }

        // Build groups object if pattern has named capture groups
        if (!namedGroups.empty()) {
            TsMap* groups = TsMap::Create();

            for (const auto& [name, groupNum] : namedGroups) {
                TsString* nameStr = TsString::Create(name.c_str());
                int32_t groupStart = matcher->start(groupNum, status);

                // Create key as proper TsValue with STRING_PTR type
                TsValue keyVal;
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = nameStr;

                if (groupStart == -1) {
                    // Group did not participate in match
                    TsValue undefinedVal;
                    undefinedVal.type = ValueType::UNDEFINED;
                    undefinedVal.ptr_val = nullptr;
                    groups->Set(keyVal, undefinedVal);
                } else {
                    icu::UnicodeString groupValue = matcher->group(groupNum, status);
                    std::string utf8;
                    groupValue.toUTF8String(utf8);

                    TsValue stringVal;
                    stringVal.type = ValueType::STRING_PTR;
                    stringVal.ptr_val = TsString::Create(utf8.c_str());
                    groups->Set(keyVal, stringVal);
                }
            }

            result->SetGroups(groups);
        }

        if (global || sticky) {
            lastIndex = matcher->end(status);
        }

        return result;
    }

    if (global || sticky) {
        lastIndex = 0;
    }

    return nullptr;
}

// ECMA-262 pattern early errors for the RUNTIME `new RegExp(pattern, flags)`
// path: the compile-time literal recognizer (RegExpEarlyErrors.cpp, linked
// into the runtime for this) never sees runtime-built patterns, so
// `new RegExp("a**")` silently produced a never-matching matcher instead of
// SyntaxError. The validator throws std::runtime_error; catch it HERE and
// copy the message to a POD buffer — the caller ts_throws from its clean
// frame (C++ unwind state must be fully settled before any longjmp).
namespace tsaot {
void validateRegExpLiteral(const std::string& body, const std::string& flags,
                           int line, int col);
}
static bool validateRegExpPatternRuntime(const char* pattern, const char* flags,
                                         char* msgBuf, size_t msgLen) {
    try {
        tsaot::validateRegExpLiteral(std::string(pattern ? pattern : ""),
                                     std::string(flags ? flags : ""), 0, 0);
        return true;
    } catch (const std::exception& e) {
        const char* w = e.what();
        // Strip the "0:0: SyntaxError: " prefix the validator formats for
        // compile-time diagnostics.
        const char* m = strstr(w, "SyntaxError: ");
        snprintf(msgBuf, msgLen, "%s", m ? m + 13 : w);
        return false;
    } catch (...) {
        snprintf(msgBuf, msgLen, "invalid regular expression");
        return false;
    }
}

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags) {
        // pattern/flags may be NaN-boxed TsValue* from slow path
        TsString* p = nullptr;
        if (pattern) {
            void* rawP = ts_value_get_string((TsValue*)pattern);
            p = rawP ? (TsString*)rawP : (TsString*)pattern;
            // Validate it's actually a TsString
            if (p && !ts_is<TsString>(p)) {
                void* rawObj = ts_value_get_object((TsValue*)pattern);
                p = ts_cast<TsString>(rawObj) ? (TsString*)rawObj : (TsString*)pattern;
            }
        }
        if (!p) return nullptr;

        // flags could be undefined (NaN-boxed) from `new RegExp(pat, undefined)`
        const char* flagsStr = "";
        if (flags && !ts_value_is_undefined((TsValue*)flags) && !ts_value_is_null((TsValue*)flags)) {
            void* rawF = ts_value_get_string((TsValue*)flags);
            TsString* f = rawF ? (TsString*)rawF : (TsString*)flags;
            if (ts_is<TsString>(f)) {
                flagsStr = f->ToUtf8();
            }
        }
        // Validate flags HERE, in this std::string-free frame, BEFORE entering
        // TsRegExp::Create (whose frame holds named-group std::vector state that the
        // longjmp ts_throw cannot safely unwind through). See the note in
        // TsRegExp::Create and the longjmp-stdstring-frame-crash memory.
        if (!validateRegExpFlags(flagsStr)) {
            ts_throw((TsValue*)ts_error_create_typed(
                "SyntaxError", "Invalid flags supplied to RegExp constructor"));
            return nullptr;
        }
        // Pattern early errors (same recognizer as regex literals). The
        // helper fully unwinds its C++ exception before we longjmp; this
        // frame holds only POD locals.
        {
            char msg[256];
            if (!validateRegExpPatternRuntime(p->ToUtf8(), flagsStr, msg,
                                              sizeof(msg))) {
                ts_throw((TsValue*)ts_error_create_typed("SyntaxError", msg));
                return nullptr;
            }
        }
        return TsRegExp::Create(p->ToUtf8(), flagsStr);
    }

    void* ts_regexp_from_literal(void* literal) {
        TsString* s = (TsString*)literal;
        std::string text = s->ToUtf8();
        if (text.empty() || text[0] != '/') return nullptr;
        
        size_t lastSlash = text.find_last_of('/');
        if (lastSlash == 0 || lastSlash == std::string::npos) return nullptr;
        
        std::string pattern = text.substr(1, lastSlash - 1);
        std::string flags = text.substr(lastSlash + 1);
        
        return TsRegExp::Create(pattern.c_str(), flags.c_str());
    }

    int32_t RegExp_test(void* re, void* str) {
        if (!re) return 0;
        TsRegExp* r = (TsRegExp*)re;
        // Per JS spec, RegExp.test() converts its argument to string.
        // In the slow path, str may be any NaN-boxed value, not just a TsString*.
        uint64_t nb = (uint64_t)(uintptr_t)str;
        TsString* s;
        if (!str || nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
            s = (TsString*)ts_string_from_value((TsValue*)str);
        } else if (nanbox_is_ptr(nb)) {
            // Check if it's a TsString (magic 0x53545247) or needs conversion
            void* base = ts_gc_base(str);
            if (base && ts_is_any_string(base)) {
                s = ts_ensure_flat(base);
            } else {
                s = (TsString*)ts_string_from_value((TsValue*)str);
            }
        } else {
            // Number, bool, or other NaN-boxed value -- convert to string
            s = (TsString*)ts_string_from_value((TsValue*)str);
        }
        if (!s) s = TsString::Create("undefined");
        return r->Test(s) ? 1 : 0;
    }

    void* RegExp_exec(void* re, void* str) {
        if (!re) return nullptr;
        TsRegExp* r = (TsRegExp*)re;
        // Per JS spec, convert argument to string (may be NaN-boxed non-string)
        uint64_t nb = (uint64_t)(uintptr_t)str;
        TsString* s;
        if (!str || nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
            s = (TsString*)ts_string_from_value((TsValue*)str);
        } else if (nanbox_is_ptr(nb)) {
            void* base = ts_gc_base(str);
            if (base && ts_is_any_string(base)) {
                s = ts_ensure_flat(base);
            } else {
                s = (TsString*)ts_string_from_value((TsValue*)str);
            }
        } else {
            s = (TsString*)ts_string_from_value((TsValue*)str);
        }
        if (!s) return nullptr;
        return r->Exec(s);  // Return raw result array or null
    }

    int64_t RegExp_get_lastIndex(void* re) {
        return ((TsRegExp*)re)->GetLastIndex();
    }

    void RegExp_set_lastIndex(void* re, int64_t index) {
        ((TsRegExp*)re)->SetLastIndex(index);
    }

    void* RegExp_get_source(void* re) {
        return ((TsRegExp*)re)->GetSource();
    }

    void* RegExp_get_flags(void* re) {
        return ((TsRegExp*)re)->GetFlags();
    }

    int32_t RegExp_get_global(void* re) {
        return ((TsRegExp*)re)->IsGlobal() ? 1 : 0;
    }

    int32_t RegExp_get_sticky(void* re) {
        return ((TsRegExp*)re)->IsSticky() ? 1 : 0;
    }

    int32_t RegExp_get_ignoreCase(void* re) {
        return ((TsRegExp*)re)->IsIgnoreCase() ? 1 : 0;
    }

    int32_t RegExp_get_multiline(void* re) {
        return ((TsRegExp*)re)->IsMultiline() ? 1 : 0;
    }

    int32_t RegExp_get_hasIndices(void* re) {
        return ((TsRegExp*)re)->HasIndices() ? 1 : 0;
    }

    // Mangled-name aliases for `RegExp(pattern)` and `RegExp(pattern, flags)`
    // call expressions in untyped JS. The analyzer mangles to
    // `RegExp_any` / `RegExp_any_any`. Forward to ts_regexp_create which
    // accepts NaN-boxed pattern/flags args. Per ECMA-262 §22.2.3.1,
    // RegExp(pat) is equivalent to new RegExp(pat).
    void* RegExp_any(void* pattern) {
        return ts_value_make_object(ts_regexp_create(pattern, nullptr));
    }
    void* RegExp_any_any(void* pattern, void* flags) {
        return ts_value_make_object(ts_regexp_create(pattern, flags));
    }
}
