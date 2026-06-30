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
#include <string>
#include <unordered_set>
#include <vector>

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

// ECMA-262 22.2.1: the binary Unicode "properties of strings" — these match
// finite-length strings (sequences), not single code points. They are only
// valid via `\p{...}` under the `v` flag; `\P{...}` (negation) is never valid,
// and `\p{...}` is invalid under the `u` flag (it requires `v`). Under `v`,
// a property-of-strings may not appear inside a negated class set (`[^...]`).
static bool isPropertyOfStrings(const std::string& name) {
    static const char* kStringProps[] = {
        "Basic_Emoji",
        "Emoji_Keycap_Sequence",
        "RGI_Emoji",
        "RGI_Emoji_Flag_Sequence",
        "RGI_Emoji_Modifier_Sequence",
        "RGI_Emoji_Tag_Sequence",
        "RGI_Emoji_ZWJ_Sequence",
    };
    for (const char* p : kStringProps)
        if (name == p) return true;
    return false;
}

// ECMA-262 22.2.1.10 Table: the EXACT set of binary Unicode property names
// (canonical names AND their aliases) valid as a lone `\p{Name}`. ECMAScript
// does NOT loose-match (no whitespace/case/underscore folding), so `\p{Ascii}`,
// `\p{ Lowercase }`, `\p{ANY}` are SyntaxErrors even though ICU compiles them.
static bool isBinaryPropertyName(const std::string& n) {
    static const std::unordered_set<std::string> kBin = {
        "ASCII","ASCII_Hex_Digit","AHex","Alphabetic","Alpha","Any","Assigned",
        "Bidi_Control","Bidi_C","Bidi_Mirrored","Bidi_M","Case_Ignorable","CI",
        "Cased","Changes_When_Casefolded","CWCF","Changes_When_Casemapped","CWCM",
        "Changes_When_Lowercased","CWL","Changes_When_NFKC_Casefolded","CWKCF",
        "Changes_When_Titlecased","CWT","Changes_When_Uppercased","CWU","Dash",
        "Default_Ignorable_Code_Point","DI","Deprecated","Dep","Diacritic","Dia",
        "Emoji","Emoji_Component","EComp","Emoji_Modifier","EMod",
        "Emoji_Modifier_Base","EBase","Emoji_Presentation","EPres",
        "Extended_Pictographic","ExtPict","Extender","Ext","Grapheme_Base",
        "Gr_Base","Grapheme_Extend","Gr_Ext","Hex_Digit","Hex",
        "IDS_Binary_Operator","IDSB","IDS_Trinary_Operator","IDST","ID_Continue",
        "IDC","ID_Start","IDS","Ideographic","Ideo","Join_Control","Join_C",
        "Logical_Order_Exception","LOE","Lowercase","Lower","Math",
        "Noncharacter_Code_Point","NChar","Pattern_Syntax","Pat_Syn",
        "Pattern_White_Space","Pat_WS","Quotation_Mark","QMark","Radical",
        "Regional_Indicator","RI","Sentence_Terminal","STerm","Soft_Dotted","SD",
        "Terminal_Punctuation","Term","Unified_Ideograph","UIdeo","Uppercase",
        "Upper","Variation_Selector","VS","White_Space","space","XID_Continue",
        "XIDC","XID_Start","XIDS",
    };
    return kBin.count(n) != 0;
}

