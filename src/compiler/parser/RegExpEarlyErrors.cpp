// ECMA-262 early errors for regular expression literals (R1a).
//
// Two layers:
//   1. Flag validation (22.2.1.1): only dgimsuvy, no duplicates, u/v exclusive.
//   2. Pattern validation: probe-compile the pattern with ICU using the SAME
//      JS->ICU translation the runtime applies in TsRegExp's constructor.
//      Because the runtime feeds every regex literal through that exact
//      translation + icu::RegexMatcher at evaluation time (and a failed
//      compile yields a silently-null matcher there), any pattern that works
//      at runtime today compiles here too — the probe can only newly reject
//      patterns that were already broken at runtime.
//
// The translation helpers below are copied VERBATIM from
// src/runtime/src/TsRegExp.cpp (reHexDigit .. transformJsPatternForIcu).
// If the runtime translation changes, mirror it here, or the probe drifts.

#include "RegExpEarlyErrors.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <unicode/regex.h>
#include <unicode/unistr.h>
#include <unicode/uregex.h>

namespace tsaot {

namespace {

[[noreturn]] void failSyntax(int line, int col, const std::string& msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%d:%d: SyntaxError: %s", line, col,
             msg.c_str());
    throw std::runtime_error(buf);
}

// ---- begin verbatim copy from src/runtime/src/TsRegExp.cpp ----

static int reHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

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

static size_t reEmitUnicodeEscape(const std::string& s, size_t i, size_t len,
                                  long v, bool inClass, std::string& out) {
    bool extended = (i + 2 < s.size() && s[i + 2] == '{');
    if (extended) { reAppendCp(out, v); return len; }      // \u{..} -> \x{..}
    if (reIsHighSurr(v)) {
        size_t l2; long lo = reParseUnicodeEscape(s, i + len, &l2);
        if (reIsLowSurr(lo)) { reAppendCp(out, reCombine(v, lo)); return len + l2; }
        long lo1, lo2; size_t lowLen;
        if (reParseLowSurrClass(s, i + len, &lo1, &lo2, &lowLen)) {
            out += '['; reAppendCp(out, reCombine(v, lo1)); out += '-';
            reAppendCp(out, reCombine(v, lo2)); out += ']';
            return len + lowLen;
        }
        if (inClass && i + len < s.size() && s[i + len] == '-') {
            size_t l3; long hi2 = reParseUnicodeEscape(s, i + len + 1, &l3);
            if (reIsLowSurr(hi2)) {
                reAppendCp(out, v); out += '-'; reAppendCp(out, hi2);
                out += "\\x{10000}-\\x{10FFFF}";
                return len + 1 + l3;
            }
        }
        out.append(s, i, len); return len;
    }
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

static std::string transformJsPatternForIcu(const std::string& pat) {
    std::string result;
    result.reserve(pat.size() + 8);
    bool inClass = false;
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) {
            // JS: \b inside a class is U+0008, not a word boundary.
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
            if (i + 1 < pat.size() && pat[i + 1] == '^') {
                result += pat[++i];
            }
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
            result += "\\[";
            continue;
        }
        result += pat[i];
    }
    return result;
}

// ---- end verbatim copy from src/runtime/src/TsRegExp.cpp ----

} // namespace

void validateRegExpLiteral(const std::string& body, const std::string& flags,
                           int line, int col) {
    // 1. Flag early errors (ECMA-262 22.2.1.1).
    static const char* kAllowed = "dgimsuvy";
    bool hasU = false, hasV = false;
    uint32_t seen = 0;
    for (char f : flags) {
        const char* hit = strchr(kAllowed, f);
        if (!hit || f == '\0') {
            failSyntax(line, col,
                       std::string("invalid regular expression flag '") + f + "'");
        }
        uint32_t bit = 1u << (hit - kAllowed);
        if (seen & bit) {
            failSyntax(line, col,
                       std::string("duplicate regular expression flag '") + f + "'");
        }
        seen |= bit;
        if (f == 'u') hasU = true;
        if (f == 'v') hasV = true;
    }
    if (hasU && hasV) {
        failSyntax(line, col,
                   "regular expression flags 'u' and 'v' may not be combined");
    }

    // 2. Pattern probe via ICU with the runtime's translation. The `v` flag's
    // unicodeSets grammar is not ICU-compatible; skip the probe there (flag
    // checks above still apply).
    if (hasV) return;

    std::string transformed = transformJsPatternForIcu(rewriteUnicodeForIcu(body));
    icu::UnicodeString icuPattern = icu::UnicodeString::fromUTF8(transformed);

    uint32_t icuFlags = 0;
    if (flags.find('i') != std::string::npos) icuFlags |= UREGEX_CASE_INSENSITIVE;
    if (flags.find('m') != std::string::npos) icuFlags |= UREGEX_MULTILINE;
    if (flags.find('s') != std::string::npos) icuFlags |= UREGEX_DOTALL;
    if (flags.find('u') != std::string::npos) icuFlags |= UREGEX_UWORD;

    UErrorCode status = U_ZERO_ERROR;
    UParseError parseError;
    icu::RegexPattern* compiled =
        icu::RegexPattern::compile(icuPattern, icuFlags, parseError, status);
    delete compiled;
    if (U_FAILURE(status)) {
        // Two classes of valid-JS pattern our ICU rejects; tolerate them
        // (a missed rejection just leaves the test failing as before):
        //  (a) Annex B CharacterEscape: \1..\9 without a matching capture
        //      group is legal in non-`u` mode (ICU: invalid back reference).
        //  (b) \p{Script=X} / \p{Script_Extensions=X} (and sc=/scx=) where X
        //      is a script newer than the linked ICU's Unicode tables.
        bool sawDecimalEscape = false, sawScriptProperty = false;
        for (size_t i = 0; i + 1 < body.size(); i++) {
            if (body[i] != '\\') continue;
            char n = body[i + 1];
            if (n == '\\') { i++; continue; }
            if (n >= '1' && n <= '9') sawDecimalEscape = true;
            if ((n == 'p' || n == 'P') && i + 2 < body.size() && body[i + 2] == '{') {
                size_t c = i + 3;
                static const char* kScriptPrefixes[] = {
                    "Script=", "Script_Extensions=", "sc=", "scx=" };
                for (const char* pre : kScriptPrefixes) {
                    if (body.compare(c, strlen(pre), pre) == 0) {
                        sawScriptProperty = true;
                        break;
                    }
                }
            }
            i++;
        }
        //  (c) Empty character class `[]` (matches nothing) and empty
        //      non-capturing group `(?:)` are valid JS; ICU rejects both.
        bool sawIcuOnlyReject = body.find("[]") != std::string::npos ||
                                body.find("(?:)") != std::string::npos;
        //  (d) Quantifier bounds past INT32_MAX are legal JS (no spec limit);
        //      ICU caps them (U_REGEX_NUMBER_TOO_BIG).
        if ((sawDecimalEscape && !hasU) || sawScriptProperty ||
            sawIcuOnlyReject || status == U_REGEX_NUMBER_TOO_BIG) {
            return;
        }
        failSyntax(line, col,
                   std::string("invalid regular expression: ") + u_errorName(status));
    }
}

} // namespace tsaot