// ECMA-262 22.2.1.10 Table: EXACT General_Category value names (canonical +
// aliases). Valid as a lone `\p{Value}` and as `\p{General_Category=Value}`.
static bool isGeneralCategoryValue(const std::string& n) {
    static const std::unordered_set<std::string> kGC = {
        "Cased_Letter","LC","Close_Punctuation","Pe","Connector_Punctuation","Pc",
        "Control","Cc","cntrl","Currency_Symbol","Sc","Dash_Punctuation","Pd",
        "Decimal_Number","Nd","digit","Enclosing_Mark","Me","Final_Punctuation",
        "Pf","Format","Cf","Initial_Punctuation","Pi","Letter","L","Letter_Number",
        "Nl","Line_Separator","Zl","Lowercase_Letter","Ll","Mark","M",
        "Combining_Mark","Math_Symbol","Sm","Modifier_Letter","Lm",
        "Modifier_Symbol","Sk","Nonspacing_Mark","Mn","Number","N",
        "Open_Punctuation","Ps","Other","C","Other_Letter","Lo","Other_Number",
        "No","Other_Punctuation","Po","Other_Symbol","So","Paragraph_Separator",
        "Zp","Private_Use","Co","Punctuation","P","punct","Separator","Z",
        "Space_Separator","Zs","Spacing_Mark","Mc","Surrogate","Cs","Symbol","S",
        "Titlecase_Letter","Lt","Unassigned","Cn","Uppercase_Letter","Lu",
    };
    return kGC.count(n) != 0;
}

// Scan the pattern for `\p{Name}` / `\P{Name}` where Name is a property of
// strings and enforce the early errors above. Runs in `u` and `v` modes only
// (in non-unicode mode `\p` is the literal `p` per Annex B, not a property
// escape). Throws on the first violation.
static void validatePropertiesOfStrings(const std::string& body, bool hasU,
                                        bool hasV, int line, int col) {
    if (!hasU && !hasV) return;
    // Track character-class nesting and whether any enclosing class is
    // negated (`[^...]`). `/v` allows nested classes; a property-of-strings
    // inside a complemented set is a SyntaxError.
    int negatedClassDepth = 0;
    std::vector<bool> classNegStack;
    for (size_t i = 0; i < body.size(); i++) {
        char ch = body[i];
        if (ch == '\\') {
            // Escape: check for \p{ / \P{ property escapes; otherwise skip
            // the escaped char so a literal `\[` doesn't open a class.
            if (i + 2 < body.size() && (body[i + 1] == 'p' || body[i + 1] == 'P') &&
                body[i + 2] == '{') {
                bool negated = body[i + 1] == 'P';
                size_t close = body.find('}', i + 3);
                if (close != std::string::npos) {
                    std::string name = body.substr(i + 3, close - (i + 3));
                    if (isPropertyOfStrings(name)) {
                        if (negated) {
                            failSyntax(line, col,
                                "a Unicode property of strings may not be "
                                "negated with \\P{...}");
                        }
                        if (!hasV) {
                            failSyntax(line, col,
                                "the Unicode property of strings '" + name +
                                "' requires the 'v' flag");
                        }
                        if (negatedClassDepth > 0) {
                            failSyntax(line, col,
                                "a Unicode property of strings may not appear "
                                "in a negated character class");
                        }
                    } else if (name.find('=') != std::string::npos) {
                        // UnicodePropertyName=UnicodePropertyValue form
                        // (ECMA-262 22.2.1.10). The only valid property names are
                        // General_Category/gc, Script/sc, Script_Extensions/scx;
                        // the value must be non-empty. ECMAScript does NOT loose-
                        // match, so the name must be EXACT (`gc = ...` with spaces
                        // is invalid). ICU accepts Line_Break=, Block=, empty
                        // values, and loose names — all JS early errors.
                        size_t eq = name.find('=');
                        std::string propName = name.substr(0, eq);
                        std::string propValue = name.substr(eq + 1);
                        bool isGC = (propName == "General_Category" || propName == "gc");
                        bool isScript = (propName == "Script" || propName == "sc" ||
                                         propName == "Script_Extensions" || propName == "scx");
                        if (!isGC && !isScript) {
                            failSyntax(line, col,
                                "'" + propName + "' is not a valid Unicode "
                                "property name in a regular expression");
                        }
                        if (propValue.empty()) {
                            failSyntax(line, col,
                                "missing Unicode property value after '" +
                                propName + "='");
                        }
                        // General_Category values are a closed, exact set; reject
                        // loose forms (`gc=uppercase`). Script/Script_Extensions
                        // values (the open Unicode script list) are left to the
                        // ICU compile probe below, which knows them.
                        if (isGC && !isGeneralCategoryValue(propValue)) {
                            failSyntax(line, col,
                                "'" + propValue + "' is not a valid "
                                "General_Category value");
                        }
                    } else {
                        // Lone `\p{Name}` (ECMA-262 22.2.1.10
                        // LoneUnicodePropertyNameOrValue): Name must EXACTLY be a
                        // binary property name OR a General_Category value. This
                        // rejects loose forms (`\p{Ascii}`, `\p{ Lowercase }`,
                        // `\p{ANY}`) and the non-JS `In`-prefix (`\p{InAdlam}`),
                        // all of which ICU otherwise loose-matches.
                        if (!isBinaryPropertyName(name) &&
                            !isGeneralCategoryValue(name)) {
                            failSyntax(line, col,
                                "'" + name + "' is not a valid Unicode property "
                                "name or value in a regular expression");
                        }
                    }
                    i = close;
                    continue;
                }
            }
            i++;  // skip the escaped character
            continue;
        }
        if (ch == '[') {
            bool neg = (i + 1 < body.size() && body[i + 1] == '^');
            classNegStack.push_back(neg);
            if (neg) negatedClassDepth++;
            if (neg) i++;  // consume the '^'
            continue;
        }
        if (ch == ']') {
            if (!classNegStack.empty()) {
                if (classNegStack.back()) negatedClassDepth--;
                classNegStack.pop_back();
            }
            continue;
        }
    }
}

// ECMA-262 22.2.1 `v`-mode (unicodeSets) ClassSetExpression early errors. ICU
// has no `v` mode, so these aren't covered by the compile probe. Inside a
// `v`-mode character class a literal ClassSetSyntaxCharacter `( ) { } / |` must
// be escaped, and a ClassSetReservedDoublePunctuator (`!! ## $$ %% ** ++ ,, ..
// :: ;; << == >> ?? @@ ^^ `` ~~`) is reserved. `\p{}` / `\P{}` / `\q{}` and the
// real set operators `&&` / `--` are intentionally left for the runtime/ICU.
static void validateVModeClasses(const std::string& body, int line, int col) {
    static const char* kReservedDouble = "!#$%*+,.:;<=>?@^`~";
    int depth = 0;
    for (size_t i = 0; i < body.size(); i++) {
        char c = body[i];
        if (c == '\\') {
            // Skip `\p{...}` / `\P{...}` / `\q{...}` whole; else skip one char.
            if (i + 2 < body.size() &&
                (body[i + 1] == 'p' || body[i + 1] == 'P' || body[i + 1] == 'q') &&
                body[i + 2] == '{') {
                size_t close = body.find('}', i + 3);
                i = (close == std::string::npos) ? body.size() : close;
            } else {
                i++;
            }
            continue;
        }
        if (depth == 0) {
            if (c == '[') {
                depth = 1;
                if (i + 1 < body.size() && body[i + 1] == '^') i++;  // negation
            }
            continue;
        }
        if (c == ']') { depth--; continue; }
        if (c == '[') {
            depth++;
            if (i + 1 < body.size() && body[i + 1] == '^') i++;  // nested negation
            continue;
        }
        // Reserved double punctuator (two identical reserved chars).
        if (i + 1 < body.size() && body[i + 1] == c && strchr(kReservedDouble, c)) {
            failSyntax(line, col,
                std::string("the reserved double punctuator '") + c + c +
                "' must be escaped in a 'v'-mode character class");
        }
        // Literal ClassSetSyntaxCharacter that must be escaped.
        if (strchr("(){}/|", c)) {
            failSyntax(line, col,
                std::string("'") + c + "' must be escaped in a 'v'-mode "
                "character class");
        }
    }
}

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

    // 2. Unicode "properties of strings" early errors (22.2.1). Runs for both
    // `u` and `v` modes — must precede the `hasV` early return below, since
    // the most common violations are `v`-flag patterns.
    validatePropertiesOfStrings(body, hasU, hasV, line, col);

    // 3. Pattern probe via ICU with the runtime's translation. The `v` flag's
    // unicodeSets grammar is not ICU-compatible; ICU can't probe it, so run the
    // `v`-mode class early-error checks here instead and skip the probe.
    if (hasV) {
        validateVModeClasses(body, line, col);
        return;
    }

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
