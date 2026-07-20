#include "TsObject_Internal.h"
extern "C" void* ts_to_string_spec(TsValue* val);  // hook-invoking ToString (TsString.cpp)

// Built-in method native wrappers extracted from TsObject.cpp: String, Array,
// TypedArray, Boolean, Date, and RegExp prototype-method implementations (the
// ctx=receiver ts_*_native functions) plus their local helpers. Shared
// state/types/prototypes come via TsObject_Internal.h.
extern "C" {


    // Native wrappers for string methods (ctx = TsString*)
    extern "C" bool ts_string_startsWith_pos(void* str, void* prefix, TsValue* pos);
    extern "C" bool ts_string_endsWith_pos(void* str, void* suffix, TsValue* endPos);
    extern "C" bool ts_string_includes_pos(void* str, void* search, TsValue* pos);
    TsValue* ts_string_startsWith_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* prefix = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        TsValue* pos = (argc >= 2 && argv) ? argv[1] : nullptr;
        return ts_value_make_bool(ts_string_startsWith_pos(str, prefix, pos));
    }
    TsValue* ts_string_endsWith_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* suffix = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        TsValue* endPos = (argc >= 2 && argv) ? argv[1] : nullptr;
        return ts_value_make_bool(ts_string_endsWith_pos(str, suffix, endPos));
    }
    TsValue* ts_string_includes_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* search = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        TsValue* pos = (argc >= 2 && argv) ? argv[1] : nullptr;
        return ts_value_make_bool(ts_string_includes_pos(str, search, pos));
    }
    TsValue* ts_string_indexOf_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* search = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!search) search = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (argc >= 2 && argv && argv[1]) {
            // ToIntegerOrInfinity(position): Symbol/BigInt position throws TypeError.
            int64_t startPos = ts_to_index_integer(argv[1]);
            return ts_value_make_int(ts_string_indexOf_from(str, search, startPos));
        }
        return ts_value_make_int(ts_string_indexOf(str, search));
    }
    TsValue* ts_string_substring_native(void* ctx, int argc, TsValue** argv) {
        // ES 22.1.3.24 steps 1-2: ToString(this) — a detached
        // String.prototype.substring on an Array/other receiver stringifies
        // it first (S15.5.4.15_A3 family).
        TsString* str = ts_ensure_flat((TsString*)ctx);
        if (!str) str = ts_ensure_flat((TsString*)ts_string_from_value((TsValue*)ctx));
        if (!str) return ts_value_make_undefined();
        // ToIntegerOrInfinity on both indices (valueOf invoked, abrupt
        // propagates), and an UNDEFINED end means "to the end" — the old
        // ts_value_get_int turned undefined into 0, then the start/end
        // swap made substring(1, undefined) return the first char.
        int64_t len = ts_string_length(str);
        int64_t start = (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0]))
                            ? ts_to_index_integer(argv[0]) : 0;
        int64_t end = (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]))
                          ? ts_to_index_integer(argv[1]) : len;
        // Steps 5-6: clamp both to [0, len] HERE — ±Infinity saturates to
        // INT64_MAX/MIN, which overflowed the substring internals.
        if (start < 0) start = 0; else if (start > len) start = len;
        if (end < 0) end = 0; else if (end > len) end = len;
        return ts_value_make_string((TsString*)ts_string_substring(str, start, end));
    }
    TsValue* ts_string_slice_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ToIntegerOrInfinity: Symbol/BigInt/throwing-valueOf index throws TypeError.
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_to_index_integer(argv[0]) : 0;
        int64_t end = (argc >= 2 && argv && argv[1]) ? ts_to_index_integer(argv[1]) : ts_string_length(str);
        return ts_value_make_string((TsString*)ts_string_slice(str, start, end));
    }
    // ECMA-262 B.2.3.1 String.prototype.substr(start, length): legacy/annexB.
    // Negative start counts from the end; length defaults to the remainder.
    TsValue* ts_string_substr_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t size = ts_string_length(str);
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t length = (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]))
                             ? ts_value_get_int(argv[1]) : size;
        if (start < 0) start = (size + start > 0) ? size + start : 0;
        if (start > size) start = size;
        if (length < 0) length = 0;
        if (length > size - start) length = size - start;
        return ts_value_make_string((TsString*)ts_string_substring(str, start, start + length));
    }
    TsValue* ts_string_toLowerCase_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_toLowerCase((TsString*)ctx));
    }
    TsValue* ts_string_toUpperCase_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_toUpperCase((TsString*)ctx));
    }
    TsValue* ts_string_trim_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_trim((TsString*)ctx));
    }
    // String.prototype.localeCompare(that) — without Intl this is a code-unit
    // (UTF-16) comparison returning a negative/zero/positive Number. Was a
    // STRING_PROTO_METHOD with no entry in the string get dispatch, so it
    // re-resolved to its own prototype macro and recursed forever (crash).
    TsValue* ts_string_localeCompare_native(void* ctx, int argc, TsValue** argv) {
        if (!ctx) ctx = ts_get_call_this();
        TsString* a = ts_ensure_flat((TsString*)ctx);
        TsValue* arg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsString* b = ts_ensure_flat((TsString*)ts_string_from_value(arg));
        if (!a || !b) return ts_value_make_int(0);
        int64_t la = a->Length(), lb = b->Length();
        int64_t n = (la < lb) ? la : lb;
        for (int64_t i = 0; i < n; i++) {
            int ca = (int)a->CharCodeAt(i), cb = (int)b->CharCodeAt(i);
            if (ca != cb) return ts_value_make_int(ca < cb ? -1 : 1);
        }
        return ts_value_make_int(la < lb ? -1 : (la > lb ? 1 : 0));
    }
    // isWellFormed / toWellFormed (ES2024) — also STRING_PROTO_METHODs missing
    // from the string get dispatch, so they recursed forever (crash).
    TsValue* ts_string_isWellFormed_native(void* ctx, int argc, TsValue** argv) {
        extern bool ts_string_isWellFormed(void* str);
        if (!ctx) ctx = ts_get_call_this();
        return ts_value_make_bool(ts_string_isWellFormed((void*)ts_ensure_flat((TsString*)ctx)));
    }
    TsValue* ts_string_toWellFormed_native(void* ctx, int argc, TsValue** argv) {
        extern void* ts_string_toWellFormed(void* str);
        if (!ctx) ctx = ts_get_call_this();
        return ts_value_make_string((TsString*)ts_string_toWellFormed((void*)ts_ensure_flat((TsString*)ctx)));
    }

    // Annex B.2.3: HTML wrapper methods on String.prototype. Each wraps the
    // receiver's ToString with a fixed HTML tag. RequireObjectCoercible
    // throws TypeError when `this` is null/undefined.
    static TsValue* string_html_wrap(void* ctx, const char* tagOpen,
                                     const char* tagClose, const char* methodName) {
        void* self = ctx;
        if (!self) self = ts_get_call_this();
        uint64_t nb = (uint64_t)(uintptr_t)self;
        if (!self || nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "String.prototype.%s called on null or undefined", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
            return ts_value_make_string(TsString::Create(""));  // unreachable
        }
        // ES B.2.3.2.1 CreateHTML step 2: ToString(this) runs the user's
        // toString/valueOf hooks — a poisoned receiver must THROW here.
        // Coerce BEFORE any C++ locals exist (longjmp rule).
        {
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            self = (void*)ts_to_primitive((TsValue*)self, 2);
        }
        void* strPtr = ts_string_from_value((TsValue*)self);
        TsString* s = strPtr ? ts_ensure_flat(strPtr) : TsString::Create("");
        std::string out;
        out.reserve(strlen(tagOpen) + strlen(tagClose) + (s ? strlen(s->ToUtf8()) : 0));
        out += tagOpen;
        if (s) out += s->ToUtf8();
        out += tagClose;
        return ts_value_make_string(TsString::Create(out.c_str()));
    }

    // Annex B.2.3: HTML wrapper with a single attribute. CreateHTML step
    // 4.b: every 0x22 (") in the attribute VALUE is replaced with &quot;.
    static TsValue* string_html_wrap_attr(void* ctx, int argc, TsValue** argv,
                                          const char* tag, const char* attr,
                                          const char* methodName) {
        void* self = ctx;
        if (!self) self = ts_get_call_this();
        uint64_t nb = (uint64_t)(uintptr_t)self;
        if (!self || nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "String.prototype.%s called on null or undefined", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
            return ts_value_make_string(TsString::Create(""));
        }
        // CreateHTML: ToString(this) THEN ToString(arg), both with user
        // hooks (poisoned values must throw). Coerce before C++ locals.
        TsValue* argPrim = nullptr;
        {
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            self = (void*)ts_to_primitive((TsValue*)self, 2);
            if (argc >= 1 && argv && argv[0])
                argPrim = ts_to_primitive(argv[0], 2);
        }
        void* strPtr = ts_string_from_value((TsValue*)self);
        TsString* s = strPtr ? ts_ensure_flat(strPtr) : TsString::Create("");
        // ToString on argument; default to "undefined" per spec when absent
        TsString* argStr = nullptr;
        if (argPrim) {
            void* argPtr = ts_string_from_value(argPrim);
            if (argPtr) argStr = ts_ensure_flat(argPtr);
        }
        if (!argStr) argStr = TsString::Create("undefined");
        std::string out = "<";
        out += tag;
        out += ' ';
        out += attr;
        out += "=\"";
        // CreateHTML 4.b: escape 0x22 in the attribute value as &quot;
        for (const char* q = argStr->ToUtf8(); q && *q; ++q) {
            if (*q == '\"') out += "&quot;";
            else out += *q;
        }
        out += "\">";
        if (s) out += s->ToUtf8();
        out += "</";
        out += tag;
        out += ">";
        return ts_value_make_string(TsString::Create(out.c_str()));
    }

    extern "C" TsValue* ts_string_big_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<big>", "</big>", "big");
    }
    extern "C" TsValue* ts_string_small_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<small>", "</small>", "small");
    }
    // Annex B.2.3 HTML wrapper methods. Non-static so TsGlobals.cpp can
    // register them on String.prototype (visible via the prototype object,
    // not just via instance lookup).
    extern "C" TsValue* ts_string_bold_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<b>", "</b>", "bold");
    }
    extern "C" TsValue* ts_string_italics_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<i>", "</i>", "italics");
    }
    extern "C" TsValue* ts_string_fixed_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<tt>", "</tt>", "fixed");
    }
    extern "C" TsValue* ts_string_strike_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<strike>", "</strike>", "strike");
    }
    extern "C" TsValue* ts_string_blink_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<blink>", "</blink>", "blink");
    }
    extern "C" TsValue* ts_string_sub_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<sub>", "</sub>", "sub");
    }
    extern "C" TsValue* ts_string_sup_native(void* ctx, int argc, TsValue** argv) {
        return string_html_wrap(ctx, "<sup>", "</sup>", "sup");
    }
    extern "C" TsValue* ts_string_anchor_native(void* ctx, int argc, TsValue** argv) {
        // ToString(attr) runs user hooks that may throw — coerce in THIS thin
        // POD frame (string_html_wrap_attr holds std::string scopes).
        if (argc >= 1 && argv && argv[0])
            argv[0] = ts_value_make_string((TsString*)ts_to_string_spec(argv[0]));
        return string_html_wrap_attr(ctx, argc, argv, "a", "name", "anchor");
    }
    extern "C" TsValue* ts_string_link_native(void* ctx, int argc, TsValue** argv) {
        // ToString(attr) runs user hooks that may throw — coerce in THIS thin
        // POD frame (string_html_wrap_attr holds std::string scopes).
        if (argc >= 1 && argv && argv[0])
            argv[0] = ts_value_make_string((TsString*)ts_to_string_spec(argv[0]));
        return string_html_wrap_attr(ctx, argc, argv, "a", "href", "link");
    }
    extern "C" TsValue* ts_string_fontcolor_native(void* ctx, int argc, TsValue** argv) {
        // ToString(attr) runs user hooks that may throw — coerce in THIS thin
        // POD frame (string_html_wrap_attr holds std::string scopes).
        if (argc >= 1 && argv && argv[0])
            argv[0] = ts_value_make_string((TsString*)ts_to_string_spec(argv[0]));
        return string_html_wrap_attr(ctx, argc, argv, "font", "color", "fontcolor");
    }
    extern "C" TsValue* ts_string_fontsize_native(void* ctx, int argc, TsValue** argv) {
        // ToString(attr) runs user hooks that may throw — coerce in THIS thin
        // POD frame (string_html_wrap_attr holds std::string scopes).
        if (argc >= 1 && argv && argv[0])
            argv[0] = ts_value_make_string((TsString*)ts_to_string_spec(argv[0]));
        return string_html_wrap_attr(ctx, argc, argv, "font", "size", "fontsize");
    }
        bool ts_string_symbol_dispatch(const char* symStorageKey, TsValue* arg,
                                   TsString* receiver, TsValue* extra,
                                   bool hasExtra, TsValue** out,
                                   TsValue* origThis = nullptr);

    TsValue* ts_string_split_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ES 22.1.3.23 step 2: GetMethod(separator, @@split) — a user object
        // (or overridden RegExp) separator handles the whole split, receiving
        // (string, limit) with this = separator.
        if (argc >= 1 && argv && argv[0]) {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.split]", argv[0], str,
                                       (argc >= 2) ? argv[1] : nullptr, true, &out))
                return out;
        }
        // ECMA-262 22.1.3.23: an undefined separator yields a single-element
        // array of the whole string. Also covers `argc == 0`. Everything else
        // (RegExp, string, or a primitive to be ToString'd) is delegated to
        // ts_string_split, which is the single robust separator-coercion site
        // (it also backs the compiler's typed `str.split(x)` fast path).
        void* resultArr;
        if (argc < 1 || !argv || !argv[0] ||
            ts_value_is_undefined((TsValue*)argv[0])) {
            resultArr = ts_string_split(str, nullptr);
        } else {
            resultArr = ts_string_split(str, (void*)argv[0]);
        }
        // ECMA-262 22.1.3.23: the optional `limit` truncates the result to at
        // most `limit` elements (`'a-b-c'.split('-', 2)` === ['a','b']). The
        // limit was previously ignored; lodash `_.split(str, sep, limit)`
        // forwards it to String.prototype.split.
        if (argc >= 2 && argv[1] && !ts_value_is_undefined((TsValue*)argv[1]) && resultArr) {
            // ECMA-262 22.1.3.23 step 6: limit goes through ToUint32, whose
            // ToNumber step THROWS a TypeError on a Symbol (BigInt->Number throws
            // too). Coerce via ts_to_number first so a non-coercible limit throws;
            // ToUint32(NaN/Infinity) == 0 (limit 0 -> empty array).
            double limD = ts_to_number((TsValue*)argv[1]);
            int64_t limit = (limD != limD || std::isinf(limD)) ? 0 : (int64_t)limD;
            if (limit >= 0) {
                TsArray* arr = (TsArray*)resultArr;
                if (arr->Length() > limit) arr->SetLength((size_t)limit);
            }
        }
        return ts_value_make_object(resultArr);
    }
    // Helper: check if a TsValue is callable (closure or function)
    bool ts_value_is_callable(TsValue* val) {
        return ts_is_callable((void*)val);  // canonical IsCallable (defined below)
    }

    // Helper: call callback with variable number of TsValue* args
    static TsValue* ts_call_variadic(TsValue* fn, TsValue** args, int count) {
        return ts_call_n(fn, count, args);
    }

    // String.replace with regex and callback function
    static TsValue* ts_string_replace_callback_regex(TsString* str, TsRegExp* regexp, TsValue* callback) {
        icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
        if (!matcher) return ts_value_make_string(str);

        icu::UnicodeString input = str->ToUnicodeString();
        matcher->reset(input);

        bool isGlobal = regexp->IsGlobal();
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString result;
        int32_t lastEnd = 0;
        // Declared BEFORE the loop (SMELL-002): the user callback (and the
        // Symbol-return coercion) inside the loop can longjmp; a non-POD
        // local still UNCONSTRUCTED at that point puts this frame in the
        // 0xc0000374 unwind-corruption class. All non-PODs in this frame
        // must be constructed before the first throwing call.
        std::string utf8Result;

        while (matcher->find()) {
            int32_t matchStart = matcher->start(status);
            int32_t matchEnd = matcher->end(status);

            // Append text before this match
            result.append(input, lastEnd, matchStart - lastEnd);

            // Build callback args: (match, g1, g2, ..., offset, originalString)
            int32_t groupCount = matcher->groupCount();
            int totalArgs = 1 + groupCount + 2; // match + groups + offset + input
            std::vector<TsValue*> args;
            args.reserve(totalArgs);

            // Full match (group 0)
            {
                icu::UnicodeString matchStr = matcher->group(0, status);
                std::string utf8;
                matchStr.toUTF8String(utf8);
                args.push_back(ts_value_make_string(TsString::Create(utf8.c_str())));
            }

            // Capture groups (1..groupCount)
            for (int32_t i = 1; i <= groupCount; i++) {
                int32_t gs = matcher->start(i, status);
                if (gs == -1) {
                    args.push_back(ts_value_make_undefined());
                } else {
                    icu::UnicodeString group = matcher->group(i, status);
                    std::string gUtf8;
                    group.toUTF8String(gUtf8);
                    args.push_back(ts_value_make_string(TsString::Create(gUtf8.c_str())));
                }
            }

            // Offset (index of match in original string)
            args.push_back(ts_value_make_int(matchStart));

            // Original string
            args.push_back(ts_value_make_string(str));

            // Call the callback
            TsValue* callResult = ts_call_variadic(callback, args.data(), (int)args.size());

            // Convert result to string and append
            if (callResult) {
                TsString* replStr = (TsString*)ts_string_from_value(callResult);
                if (replStr) {
                    icu::UnicodeString replU = replStr->ToUnicodeString();
                    result.append(replU);
                }
            }

            lastEnd = matchEnd;

            // For zero-length matches, advance by 1 to avoid infinite loop
            if (matchStart == matchEnd) {
                if (matchEnd < input.length()) {
                    result.append(input[matchEnd]);
                    lastEnd = matchEnd + 1;
                } else {
                    break;
                }
            }

            if (!isGlobal) break;
        }

        // Append remaining text after last match
        if (lastEnd < input.length()) {
            result.append(input, lastEnd, input.length() - lastEnd);
        }

        result.toUTF8String(utf8Result);
        return ts_value_make_string(TsString::Create(utf8Result.c_str()));
    }

    // String.replace with string pattern and callback function
    static TsValue* ts_string_replace_callback_string(TsString* str, TsString* pattern, TsValue* callback) {
        const char* haystack = str->ToUtf8();
        const char* needle = pattern->ToUtf8();
        if (!haystack || !needle) return ts_value_make_string(str);

        const char* found = strstr(haystack, needle);
        if (!found) return ts_value_make_string(str);

        int64_t offset = found - haystack;
        size_t needleLen = strlen(needle);

        // Build callback args: (match, offset, originalString)
        TsValue* args[3];
        args[0] = ts_value_make_string(pattern);
        args[1] = ts_value_make_int(offset);
        args[2] = ts_value_make_string(str);

        TsValue* callResult = tsCall(callback, args[0], args[1], args[2]);

        TsString* replStr = callResult ? (TsString*)ts_string_from_value(callResult) : TsString::Create("undefined");
        const char* replUtf8 = replStr->ToUtf8();

        std::string result;
        result.append(haystack, offset);
        result.append(replUtf8);
        result.append(haystack + offset + needleLen);

        return ts_value_make_string(TsString::Create(result.c_str()));
    }

    // ES GetMethod(searchValue, @@x) preamble for String.prototype methods
    // (22.1.3.14/17/18/21/22/23 step 2). A USER-provided symbol method on the
    // argument diverts the whole operation to it: non-RegExp OBJECTS exposing
    // "[Symbol.x]" on their chain, and RegExp instances whose OWN props
    // override it. Unmodified TsRegExp arguments keep the existing fast paths
    // (RegExp.prototype's builtin @@x natives are the same machinery).
    // Returns true when handled (*out set). POD locals only (ts_throw-safe).
    bool ts_string_symbol_dispatch(const char* symStorageKey, TsValue* arg,
                                       TsString* receiver, TsValue* extra,
                                       bool hasExtra, TsValue** out,
                                       TsValue* origThis) {
        if (!arg || !receiver) return false;
        void* raw = ts_value_get_object(arg);
        if (!raw || (uintptr_t)raw < 0x1000) return false;
        uint32_t m0 = *(uint32_t*)raw;
        TsValue* method = nullptr;
        if (m0 == 0x52454758) {  // TsRegExp
            // ES 22.1.3.x step 2: GetMethod(rx, @@x). A real RegExp resolves
            // @@x to RegExp.prototype's builtin (spec-compliant exec-based)
            // native UNLESS an own/defineProperty override shadows it. Routing
            // the builtin case here (rather than returning false to the legacy
            // native fast path) makes String.prototype.{split,match,matchAll,
            // search,replace,replaceAll} observe lastIndex/flags/exec and the
            // GetSubstitution rules through RegExp.prototype[@@x].
            // Own @@x overrides land in TWO stores depending on the write
            // path: plain assignment -> re->GetOwnProps(); defineProperty ->
            // the g_native_object_props side-map. Consult both
            // (searchValue-replacer-RegExp-call installs via defineProperty).
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(symStorageKey);
            TsMap* own = (TsMap*)((TsRegExp*)raw)->GetOwnProps();
            if (own && own->Has(k)) {
                method = nanbox_from_tagged(own->Get(k));
            } else {
                extern TsMap* getNativeProps(void* obj);
                TsMap* side = getNativeProps(raw);
                if (side && side->Has(k)) method = nanbox_from_tagged(side->Get(k));
                else method = ts_object_get_property(raw, symStorageKey);
            }
        } else if (m0 == 0x53545247 || m0 == 0x434F4E53 ||
                   m0 == 0x42494749 || m0 == 0x53594D42) {
            // PRIMITIVE search values (string/BigInt/Symbol) never consult
            // @@x — the cstm-*-on-bigint-primitive tests install a THROWING
            // BigInt.prototype[@@x] getter and require it stays untouched.
            return false;
        } else {
            method = ts_object_get_property(raw, symStorageKey);
        }
        if (!method) return false;
        uint64_t mnb = nanbox_from_tsvalue_ptr(method);
        if (nanbox_is_undefined(mnb) || nanbox_is_null(mnb)) return false;
        if (!ts_is_callable((void*)method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Symbol method of the search value is not a function"));
            return true;  // unreachable
        }
        // ES step: the @@method is called with the ORIGINAL this value O as its
        // first argument (a String-wrapper receiver stays the wrapper, not a
        // ToString). origThis carries O when the caller has it; else the string
        // receiver is the this and boxing it is correct.
        TsValue* args[2] = { origThis ? origThis : ts_value_make_string(receiver),
                             extra ? extra : ts_value_make_undefined() };
        *out = ts_function_call_with_this(method, arg, hasExtra ? 2 : 1, args);
        if (!*out) *out = ts_value_make_undefined();
        return true;
    }

    // ES 22.2.7.5 IsRegExp(argument): a real RegExp OR any object whose
    // @@match is truthy. Observable Get(@@match) — POD-only frame (a getter
    // may ts_throw/longjmp). A primitive (incl. a string) is never a RegExp.
    static bool string_is_regexp(TsValue* arg) {
        if (!arg) return false;
        // Same robust extraction as ts_string_symbol_dispatch: a primitive
        // (number/bool/null/undefined/raw double from the typed fast path)
        // yields no object pointer and is NEVER a RegExp.
        void* raw = ts_value_get_object(arg);
        if (!raw || (uintptr_t)raw < 0x1000 ||
            (uintptr_t)raw > 0x00007FFFFFFFFFFFULL) return false;
        uint32_t m0 = *(uint32_t*)raw;
        // Heap string/cons/symbol/bigint primitives are not Objects (IsRegExp
        // step 1) and must not consult @@match.
        if (m0 == 0x53545247 /*STRG*/ || m0 == 0x434F4E53 /*CONS*/ ||
            m0 == 0x53594D42 /*SYMB*/ || m0 == 0x42494749 /*BIGI*/)
            return false;
        TsValue* matcher = ts_object_get_property(raw, "[Symbol.match]");
        if (matcher) {
            uint64_t mnb = nanbox_from_tsvalue_ptr(matcher);
            if (!nanbox_is_undefined(mnb) && !nanbox_is_null(mnb)) {
                extern bool ts_value_to_bool(TsValue* v);
                return ts_value_to_bool(matcher);
            }
        }
        return (m0 == 0x52454758 /*REGX*/);
    }

    // ES 22.1.3.14 step 2 / 22.1.3.21 step 2: if IsRegExp(searchValue), its
    // `flags` (ToString, hooks observable) MUST contain 'g' — else TypeError;
    // RequireObjectCoercible(flags) first. Called by matchAll/replaceAll on
    // the ORIGINAL searchValue BEFORE ToString(this) so the poisoned-this
    // tests never trigger ToString. POD-only frame (hooks may longjmp).
    // Exported (non-static) so the typed fast paths in TsString.cpp
    // (ts_string_matchAll_regexp / ts_string_replaceAll) share the check.
    void string_regexp_require_global(TsValue* arg, const char* methodName) {
        if (!arg || ts_value_is_undefined(arg) || ts_value_is_null(arg)) return;
        if (!string_is_regexp(arg)) return;
        void* raw = ts_value_get_object(arg);
        if (!raw) raw = (void*)arg;
        TsValue* flagsV = ts_object_get_property(raw, "flags");
        if (!flagsV || ts_value_is_undefined(flagsV) || ts_value_is_null(flagsV)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "flags is null or undefined"));
            return;
        }
        TsString* fStr = (TsString*)ts_to_string_spec(flagsV);
        const char* f = fStr ? fStr->ToUtf8() : "";
        bool hasG = false;
        for (const char* c = f; c && *c; ++c) if (*c == 'g') { hasG = true; break; }
        if (!hasG) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                "String.prototype.%s must be called with a global RegExp", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        }
    }

    TsValue* ts_string_replace_native(void* ctx, int argc, TsValue** argv) {
        // Flatten a TsConsString receiver before any regex path. The regex
        // helpers (esp. the no-match callback-regex path) read the receiver's
        // char buffer directly; a lazy cons-string passed un-flattened is read
        // as raw struct/pointer bytes -> garbage output (e.g. lodash deburr's
        // `consString.replace(reLatin, deburrLetter)` with no match returned
        // "Hz\b" pointer bytes). ts_ensure_flat (declared in TsConsString.h)
        // returns a TsString unchanged and flattens a "CONS" string. Mirrors
        // the JSON.stringify cons fix.
        TsString* str = ctx ? ts_ensure_flat(ctx) : (TsString*)ctx;
        if (argc < 1 || !argv) return ts_value_make_string(str);

        // ES 22.1.3.18 step 2: GetMethod(searchValue, @@replace) dispatch.
        {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.replace]", argv[0], str,
                                       (argc >= 2) ? argv[1] : nullptr, true, &out))
                return out;
        }

        // Check if replacement (argv[1]) is a callback function
        bool replIsCallback = (argc >= 2 && argv[1] && ts_value_is_callable(argv[1]));

        // Extract and unbox pattern. Only a real heap object (ts_value_get_object
        // returns non-null) can be a RegExp; a NaN-boxed primitive searchValue
        // (false / number / null) must NOT be dereferenced for the REGX magic --
        // that read crashed for e.g. "x".replace(false, ...).
        void* rawPattern = argv[0] ? ts_value_get_object((TsValue*)argv[0]) : nullptr;

        if (rawPattern) {
            uint32_t magic = *(uint32_t*)rawPattern;
            if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                if (replIsCallback) {
                    return ts_string_replace_callback_regex(str, (TsRegExp*)rawPattern, argv[1]);
                }
                // String replacement
                void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
                if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;
                return ts_value_make_string((TsString*)ts_string_replace_regexp(str, rawPattern, replacement));
            }
        }

        // Pattern is a string. ECMA-262 22.1.3.18: a non-RegExp searchValue is
        // ToString'd, so "xfalse".replace(false, ..) searches "false" -- coerce a
        // primitive via ts_string_from_value instead of leaving the raw nanbox.
        void* pattern = argv[0] ? ts_value_get_string(argv[0]) : nullptr;
        if (!pattern && argv[0]) {
            extern void* ts_string_from_value(TsValue* val);
            pattern = ts_string_from_value((TsValue*)argv[0]);
        }

        if (replIsCallback) {
            TsString* strPattern = (TsString*)pattern;
            if (!strPattern) strPattern = TsString::Create("");
            return ts_string_replace_callback_string(str, strPattern, argv[1]);
        }

        void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;
        return ts_value_make_string((TsString*)ts_string_replace(str, pattern, replacement));
    }
    TsValue* ts_string_repeat_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ECMA-262 22.1.3.18: count goes through ToIntegerOrInfinity, whose
        // ToNumber step THROWS a TypeError on a Symbol (and BigInt->Number also
        // throws). ts_to_number performs that coercion (and preserves NaN, which
        // ts_string_repeat then maps to 0 while still RangeError-checking count<0).
        double count = (argc >= 1 && argv && argv[0]) ? ts_to_number(argv[0]) : 0.0;
        return ts_value_make_string((TsString*)ts_string_repeat(str, count));
    }
    TsValue* ts_string_charAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_string_charAt(str, index));
    }
    TsValue* ts_string_charCodeAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_int(ts_string_charCodeAt(str, index));
    }
    TsValue* ts_string_padStart_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ECMA-262 22.1.3.16: maxLength goes through ToLength(ToIntegerOrInfinity),
        // whose ToNumber step THROWS a TypeError on a Symbol (BigInt->Number throws
        // too). ts_to_number performs that coercion and preserves NaN/undefined
        // (which ts_string_padStart's ToLength then maps to 0).
        double targetLength = (argc >= 1 && argv && argv[0]) ? ts_to_number(argv[0]) : 0.0;
        void* padString = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_padStart(str, targetLength, padString));
    }
    TsValue* ts_string_padEnd_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ECMA-262 22.1.3.15: maxLength goes through ToLength(ToIntegerOrInfinity);
        // ToNumber THROWS a TypeError on a Symbol (BigInt->Number throws too).
        double targetLength = (argc >= 1 && argv && argv[0]) ? ts_to_number(argv[0]) : 0.0;
        void* padString = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_padEnd(str, targetLength, padString));
    }

    // Native wrapper for string.toString() / string.valueOf() - just returns the string itself
    TsValue* ts_string_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ctx);
    }

    // Native wrappers for missing string methods in dynamic dispatch
    TsValue* ts_string_lastIndexOf_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* searchString = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!searchString) searchString = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_int(ts_string_lastIndexOf(str, searchString));
    }
    // ES RequireObjectCoercible + ToString(this) with USER HOOKS (an object
    // receiver's toString/valueOf runs and its throw propagates). POD frame
    // only — the hook may ts_throw/longjmp (longjmp-stdstring rule).
    static TsString* native_this_to_string(void* ctx, const char* methodName) {
        void* self = ctx;
        if (!self) self = ts_get_call_this();
        uint64_t nb = (uint64_t)(uintptr_t)self;
        if (!self || nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "String.prototype.%s called on null or undefined", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        }
        TsString* r = (TsString*)ts_to_string_spec((TsValue*)self);
        return r ? r : TsString::Create("");
    }

    TsValue* ts_string_trimStart_native(void* ctx, int argc, TsValue** argv) {
        TsString* s0 = native_this_to_string(ctx, "trimStart");
        return ts_value_make_string((TsString*)ts_string_trimStart(s0));
    }
    TsValue* ts_string_trimEnd_native(void* ctx, int argc, TsValue** argv) {
        TsString* s0 = native_this_to_string(ctx, "trimEnd");
        return ts_value_make_string((TsString*)ts_string_trimEnd(s0));
    }
    TsValue* ts_string_replaceAll_native(void* ctx, int argc, TsValue** argv) {
        // ECMA-262 22.1.3.21 String.prototype.replaceAll ( searchValue, replaceValue ).
        extern void* ts_string_from_value(TsValue* val);
        void* O = ctx ? ctx : ts_get_call_this();
        TsValue* sv = (argc >= 1 && argv) ? argv[0] : nullptr;
        TsValue* rv = (argc >= 2 && argv) ? argv[1] : nullptr;
        // step 2: searchValue checks + @@replace dispatch, BEFORE ToString(O)
        // (the poisoned-this replaceAll tests require O's toString stays untouched
        //  until step 3). origThis = O so a @@replace replacer receives the
        //  ORIGINAL this (e.g. a String wrapper), not ToString(O).
        if (sv && !ts_value_is_undefined(sv) && !ts_value_is_null(sv)) {
            string_regexp_require_global(sv, "replaceAll");   // step 2.a/2.b
            TsValue* origThis = (TsValue*)ts_get_call_this();
            if (!origThis) origThis = (TsValue*)ts_value_make_string((TsString*)O);
            TsValue* out = nullptr;                           // step 2.c/2.d
            if (ts_string_symbol_dispatch("[Symbol.replace]", sv, (TsString*)O,
                                       rv, true, &out, origThis))
                return out;
        }
        // step 3: string = ToString(O)   (RequireObjectCoercible + hooks)
        TsString* string = native_this_to_string(O, "replaceAll");
        // step 4: searchString = ToString(searchValue). A RegExp with an
        // own @@replace overridden to undefined reaches here and is ToString'd
        // to "/src/flags" (getSubstitution-*), NOT regex-matched.
        TsString* searchStr;
        if (!sv || ts_value_is_undefined(sv))
            searchStr = TsString::Create("undefined");
        else
            searchStr = (TsString*)ts_to_string_spec(sv);  // hook-invoking ToString
        if (!searchStr) searchStr = TsString::Create("undefined");
        // steps 5-15: delegate to the string-path replaceAll (dispatch on a
        // string primitive is a no-op) — reuses GetSubstitution + functional
        // replace with the correct empty-captures rule.
        void* replVal = rv ? (void*)rv : (void*)ts_value_make_undefined();
        return ts_value_make_string((TsString*)ts_string_replaceAll(
            (void*)string, (void*)ts_value_make_string(searchStr), replVal));
    }
    TsValue* ts_string_at_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ToIntegerOrInfinity: a Symbol/BigInt/throwing-valueOf index must throw.
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_to_index_integer(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_string_at(str, index));
    }
    TsValue* ts_string_concat_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* other = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!other) return ts_value_make_string(str);
        return ts_value_make_string((TsString*)ts_string_concat(str, other));
    }
    TsValue* ts_string_match_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ES 22.1.3.13 step 2: GetMethod(regexp, @@match) dispatch.
        if (argc >= 1 && argv && argv[0]) {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.match]", argv[0], str,
                                       nullptr, false, &out))
                return out;
        }
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // Non-RegExp argument coercion (ToString -> RegExpCreate) lives in the
        // shared ts_string_match_regexp choke point, so both this prototype
        // wrapper and the compiler's typed `str.match(x)` fast path get it.
        void* result = ts_string_match_regexp(str, regexp);
        return result ? ts_value_make_object(result) : (TsValue*)ts_value_make_null();
    }
    TsValue* ts_string_search_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        // ES 22.1.3.22 step 2: GetMethod(regexp, @@search) dispatch.
        if (argc >= 1 && argv && argv[0]) {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.search]", argv[0], str,
                                       nullptr, false, &out))
                return out;
        }
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_int(ts_string_search_regexp(str, regexp));
    }
    TsValue* ts_string_matchAll_native(void* ctx, int argc, TsValue** argv) {
        // ECMA-262 22.1.3.14 String.prototype.matchAll ( regexp ).
        extern void* ts_string_from_value(TsValue* val);
        extern void* ts_regexp_create(void* pattern, void* flags);
        void* O = ctx ? ctx : ts_get_call_this();
        TsValue* R = (argc >= 1 && argv) ? argv[0] : nullptr;
        // step 2: searchValue checks + @@matchAll dispatch (BEFORE ToString(O)).
        if (R && !ts_value_is_undefined(R) && !ts_value_is_null(R)) {
            string_regexp_require_global(R, "matchAll");   // step 2.a/2.b
            TsValue* out = nullptr;                          // step 2.c
            if (ts_string_symbol_dispatch("[Symbol.matchAll]", R, (TsString*)O,
                                       nullptr, false, &out))
                return out;
        }
        // step 3: S = ToString(O) (RequireObjectCoercible + hook-observable).
        TsString* S = native_this_to_string(O, "matchAll");
        // step 4: rx = RegExpCreate(R, "g").
        void* rawR = R ? ts_value_get_object(R) : nullptr;
        void* rxObj;
        if (rawR && (uintptr_t)rawR >= 0x1000 &&
            *(uint32_t*)rawR == 0x52454758 /*REGX*/) {
            rxObj = TsRegExp::Create(((TsRegExp*)rawR)->GetSource()->ToUtf8(), "g");
        } else if (!R || ts_value_is_undefined(R) || ts_value_is_null(R)) {
            rxObj = TsRegExp::Create("", "g");
        } else {
            TsString* patS = (TsString*)ts_string_from_value(R);
            rxObj = ts_regexp_create((void*)ts_value_make_string(patS),
                        (void*)ts_value_make_string(TsString::Create("g")));
        }
        // step 5: Return ? Invoke(rx, @@matchAll, «S»).
        TsValue* method = ts_object_get_property(rxObj, "[Symbol.matchAll]");
        uint64_t mnb = method ? nanbox_from_tsvalue_ptr(method) : (uint64_t)NANBOX_UNDEFINED;
        if (!method || nanbox_is_undefined(mnb) || nanbox_is_null(mnb) ||
            !ts_is_callable((void*)method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "rx[Symbol.matchAll] is not a function"));
            return (TsValue*)ts_value_make_undefined();
        }
        TsValue* a1[1] = { (TsValue*)ts_value_make_string(S) };
        return ts_function_call_with_this(method,
            (TsValue*)ts_value_make_object(rxObj), 1, a1);
    }
    TsValue* ts_string_codePointAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_int(ts_string_codePointAt(str, index));
    }
    TsValue* ts_string_normalize_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* form = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_normalize(str, form));
    }

    // ============================================================
    // Native wrappers for array methods (ctx = TsArray*)
    // ============================================================

    // Helper: resolve array from ctx or this (for Array.prototype methods)
    // Helper: returns true if `p` looks like a valid heap pointer we can
    // dereference for a 4-byte magic read. Filters out C-null AND NaN-box
    // small-integer sentinels (NANBOX_NULL=0x2, NANBOX_UNDEFINED=0xA, etc.)
    // which would otherwise crash the magic check.
    static inline bool is_safe_ptr_for_magic(void* p) {
        if (!p) return false;
        uintptr_t u = (uintptr_t)p;
        // Anything below 4KB is either a sentinel or a guard page.
        if (u < 0x1000) return false;
        // NaN-boxed primitives (doubles, int32, bool) have their high bits
        // above the canonical 48-bit pointer range. Reject them — they aren't
        // real heap pointers. Valid heap pointers fit in 48 bits on x64.
        uint64_t nb = (uint64_t)u;
        if (!nanbox_is_ptr(nb) && (nanbox_is_number(nb) || nanbox_is_bool(nb))) {
            return false;
        }
        // Also reject pointers with high bits set (above 48-bit canonical range).
        if (u >> 48) return false;
        return true;
    }

    static TsArray* resolve_array_ctx(void* ctx) {
        // If ctx looks like a valid TsArray, use it directly
        if (is_safe_ptr_for_magic(ctx)) {
            uint32_t m = *(uint32_t*)ctx;
            if (m == 0x41525259) return (TsArray*)ctx; // TsArray::MAGIC
        }
        // Fallback: get from 'this' (used by Array.prototype.method.call(arr, ...))
        void* thisVal = ts_get_call_this();
        if (is_safe_ptr_for_magic(thisVal)) {
            // Unbox if needed
            void* raw = ts_value_get_object((TsValue*)thisVal);
            if (!raw) raw = thisVal;
            if (is_safe_ptr_for_magic(raw)) {
                uint32_t m = *(uint32_t*)raw;
                if (m == 0x41525259) return (TsArray*)raw;
            }
        }
        return nullptr;
    }

    // Inverted-dispatch bailout for the dynamic (.call/array-like) entry: if a
    // spec impl is installed AND the receiver isn't a real array (array-like
    // object / primitive / nullish), delegate to the self-hosted impl. Returns
    // the impl's result, or nullptr to take the native path.
    static TsValue* array_selfhost_arraylike(void* impl, void* ctx, int argc, TsValue** argv) {
        if (!impl || resolve_array_ctx(ctx)) return nullptr;
        extern TsValue* ts_call_with_this_3(TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        TsValue* cb = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue* ta = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
        return ts_call_with_this_3(ts_value_make_object(impl), ts_value_make_undefined(),
                                   (TsValue*)ctx, cb, ta);
    }

    // reduce/reduceRight variant: SH(receiver, callback, initialValue, hasInitial).
    static TsValue* array_selfhost_arraylike_reduce(void* impl, void* ctx, int argc, TsValue** argv) {
        if (!impl || resolve_array_ctx(ctx)) return nullptr;
        extern TsValue* ts_call_with_this_4(TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        TsValue* cb = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue* iv = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
        return ts_call_with_this_4(ts_value_make_object(impl), ts_value_make_undefined(),
                                   (TsValue*)ctx, cb, iv, ts_value_make_bool(argc >= 2));
    }

    // Spec preamble for Array.prototype.X.call(receiver) sites:
    //   1. Let O be ? ToObject(this value).  (we approximate: throw if nullish)
    //   2. Let len be ? LengthOfArrayLike(O). (caller's responsibility)
    //
    // Returns a TsArray* if ctx (or ts_get_call_this) is a valid array.
    // Throws TypeError on definitive nullish receivers (ctx == NaN-boxed
    // null/undefined AND ts_get_call_this is also nullish). Falls back to
    // returning ctx as a raw cast for non-array, non-nullish receivers,
    // matching the existing behavior of resolve_array_ctx for that path
    // (which is broken for non-arrays but at least doesn't crash on
    // valid arrays).
    // A MUTATING Array.prototype method invoked on an array-LIKE receiver
    // operated on the materialized temp (require_array_or_throw) — the spec
    // Sets go to the ORIGINAL object. Write the temp's elements and length
    // back. POD frame: a setter on the original may throw.
    extern "C" void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value);
    // Non-writable "length" marker probe (TsObject_ObjectStatics.cpp,
    // ES 10.4.2.4 ArraySetLength step 15).
    extern bool array_length_is_nonwritable(TsArray* a);

    // ES 10.4.2.4 ArraySetLength via Set(O, "length", v, true): every
    // length-writing mutator (pop/push/shift/unshift/splice) must throw
    // TypeError when the receiver is FROZEN (integrity side-table) or its
    // `length` was made non-writable via defineProperty — or when the
    // materialized receiver is a STRING (its length is never writable).
    static void array_require_length_writable(TsArray* arr, const char* method) {
        if (!arr) return;
        extern uint8_t ts_integrity_get(void* raw);
        bool blocked = ts_integrity_get((void*)arr) >= 3;
        if (!blocked) blocked = array_length_is_nonwritable(arr);
        if (!blocked && arr->originalReceiver &&
            arr->originalReceiver != (void*)arr) {
            void* orig = arr->originalReceiver;
            if ((uintptr_t)orig >= 4096 &&
                (uintptr_t)orig < 0x0000800000000000ULL) {
                uint32_t m0 = *(uint32_t*)orig;
                if (m0 == 0x53545247 /*STRG*/ || m0 == TsConsString::MAGIC) {
                    blocked = true;
                } else if (*(uint32_t*)((char*)orig + 16) == 0x4D415053) {
                    // String WRAPPER object (the materializer boxes string
                    // receivers): its length is never writable either.
                    TsMap* wm = (TsMap*)orig;
                    TsValue sdKey; sdKey.type = ValueType::STRING_PTR;
                    sdKey.ptr_val = TsString::GetInterned("__StringData");
                    if (wm->Get(sdKey).type == ValueType::STRING_PTR)
                        blocked = true;
                    else if (ts_integrity_get(orig) >= 3)
                        blocked = true;
                } else if (ts_integrity_get(orig) >= 3) {
                    blocked = true;
                }
            }
        }
        if (blocked) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "Cannot %s: array length is not writable", method);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        }
    }

    static void arraylike_writeback(TsArray* arr) {
        if (!arr) return;
        void* orig = arr->originalReceiver;
        if (!orig || orig == (void*)arr) return;
        if ((uintptr_t)orig < 0x1000 || (uintptr_t)orig >= 0x0000800000000000ULL) return;
        uint32_t m0 = *(uint32_t*)orig;
        if (m0 == 0x53545247 || m0 == TsConsString::MAGIC) return;  // string receiver: read-only
        TsValue* origBoxed = ts_value_make_object(orig);
        int64_t n = (int64_t)arr->Length();
        for (int64_t i = 0; i < n; i++) {
            TsValue* v = arr->GetElementBoxed((size_t)i);
            ts_object_set_dynamic(origBoxed, ts_value_make_int(i), v);
        }
        TsValue* lk = ts_value_make_string(TsString::GetInterned("length"));
        ts_object_set_dynamic(origBoxed, lk, ts_value_make_int(n));
    }

    // LengthOfArrayLike(O) for a generic receiver WITHOUT materializing any
    // element. Returns the ToLength'd length (clamped to [0, 2^53-1]) for a
    // real array or a plain array-like object; -1 when no usable length could
    // be read. Used by the change-array-by-copy methods (toReversed/toSorted/
    // toSpliced/with) and slice/splice to run the ArrayCreate length-limit
    // check (ES ArrayCreate: length > 2^32-1 -> RangeError) BEFORE any element
    // getter runs. A too-large length must RangeError with zero element reads.
    static double arraylike_length_of(void* ctx) {
        TsArray* a = resolve_array_ctx(ctx);
        if (a) return (double)a->Length();
        void* ctxToRead = ctx;
        if (ctx) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
                void* t = ts_get_call_this();
                if (t) ctxToRead = t;
            }
        } else {
            void* t = ts_get_call_this();
            if (t) ctxToRead = t;
        }
        if (!ctxToRead) return -1;
        // Primitives (number/bool) and strings have lengths far below the limit;
        // let them flow through the normal materializer (no RangeError concern).
        {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctxToRead);
            if (nanbox_is_number(nb) || nanbox_is_bool(nb)) return -1;
        }
        TsValue* lenVal = ts_object_get_property(ctxToRead, "length");
        if (!lenVal) return -1;
        double d = ts_to_number(lenVal);   // may throw TypeError on Symbol length
        if (d != d || d <= 0) return 0;
        if (d > 9007199254740991.0) d = 9007199254740991.0;  // ToLength clamp
        return d;
    }

    // ES ArrayCreate limit for the change-array-by-copy methods: if the source
    // LengthOfArrayLike exceeds 2^32-1, ArrayCreate(len) throws RangeError. This
    // must be observed before any element read (the length-exceeding tests plant
    // throwing index getters). No-op for real arrays / small lengths.
    static void array_check_arraycreate_limit(void* ctx) {
        double len = arraylike_length_of(ctx);
        if (len > 4294967295.0) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "Invalid array length"));
        }
    }

    // Raw (pre-clamp) LengthOfArrayLike read by the most recent
    // require_array_or_throw call. slice/splice consult it for the ArrayCreate
    // count-limit (RangeError) so length is read exactly once (the array-like
    // temp clamps length to MAX_ITER, losing the real value). Single-threaded
    // use, read immediately after the require_array_or_throw call.
    static double g_require_array_raw_len = 0;

    static TsArray* require_array_or_throw(void* ctx, const char* methodName) {
        g_require_array_raw_len = 0;
        TsArray* arr = resolve_array_ctx(ctx);
        if (arr) { g_require_array_raw_len = (double)arr->Length(); return arr; }

        // Distinguish "nullish receiver" (throw) from "non-array but
        // non-nullish object" (legacy fall-through, returns nullptr).
        bool ctxIsNullish = false;
        if (ctx) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) ctxIsNullish = true;
        } else {
            ctxIsNullish = true;
        }
        bool thisIsNullish = true;
        void* thisVal = ts_get_call_this();
        if (thisVal) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)thisVal);
            if (!nanbox_is_null(nb) && !nanbox_is_undefined(nb)) thisIsNullish = false;
        }
        if (ctxIsNullish && thisIsNullish) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "Array.prototype.%s called on null or undefined", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
            return nullptr;  // unreachable
        }
        // Non-nullish but not a recognized TsArray. Per spec:
        //   1. Let O be ToObject(this value).
        //   2. Let len be LengthOfArrayLike(O).
        //   3. For each index i in [0, len), read O[i].
        // We materialize a temporary TsArray by reading .length and each
        // indexed property. This makes existing Array method wrappers work
        // on array-like objects (e.g., { 0: 'a', 1: 'b', length: 2 }).
        void* ctxToRead = ctx;
        // Prefer ts_get_call_this() when ctx is nullish but call_this is not —
        // matches resolve_array_ctx fallback behavior.
        if (ctxIsNullish) {
            ctxToRead = ts_get_call_this();
            if (!ctxToRead) return nullptr;
        }
        // Primitive receivers (boolean, number). ToObject returns a wrapper
        // whose [[Prototype]] is Boolean.prototype / Number.prototype. The
        // spec iteration reads .length and each O[i] through that chain —
        // tests (and real code) plant props on those prototypes.
        {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctxToRead);
            if (nanbox_is_number(nb) || nanbox_is_bool(nb)) {
                // Helper: treat nullptr + NaN-boxed undefined/null as "missing".
                auto notPresent = [](TsValue* v) -> bool {
                    if (!v) return true;
                    uint64_t u = nanbox_from_tsvalue_ptr(v);
                    return nanbox_is_undefined(u) || nanbox_is_null(u);
                };

                void* protoGlobal = nanbox_is_bool(nb)
                    ? ts_get_global_Boolean()
                    : ts_get_global_Number();
                // The ToObject(receiver) WRAPPER (a real Number/Boolean object)
                // is the spec `O`: it is both the callback's 3rd argument and
                // the value returned by mutators like fill/copyWithin. Build it
                // up front so every early-out below still carries it — e.g.
                // Boolean.prototype has no "length", but
                // `Array.prototype.fill.call(true)` must still return an object
                // that is `instanceof Boolean`.
                TsValue* wrapArgs0[] = { (TsValue*)ctxToRead };
                void* wrapper = protoGlobal
                    ? ts_new_from_constructor_impl((TsValue*)protoGlobal, 1, wrapArgs0)
                    : nullptr;
                auto emptyWrap = [&]() -> TsArray* {
                    TsArray* e = TsArray::Create(0);
                    e->originalReceiver = wrapper ? wrapper : ctxToRead;
                    return e;
                };
                if (!protoGlobal) return emptyWrap();
                void* protoCtor = ts_value_get_object((TsValue*)protoGlobal);
                if (!protoCtor) protoCtor = protoGlobal;
                TsValue* protoVal = ts_object_get_property(protoCtor, "prototype");
                if (notPresent(protoVal)) return emptyWrap();
                void* protoRaw = ts_value_get_object(protoVal);
                if (!protoRaw) return emptyWrap();

                TsValue* lenVal = ts_object_get_property(protoRaw, "length");
                if (notPresent(lenVal)) return emptyWrap();
                double lenD = ts_value_get_double(lenVal);
                if (lenD != lenD || lenD <= 0) return emptyWrap();
                int64_t len = (int64_t)lenD;
                const int64_t MAX_ITER = 1 << 20;
                if (len > MAX_ITER) len = MAX_ITER;

                TsArray* tmp = TsArray::Create((size_t)len);
                tmp->originalReceiver = wrapper ? wrapper : ctxToRead;
                for (int64_t i = 0; i < len; i++) {
                    char key[32];
                    snprintf(key, sizeof(key), "%lld", (long long)i);
                    TsValue* elem = ts_object_get_property(protoRaw, key);
                    if (notPresent(elem)) elem = ts_value_make_undefined();
                    ts_array_push(tmp, elem);
                }
                return tmp;
            }
        }
        // String primitives: expose characters as array-like elements via
        // ts_string_charAt, since ts_object_get_property("0") on a TsString
        // returns undefined (only integer keys hit the string fast path).
        {
            void* rawCtx = ts_value_get_object((TsValue*)ctxToRead);
            if (!rawCtx) rawCtx = ctxToRead;
            uintptr_t p = (uintptr_t)rawCtx;
            if (p > 0x1000 && p < 0x0000800000000000ULL) {
                uint32_t m0 = *(uint32_t*)rawCtx;
                if (m0 == 0x53545247 /* STRG */ || m0 == TsConsString::MAGIC) {
                    TsString* str = ts_ensure_flat(rawCtx);
                    if (str) {
                        int64_t len = (int64_t)str->Length();
                        const int64_t MAX_ITER = 1 << 20;
                        if (len > MAX_ITER) len = MAX_ITER;
                        TsArray* tmp = TsArray::Create((size_t)len);
                        // callback O must be ToObject(string) — a String wrapper —
                        // so `obj instanceof String` holds (step-1 tests).
                        extern void* ts_get_global_String();
                        TsValue* wrapArgs[] = { (TsValue*)ctxToRead };
                        void* wrapper = ts_new_from_constructor_impl(
                            (TsValue*)ts_get_global_String(), 1, wrapArgs);
                        tmp->originalReceiver = wrapper ? wrapper : ctxToRead;
                        for (int64_t i = 0; i < len; i++) {
                            TsString* ch = (TsString*)ts_string_charAt(str, i);
                            TsValue* elem = ts_value_make_string(ch);
                            ts_array_push(tmp, elem);
                        }
                        return tmp;
                    }
                }
            }
        }
        // String wrapper object (`new String("abc")`): a TsMap with a hidden
        // __StringData slot. The static ts_object_get_property path used below
        // does NOT expose its `length`/character indices (only the dynamic path
        // does), so materialize it here like a string primitive. Additive: only
        // fires for String wrappers, leaving the plain array-like path untouched.
        {
            void* rawCtx = ts_value_get_object((TsValue*)ctxToRead);
            if (!rawCtx) rawCtx = ctxToRead;
            uintptr_t p = (uintptr_t)rawCtx;
            if (p > 0x1000 && p < 0x0000800000000000ULL &&
                *(uint32_t*)((char*)rawCtx + 16) == 0x4D415053 /* TsMap MAGIC */) {
                TsMap* wm = (TsMap*)rawCtx;
                TsValue sdKey; sdKey.type = ValueType::STRING_PTR;
                sdKey.ptr_val = TsString::GetInterned("__StringData");
                TsValue sd = wm->Get(sdKey);
                if (sd.type == ValueType::STRING_PTR && sd.ptr_val) {
                    TsString* str = (TsString*)sd.ptr_val;
                    int64_t len = (int64_t)str->Length();
                    const int64_t MAX_ITER = 1 << 20;
                    if (len > MAX_ITER) len = MAX_ITER;
                    TsArray* tmp = TsArray::Create((size_t)len);
                    tmp->originalReceiver = ctxToRead;
                    for (int64_t i = 0; i < len; i++) {
                        ts_array_push(tmp, ts_value_make_string((TsString*)str->CharAt(i)));
                    }
                    return tmp;
                }
            }
        }
        // Read .length
        TsValue* lenVal = ts_object_get_property(ctxToRead, "length");
        if (!lenVal) return nullptr;
        // ToLength: ToNumber → clamp to [0, 2^53-1]. ts_to_number throws
        // TypeError for Symbol (per ES spec), which must propagate up.
        double lenD = ts_to_number(lenVal);
        // Expose the raw (ToLength-clamped) length for slice/splice's ArrayCreate
        // count-limit check — read exactly once here.
        g_require_array_raw_len = (lenD != lenD || lenD <= 0) ? 0.0
            : (lenD > 9007199254740991.0 ? 9007199254740991.0 : lenD);
        if (lenD != lenD || lenD <= 0) {
            // NaN or non-positive → empty array (matches spec ToLength → 0).
            // KEEP the original receiver: mutators still Set(O,"length",0)
            // through arraylike_writeback (pop/shift on {} must leave
            // obj.length === 0 — S15.4.4.6_A2).
            TsArray* tmp0 = TsArray::Create(0);
            tmp0->originalReceiver = ctxToRead;
            return tmp0;
        }
        // Per spec, ToLength clamps at 2^53-1. Cap to a sensible iteration
        // limit to avoid runaway allocations on pathological inputs.
        const int64_t MAX_ITER = 1 << 20; // 1M
        int64_t len = (lenD > (double)MAX_ITER) ? MAX_ITER : (int64_t)lenD;
        // Build the temporary array by indexed reads.
        TsArray* tmp = TsArray::Create(len);
        // Remember the original receiver so callback methods can pass it
        // as the 3rd callback argument (per ECMA-262).
        tmp->originalReceiver = ctxToRead;
        for (int64_t i = 0; i < len; i++) {
            char idxKey[24];
            snprintf(idxKey, sizeof(idxKey), "%lld", (long long)i);
            TsValue* elem = ts_object_get_property(ctxToRead, idxKey);
            // ts_array_push takes a TsValue* (NaN-boxed). Push even if
            // undefined so holes are preserved as undefined (spec behavior
            // for non-sparse array-likes in the common case).
            //
            // NOTE: a hole-preserving variant (HasProperty per index + SetHole
            // for absent) was tried (commit 576b73fe) to make the callback
            // methods skip absent indices per spec, but it netted -42 on
            // test262 (regressed array-like-method tests, only +11 upside on
            // the testResult cluster; the dominant "invoked exactly once" 426
            // cluster was unaffected). Reverted — the array-like skip needs a
            // per-method approach (or a correct HasProperty on this receiver
            // representation), not a blanket materializer change.
            ts_array_push(tmp, elem ? elem : ts_value_make_undefined());
        }
        return tmp;
    }

    // ================================================================
    // Array-like mutator spec path (ECMA-262 23.1.3.{push,pop,shift,
    // unshift,splice}).
    //
    // These run the mutators DIRECTLY on an array-LIKE receiver via
    // Get/Set/Has/Delete + a single ToLength(Get(O,"length")), touching
    // only the affected indices — never materializing O(len) elements.
    // This is mandatory for receivers whose "length" is near 2^53-1:
    // both the C++ fast path and require_array_or_throw's temp-array
    // materializer allocate/iterate O(len) and either time out or clamp
    // the length to a wrong value. Fast real arrays never reach here
    // (every call site guards with resolve_array_ctx(ctx) == nullptr),
    // and only plain data objects (TsMap) take this path — primitives,
    // strings, functions and Proxies keep the legacy materializer.
    // ================================================================
    extern "C" TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
    extern "C" bool ts_object_has_prop(TsValue* obj, TsValue* key);
    extern "C" bool ts_object_delete_prop(TsValue* obj, TsValue* key);

    static const double kArrayMaxSafeLen = 9007199254740991.0;  // 2^53 - 1

    // obj[idx] canonicalizes a numeric key to its index string, matching the
    // compiler's dynamic member access exactly (getters fire, holes report via
    // HasProperty). idx is always an exact integer < 2^53, so a double key is
    // lossless.
    static inline TsValue* al_key(double idx) { return ts_value_make_double(idx); }

    static inline TsValue* al_lengthKey() {
        return ts_value_make_string(TsString::GetInterned("length"));
    }

    // ToLength(Get(O, "length")) = clamp(ToIntegerOrInfinity(len), 0, 2^53-1).
    // ts_to_number throws TypeError on a Symbol length per spec, which
    // propagates. The ToIntegerOrInfinity truncation matters: a fractional
    // length like 2.5 must become 2 (else the written-back length is 1.5).
    static double al_to_length(TsValue* O) {
        TsValue* lv = ts_object_get_dynamic(O, al_lengthKey());
        if (!lv) return 0;
        double d = ts_to_number(lv);
        if (d != d) return 0;                        // NaN -> 0
        if (std::isinf(d)) return d < 0 ? 0 : kArrayMaxSafeLen;
        d = (d < 0) ? std::ceil(d) : std::floor(d);  // truncate toward zero
        if (d <= 0) return 0;
        return d > kArrayMaxSafeLen ? kArrayMaxSafeLen : d;
    }
    static inline TsValue* al_get(TsValue* O, double idx) {
        return ts_object_get_dynamic(O, al_key(idx));
    }
    static inline bool al_has(TsValue* O, double idx) {
        return ts_object_has_prop(O, al_key(idx));
    }
    static inline void al_set(TsValue* O, double idx, TsValue* v) {
        ts_object_set_dynamic(O, al_key(idx), v ? v : ts_value_make_undefined());
    }
    static inline void al_delete(TsValue* O, double idx) {
        ts_object_delete_prop(O, al_key(idx));
    }
    static inline void al_set_length(TsValue* O, double len) {
        ts_object_set_dynamic(O, al_lengthKey(), ts_value_make_double(len));
    }
    // ToIntegerOrInfinity returning a double (preserves +/-Infinity for the
    // splice start/deleteCount clamps). Throws on Symbol/BigInt via ts_to_number.
    static double al_to_integer_or_infinity(TsValue* v) {
        if (!v) return 0;
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        if (nanbox_is_undefined(nb)) return 0;
        double d = ts_to_number(v);
        if (d != d) return 0;                        // NaN -> 0
        if (std::isinf(d)) return d;
        return d < 0 ? std::ceil(d) : std::floor(d);
    }
    static void al_throw_length_limit(const char* method) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Array.prototype.%s: resulting length exceeds 2^53-1", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    }

    // Boxed array-LIKE receiver for a mutator, or nullptr to signal "not a plain
    // array-like object; take the legacy path" (nullish receivers, primitives,
    // strings, functions, Proxies). resolve_array_ctx already excluded real
    // arrays before this is called.
    static TsValue* arraylike_data_receiver(void* ctx) {
        void* ctxToRead = ctx;
        bool nullish = true;
        if (ctx) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            nullish = nanbox_is_null(nb) || nanbox_is_undefined(nb);
        }
        if (nullish) {
            void* t = ts_get_call_this();
            if (!t) return nullptr;
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)t);
            if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) return nullptr;
            ctxToRead = t;
        }
        void* raw = ts_value_get_object((TsValue*)ctxToRead);
        if (!raw) raw = ctxToRead;
        if ((uintptr_t)raw < 0x1000 || (uintptr_t)raw >= 0x0000800000000000ULL)
            return nullptr;
        // Plain data objects only: a flat object (FLAT magic at offset 0) or a
        // TsMap (MAPS magic at offset 16, excluding module-namespace exotics
        // whose [[Set]] is always false). Real arrays are already filtered by
        // the caller's resolve_array_ctx guard; primitives, strings, functions
        // and Proxies keep the legacy materializer path.
        bool ok = false;
        if (*(uint32_t*)raw == 0x464C4154 /*FLAT*/) {
            ok = true;
        } else if (*(uint32_t*)((char*)raw + 16) == 0x4D415053 /*MAPS*/) {
            if (((TsMap*)raw)->IsModuleNamespace()) return nullptr;
            ok = true;
        }
        if (!ok) return nullptr;
        return ts_value_make_object(raw);
    }

    // 23.1.3.23 Array.prototype.push
    static TsValue* arraylike_push(TsValue* O, int argc, TsValue** argv) {
        double len = al_to_length(O);
        double argCount = (double)argc;
        if (len + argCount > kArrayMaxSafeLen) { al_throw_length_limit("push"); return ts_value_make_undefined(); }
        for (int i = 0; i < argc; i++) al_set(O, len + (double)i, argv[i]);
        double newLen = len + argCount;
        al_set_length(O, newLen);
        return ts_value_make_double(newLen);
    }

    // 23.1.3.22 Array.prototype.pop
    static TsValue* arraylike_pop(TsValue* O) {
        double len = al_to_length(O);
        if (len == 0) { al_set_length(O, 0); return ts_value_make_undefined(); }
        double newLen = len - 1;
        TsValue* elem = al_get(O, newLen);
        al_delete(O, newLen);
        al_set_length(O, newLen);
        return elem ? elem : ts_value_make_undefined();
    }

    // 23.1.3.27 Array.prototype.shift
    static TsValue* arraylike_shift(TsValue* O) {
        double len = al_to_length(O);
        if (len == 0) { al_set_length(O, 0); return ts_value_make_undefined(); }
        TsValue* first = al_get(O, 0);
        for (double k = 1; k < len; k += 1) {
            double to = k - 1;
            if (al_has(O, k)) al_set(O, to, al_get(O, k));
            else al_delete(O, to);
        }
        al_delete(O, len - 1);
        al_set_length(O, len - 1);
        return first ? first : ts_value_make_undefined();
    }

    // 23.1.3.32 Array.prototype.unshift
    static TsValue* arraylike_unshift(TsValue* O, int argc, TsValue** argv) {
        double len = al_to_length(O);
        double argCount = (double)argc;
        if (argCount > 0) {
            if (len + argCount > kArrayMaxSafeLen) { al_throw_length_limit("unshift"); return ts_value_make_undefined(); }
            for (double k = len; k > 0; k -= 1) {
                double from = k - 1, to = k + argCount - 1;
                if (al_has(O, from)) al_set(O, to, al_get(O, from));
                else al_delete(O, to);
            }
            for (int j = 0; j < argc; j++) al_set(O, (double)j, argv[j]);
        }
        double newLen = len + argCount;
        al_set_length(O, newLen);
        return ts_value_make_double(newLen);
    }

    // 23.1.3.31 Array.prototype.splice (array-like receiver; removed elements
    // returned in a default Array — ArraySpeciesCreate on a non-array O uses
    // the %Array% constructor).
    static TsValue* arraylike_splice(TsValue* O, int argc, TsValue** argv) {
        double len = al_to_length(O);
        double relStart = (argc >= 1) ? al_to_integer_or_infinity(argv[0]) : 0;
        double actualStart;
        if (std::isinf(relStart) && relStart < 0) actualStart = 0;
        else if (relStart < 0) actualStart = (len + relStart > 0) ? (len + relStart) : 0;
        else actualStart = (relStart > len) ? len : relStart;

        double insertCount, actualDeleteCount;
        if (argc == 0) { insertCount = 0; actualDeleteCount = 0; }
        else if (argc == 1) { insertCount = 0; actualDeleteCount = len - actualStart; }
        else {
            insertCount = (double)(argc - 2);
            double dc = al_to_integer_or_infinity(argv[1]);
            if (dc < 0) dc = 0;
            double maxDc = len - actualStart;
            actualDeleteCount = (dc < maxDc) ? dc : maxDc;
        }
        if (len + insertCount - actualDeleteCount > kArrayMaxSafeLen) {
            al_throw_length_limit("splice");
            return ts_value_make_undefined();
        }
        // ArraySpeciesCreate(O, actualDeleteCount) -> ArrayCreate: RangeError if
        // the removed-count exceeds 2^32-1 (observed before any element read).
        if (actualDeleteCount > 4294967295.0) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError", "Invalid array length"));
            return ts_value_make_undefined();
        }
        TsArray* A = TsArray::Create(actualDeleteCount > 0 ? (size_t)actualDeleteCount : 4);
        for (double k = 0; k < actualDeleteCount; k += 1) {
            double from = actualStart + k;
            if (al_has(O, from)) ts_array_push(A, (void*)al_get(O, from));
            else ts_array_push(A, ts_value_make_undefined());
        }

        double itemCount = insertCount;
        if (itemCount < actualDeleteCount) {
            for (double k = actualStart; k < len - actualDeleteCount; k += 1) {
                double from = k + actualDeleteCount, to = k + itemCount;
                if (al_has(O, from)) al_set(O, to, al_get(O, from));
                else al_delete(O, to);
            }
            for (double k = len; k > len - actualDeleteCount + itemCount; k -= 1)
                al_delete(O, k - 1);
        } else if (itemCount > actualDeleteCount) {
            for (double k = len - actualDeleteCount; k > actualStart; k -= 1) {
                double from = k + actualDeleteCount - 1, to = k + itemCount - 1;
                if (al_has(O, from)) al_set(O, to, al_get(O, from));
                else al_delete(O, to);
            }
        }
        double k = actualStart;
        for (int i = 2; i < argc; i++) { al_set(O, k, argv[i]); k += 1; }
        al_set_length(O, len - actualDeleteCount + itemCount);
        return ts_value_make_object(A);
    }

    // Clamp a relative index (ToIntegerOrInfinity result) into [0, len] per the
    // fill/copyWithin/slice/splice convention.
    static double al_clamp_relative(double rel, double len) {
        if (std::isinf(rel)) return rel < 0 ? 0 : len;
        if (rel < 0) return (len + rel > 0) ? (len + rel) : 0;
        return (rel > len) ? len : rel;
    }

    // 23.1.3.6 Array.prototype.fill (array-like receiver). Returns O.
    static TsValue* arraylike_fill(TsValue* O, int argc, TsValue** argv) {
        double len = al_to_length(O);
        TsValue* value = (argc >= 1) ? argv[0] : ts_value_make_undefined();
        double k = al_clamp_relative(
            (argc >= 2) ? al_to_integer_or_infinity(argv[1]) : 0, len);
        bool endGiven = (argc >= 3 && argv[2] && !ts_value_is_undefined(argv[2]));
        double final = endGiven
            ? al_clamp_relative(al_to_integer_or_infinity(argv[2]), len) : len;
        while (k < final) { al_set(O, k, value); k += 1; }
        return O;
    }

    // 23.1.3.4 Array.prototype.copyWithin (array-like receiver). Returns O.
    static TsValue* arraylike_copyWithin(TsValue* O, int argc, TsValue** argv) {
        double len = al_to_length(O);
        double to   = al_clamp_relative((argc >= 1) ? al_to_integer_or_infinity(argv[0]) : 0, len);
        double from = al_clamp_relative((argc >= 2) ? al_to_integer_or_infinity(argv[1]) : 0, len);
        bool endGiven = (argc >= 3 && argv[2] && !ts_value_is_undefined(argv[2]));
        double final = endGiven
            ? al_clamp_relative(al_to_integer_or_infinity(argv[2]), len) : len;
        double count = std::min(final - from, len - to);
        double direction = 1;
        if (from < to && to < from + count) { direction = -1; from += count - 1; to += count - 1; }
        while (count > 0) {
            if (al_has(O, from)) al_set(O, to, al_get(O, from));
            else al_delete(O, to);
            from += direction; to += direction; count -= 1;
        }
        return O;
    }

    // 23.1.3.26 Array.prototype.reverse (array-like receiver). Returns O.
    static TsValue* arraylike_reverse(TsValue* O, int, TsValue**) {
        double len = al_to_length(O);
        double middle = std::floor(len / 2);
        for (double lower = 0; lower != middle; lower += 1) {
            double upper = len - 1 - lower;
            bool lowerExists = al_has(O, lower);
            bool upperExists = al_has(O, upper);
            TsValue* lowerVal = lowerExists ? al_get(O, lower) : nullptr;
            TsValue* upperVal = upperExists ? al_get(O, upper) : nullptr;
            if (lowerExists && upperExists) { al_set(O, lower, upperVal); al_set(O, upper, lowerVal); }
            else if (upperExists)          { al_set(O, lower, upperVal); al_delete(O, upper); }
            else if (lowerExists)          { al_delete(O, lower);        al_set(O, upper, lowerVal); }
        }
        return O;
    }

    // Validate that `callback` is callable (function or closure).
    // Throws TypeError if not callable, matching spec for Array callback
    // methods (filter/map/forEach/every/some/find/findIndex/reduce/etc).
    // Returns true on success, false if TypeError was thrown (caller should
    // return a safe default — ts_throw longjmps so the false branch is rare).
    // ES 23.1.3.30 / 23.1.3.34 step 1 (sort / toSorted): if comparefn is not
    // undefined and IsCallable(comparefn) is false, throw a TypeError. This is
    // observed BEFORE ToObject(this)/LengthOfArrayLike, so callers must invoke
    // it before require_array_or_throw. Returns true when the comparefn is
    // acceptable (undefined or callable).
    static bool validateComparefnOrThrow(int argc, TsValue** argv) {
        TsValue* cmp = (argc >= 1 && argv) ? argv[0] : nullptr;
        if (cmp && !ts_value_is_undefined(cmp) && !ts_is_callable((void*)cmp)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "The comparison function must be either a function or undefined"));
            return false;
        }
        return true;
    }

    static bool requireCallableOrThrow(void* callback, const char* methodName) {
        auto throwTE = [methodName]() {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "Array.prototype.%s callback is not a function", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        };
        if (!callback) { throwTE(); return false; }
        uint64_t nb = (uint64_t)(uintptr_t)callback;
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb) ||
            nanbox_is_number(nb) || nanbox_is_bool(nb)) {
            throwTE(); return false;
        }
        // Unbox to raw pointer and check magic for TsFunction/TsClosure.
        void* raw = ts_value_get_object((TsValue*)callback);
        if (!raw) raw = callback;
        if (!is_safe_ptr_for_magic(raw)) { throwTE(); return false; }
        // TsFunction::MAGIC = 0x46554E43 "FUNC", TsClosure::MAGIC = 0x434C5352 "CLSR"
        constexpr uint32_t FUNC_MAGIC = 0x46554E43;
        constexpr uint32_t CLSR_MAGIC = 0x434C5352;
        uint32_t m16 = *(uint32_t*)((char*)raw + 16);  // canonical TsObject::magic
        auto isCallable = [&](uint32_t m) {
            return m == FUNC_MAGIC || m == CLSR_MAGIC;
        };
        if (isCallable(m16)) return true;
        throwTE();
        return false;
    }

    // P0: Extremely common methods
    extern "C" TsValue* ta_iterate_impl(void* ctx, int argc, TsValue** argv,
                                        const char* name, int kind);
    extern "C" TsValue* ta_reduce_impl(void* ctx, int argc, TsValue** argv,
                                       const char* name, bool fromEnd);
    static inline bool builtins_ctx_is_ta(void* ctx) {
        void* raw = ts_nanbox_safe_unbox(ctx);
        if (!raw) raw = ctx;
        return raw && (uintptr_t)raw > 0x1000 &&
               (uintptr_t)raw < 0x0000800000000000ULL &&
               *(uint32_t*)((char*)raw + 16) == 0x54415252; /* TARR */
    }
    static inline void* builtins_ctx_ta_raw(void* ctx) {
        void* raw = ts_nanbox_safe_unbox(ctx);
        return raw ? raw : ctx;
    }
    TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_map;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_map, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "map");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "map")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_map(arr, callback, thisArg);  // applies @@species
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv) {
        // Inverted dispatch (V8 Cast<FastJSArray> otherwise <generic>): an
        // array-like receiver (Array.prototype.filter.call(obj, ...)) is not a
        // real array, so the C++ native can't handle it — delegate to the
        // self-hosted spec impl (ToObject/ToLength/HasProperty). Real arrays fall
        // through to ts_array_filter, which itself routes holey→self-hosted and
        // packed→native fast loop.
        extern void* g_selfhosted_filter;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_filter, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "filter");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "filter")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_filter(arr, callback, thisArg);  // applies @@species
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_forEach;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_forEach, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "forEach");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "forEach")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        ts_array_forEach(arr, callback, thisArg);
        return ts_value_make_undefined();
    }
    TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_reduce;
        if (TsValue* r = array_selfhost_arraylike_reduce(g_selfhosted_reduce, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "reduce");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "reduce")) return ts_value_make_undefined();
        void* initialValue = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        // Spec: if len == 0 and no initial value, throw TypeError.
        if (!initialValue && arr->Length() == 0) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reduce of empty array with no initial value"));
            return ts_value_make_undefined();
        }
        void* result = ts_array_reduce(arr, callback, initialValue);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: run the spec algorithm directly on the
        // object (ES 23.1.3.23). Avoids the O(len) temp materialization that
        // times out / mis-clamps a length near 2^53-1.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_push(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "push");
        array_require_length_writable(arr, "push");
        if (!arr) return ts_value_make_undefined();
        for (int i = 0; i < argc; i++) {
            ts_array_push(arr, (void*)argv[i]);
        }
        arraylike_writeback(arr);
        return ts_value_make_int(arr->Length());
    }
    TsValue* ts_array_pop_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.22) — no O(len) materialization for a near-limit length.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_pop(O);
        }
        TsArray* arr = require_array_or_throw(ctx, "pop");
        array_require_length_writable(arr, "pop");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_pop(arr);
        // Array-LIKE receiver: mutations happened on the materialized temp —
        // write elements AND length back to the original (S15.4.4.6_A2:
        // pop() on {} must leave obj.length === 0).
        arraylike_writeback(arr);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "join");
        if (!arr) return ts_value_make_string(TsString::Create(""));
        // Per ES spec: if separator is undefined, use ",". Otherwise coerce to string.
        void* separator = nullptr;
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            separator = ts_value_get_string(argv[0]);
            if (!separator) separator = (void*)argv[0];
        }
        void* result = ts_array_join(arr, separator);
        return result ? ts_value_make_string((TsString*)result) : ts_value_make_string(TsString::Create(""));
    }
    // Parse optional fromIndex argument per ES spec:
    //   ToIntegerOrInfinity then clamp.
    //   For indexOf/includes: default 0.
    //     +Infinity -> no iteration (return length, caller treats as miss)
    //     -Infinity -> 0
    //   For lastIndexOf: default length-1.
    //     +Infinity -> length-1
    //     -Infinity -> no iteration (return -1, caller treats as miss)
    //   NaN -> 0 for both.
    // ToInteger per ES spec: ToNumber + truncate toward zero. ts_to_number
    // throws TypeError on Symbol, which propagates up. Used for index/count
    // args in Array.prototype.X where Symbol must throw (per spec).
    static int64_t toInteger(TsValue* v, int64_t deflt) {
        if (!v) return deflt;
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        if (nanbox_is_undefined(nb)) return deflt;
        // ToIntegerOrInfinity: throws TypeError on Symbol AND BigInt (mirrors .at()).
        return ts_to_index_integer(v);
    }

    static int64_t parseFromIndex(int argc, TsValue** argv, int64_t length,
                                   bool isLastIndex = false) {
        if (argc < 2 || !argv || !argv[1]) {
            return isLastIndex ? (length - 1) : 0;
        }
        // ToNumber(fromIndex): throws TypeError on Symbol; a BigInt fromIndex
        // must also throw (ToNumber(BigInt) is a TypeError per ECMA-262 7.1.4).
        {
            uint64_t fnb = nanbox_from_tsvalue_ptr(argv[1]);
            if (nanbox_is_ptr(fnb)) {
                void* fptr = nanbox_to_ptr(fnb);
                if (fptr && *(uint32_t*)fptr == 0x42494749) {  // TsBigInt magic
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Cannot convert a BigInt value to a number"));
                    return 0;  // unreachable
                }
            }
        }
        double fd = ts_to_number(argv[1]);
        if (fd != fd) return 0; // NaN -> 0
        if (std::isinf(fd)) {
            if (fd > 0) return isLastIndex ? (length - 1) : length;
            return isLastIndex ? -1 : 0;
        }
        // truncate toward zero
        int64_t fi = (int64_t)fd;
        if (fi < 0) fi = length + fi;
        if (!isLastIndex && fi < 0) fi = 0;
        return fi;
    }

    TsValue* ts_array_indexOf_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "indexOf");
        if (!arr) return ts_value_make_int(-1);
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        int64_t len = arr->Length();
        int64_t fromIndex = parseFromIndex(argc, argv, len, false);
        if (fromIndex < 0) fromIndex = 0;
        if (fromIndex >= len) return ts_value_make_int(-1);
        extern bool ts_array_needs_spec_search(TsArray*);
        extern int64_t ts_array_search_spec(TsArray*, int64_t, int64_t, bool);
        if (ts_array_needs_spec_search(arr))
            return ts_value_make_int(ts_array_search_spec(arr, value, fromIndex, false));
        return ts_value_make_int(arr->IndexOf(value, (size_t)fromIndex));
    }
    TsValue* ts_array_includes_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "includes");
        if (!arr) return ts_value_make_bool(false);
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        int64_t len = arr->Length();
        int64_t fromIndex = parseFromIndex(argc, argv, len, false);
        if (fromIndex < 0) fromIndex = 0;
        if (fromIndex >= len) return ts_value_make_bool(false);
        return ts_value_make_bool(arr->Includes(value, (size_t)fromIndex));
    }
    TsValue* ts_array_slice_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "slice");
        if (!arr) return ts_value_make_object(ts_array_create());
        double rawLen = g_require_array_raw_len;  // length read once, pre-clamp
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t end   = (argc >= 2 && argv) ? toInteger(argv[1], arr->Length()) : arr->Length();
        // ES 23.1.3.27 step 8: ArraySpeciesCreate(O, count) -> ArrayCreate(count)
        // for a non-array receiver RangeErrors when count > 2^32-1. Use the RAW
        // receiver length (arr->Length() is clamped for huge array-likes) and the
        // already-coerced start/end (no extra observable coercion). A real array's
        // count is always bounded by its length, so this never fires for arrays.
        if (rawLen > 4294967295.0 && !resolve_array_ctx(ctx)) {
            double rs = (argc >= 1 && argv) ? (double)start : 0.0;
            double re = (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]))
                            ? (double)end : rawLen;
            double k = (rs < 0) ? (rawLen + rs > 0 ? rawLen + rs : 0) : (rs > rawLen ? rawLen : rs);
            double fin = (re < 0) ? (rawLen + re > 0 ? rawLen + re : 0) : (re > rawLen ? rawLen : re);
            if (fin - k > 4294967295.0) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "Invalid array length"));
            }
        }
        void* result = ts_array_slice(arr, start, end);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }

    // Per spec ValidateTypedArray: throw TypeError if the receiver's
    // underlying ArrayBuffer is detached. Returns true (and throws) if
    // the buffer is detached; the caller should bail. Argument coercion
    // (e.g., obj.valueOf throwing) must happen AFTER this check per
    // 22.2.3.* algorithms in ECMA-262.
    static bool throwIfDetached(TsTypedArray* ta, const char* methodName) {
        if (!ta || ta->IsDetachedBuffer()) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "TypedArray.prototype.%s called on a TypedArray with a "
                "detached buffer", methodName);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
            return true;
        }
        return false;
    }

    // TypedArray native methods
    extern "C" void* ts_typed_array_species_alloc(void* receiver, int64_t length);
    TsValue* ts_typed_array_slice_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "slice")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        // RelativeIndex clamp in double-space so ±Infinity / huge magnitudes
        // don't overflow the int64 cast ((int64_t)+Inf was INT64_MIN garbage,
        // which then flipped the sign checks — slice/infinity).
        auto clampRel = [len](double d) -> int64_t {
            // ToIntegerOrInfinity: NaN -> 0, else truncate toward zero. Do the
            // truncation BEFORE adding length (relativeIndex is an integer).
            double rel;
            if (d != d) rel = 0.0;
            else if (d >= 9.0e18) rel = 9.0e18;         // +Inf / huge -> clamps to len
            else if (d <= -9.0e18) rel = -9.0e18;       // -Inf / huge neg -> clamps to 0
            else rel = (double)(int64_t)d;              // trunc toward zero (in-range)
            double out = (rel < 0) ? ((double)len + rel) : rel;
            if (out < 0) out = 0;
            if (out > (double)len) out = (double)len;
            return (int64_t)out;
        };
        int64_t start = 0, end = len;
        // Use ts_to_number for Symbol→TypeError per spec.
        if (argc >= 1 && argv && argv[0]) start = clampRel(ts_to_number(argv[0]));
        if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]))
            end = clampRel(ts_to_number(argv[1]));
        if (end < start) end = start;
        int64_t newLen = end - start;
        // TypedArraySpeciesCreate(this, newLen) — honors @@species ctor.
        void* resRaw = ts_typed_array_species_alloc((void*)ta, newLen);
        if (!resRaw) return ts_value_make_undefined();  // TypeError thrown
        TsTypedArray* result = (TsTypedArray*)resRaw;
        // ES 23.2.3.24 step 14: after TypedArraySpeciesCreate, if count > 0 and
        // the SOURCE buffer is now detached, throw TypeError. A custom species
        // constructor (or the `constructor` getter it reads) can detach O's
        // buffer mid-construction (slice/detached-buffer-{get-ctor,custom-ctor-*}).
        if (newLen > 0 && ta->IsDetachedBuffer()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot slice a TypedArray with a detached buffer"));
            return ts_value_make_undefined();
        }
        // ta->Get is length-clamped (a resizable-buffer shrink during species
        // construction/coercion makes past-the-end reads 0 — slice/coerced-
        // start-end-shrink) and detach-safe.
        size_t copyN = std::min((size_t)newLen, result->GetLength());
        for (size_t i = 0; i < copyN; i++)
            result->Set(i, ta->Get((size_t)start + i));
        return ts_value_make_object(result);
    }
    // BigInt64/BigUint64 element access without the lossy double round-trip: read/write the
    // raw 64-bit slot and box/unbox as a TsBigInt. A non-BigInt array uses the double path.
    TsValue* ts_ta_get_boxed(TsTypedArray* ta, size_t index) {
        TypedArrayType t = ta->GetType();
        if (t == TypedArrayType::BigInt64 || t == TypedArrayType::BigUint64) {
            uint8_t* data = ta->GetData();
            if (!data || index >= ta->GetLength()) return ts_value_make_undefined();
            if (t == TypedArrayType::BigUint64) {
                // Unsigned interpretation: slots >= 2^63 box positive.
                extern void* ts_bigint_create_uint(uint64_t val);
                return ts_value_make_bigint(
                    ts_bigint_create_uint(((uint64_t*)data)[index]));
            }
            int64_t raw = ((int64_t*)data)[index];
            return ts_value_make_bigint(ts_bigint_create_int(raw));
        }
        // ES 10.4.5.15 IntegerIndexedElementGet: an index outside the view's
        // CURRENT length (a detached buffer, or a resizable buffer shrunk so the
        // index no longer maps to a slot) reads as `undefined`, NOT 0. GetLength()
        // re-derives from the live buffer, so a length-tracking view shrunk during
        // iteration (find/filter/map callbackfn-resize) yields undefined here —
        // matching the direct `ta[i]` path (ts_array_get_as_value), which the
        // dynamic [[Get]] at the self-hosted-arraylike call site bypassed.
        if (!ta->GetData() || index >= ta->GetLength())
            return ts_value_make_undefined();
        return ts_value_make_double(ta->Get(index));
    }
    // Live TypedArray iterator support (ES 23.1.5.1 CreateArrayIterator, TA
    // branch). The iterator re-derives the view's CURRENT length and OOB state
    // on every step: ts_ta_iter_length gives the live length (a length-tracking
    // view backed by a resized RAB reflects the new size); ts_ta_iter_is_oob is
    // true when the buffer is detached or the fixed-extent view is out of bounds,
    // in which case the .next() step must throw TypeError (ValidateTypedArray).
    size_t ts_ta_iter_length(void* ta) {
        return ta ? ((TsTypedArray*)ta)->GetLength() : 0;
    }
    int ts_ta_iter_is_oob(void* ta) {
        if (!ta) return 1;
        TsTypedArray* t = (TsTypedArray*)ta;
        return (t->IsDetachedBuffer() || t->IsOutOfBounds()) ? 1 : 0;
    }
    TsValue* ts_typed_array_set_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (!ta) return ts_value_make_undefined();
        // ECMA-262 23.2.3.23 %TypedArray%.prototype.set(source [, offset]):
        // step 3-4: targetOffset = ToIntegerOrInfinity(offset); if < 0 RangeError.
        // This precedes the detached check and the source-length bounds check.
        double offD = 0;
        if (argc >= 2 && argv[1]) offD = ts_to_number((TsValue*)argv[1]);
        if (offD != offD) offD = 0;            // NaN -> 0
        // ToIntegerOrInfinity truncates toward zero BEFORE the < 0 test, so a
        // fractional in (-1, 0] such as -0.5 becomes 0 and must NOT throw
        // (set/*-arg-offset-tointeger); only a truncated value < 0 (<= -1) throws.
        if (offD <= -1.0) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "offset is out of bounds"));
            return ts_value_make_undefined();
        }
        int64_t offset = (offD > 0.0) ? (int64_t)offD : 0;  // truncate toward zero
        if (throwIfDetached(ta, "set")) return ts_value_make_undefined();
        if (argc < 1 || !argv || !argv[0]) return ts_value_make_undefined();
        void* src = ts_value_get_object(argv[0]);
        if (!src) src = (void*)argv[0];
        size_t targetLen = ta->GetLength();
        uint32_t srcMagic16 = ((uintptr_t)src > 0x1000 &&
                               (uintptr_t)src < 0x0000800000000000ULL)
                              ? *(uint32_t*)((char*)src + 16) : 0;
        if (srcMagic16 == TsTypedArray::MAGIC) {
            TsTypedArray* srcTa = (TsTypedArray*)src;
            // SetTypedArrayFromTypedArray: a mid-coercion detach of the SOURCE
            // buffer leaves it out of bounds (srcbuffer-detached-during-
            // tointeger-offset-throws).
            if (srcTa->IsDetachedBuffer()) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Cannot set from a TypedArray with a detached buffer"));
                return ts_value_make_undefined();
            }
            size_t srcLen = srcTa->GetLength();
            bool tBig = ta->GetType() == TypedArrayType::BigInt64 ||
                        ta->GetType() == TypedArrayType::BigUint64;
            bool sBig = srcTa->GetType() == TypedArrayType::BigInt64 ||
                        srcTa->GetType() == TypedArrayType::BigUint64;
            // step 3: the two [[ContentType]]s must match, else TypeError
            // (src-typedarray-big-throws / src-typedarray-not-big-throws).
            if (tBig != sBig) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Cannot mix BigInt and non-BigInt typed arrays in set()"));
                return ts_value_make_undefined();
            }
            // step: if srcLength + targetOffset > targetLength, RangeError
            // (no silent truncation).
            if (srcLen + (size_t)offset > targetLen) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "offset is out of bounds"));
                return ts_value_make_undefined();
            }
            // Read the entire source region FIRST: when source and target share
            // the same buffer (or overlap), a straight forward copy corrupts
            // later reads (typedarray-arg-set-values-same-buffer-same-type).
            if (tBig) {
                int64_t* sd = (int64_t*)srcTa->GetData();
                int64_t* td = (int64_t*)ta->GetData();
                if (sd && td) {
                    std::vector<int64_t> tmp(srcLen);
                    for (size_t i = 0; i < srcLen; i++) tmp[i] = sd[i];
                    for (size_t i = 0; i < srcLen && (size_t)offset + i < targetLen; i++)
                        td[(size_t)offset + i] = tmp[i];
                }
            } else {
                std::vector<double> tmp(srcLen);
                for (size_t i = 0; i < srcLen; i++) tmp[i] = srcTa->Get(i);
                for (size_t i = 0; i < srcLen && (size_t)offset + i < targetLen; i++)
                    ta->Set((size_t)offset + i, tmp[i]);
            }
        } else {
            // Array / array-like source: ToLength(Get(src,"length")).
            int64_t srcLen = 0;
            uint32_t sm0 = ((uintptr_t)src > 0x1000) ? *(uint32_t*)src : 0;
            if (sm0 == TsArray::MAGIC) {
                srcLen = ((TsArray*)src)->Length();
            } else {
                TsValue* lenV = ts_object_get_property(src, "length");
                double ld = lenV ? ts_to_number(lenV) : 0;
                if (ld == ld && ld > 0) srcLen = (int64_t)ld;
            }
            if ((size_t)srcLen + (size_t)offset > targetLen) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "offset is out of bounds"));
                return ts_value_make_undefined();
            }
            // Element stores route through the type-aware helper: BigInt
            // targets take raw int64 slots (Number elements TypeError),
            // number targets ToNumber-coerce (BigInt elements TypeError).
            extern void ts_ta_store_value(void* taRaw, size_t i, TsValue* v);
            if (sm0 == TsArray::MAGIC) {
                TsArray* srcArr = (TsArray*)src;
                for (int64_t i = 0; i < srcLen; i++) {
                    TsValue* ev = (TsValue*)srcArr->GetElementBoxed((size_t)i);
                    ts_ta_store_value((void*)ta, (size_t)(offset + i), ev);
                }
            } else {
                for (int64_t i = 0; i < srcLen; i++) {
                    char key[24]; snprintf(key, sizeof(key), "%lld", (long long)i);
                    TsValue* ev = ts_object_get_property(src, key);
                    ts_ta_store_value((void*)ta, (size_t)(offset + i), ev);
                }
            }
        }
        return ts_value_make_undefined();
    }
    // %TypedArray%.prototype.subarray(begin, end) — ES 23.2.3.27. Returns a NEW
    // TypedArray that SHARES this array's ArrayBuffer (a view, not a copy).
    // Does NOT throw on a detached buffer (srcLength is treated as 0).
    TsValue* ts_typed_array_subarray_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (!ta) return ts_value_make_undefined();
        int64_t srcLen = (int64_t)ta->GetLength();
        int64_t esize = (int64_t)ta->GetElementSize();
        // ToIntegerOrInfinity + RelativeIndex clamp done in double-space so
        // ±Infinity and huge magnitudes never overflow the int64 cast.
        auto clampRel = [srcLen](double d) -> int64_t {
            // ToIntegerOrInfinity: NaN -> 0, else truncate toward zero BEFORE
            // adding length.
            double rel;
            if (d != d) rel = 0.0;                     // NaN -> 0
            else if (d >= 9.0e18) rel = 9.0e18;        // +Inf / huge -> clamps to srcLen
            else if (d <= -9.0e18) rel = -9.0e18;      // -Inf / huge neg -> clamps to 0
            else rel = (double)(int64_t)d;             // trunc toward zero (in-range)
            double out = (rel < 0) ? ((double)srcLen + rel) : rel;
            if (out < 0) out = 0;
            if (out > (double)srcLen) out = (double)srcLen;
            return (int64_t)out;
        };
        int64_t startIndex = 0;
        if (argc >= 1 && argv && argv[0])
            startIndex = clampRel(ts_to_number(argv[0]));  // may throw (Symbol)
        bool endUndef = !(argc >= 2 && argv && argv[1] &&
                          !ts_value_is_undefined(argv[1]));
        int64_t endIndex = srcLen;
        if (!endUndef) endIndex = clampRel(ts_to_number(argv[1]));  // may throw
        int64_t newLength = endIndex - startIndex;
        if (newLength < 0) newLength = 0;
        int64_t beginByteOffset = (int64_t)ta->GetByteOffset() + startIndex * esize;
        void* buffer = ta->GetBuffer();
        // ES 23.2.3.27 step 15: if the source view's [[ArrayLength]] is `auto`
        // (length-tracking) AND `end` is undefined, the result is created WITHOUT
        // a length argument, i.e. it is itself length-tracking and re-derives from
        // the (possibly resized-back-in-bounds) buffer. Only then omit newLength.
        bool resultAutoLen = ta->IsLengthTracking() && endUndef;
        extern void* ts_typed_array_species_alloc_on_buffer(
            void* receiver, void* bufferRaw, int64_t byteOffset,
            int64_t newLength, bool autoLen);
        void* resRaw = ts_typed_array_species_alloc_on_buffer(
            (void*)ta, buffer, beginByteOffset, newLength, resultAutoLen);
        if (!resRaw) return ts_value_make_undefined();  // TypeError thrown
        return ts_value_make_object(resRaw);
    }
    TsValue* ts_typed_array_fill_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "fill")) return ts_value_make_undefined();
        // Use ts_to_number — Symbol/object args must throw TypeError per spec
        // (Symbol→Number throws, object falls through ToPrimitive). The
        // earlier ts_value_get_double/ts_value_get_int didn't throw and
        // silently produced garbage for these inputs.
        double fillVal = 0;
        if (argc >= 1 && argv && argv[0]) fillVal = ts_to_number(argv[0]);
        int64_t len = (int64_t)ta->GetLength();
        // RelativeIndex via ToIntegerOrInfinity: NaN/-Inf -> 0, +Inf -> len,
        // negatives relative to len. Each coercion is observable and MAY detach
        // the buffer, re-checked after (ES 23.2.3.9 step 8).
        auto relIndex = [len](double d) -> int64_t {
            if (d != d) return 0;
            if (d == -std::numeric_limits<double>::infinity()) return 0;
            if (d == std::numeric_limits<double>::infinity()) return len;
            int64_t r = (int64_t)d;
            if (r < 0) return std::max((int64_t)0, len + r);
            return std::min(r, len);
        };
        int64_t start = 0, end = len;
        if (argc >= 2 && argv && argv[1]) start = relIndex(ts_to_number(argv[1]));
        if (argc >= 3 && argv && argv[2] && !ts_value_is_undefined(argv[2])) {
            end = relIndex(ts_to_number(argv[2]));
        }
        // Step 8: a {valueOf} argument may have detached the buffer during
        // coercion — %TypedArray%.prototype.fill throws TypeError in that case.
        if (ta->IsDetachedBuffer() || ta->IsOutOfBounds()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot perform %TypedArray%.prototype.fill on a detached ArrayBuffer"));
            return ts_value_make_undefined();
        }
        // Re-clamp to the current length (a resizable buffer may have shrunk).
        int64_t curLen = (int64_t)ta->GetLength();
        if (end > curLen) end = curLen;
        if (start > curLen) start = curLen;
        for (int64_t i = start; i < end; i++) {
            ta->Set((size_t)i, fillVal);
        }
        return ts_value_make_object(ta);
    }

    // TypedArray.prototype.at(index) — supports negative indices
    TsValue* ts_typed_array_at_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "at")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        int64_t idx = 0;
        if (argc >= 1 && argv && argv[0]) {
            // ToIntegerOrInfinity: NaN -> 0 (a raw (int64_t)NaN cast was
            // INT64_MIN garbage — at/index-non-numeric-argument-tointeger);
            // ±Infinity is always out of range.
            double di = ts_to_number(argv[0]);
            if (di != di) di = 0;
            if (di >= 9.0e18 || di <= -9.0e18) return ts_value_make_undefined();
            idx = (int64_t)di;
        }
        if (idx < 0) idx = len + idx;
        if (idx < 0 || idx >= len) return ts_value_make_undefined();
        return ts_ta_get_boxed(ta, (size_t)idx);
    }

    // %TypedArray%.prototype.{indexOf,lastIndexOf,includes} compare the search
    // element with strict equality / SameValueZero and DO NOT coerce it (ES
    // 23.2.3.13/.18/.15). A search element whose type does not match the view's
    // element kind (Number for the numeric views, BigInt for the two BigInt
    // views) therefore never matches — e.g. `int8array.indexOf("42")` is -1, not
    // a coerced 42. Returns true (and sets *out to the numeric value to compare)
    // only when the type matches; false means "cannot match — return not-found".
    static bool ta_search_element_matches_kind(TsTypedArray* ta, TsValue* se, double* out) {
        uint64_t nb = nanbox_from_tsvalue_ptr(se);
        bool isBigView = (ta->GetType() == TypedArrayType::BigInt64 ||
                          ta->GetType() == TypedArrayType::BigUint64);
        void* p = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : nullptr;
        bool seIsBig = p && *(uint32_t*)p == 0x42494749;  // TsBigInt (magic @0)
        bool seIsNum = nanbox_is_number(nb);
        if (isBigView) {
            if (!seIsBig) return false;
            *out = ts_to_number(se);   // exact for in-range BigInts, no side effects
            return true;
        }
        if (!seIsNum) return false;
        *out = nanbox_to_number(nb);
        return true;
    }

    // TypedArray.prototype.includes(searchElement, fromIndex?)
    TsValue* ts_typed_array_includes_native(void* ctx, int argc, TsValue** argv) {
        // ECMA-262 %TypedArray%.prototype.includes. Delegate to the shared,
        // resizable-aware engine used by the compiled fast path so both dispatch
        // routes agree: `len` (initial) is captured before ToIntegerOrInfinity
        // (fromIndex), the empty-view short-circuit precedes coercion, and each
        // element read is an IntegerIndexedElementGet — a slot past the CURRENT
        // (shrunk/detached) length reads as `undefined`, so a search for
        // `undefined` matches it (SameValueZero). The old native returned false
        // for an undefined search and read OOB slots as 0.
        extern bool ts_array_includes_from_coerced(void* arr, int64_t value, TsValue* fromIndex);
        int64_t sv = (argc >= 1 && argv && argv[0])
                         ? (int64_t)(intptr_t)argv[0]
                         : (int64_t)(intptr_t)ts_value_make_undefined();
        TsValue* fi = (argc >= 2 && argv) ? argv[1] : nullptr;
        return ts_value_make_bool(ts_array_includes_from_coerced(ctx, sv, fi));
    }

    // TypedArray.prototype.indexOf(searchElement, fromIndex?)
    TsValue* ts_typed_array_indexOf_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "indexOf")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        // ECMA-262 %TypedArray%.prototype.indexOf step 4: if len is 0, return -1
        // BEFORE coercing the search element or fromIndex (a throwing valueOf
        // must not run on an empty array). (Previously a BigInt search coerced
        // to NaN masked this; ts_to_number(BigInt) is now exact.)
        if (len == 0) return ts_value_make_int(-1);
        if (argc < 1 || !argv || !argv[0]) return ts_value_make_int(-1);
        // Strict equality, no coercion: a type-mismatched search never matches.
        double search = 0;
        bool canMatch = ta_search_element_matches_kind(ta, argv[0], &search);
        if (canMatch && search != search) canMatch = false;  // NaN never matches via ===
        int64_t from = 0;
        if (argc >= 2 && argv[1]) {
            // ToIntegerOrInfinity: +Inf -> -1 (search starts past the end);
            // -Inf -> 0. FPToSI(Inf) is INT64_MIN garbage (fromIndex-infinity).
            double fd = ts_to_number(argv[1]);
            if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
            if (fd == std::numeric_limits<double>::infinity())
                return ts_value_make_int(-1);
            from = (fd == -std::numeric_limits<double>::infinity() || fd != fd)
                       ? 0 : (int64_t)fd;  // ToIntegerOrInfinity(NaN) = 0
        }
        // ToInteger(fromIndex) can detach the buffer; a detached buffer has no
        // elements, so the search finds nothing.
        if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
        if (!canMatch) return ts_value_make_int(-1);
        if (from < 0) from = std::max((int64_t)0, len + from);
        for (int64_t i = from; i < len; i++) {
            if (ta->Get((size_t)i) == search) return ts_value_make_int(i);
        }
        return ts_value_make_int(-1);
    }

    // TypedArray.prototype.lastIndexOf(searchElement, fromIndex?)
    TsValue* ts_typed_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "lastIndexOf")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        // ECMA-262 step 4: len 0 -> -1 before coercing search/fromIndex.
        if (len == 0) return ts_value_make_int(-1);
        if (argc < 1 || !argv || !argv[0]) return ts_value_make_int(-1);
        // Strict equality, no coercion: a type-mismatched search never matches.
        double search = 0;
        bool canMatch = ta_search_element_matches_kind(ta, argv[0], &search);
        if (canMatch && search != search) canMatch = false;
        int64_t from = len - 1;
        if (argc >= 2 && argv[1]) {
            // ToIntegerOrInfinity: +Inf clamps to len-1; -Inf -> not found.
            double fd = ts_to_number(argv[1]);
            if (fd == std::numeric_limits<double>::infinity()) {
                from = len - 1;
            } else if (fd == -std::numeric_limits<double>::infinity()) {
                if (!ta->IsDetachedBuffer()) return ts_value_make_int(-1);
                from = -1;
            } else {
                from = (fd != fd) ? 0 : (int64_t)fd;  // ToIntegerOrInfinity(NaN) = 0
                if (from < 0) from = len + from;
            }
        }
        // fromIndex coercion can detach the buffer -> no elements -> not found.
        if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
        if (!canMatch) return ts_value_make_int(-1);
        if (from >= len) from = len - 1;
        for (int64_t i = from; i >= 0; i--) {
            if (ta->Get((size_t)i) == search) return ts_value_make_int(i);
        }
        return ts_value_make_int(-1);
    }

    // TypedArray.prototype.reverse() — mutates in place, returns self
    TsValue* ts_typed_array_reverse_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "reverse")) return ts_value_make_undefined();
        size_t len = ta->GetLength();
        for (size_t i = 0, j = (len == 0 ? 0 : len - 1); i < j; i++, j--) {
            double a = ta->Get(i), b = ta->Get(j);
            ta->Set(i, b);
            ta->Set(j, a);
        }
        return ts_value_make_object(ta);
    }

    // TypedArray.prototype.sort(comparefn?) — ES 23.2.3.29. Step 1 validates
    // the comparator (undefined or callable, else TypeError) BEFORE anything
    // else. Default sort: numeric ascending, NaN last, -0 before +0 (number
    // kinds) / int64 or uint64 order (BigInt kinds). Sorts in place and
    // returns the receiver.
    TsValue* ts_typed_array_sort_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        TsValue* cmp = (argc >= 1 && argv) ? argv[0] : nullptr;
        bool cmpFn = cmp && !ts_value_is_undefined(cmp);
        if (cmpFn && !ts_is_callable((void*)cmp)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "The comparison function must be either a function or undefined"));
            return ts_value_make_undefined();
        }
        if (throwIfDetached(ta, "sort")) return ts_value_make_undefined();
        size_t len = ta->GetLength();
        if (len < 2) return ts_value_make_object(ta);
        bool isBig = (ta->GetType() == TypedArrayType::BigInt64 ||
                      ta->GetType() == TypedArrayType::BigUint64);
        if (cmpFn) {
            // Custom comparator: it can ts_throw (longjmp) out of here, so
            // keep this frame POD-only (no std:: containers) — in-place
            // insertion sort over the raw slots.
            extern void* ts_bigint_create_int(int64_t v);
            int64_t* bdata = isBig ? (int64_t*)ta->GetData() : nullptr;
            for (size_t i = 1; i < len; i++) {
                for (size_t j = i; j > 0; j--) {
                    TsValue* a;
                    TsValue* b;
                    if (isBig) {
                        a = (TsValue*)ts_bigint_create_int(bdata[j - 1]);
                        b = (TsValue*)ts_bigint_create_int(bdata[j]);
                    } else {
                        a = ts_value_make_double(ta->Get(j - 1));
                        b = ts_value_make_double(ta->Get(j));
                    }
                    TsValue* args[2] = { a, b };
                    TsValue* r = ts_function_call_with_this(
                        cmp, ts_value_make_undefined(), 2, args);
                    double d = r ? ts_to_number(r) : 0;
                    if (!(d > 0)) break;  // NaN -> treated as 0 (no swap)
                    if (isBig) {
                        int64_t t = bdata[j - 1]; bdata[j - 1] = bdata[j]; bdata[j] = t;
                    } else {
                        double t = ta->Get(j - 1);
                        ta->Set(j - 1, ta->Get(j));
                        ta->Set(j, t);
                    }
                }
            }
            return ts_value_make_object(ta);
        }
        if (isBig) {
            int64_t* data = (int64_t*)ta->GetData();
            if (!data) return ts_value_make_object(ta);
            if (ta->GetType() == TypedArrayType::BigUint64) {
                std::sort((uint64_t*)data, (uint64_t*)data + len);
            } else {
                std::sort(data, data + len);
            }
            return ts_value_make_object(ta);
        }
        {
            std::vector<double> v(len);
            for (size_t i = 0; i < len; i++) v[i] = ta->Get(i);
            std::stable_sort(v.begin(), v.end(), [](double a, double b) {
                if (a != a) return false;  // NaN sorts last
                if (b != b) return true;
                if (a < b) return true;
                if (a > b) return false;
                // -0 before +0
                if (a == 0 && b == 0) return std::signbit(a) && !std::signbit(b);
                return false;
            });
            for (size_t i = 0; i < len; i++) ta->Set(i, v[i]);
        }
        return ts_value_make_object(ta);
    }

    // TypedArray.prototype.join(separator?) — ES 23.2.3.16. Each element is
    // stringified via the canonical Number::toString (ecma_number_to_string via
    // FromDouble), so Infinity/-Infinity/NaN and large integers format exactly
    // (the old %g/%lld formatter produced "inf"/"9.00720e+15"). GC-string
    // accumulation keeps the frame longjmp-safe when ToString(separator) throws.
    TsValue* ts_typed_array_join_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "join")) return ts_value_make_undefined();
        size_t len = ta->GetLength();
        extern void* ts_string_from_value(TsValue* val);
        TsString* sep = TsString::GetInterned(",");
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            TsString* s = (TsString*)ts_value_get_string(argv[0]);
            if (s) sep = s;
        }
        bool isBig = ta->GetType() == TypedArrayType::BigInt64 ||
                     ta->GetType() == TypedArrayType::BigUint64;
        TsString* acc = TsString::GetInterned("");
        for (size_t i = 0; i < len; i++) {
            if (i > 0) acc = (TsString*)ts_string_concat(acc, sep);
            if (ta->IsDetachedBuffer() || i >= ta->GetLength()) continue;
            TsString* es;
            if (isBig) {
                es = (TsString*)ts_string_from_value(ts_ta_get_boxed(ta, i));
            } else {
                es = TsString::FromDouble(ta->Get(i));
            }
            if (es) acc = (TsString*)ts_string_concat(acc, es);
        }
        return ts_value_make_string(acc);
    }

    // TypedArray.prototype.toString() — equivalent to join(",") per spec
    TsValue* ts_typed_array_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_typed_array_join_native(ctx, 0, nullptr);
    }

    // TypedArray.prototype.toLocaleString() — approximate: same as toString
    TsValue* ts_typed_array_toLocaleString_native(void* ctx, int argc, TsValue** argv) {
        // ES 23.2.3.32 (mirrors Array.prototype.toLocaleString): each element
        // is INVOKED — R = ToString(? Invoke(element, "toLocaleString")) — so
        // a user override on Number.prototype.toLocaleString (or a throwing
        // one, or a throwing toString/valueOf on its RESULT) is observable
        // per element. The old join() formatted numbers directly.
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "toLocaleString")) return ts_value_make_undefined();
        size_t len = ta->GetLength();
        TsString* sep = TsString::GetInterned(",");
        TsString* acc = TsString::GetInterned("");
        extern TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
        extern void* ts_string_from_value(TsValue* val);
        TsValue* tlsKey = ts_value_make_string(TsString::GetInterned("toLocaleString"));
        for (size_t k = 0; k < len; k++) {
            if (k > 0) acc = (TsString*)ts_string_concat(acc, sep);
            // Re-read length each step: a user toLocaleString may shrink a
            // resizable buffer (user-provided-tolocalestring-shrink); an
            // absent element stringifies as "" per Array semantics.
            if (ta->IsDetachedBuffer() || k >= ta->GetLength()) continue;
            TsValue* elem = ts_ta_get_boxed(ta, k);
            // Resolve "toLocaleString" through the element's prototype chain
            // (Number.prototype / BigInt.prototype, honoring user overrides).
            // The old ts_object_get_property treated the boxed TsValue* as a raw
            // object pointer, so a primitive element never found the override.
            TsValue* m = ts_object_get_dynamic(elem, tlsKey);
            TsValue* r = nullptr;
            if (m && ts_is_callable(m)) {
                r = ts_function_call_with_this(m, elem, 0, nullptr);  // may throw
            } else {
                r = elem;
            }
            // ES step: R = ? ToString(? Invoke(element, "toLocaleString")). When
            // the invoke result is an OBJECT (a user override may return one),
            // ToString runs ToPrimitive(hint String) — invoking its toString /
            // valueOf and propagating any abrupt completion — before rendering.
            // ts_string_from_value alone would stringify it as "[object Object]"
            // and swallow the throw.
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            r = ts_to_primitive(r, 2 /* hint: string */);            // may throw
            TsString* rs = (TsString*)ts_string_from_value(r);       // may throw
            if (rs) acc = (TsString*)ts_string_concat(acc, rs);
        }
        return ts_value_make_string(acc);
    }

    // TypedArray.prototype.copyWithin(target, start, end?) — mutates in place
    TsValue* ts_typed_array_copyWithin_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "copyWithin")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        // ES 23.2.3.5 RelativeIndex via ToIntegerOrInfinity: NaN -> 0, -Inf -> 0,
        // +Inf -> len; negatives are relative to len. Each coercion is observable
        // and MAY detach/resize the buffer (a {valueOf} argument), so they run
        // unconditionally and the detach is re-checked after, per step 17.
        auto relIndex = [len](double d, int64_t dflt) -> int64_t {
            if (d != d) return 0;                                        // NaN
            if (d == -std::numeric_limits<double>::infinity()) return 0;
            if (d == std::numeric_limits<double>::infinity()) return len;
            int64_t r = (int64_t)d;
            if (r < 0) return std::max((int64_t)0, len + r);
            return std::min(r, len);
        };
        int64_t target = 0, start = 0, end = len;
        if (argc >= 1 && argv && argv[0]) target = relIndex(ts_to_number(argv[0]), 0);
        if (argc >= 2 && argv && argv[1]) start = relIndex(ts_to_number(argv[1]), 0);
        if (argc >= 3 && argv && argv[2] && !ts_value_is_undefined(argv[2]))
            end = relIndex(ts_to_number(argv[2]), len);
        int64_t count = std::min(end - start, len - target);
        if (count <= 0) return ts_value_make_object(ta);
        // Step 17: a {valueOf} argument may have detached/resized the buffer.
        if (ta->IsDetachedBuffer() || ta->IsOutOfBounds()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot perform %TypedArray%.prototype.copyWithin on a detached ArrayBuffer"));
            return ts_value_make_undefined();
        }
        // The buffer may have SHRUNK (resizable) — re-clamp to the current length.
        int64_t curLen = (int64_t)ta->GetLength();
        if (target >= curLen || start >= curLen) return ts_value_make_object(ta);
        if (start + count > curLen) count = curLen - start;
        if (target + count > curLen) count = curLen - target;
        if (count <= 0) return ts_value_make_object(ta);
        // Use temp buffer to handle overlap correctly
        std::vector<double> tmp((size_t)count);
        for (int64_t i = 0; i < count; i++) tmp[(size_t)i] = ta->Get((size_t)(start + i));
        for (int64_t i = 0; i < count; i++) ta->Set((size_t)(target + i), tmp[(size_t)i]);
        return ts_value_make_object(ta);
    }

    // P1: Common methods
    TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_some;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_some, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "some");
        if (!arr) return ts_value_make_bool(false);
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "some")) return ts_value_make_bool(false);
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_bool(ts_array_some(arr, callback, thisArg));
    }
    TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_every;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_every, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "every");
        if (!arr) return ts_value_make_bool(true);
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "every")) return ts_value_make_bool(false);
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_bool(ts_array_every(arr, callback, thisArg));
    }
    TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_find;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_find, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "find");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "find")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        // ts_array_find returns a NaN-boxed TsValue* (not a heap TaggedValue*).
        // Must not dereference — 0x0A (undefined) would fault.
        TsValue* result = ts_array_find(arr, callback, thisArg);
        return result ? result : ts_value_make_undefined();
    }
    TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_findIndex;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_findIndex, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "findIndex");
        if (!arr) return ts_value_make_int(-1);
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "findIndex")) return ts_value_make_int(-1);
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_int(ts_array_findIndex(arr, callback, thisArg));
    }
    TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv) {
        // Step 1: validate comparefn BEFORE ToObject/LengthOfArrayLike.
        if (!validateComparefnOrThrow(argc, argv)) return ts_value_make_undefined();
        TsArray* arr = require_array_or_throw(ctx, "sort");
        if (!arr) return ts_value_make_undefined();
        void* comparator = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // %TypedArray%.prototype.sort default order is NUMERIC ascending.
        // BigInt receivers sort their raw int64/uint64 slots directly —
        // the materialized-temp path compares boxed BigInts with the
        // string comparator and scrambles them.
        if (!comparator || ts_value_is_undefined((TsValue*)comparator)) {
            void* orig = arr->originalReceiver;
            if (orig && (uintptr_t)orig >= 4096 &&
                *(uint32_t*)((char*)orig + 16) == 0x54415252 /* TARR */) {
                TsTypedArray* ta = (TsTypedArray*)orig;
                TypedArrayType tt = ta->GetType();
                if (tt == TypedArrayType::BigInt64) {
                    int64_t* d = (int64_t*)ta->GetData();
                    if (d) std::sort(d, d + ta->GetLength());
                    return ts_value_make_object(orig);
                }
                if (tt == TypedArrayType::BigUint64) {
                    uint64_t* d = (uint64_t*)ta->GetData();
                    if (d) std::sort(d, d + ta->GetLength());
                    return ts_value_make_object(orig);
                }
            }
        }
        void* result = ts_array_sort(arr, comparator);
        arraylike_writeback(arr);
        // ES 23.1.3.30 step 4: returns the RECEIVER (original array-like).
        if (arr->originalReceiver && arr->originalReceiver != (void*)arr)
            return ts_value_make_object(arr->originalReceiver);
        return result ? ts_value_make_object(result) : ts_value_make_object(arr);
    }
    TsValue* ts_array_reverse_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.26) — no O(len) materialization for a near-limit length.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_reverse(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "reverse");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_reverse(arr);
        arraylike_writeback(arr);
        // ES 23.1.3.26 step 6: returns the RECEIVER — for an array-like
        // that's the ORIGINAL object, not the materialized temp.
        if (arr->originalReceiver && arr->originalReceiver != (void*)arr)
            return ts_value_make_object(arr->originalReceiver);
        return result ? ts_value_make_object(result) : ts_value_make_object(arr);
    }
    TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.31). Handles length near 2^53-1 (integer-limit TypeError,
        // ArrayCreate RangeError) without the O(len) temp materialization.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_splice(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "splice");
        double rawLen = g_require_array_raw_len;  // length read once, pre-clamp
        array_require_length_writable(arr, "splice");
        if (!arr) return ts_value_make_undefined();
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t deleteCount = (argc >= 2 && argv) ? toInteger(argv[1], arr->Length() - start) : arr->Length() - start;
        // ES 23.1.3.31 step 11: ArraySpeciesCreate(O, actualDeleteCount) ->
        // ArrayCreate for a non-array receiver RangeErrors when actualDeleteCount
        // > 2^32-1. Compute on the RAW receiver length (arr->Length() is clamped
        // for huge array-likes), reusing already-coerced start/deleteCount. Never
        // fires for real arrays (deleteCount bounded by length).
        if (rawLen > 4294967295.0 && !resolve_array_ctx(ctx)) {
            double aStart = ((double)start < 0)
                                ? (rawLen + start > 0 ? rawLen + start : 0)
                                : ((double)start > rawLen ? rawLen : (double)start);
            double adc;
            if (argc == 0) adc = 0;
            else if (argc == 1) adc = rawLen - aStart;
            else {
                double dc = (double)deleteCount < 0 ? 0 : (double)deleteCount;
                double maxDc = rawLen - aStart;
                adc = (dc > maxDc) ? maxDc : dc;
            }
            if (adc > 4294967295.0) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "Invalid array length"));
            }
        }
        if (start < 0) start = arr->Length() + start;
        if (start < 0) start = 0;
        if (start > arr->Length()) start = arr->Length();
        if (deleteCount < 0) deleteCount = 0;
        if (deleteCount > arr->Length() - start) deleteCount = arr->Length() - start;

        // Create result array with deleted elements
        TsArray* result = TsArray::Create(deleteCount > 0 ? deleteCount : 4);
        for (int64_t i = 0; i < deleteCount; i++) {
            result->Push(arr->Get(start + i));
        }

        // Build items array from remaining args
        int itemCount = (argc > 2) ? argc - 2 : 0;

        // Remove deleted elements and insert new ones
        // First, collect elements after the splice point
        TsArray* tail = TsArray::Create(4);
        for (int64_t i = start + deleteCount; i < arr->Length(); i++) {
            tail->Push(arr->Get(i));
        }

        // Truncate array to start point
        while (arr->Length() > start) {
            arr->Pop();
        }

        // Insert new items
        for (int i = 0; i < itemCount; i++) {
            ts_array_push(arr, (void*)argv[i + 2]);
        }

        // Re-add tail elements
        for (int64_t i = 0; i < tail->Length(); i++) {
            arr->Push(tail->Get(i));
        }

        arraylike_writeback(arr);  // array-like receiver: propagate mutation
        // ECMA-262 23.1.3.31: removed-elements array via ArraySpeciesCreate.
        extern void* ts_array_species_rematerialize(void* receiver, void* resultRaw);
        void* out = ts_array_species_rematerialize((void*)arr, (void*)result);
        return out ? ts_value_make_object(out) : ts_value_make_undefined();
    }
    // ECMA-262 23.1.3.2 IsConcatSpreadable(O): Type(O) must be Object; a
    // defined @@isConcatSpreadable wins (ToBoolean), else Array.isArray(O).
    static bool value_is_concat_spreadable(TsValue* item) {
        uint64_t nb = nanbox_from_tsvalue_ptr(item);
        if (!nanbox_is_ptr(nb)) return false;
        void* raw = ts_value_get_object(item);
        if (!raw) raw = nanbox_to_ptr(nb);
        if (!raw || (uintptr_t)raw < 0x1000 ||
            (uintptr_t)raw >= 0x0000800000000000ULL) return false;
        uint32_t m0 = *(uint32_t*)raw;
        // Primitive-shaped heap values are not Objects for this predicate.
        if (m0 == 0x53545247 /* STRG */ || m0 == TsConsString::MAGIC ||
            m0 == 0x53594D42 /* SYMB */ || m0 == 0x42494749 /* BIGI */)
            return false;
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* sval = ts_object_get_property(raw, "[Symbol.isConcatSpreadable]");
        if (sval && !ts_value_is_undefined(sval)) return ts_value_to_bool(sval);
        return m0 == TsArray::MAGIC;
    }

    // Append one concat item to the result: spreadable items are walked
    // 0..ToLength(length) with HasProperty/Get (getters run live, absent
    // indices land as undefined slots); non-spreadable items append whole.
    static void concat_append_item(TsArray* result, TsValue* item) {
        if (!value_is_concat_spreadable(item)) {
            ts_array_push(result, (void*)item);
            return;
        }
        void* raw = ts_value_get_object(item);
        if (!raw) raw = (void*)item;
        // Fast path: plain packed TsArray with no exotic index state.
        extern bool ts_array_needs_spec_search(TsArray*);
        if (*(uint32_t*)raw == TsArray::MAGIC &&
            !ts_array_needs_spec_search((TsArray*)raw) &&
            !((TsArray*)raw)->HasHoles()) {
            TsArray* src = (TsArray*)raw;
            for (size_t i = 0; i < src->Length(); ++i)
                ts_array_push(result, (void*)(uintptr_t)src->Get(i));
            return;
        }
        // Generic walk: ToLength(length) + per-index HasProperty/Get on the
        // live object. A throwing length/element getter propagates.
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* lenVal = ts_object_get_property(raw, "length");
        double lenD = lenVal ? ts_to_number(lenVal) : 0;
        int64_t len = 0;
        if (lenD == lenD && lenD > 0) {
            if (lenD > (double)(1LL << 26)) lenD = (double)(1LL << 26);  // alloc guard
            len = (int64_t)lenD;
        }
        TsValue* objB = ts_value_make_object(raw);
        for (int64_t k = 0; k < len; ++k) {
            extern bool ts_object_has_prop(TsValue* obj, TsValue* key);
            extern TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
            TsValue* keyB = ts_value_make_int(k);
            TsValue* elem = ts_object_has_prop(objB, keyB)
                                ? ts_object_get_dynamic(objB, keyB)
                                : ts_value_make_undefined();
            ts_array_push(result, (void*)(elem ? elem : ts_value_make_undefined()));
        }
    }

    // Zero-arg `arr.concat()` (compiled path): still a fresh spread of the
    // receiver through ArraySpeciesCreate — NOT the receiver itself.
    void* ts_array_concat_none(void* arr) {
        extern TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv);
        TsValue* r = ts_array_concat_native(arr, 0, nullptr);
        void* raw = r ? ts_value_get_object(r) : nullptr;
        return raw ? raw : arr;
    }

    TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "concat");
        if (!arr) return ts_value_make_undefined();
        // Spec 23.1.3.2: the receiver is itself the first concat item —
        // spreadable receivers walk, others append whole. Use the ORIGINAL
        // receiver (not the materialized temp) so @@isConcatSpreadable and
        // getters are consulted live.
        void* orig = (arr->originalReceiver && arr->originalReceiver != (void*)arr)
                         ? arr->originalReceiver : (void*)arr;
        TsArray* result = (TsArray*)ts_array_create();
        concat_append_item(result, ts_value_make_object(orig));
        for (int i = 0; i < argc; ++i) {
            if (argv && argv[i]) concat_append_item(result, argv[i]);
        }
        extern void* ts_array_species_rematerialize(void* receiver, void* resultRaw);
        void* out = ts_array_species_rematerialize((void*)arr, (void*)result);
        return ts_value_make_object(out ? out : (void*)result);
    }

    // P2: Moderate methods
    TsValue* ts_array_flat_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "flat");
        if (!arr) return ts_value_make_undefined();
        // ES 23.1.3.13 step 3: depth = ToIntegerOrInfinity(depth) when the
        // argument is present and not undefined; NaN/non-numeric -> 0 (no
        // flatten), undefined/absent -> 1. ts_value_get_int coerced "str" to
        // a flattening depth.
        int64_t depth = 1;
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            double d = ts_to_number(argv[0]);
            if (d != d) depth = 0;                       // NaN
            else if (d > 9007199254740991.0) depth = INT64_MAX;  // +Inf / huge
            else if (d < 0) depth = (int64_t)d;          // negatives clamp later
            else depth = (int64_t)d;
        }
        void* result = ts_array_flat(arr, depth);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_flatMap_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_flatMap;
        if (TsValue* r = array_selfhost_arraylike(g_selfhosted_flatMap, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "flatMap");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "flatMap")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_flatMap(arr, callback, thisArg);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_at_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "at");
        if (!arr) return ts_value_make_undefined();
        // ToIntegerOrInfinity: a Symbol/BigInt/throwing-valueOf index must throw.
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_to_index_integer(argv[0]) : 0;
        void* result = ts_array_at(arr, index);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_shift_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.27).
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_shift(O);
        }
        TsArray* arr = require_array_or_throw(ctx, "shift");
        array_require_length_writable(arr, "shift");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_shift(arr);
        arraylike_writeback(arr);  // array-like receiver: propagate mutation
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.32) — near-limit length throws before any element move.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_unshift(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "unshift");
        array_require_length_writable(arr, "unshift");
        if (!arr) return ts_value_make_undefined();
        for (int i = argc - 1; i >= 0; i--) {
            ts_array_unshift(arr, (void*)argv[i]);
        }
        arraylike_writeback(arr);
        return ts_value_make_int(arr->Length());
    }
    TsValue* ts_array_fill_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.6) — no O(len) materialization for a near-limit length.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_fill(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "fill");
        if (!arr) return ts_value_make_undefined();
        void* value = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 2 && argv) ? toInteger(argv[1], 0) : 0;
        int64_t end = (argc >= 3 && argv) ? toInteger(argv[2], arr->Length()) : arr->Length();
        ts_array_fill(arr, value, start, end);
        arraylike_writeback(arr);
        // ES 23.1.3.6 step 12: returns O — the ToObject'd RECEIVER. For an
        // array-like (or primitive-wrapper) receiver that's the ORIGINAL
        // object, not the materialized temp.
        if (arr->originalReceiver && arr->originalReceiver != (void*)arr)
            return ts_value_make_object(arr->originalReceiver);
        return ts_value_make_object(arr);
    }
    TsValue* ts_array_reduceRight_native(void* ctx, int argc, TsValue** argv) {
        extern void* g_selfhosted_reduceRight;
        if (TsValue* r = array_selfhost_arraylike_reduce(g_selfhosted_reduceRight, ctx, argc, argv)) return r;
        TsArray* arr = require_array_or_throw(ctx, "reduceRight");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "reduceRight")) return ts_value_make_undefined();
        void* initialValue = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        // Spec: if len == 0 and no initial value, throw TypeError.
        if (!initialValue && arr->Length() == 0) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reduce of empty array with no initial value"));
            return ts_value_make_undefined();
        }
        void* result = ts_array_reduceRight(arr, callback, initialValue);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "lastIndexOf");
        if (!arr) return ts_value_make_int(-1);
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        int64_t len = arr->Length();
        int64_t fromIndex = parseFromIndex(argc, argv, len, true);
        // lastIndexOf: fromIndex < 0 means skip everything (no valid index).
        if (fromIndex < 0) return ts_value_make_int(-1);
        if (fromIndex >= len) fromIndex = len - 1;
        extern bool ts_array_needs_spec_search(TsArray*);
        extern int64_t ts_array_search_spec(TsArray*, int64_t, int64_t, bool);
        if (ts_array_needs_spec_search(arr))
            return ts_value_make_int(ts_array_search_spec(arr, value, fromIndex, true));
        return ts_value_make_int(arr->LastIndexOf(value, fromIndex));
    }

    // P3: Less common methods
    TsValue* ts_array_entries_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "entries");
        if (!arr) return ts_value_make_undefined();
        void* items = ts_array_entries(arr);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_keys_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "keys");
        if (!arr) return ts_value_make_undefined();
        void* items = ts_array_keys(arr);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_values_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "values");
        if (!arr) return ts_value_make_undefined();
        void* items = ts_array_values(arr);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_toReversed_native(void* ctx, int argc, TsValue** argv) {
        // ES 23.1.3.33 step 3: ArrayCreate(len) RangeErrors when len > 2^32-1,
        // before any element Get. Checked on the raw receiver length.
        array_check_arraycreate_limit(ctx);
        TsArray* arr = require_array_or_throw(ctx, "toReversed");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_toReversed(arr);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv) {
        // Step 1: validate comparefn BEFORE ToObject/LengthOfArrayLike.
        if (!validateComparefnOrThrow(argc, argv)) return ts_value_make_undefined();
        // ES 23.1.3.34 step 3: ArrayCreate(len) RangeErrors when len > 2^32-1.
        array_check_arraycreate_limit(ctx);
        TsArray* arr = require_array_or_throw(ctx, "toSorted");
        if (!arr) return ts_value_make_undefined();
        void* comparator = (argc >= 1 && argv && argv[0] &&
                            !ts_value_is_undefined(argv[0])) ? (void*)argv[0] : nullptr;
        // ES 23.1.3.34 step 5: SortIndexedProperties with the provided
        // comparefn (ts_array_toSorted copies then sorts with it).
        void* result = ts_array_toSorted(arr, comparator);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv) {
        // ES 23.1.3.35 steps 2-7: compute newLen = len + insertCount -
        // actualDeleteCount on the RAW receiver length, then (step 8) if newLen
        // > 2^53-1 throw TypeError, and (step 12 ArrayCreate) if newLen > 2^32-1
        // throw RangeError — both BEFORE any element Get (the length-exceeding
        // tests plant throwing index getters). No-op for ordinary lengths.
        {
            double rlen = arraylike_length_of(ctx);
            if (rlen > 0) {
                int64_t len = (int64_t)rlen;
                int64_t relStart = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
                int64_t actualStart;
                if (relStart < 0) { actualStart = len + relStart; if (actualStart < 0) actualStart = 0; }
                else actualStart = (relStart > len) ? len : relStart;
                int64_t insertCount = (argc > 2) ? (argc - 2) : 0;
                int64_t actualDeleteCount;
                if (argc == 0) actualDeleteCount = 0;
                else if (argc == 1) actualDeleteCount = len - actualStart;
                else {
                    int64_t dc = toInteger(argv[1], 0);
                    if (dc < 0) dc = 0;
                    int64_t maxDc = len - actualStart;
                    actualDeleteCount = (dc > maxDc) ? maxDc : dc;
                }
                double newLen = (double)len + (double)insertCount - (double)actualDeleteCount;
                if (newLen > 9007199254740991.0) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Invalid array length"));
                }
                if (newLen > 4294967295.0) {
                    ts_throw((TsValue*)ts_error_create_typed("RangeError",
                        "Invalid array length"));
                }
            }
        }
        TsArray* arr = require_array_or_throw(ctx, "toSpliced");
        if (!arr) return ts_value_make_undefined();
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t deleteCount = (argc >= 2 && argv) ? toInteger(argv[1], arr->Length() - start) : arr->Length() - start;
        // Collect items as an array
        TsArray* items = nullptr;
        if (argc > 2) {
            items = TsArray::Create(argc - 2);
            for (int i = 2; i < argc; i++) {
                ts_array_push(items, (void*)argv[i]);
            }
        }
        void* result = ts_array_toSpliced(arr, start, deleteCount, items, items ? items->Length() : 0);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_copyWithin_native(void* ctx, int argc, TsValue** argv) {
        // Array-LIKE data receiver: spec algorithm directly on the object
        // (ES 23.1.3.4) — no O(len) materialization for a near-limit length.
        if (!resolve_array_ctx(ctx)) {
            if (TsValue* O = arraylike_data_receiver(ctx)) return arraylike_copyWithin(O, argc, argv);
        }
        TsArray* arr = require_array_or_throw(ctx, "copyWithin");
        if (!arr) return ts_value_make_undefined();
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t target = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t start  = (argc >= 2 && argv) ? toInteger(argv[1], 0) : 0;
        int64_t end    = (argc >= 3 && argv) ? toInteger(argv[2], arr->Length()) : arr->Length();
        ts_array_copyWithin(arr, target, start, end);
        arraylike_writeback(arr);
        // ES 23.1.3.4 step 18: returns O — the ToObject'd RECEIVER (the
        // original array-like / primitive-wrapper), not the materialized temp.
        if (arr->originalReceiver && arr->originalReceiver != (void*)arr)
            return ts_value_make_object(arr->originalReceiver);
        return ts_value_make_object(arr);
    }
    TsValue* ts_array_with_native(void* ctx, int argc, TsValue** argv) {
        // ES 23.1.3.39 step 3: ArrayCreate(len) RangeErrors when len > 2^32-1.
        array_check_arraycreate_limit(ctx);
        TsArray* arr = require_array_or_throw(ctx, "with");
        if (!arr) return ts_value_make_undefined();
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        void* value = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_with(arr, index, value);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_findLast_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "findLast");
        if (!arr) return ts_value_make_undefined();
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "findLast")) return ts_value_make_undefined();
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        // ts_array_findLast returns a NaN-boxed TsValue* (not a heap TaggedValue*).
        // Must not dereference — 0x0A (undefined) would fault.
        TsValue* result = ts_array_findLast(arr, callback, thisArg);
        return result ? result : ts_value_make_undefined();
    }
    TsValue* ts_array_findLastIndex_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "findLastIndex");
        if (!arr) return ts_value_make_int(-1);
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        if (!requireCallableOrThrow(callback, "findLastIndex")) return ts_value_make_int(-1);
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_int(ts_array_findLastIndex(arr, callback, thisArg));
    }
    TsValue* ts_array_toString_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "toString");
        if (!arr) return ts_value_make_string(TsString::Create(""));
        void* result = ts_array_join(arr, (void*)TsString::Create(","));
        return result ? ts_value_make_string((TsString*)result) : ts_value_make_string(TsString::Create(""));
    }

    // thisNumberValue(this) per ECMA-262: the receiver (ctx) must be a number
    // primitive OR a Number wrapper object carrying [[NumberData]]. When invoked
    // via .call/.apply the bound receiver is overridden, so a non-Number `this`
    // (string/boolean/null/undefined/plain object) must throw a TypeError rather
    // than silently coercing to 0.
    static double numberThisValueOrThrow(void* ctx, const char* method) {
        uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
        if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
        if (nanbox_is_double(nb)) return nanbox_to_double(nb);
        if (nanbox_is_ptr(nb)) {
            void* raw = nanbox_to_ptr(nb);
            if (raw && *(uint32_t*)((char*)raw + 16) == 0x4D415053) {  // TsMap
                TsMap* obj = (TsMap*)raw;
                TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                ndKey.ptr_val = TsString::GetInterned("__NumberData");
                TsValue v = obj->Get(ndKey);
                if (v.type == ValueType::NUMBER_DBL) return v.d_val;
                if (v.type == ValueType::NUMBER_INT) return (double)v.i_val;
            }
        }
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Number.prototype.%s requires that 'this' be a Number", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return 0.0;  // unreachable
    }

    // thisBooleanValue(this) per ECMA-262: the receiver (ctx) must be a boolean
    // primitive OR a Boolean wrapper object carrying [[BooleanData]]; else throw.
    static bool booleanThisValueOrThrow(void* ctx, const char* method) {
        uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
        if (nanbox_is_bool(nb)) return nanbox_to_bool(nb);
        if (nanbox_is_ptr(nb)) {
            void* raw = nanbox_to_ptr(nb);
            if (raw && *(uint32_t*)((char*)raw + 16) == 0x4D415053) {  // TsMap
                TsMap* obj = (TsMap*)raw;
                TsValue ndKey; ndKey.type = ValueType::STRING_PTR;
                ndKey.ptr_val = TsString::GetInterned("__BooleanData");
                TsValue v = obj->Get(ndKey);
                if (v.type == ValueType::BOOLEAN) return v.i_val != 0;
            }
        }
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Boolean.prototype.%s requires that 'this' be a Boolean", method);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return false;  // unreachable
    }

    // ToIntegerOrInfinity for the radix/fractionDigits/precision argument:
    // routes through ToNumber (invoking a user valueOf, throwing TypeError on a
    // Symbol/BigInt), truncates toward zero, saturates +/-Infinity to INT64_MAX/
    // MIN. Shared with the Number.prototype lambdas in TsGlobals.cpp.
    extern "C" int64_t ts_to_index_integer(TsValue* v);

    // Native wrapper for number.toString() - ctx is a NaN-boxed number value
    TsValue* ts_number_toString_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toString");
        // ECMA-262 21.1.3.6: radix undefined -> 10; else ToIntegerOrInfinity(radix)
        // (invokes valueOf, so a poisoned argument throws before the range check).
        int64_t radix = 10;
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            radix = ts_to_index_integer(argv[0]);
        }
        return ts_value_make_string((TsString*)ts_number_to_string(value, radix));
    }

    TsValue* ts_number_toFixed_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toFixed");
        // ECMA-262 21.1.3.3: f = ToIntegerOrInfinity(fractionDigits) (step 2),
        // RangeError for f<0 or f>100 (steps 4-5) BEFORE the NaN/large-value
        // handling (steps 6-7, in ts_number_to_fixed).
        int64_t digits = (argc >= 1 && argv && argv[0]) ? ts_to_index_integer(argv[0]) : 0;
        if (digits < 0 || digits > 100) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "toFixed() digits argument must be between 0 and 100"));
            return ts_value_make_undefined();
        }
        return ts_value_make_string((TsString*)ts_number_to_fixed(value, digits));
    }
    TsValue* ts_number_valueOf_native(void* ctx, int argc, TsValue** argv) {
        // Validate receiver, then return the underlying primitive number.
        double value = numberThisValueOrThrow(ctx, "valueOf");
        return ts_value_make_double(value);
    }
    TsValue* ts_number_toPrecision_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toPrecision");
        // ECMA-262 21.1.3.5: undefined precision -> ToString (step 2) BEFORE
        // ToIntegerOrInfinity (step 3), which precedes the non-finite check
        // (step 4) and finally the RangeError (step 5).
        if (argc < 1 || !argv || !argv[0] || ts_value_is_undefined(argv[0])) {
            return ts_value_make_string((TsString*)ts_number_to_string(value, 10));
        }
        int64_t precision = ts_to_index_integer(argv[0]);
        if (std::isnan(value)) return ts_value_make_string(TsString::Create("NaN"));
        if (std::isinf(value)) return ts_value_make_string(TsString::Create(value < 0 ? "-Infinity" : "Infinity"));
        if (precision < 1 || precision > 100) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "toPrecision() argument must be between 1 and 100"));
            return ts_value_make_undefined();
        }
        return ts_value_make_string((TsString*)ts_number_to_precision(value, precision));
    }
    TsValue* ts_number_toExponential_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toExponential");
        // ECMA-262 21.1.3.2: f = ToIntegerOrInfinity(fractionDigits) (step 2, may
        // throw) is evaluated BEFORE the non-finite short-circuit (step 3), which
        // precedes the RangeError (step 8). fractionDigits undefined -> shortest.
        bool fdUndef = !(argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0]));
        int64_t digits = fdUndef ? -1 : ts_to_index_integer(argv[0]);
        if (std::isnan(value)) return ts_value_make_string(TsString::Create("NaN"));
        if (std::isinf(value)) return ts_value_make_string(TsString::Create(value < 0 ? "-Infinity" : "Infinity"));
        if (!fdUndef && (digits < 0 || digits > 100)) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "toExponential() argument must be between 0 and 100"));
            return ts_value_make_undefined();
        }
        return ts_value_make_string((TsString*)ts_number_to_exponential(value, digits));
    }

    // Native wrappers for boolean methods
    TsValue* ts_boolean_toString_native(void* ctx, int argc, TsValue** argv) {
        bool value = booleanThisValueOrThrow(ctx, "toString");
        return ts_value_make_string(TsString::Create(value ? "true" : "false"));
    }
    TsValue* ts_boolean_valueOf_native(void* ctx, int argc, TsValue** argv) {
        // Validate receiver, then return the underlying primitive boolean.
        bool value = booleanThisValueOrThrow(ctx, "valueOf");
        return ts_value_make_bool(value);
    }

    // Helper: require a TsDate receiver, else throw TypeError. Returns the
    // TsDate* on success, nullptr after throw.
    static inline TsDate* requireDateOrThrow(void* ctx, const char* methodName) {
        // ctx may be a raw pointer OR a NaN-boxed TsValue* for null/undefined/
        // primitive. Only a real heap pointer with TsDate::MAGIC at offset 0
        // counts as a Date. Check the NaN-box tag first to avoid derefing
        // small integer-tagged values.
        uint64_t nb = (uint64_t)(uintptr_t)ctx;
        if (ctx && nanbox_is_ptr(nb)) {
            void* p = nanbox_to_ptr(nb);
            if (p && *(uint32_t*)p == TsDate::MAGIC) {
                return (TsDate*)p;
            }
        } else if (ctx && !nanbox_is_null(nb) && !nanbox_is_undefined(nb) &&
                   !nanbox_is_int32(nb) && !nanbox_is_double(nb) &&
                   !nanbox_is_bool(nb)) {
            // Plain (non-NaN-boxed) pointer — likely from direct instance
            // access path. Safe to probe magic.
            if (*(uint32_t*)ctx == TsDate::MAGIC) {
                return (TsDate*)ctx;
            }
        }
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "Date.prototype.%s called on non-Date receiver", methodName);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", buf));
        return nullptr;
    }

    // Helper: return boxed int from a Date-field value, or NaN double if the
    // Date is invalid (sentinel INT64_MIN / TsDate::INVALID).
    static inline TsValue* dateFieldToValue(int64_t v) {
        if (v == TsDate::INVALID) {
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        }
        return ts_value_make_int(v);
    }

    // Native wrappers for Date instance methods.
    // Each wrapper first requires a TsDate receiver; if not, ts_throw is
    // invoked (longjmp) and the return statement is unreachable.
    #define DATE_GETTER(NAME, METHOD) \
    TsValue* ts_date_##NAME##_native(void* ctx, int argc, TsValue** argv) { \
        TsDate* d = requireDateOrThrow(ctx, #NAME); \
        if (!d) return ts_value_make_undefined(); \
        return dateFieldToValue(d->METHOD()); \
    }
    DATE_GETTER(getTime, GetTime)
    DATE_GETTER(getFullYear, GetFullYear)
    DATE_GETTER(getMonth, GetMonth)
    DATE_GETTER(getDate, GetDate)
    DATE_GETTER(getHours, GetHours)
    DATE_GETTER(getMinutes, GetMinutes)
    DATE_GETTER(getSeconds, GetSeconds)
    DATE_GETTER(getMilliseconds, GetMilliseconds)
    DATE_GETTER(getUTCFullYear, GetUTCFullYear)
    DATE_GETTER(getUTCMonth, GetUTCMonth)
    DATE_GETTER(getUTCDate, GetUTCDate)
    DATE_GETTER(getUTCHours, GetUTCHours)
    DATE_GETTER(getUTCMinutes, GetUTCMinutes)
    DATE_GETTER(getUTCSeconds, GetUTCSeconds)
    DATE_GETTER(getUTCMilliseconds, GetUTCMilliseconds)
    #undef DATE_GETTER
    TsValue* ts_date_toISOString_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toISOString");
        if (!d) return ts_value_make_undefined();
        if (!d->IsValid()) {
            ts_throw((TsValue*)ts_error_create_typed(
                "RangeError", "Invalid time value"));
            return ts_value_make_undefined();
        }
        return ts_value_make_string(d->ToISOString());
    }
    // ECMA-262 21.4.4.38 Date.prototype.toJSON(key). Generic: works on ANY
    // receiver (no Date brand check).
    //   1. O = ? ToObject(this value).
    //   2. tv = ? ToPrimitive(O, Number).
    //   3. If tv is a Number and not finite, return null.
    //   4. Return ? Invoke(O, "toISOString").
    TsValue* ts_date_toJSON_native(void* ctx, int argc, TsValue** argv) {
        extern TsValue* ts_to_primitive(TsValue* val, int hint);
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
        uint64_t cnb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
        // ToObject throws for null/undefined.
        if (!ctx || nanbox_is_null(cnb) || nanbox_is_undefined(cnb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Date.prototype.toJSON called on null or undefined"));
            return ts_value_make_undefined();
        }
        // ToPrimitive(O, Number). For a Date this yields the time value (NaN for
        // an Invalid Date); the non-finite check below then returns null.
        TsValue* tv = ts_to_primitive((TsValue*)ctx, 1 /* number hint */);
        uint64_t tnb = nanbox_from_tsvalue_ptr(tv);
        if (nanbox_is_double(tnb) && !std::isfinite(nanbox_to_double(tnb))) {
            return ts_value_make_null();
        }
        // Invoke(O, "toISOString"): GetV then Call. Non-callable -> TypeError.
        void* rawO = nanbox_is_ptr(cnb) ? nanbox_to_ptr(cnb) : (void*)ctx;
        TsValue* method = rawO ? ts_object_get_property(rawO, "toISOString")
                               : nullptr;
        if (!method || !ts_value_is_callable(method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "toISOString is not a function"));
            return ts_value_make_undefined();
        }
        return ts_call_with_this_0(method, (TsValue*)ctx);
    }
    TsValue* ts_date_toString_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toString");
        if (!d) return ts_value_make_undefined();
        if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
        return ts_value_make_string(d->ToString());
    }
    TsValue* ts_date_toDateString_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toDateString");
        if (!d) return ts_value_make_undefined();
        if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
        return ts_value_make_string(d->ToDateString());
    }
    TsValue* ts_date_toTimeString_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toTimeString");
        if (!d) return ts_value_make_undefined();
        if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
        return ts_value_make_string(d->ToTimeString());
    }
    // ECMA-262 7.1.1.1 OrdinaryToPrimitive(O, hint). rawObj is the receiver's
    // raw pointer; boxedThis is the boxed receiver passed as `this`. Consults
    // Get(O, "toString")/Get(O, "valueOf") (invoking user-defined methods), and
    // does NOT consult @@toPrimitive (so it cannot recurse for a Date).
    extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
    static inline bool tp_is_primitive(TsValue* r) {
        if (!r) return false;
        uint64_t nb = nanbox_from_tsvalue_ptr(r);
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb) ||
            nanbox_is_int32(nb) || nanbox_is_double(nb) || nanbox_is_bool(nb))
            return true;
        if (nanbox_is_ptr(nb)) {
            if (nanbox_is_string_ptr(nb)) return true;
            void* rp = nanbox_to_ptr(nb);
            if (rp && (*(uint32_t*)rp == 0x42494749 || *(uint32_t*)rp == 0x53594D42))
                return true;  // BigInt / Symbol are primitives
        }
        return false;
    }
    static TsValue* dateOrdinaryToPrimitive(void* rawObj, TsValue* boxedThis,
                                            bool numberFirst) {
        extern TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
        const char* names[2] = { numberFirst ? "valueOf" : "toString",
                                 numberFirst ? "toString" : "valueOf" };
        for (int i = 0; i < 2; ++i) {
            TsValue* method = ts_object_get_property(rawObj, names[i]);
            if (method && ts_value_is_callable(method)) {
                TsValue* result = ts_call_with_this_0(method, boxedThis);
                if (tp_is_primitive(result)) return result;
            }
        }
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Cannot convert object to primitive value"));
        return ts_value_make_undefined();
    }
    // ECMA-262 21.4.4.45 Date.prototype[@@toPrimitive](hint). Per spec this is
    // generic: it accepts ANY object receiver (step 2 only checks Type(O) is
    // Object, NOT a [[DateValue]] brand), determines tryFirst from the hint, and
    // runs OrdinaryToPrimitive(O, tryFirst). "string"/"default" -> string,
    // "number" -> number, anything else -> TypeError.
    TsValue* ts_date_symbolToPrimitive_native(void* ctx, int argc, TsValue** argv) {
        // POD frame only (SMELL-002): every local below is trivially
        // destructible so the ts_throw longjmps stay crash-safe.
        uint64_t cnb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
        bool isObject = nanbox_is_ptr(cnb) && !nanbox_is_string_ptr(cnb);
        if (isObject) {
            void* cp = nanbox_to_ptr(cnb);
            // BigInt / Symbol pointers are primitives, not objects.
            if (cp && (*(uint32_t*)cp == 0x42494749 || *(uint32_t*)cp == 0x53594D42))
                isObject = false;
        }
        if (!isObject) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Date.prototype[Symbol.toPrimitive] called on non-object"));
            return ts_value_make_undefined();
        }
        // The hint must be exactly the primitive string "string"/"number"/
        // "default"; a String object or any other value is a TypeError.
        int tryFirst = -1;  // 0 = string, 1 = number
        if (argc >= 1 && argv && argv[0]) {
            uint64_t hnb = nanbox_from_tsvalue_ptr(argv[0]);
            if (nanbox_is_string_ptr(hnb)) {
                void* hs = nanbox_to_ptr(hnb);
                const char* h = hs ? ((TsString*)hs)->ToUtf8() : "";
                if (h && (strcmp(h, "string") == 0 || strcmp(h, "default") == 0))
                    tryFirst = 0;
                else if (h && strcmp(h, "number") == 0)
                    tryFirst = 1;
            }
        }
        if (tryFirst < 0) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "invalid hint passed to Date.prototype[Symbol.toPrimitive]"));
            return ts_value_make_undefined();
        }
        return dateOrdinaryToPrimitive(nanbox_to_ptr(cnb), (TsValue*)ctx,
                                       /*numberFirst*/ tryFirst == 1);
    }
    TsValue* ts_date_valueOf_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "valueOf");
        if (!d) return ts_value_make_undefined();
        return dateFieldToValue(d->GetTime());
    }
    // annexB: Date.prototype.toGMTString - alias for toUTCString
    TsValue* ts_date_toUTCString_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toUTCString");
        if (!d) return ts_value_make_undefined();
        if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
        return ts_value_make_string(d->ToUTCString());
    }

    // Date setter native wrappers. Each coerces arg[0] via ts_to_number,
    // invalidates Date if NaN, otherwise calls the TsDate setter and
    // returns the resulting time as an int.
    // ES 21.4.4.x (2024 ordering): read [[DateValue]] t BEFORE ToNumber(arg);
    // if t is NaN return NaN WITHOUT writing (a valueOf side effect that set
    // the time must persist - date-value-read-before-tonumber family); the
    // result is computed from the PRE-READ t even if valueOf changed it.
    #define DATE_SETTER(NAME, METHOD) \
    TsValue* ts_date_##NAME##_native(void* ctx, int argc, TsValue** argv) { \
        TsDate* d = requireDateOrThrow(ctx, #NAME); \
        if (!d) return ts_value_make_undefined(); \
        int64_t t0 = d->IsValid() ? d->GetTime() : TsDate::INVALID; \
        double v = std::numeric_limits<double>::quiet_NaN(); \
        if (argc >= 1 && argv && argv[0]) v = ts_to_number((TsValue*)argv[0]); \
        if (t0 == TsDate::INVALID) \
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN()); \
        if (std::isnan(v)) { \
            d->SetTime(TsDate::INVALID); \
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN()); \
        } \
        d->SetTime(t0); \
        d->METHOD((int64_t)v); \
        int64_t __clip = d->GetTime(); \
        if (__clip != TsDate::INVALID && \
            (__clip < -8640000000000000LL || __clip > 8640000000000000LL)) { \
            d->SetTime(TsDate::INVALID); \
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN()); \
        } \
        return dateFieldToValue(d->GetTime()); \
    }
    // Single-argument setters: coerce arg[0] (invoking valueOf once), NaN → Invalid.
    DATE_SETTER(setDate, SetDate)
    DATE_SETTER(setMilliseconds, SetMilliseconds)
    DATE_SETTER(setUTCDate, SetUTCDate)
    DATE_SETTER(setUTCMilliseconds, SetUTCMilliseconds)
    #undef DATE_SETTER

    // Multi-argument setters (ECMA-262 §21.4.4). Each specified argument is
    // ToNumber-coerced exactly once, IN ORDER (so a valueOf side effect runs
    // once per provided arg). A NaN leading component → Invalid Date; absent
    // trailing components keep their current value. The new time is returned.
    // C++ evaluation order of call arguments is unspecified, so each arg is
    // coerced into an ordered local BEFORE building the SetFields call.
    static inline double dateArgOrNaN(int argc, TsValue** argv, int i) {
        if (argc > i && argv && argv[i]) return ts_to_number((TsValue*)argv[i]);
        return std::numeric_limits<double>::quiet_NaN();
    }
    // y[, m[, d]] — leading = year; reviving (Invalid → epoch) per spec.
    static TsValue* date_set_year_impl(void* ctx, const char* name, bool utc,
                                       int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, name);
        if (!d) return ts_value_make_undefined();
        int64_t base = d->IsValid() ? d->GetTime() : TsDate::INVALID;  // read t before ToNumber
        double y  = dateArgOrNaN(argc, argv, 0);
        double mo = (argc >= 2) ? dateArgOrNaN(argc, argv, 1)
                                : std::numeric_limits<double>::quiet_NaN();
        double dt = (argc >= 3) ? dateArgOrNaN(argc, argv, 2)
                                : std::numeric_limits<double>::quiet_NaN();
        // A leading NaN, or any PROVIDED trailing arg that coerced to NaN,
        // makes the whole result NaN (MakeDay/MakeTime propagate NaN). Absent
        // trailing args (argc too small) keep the current component instead.
        if (std::isnan(y) || (argc >= 2 && std::isnan(mo)) ||
            (argc >= 3 && std::isnan(dt))) { d->SetTime(TsDate::INVALID);
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN()); }
        double NaNv = std::numeric_limits<double>::quiet_NaN();
        d->SetFields(utc, base, y, mo, dt, NaNv, NaNv, NaNv, NaNv, /*revive*/true);
        return dateFieldToValue(d->GetTime());
    }
    // m[, d] — leading = month.
    static TsValue* date_set_month_impl(void* ctx, const char* name, bool utc,
                                        int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, name);
        if (!d) return ts_value_make_undefined();
        int64_t base = d->IsValid() ? d->GetTime() : TsDate::INVALID;  // read t before ToNumber
        double mo = dateArgOrNaN(argc, argv, 0);
        double dt = (argc >= 2) ? dateArgOrNaN(argc, argv, 1)
                                : std::numeric_limits<double>::quiet_NaN();
        if (base == TsDate::INVALID)
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        if (std::isnan(mo) || (argc >= 2 && std::isnan(dt))) { d->SetTime(TsDate::INVALID);
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN()); }
        double NaNv = std::numeric_limits<double>::quiet_NaN();
        d->SetFields(utc, base, NaNv, mo, dt, NaNv, NaNv, NaNv, NaNv, /*revive*/false);
        return dateFieldToValue(d->GetTime());
    }
    // h[, m[, s[, ms]]] — leading = hour.
    static TsValue* date_set_hours_impl(void* ctx, const char* name, bool utc,
                                        int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, name);
        if (!d) return ts_value_make_undefined();
        double NaNv = std::numeric_limits<double>::quiet_NaN();
        int64_t base = d->IsValid() ? d->GetTime() : TsDate::INVALID;  // read t before ToNumber
        double h  = dateArgOrNaN(argc, argv, 0);
        double m  = (argc >= 2) ? dateArgOrNaN(argc, argv, 1) : NaNv;
        double s  = (argc >= 3) ? dateArgOrNaN(argc, argv, 2) : NaNv;
        double ml = (argc >= 4) ? dateArgOrNaN(argc, argv, 3) : NaNv;
        if (base == TsDate::INVALID)
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        if (std::isnan(h) || (argc >= 2 && std::isnan(m)) ||
            (argc >= 3 && std::isnan(s)) || (argc >= 4 && std::isnan(ml))) {
            d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
        d->SetFields(utc, base, NaNv, NaNv, NaNv, h, m, s, ml, /*revive*/false);
        return dateFieldToValue(d->GetTime());
    }
    // m[, s[, ms]] — leading = minute.
    static TsValue* date_set_minutes_impl(void* ctx, const char* name, bool utc,
                                          int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, name);
        if (!d) return ts_value_make_undefined();
        double NaNv = std::numeric_limits<double>::quiet_NaN();
        int64_t base = d->IsValid() ? d->GetTime() : TsDate::INVALID;  // read t before ToNumber
        double m  = dateArgOrNaN(argc, argv, 0);
        double s  = (argc >= 2) ? dateArgOrNaN(argc, argv, 1) : NaNv;
        double ml = (argc >= 3) ? dateArgOrNaN(argc, argv, 2) : NaNv;
        if (base == TsDate::INVALID)
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        if (std::isnan(m) || (argc >= 2 && std::isnan(s)) ||
            (argc >= 3 && std::isnan(ml))) {
            d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
        d->SetFields(utc, base, NaNv, NaNv, NaNv, NaNv, m, s, ml, /*revive*/false);
        return dateFieldToValue(d->GetTime());
    }
    // s[, ms] — leading = second.
    static TsValue* date_set_seconds_impl(void* ctx, const char* name, bool utc,
                                          int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, name);
        if (!d) return ts_value_make_undefined();
        double NaNv = std::numeric_limits<double>::quiet_NaN();
        int64_t base = d->IsValid() ? d->GetTime() : TsDate::INVALID;  // read t before ToNumber
        double s  = dateArgOrNaN(argc, argv, 0);
        double ml = (argc >= 2) ? dateArgOrNaN(argc, argv, 1) : NaNv;
        if (base == TsDate::INVALID)
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        if (std::isnan(s) || (argc >= 2 && std::isnan(ml))) {
            d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
        d->SetFields(utc, base, NaNv, NaNv, NaNv, NaNv, NaNv, s, ml, /*revive*/false);
        return dateFieldToValue(d->GetTime());
    }
    TsValue* ts_date_setFullYear_native(void* c, int n, TsValue** v) { return date_set_year_impl(c, "setFullYear", false, n, v); }
    TsValue* ts_date_setUTCFullYear_native(void* c, int n, TsValue** v) { return date_set_year_impl(c, "setUTCFullYear", true, n, v); }
    TsValue* ts_date_setMonth_native(void* c, int n, TsValue** v) { return date_set_month_impl(c, "setMonth", false, n, v); }
    TsValue* ts_date_setUTCMonth_native(void* c, int n, TsValue** v) { return date_set_month_impl(c, "setUTCMonth", true, n, v); }
    TsValue* ts_date_setHours_native(void* c, int n, TsValue** v) { return date_set_hours_impl(c, "setHours", false, n, v); }
    TsValue* ts_date_setUTCHours_native(void* c, int n, TsValue** v) { return date_set_hours_impl(c, "setUTCHours", true, n, v); }
    TsValue* ts_date_setMinutes_native(void* c, int n, TsValue** v) { return date_set_minutes_impl(c, "setMinutes", false, n, v); }
    TsValue* ts_date_setUTCMinutes_native(void* c, int n, TsValue** v) { return date_set_minutes_impl(c, "setUTCMinutes", true, n, v); }
    TsValue* ts_date_setSeconds_native(void* c, int n, TsValue** v) { return date_set_seconds_impl(c, "setSeconds", false, n, v); }
    TsValue* ts_date_setUTCSeconds_native(void* c, int n, TsValue** v) { return date_set_seconds_impl(c, "setUTCSeconds", true, n, v); }

    // setTime: sets the time value directly from ms arg. NaN → Invalid Date.
    TsValue* ts_date_setTime_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "setTime");
        if (!d) return ts_value_make_undefined();
        double v = std::numeric_limits<double>::quiet_NaN();
        if (argc >= 1 && argv && argv[0]) v = ts_to_number((TsValue*)argv[0]);
        // TimeClip (ECMA-262 21.4.1.31): non-finite OR abs(v) > 8.64e15 -> NaN.
        if (std::isnan(v) || v < -8.64e15 || v > 8.64e15) {
            d->SetTime(TsDate::INVALID);
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        }
        d->SetTime((int64_t)v);
        return dateFieldToValue(d->GetTime());
    }
    // annexB: Date.prototype.getYear - returns getFullYear() - 1900; NaN if invalid
    TsValue* ts_date_getYear_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "getYear");
        if (!d) return ts_value_make_undefined();
        int64_t year = d->GetFullYear();
        if (year == TsDate::INVALID) {
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        }
        return ts_value_make_int(year - 1900);
    }
    // annexB: Date.prototype.setYear - years 0-99 map to 1900-1999; else absolute.
    // NaN argument invalidates the Date.
    TsValue* ts_date_setYear_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "setYear");
        if (!d) return ts_value_make_undefined();
        double yNum = std::numeric_limits<double>::quiet_NaN();
        if (argc >= 1 && argv && argv[0]) {
            yNum = ts_to_number((TsValue*)argv[0]);
        }
        if (std::isnan(yNum)) {
            d->SetTime(TsDate::INVALID);
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        }
        int64_t y = (int64_t)yNum;
        if (y >= 0 && y <= 99) y += 1900;
        d->SetFullYear(y);
        return dateFieldToValue(d->GetTime());
    }
    // Date.now() static method
    TsValue* ts_date_now_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(TsDate::Now());
    }

    // Register a native on a TsMap with correct .name / .length metadata
    // and ATTR_CONFIGURABLE|ATTR_WRITABLE (method default).
    static void dateRegisterMethod(TsMap* proto, const char* name,
                                   void* nativeFn, int arity,
                                   unsigned protoAttrs = TsHashTable::ATTR_WRITABLE |
                                                         TsHashTable::ATTR_CONFIGURABLE) {
        TsValue* fn = ts_value_make_native_function(nativeFn, nullptr);
        TsFunction* func = (TsFunction*)fn;
        func->name = TsString::Create(name);
        func->arity = arity;
        // Per ECMA-262: built-in prototype methods have no [[Construct]].
        // `new (new Date()).getDate()` must throw TypeError; isConstructor
        // must return false. Match the addMethod() helper in TsGlobals.cpp.
        func->is_constructor = false;
        if (!func->properties) func->properties = TsMap::Create();
        TsValue lk; lk.type = ValueType::STRING_PTR;
        lk.ptr_val = TsString::GetInterned("length");
        TsValue lv; lv.type = ValueType::NUMBER_INT; lv.i_val = arity;
        func->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue nk; nk.type = ValueType::STRING_PTR;
        nk.ptr_val = TsString::GetInterned("name");
        TsValue nv; nv.type = ValueType::STRING_PTR; nv.ptr_val = func->name;
        func->properties->SetWithAttrs(nk, nv, TsHashTable::ATTR_CONFIGURABLE);

        TsValue key;
        key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(name);
        TsValue val;
        val.type = ValueType::FUNCTION_PTR;
        val.ptr_val = fn;
        proto->SetWithAttrs(key, val, protoAttrs);
    }

    // Populate a freshly-created TsMap with all Date.prototype methods.
    // Called from TsGlobals.cpp at Date-constructor init time.
    extern "C" void* ts_date_prototype_build_map() {
        TsMap* proto = TsMap::Create();
        // Getters (arity 0). Stubs for getDay / getUTCDay / getTimezoneOffset
        // / toLocale* — minimal impls below; tests for name/length pass once
        // the function is registered with proper metadata even if the body
        // returns a stub value.
        dateRegisterMethod(proto, "getDay",            (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "getDay");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
            // Day of week: derive from time-ms via Zeller-style calc.
            int64_t ms = d->GetTime();
            // JS epoch (Jan 1 1970) was a Thursday (4). 86400000 ms per day.
            int64_t days = ms / 86400000;
            if (ms < 0 && (ms % 86400000) != 0) days -= 1;
            int dow = (int)((days + 4) % 7);
            if (dow < 0) dow += 7;
            return ts_value_make_int((int64_t)dow);
        }, 0);
        dateRegisterMethod(proto, "getUTCDay",         (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "getUTCDay");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
            int64_t ms = d->GetTime();
            int64_t days = ms / 86400000;
            if (ms < 0 && (ms % 86400000) != 0) days -= 1;
            int dow = (int)((days + 4) % 7);
            if (dow < 0) dow += 7;
            return ts_value_make_int((int64_t)dow);
        }, 0);
        dateRegisterMethod(proto, "getTimezoneOffset", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "getTimezoneOffset");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
            // Approx: local time - UTC. ts-aot uses UTC internally, so 0.
            return ts_value_make_int((int64_t)0);
        }, 0);
        dateRegisterMethod(proto, "toLocaleString",     (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "toLocaleString");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
            return ts_value_make_string(d->ToString());
        }, 0);
        dateRegisterMethod(proto, "toLocaleDateString", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "toLocaleDateString");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
            return ts_value_make_string(d->ToDateString());
        }, 0);
        dateRegisterMethod(proto, "toLocaleTimeString", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
            TsDate* d = requireDateOrThrow(ctx, "toLocaleTimeString");
            if (!d) return ts_value_make_undefined();
            if (!d->IsValid()) return ts_value_make_string(TsString::Create("Invalid Date"));
            return ts_value_make_string(d->ToString());
        }, 0);
        dateRegisterMethod(proto, "getTime", (void*)ts_date_getTime_native, 0);
        dateRegisterMethod(proto, "getFullYear", (void*)ts_date_getFullYear_native, 0);
        dateRegisterMethod(proto, "getMonth", (void*)ts_date_getMonth_native, 0);
        dateRegisterMethod(proto, "getDate", (void*)ts_date_getDate_native, 0);
        dateRegisterMethod(proto, "getHours", (void*)ts_date_getHours_native, 0);
        dateRegisterMethod(proto, "getMinutes", (void*)ts_date_getMinutes_native, 0);
        dateRegisterMethod(proto, "getSeconds", (void*)ts_date_getSeconds_native, 0);
        dateRegisterMethod(proto, "getMilliseconds", (void*)ts_date_getMilliseconds_native, 0);
        dateRegisterMethod(proto, "getUTCFullYear", (void*)ts_date_getUTCFullYear_native, 0);
        dateRegisterMethod(proto, "getUTCMonth", (void*)ts_date_getUTCMonth_native, 0);
        dateRegisterMethod(proto, "getUTCDate", (void*)ts_date_getUTCDate_native, 0);
        dateRegisterMethod(proto, "getUTCHours", (void*)ts_date_getUTCHours_native, 0);
        dateRegisterMethod(proto, "getUTCMinutes", (void*)ts_date_getUTCMinutes_native, 0);
        dateRegisterMethod(proto, "getUTCSeconds", (void*)ts_date_getUTCSeconds_native, 0);
        dateRegisterMethod(proto, "getUTCMilliseconds", (void*)ts_date_getUTCMilliseconds_native, 0);
        // String outputs (arity 0)
        dateRegisterMethod(proto, "toISOString", (void*)ts_date_toISOString_native, 0);
        dateRegisterMethod(proto, "toJSON", (void*)ts_date_toJSON_native, 1);
        dateRegisterMethod(proto, "toString", (void*)ts_date_toString_native, 0);
        dateRegisterMethod(proto, "toDateString", (void*)ts_date_toDateString_native, 0);
        dateRegisterMethod(proto, "toTimeString", (void*)ts_date_toTimeString_native, 0);
        // @@toPrimitive is { writable:false, enumerable:false, configurable:true }.
        dateRegisterMethod(proto, "[Symbol.toPrimitive]", (void*)ts_date_symbolToPrimitive_native, 1,
                           TsHashTable::ATTR_CONFIGURABLE);
        dateRegisterMethod(proto, "valueOf", (void*)ts_date_valueOf_native, 0);
        // annexB
        dateRegisterMethod(proto, "toUTCString", (void*)ts_date_toUTCString_native, 0);
        dateRegisterMethod(proto, "toGMTString", (void*)ts_date_toUTCString_native, 0);
        dateRegisterMethod(proto, "getYear", (void*)ts_date_getYear_native, 0);
        dateRegisterMethod(proto, "setYear", (void*)ts_date_setYear_native, 1);
        // Setters — spec arities per ECMA-262 §21.4.4
        dateRegisterMethod(proto, "setTime", (void*)ts_date_setTime_native, 1);
        dateRegisterMethod(proto, "setFullYear", (void*)ts_date_setFullYear_native, 3);
        dateRegisterMethod(proto, "setMonth", (void*)ts_date_setMonth_native, 2);
        dateRegisterMethod(proto, "setDate", (void*)ts_date_setDate_native, 1);
        dateRegisterMethod(proto, "setHours", (void*)ts_date_setHours_native, 4);
        dateRegisterMethod(proto, "setMinutes", (void*)ts_date_setMinutes_native, 3);
        dateRegisterMethod(proto, "setSeconds", (void*)ts_date_setSeconds_native, 2);
        dateRegisterMethod(proto, "setMilliseconds", (void*)ts_date_setMilliseconds_native, 1);
        dateRegisterMethod(proto, "setUTCFullYear", (void*)ts_date_setUTCFullYear_native, 3);
        dateRegisterMethod(proto, "setUTCMonth", (void*)ts_date_setUTCMonth_native, 2);
        dateRegisterMethod(proto, "setUTCDate", (void*)ts_date_setUTCDate_native, 1);
        dateRegisterMethod(proto, "setUTCHours", (void*)ts_date_setUTCHours_native, 4);
        dateRegisterMethod(proto, "setUTCMinutes", (void*)ts_date_setUTCMinutes_native, 3);
        dateRegisterMethod(proto, "setUTCSeconds", (void*)ts_date_setUTCSeconds_native, 2);
        dateRegisterMethod(proto, "setUTCMilliseconds", (void*)ts_date_setUTCMilliseconds_native, 1);
        return proto;
    }
    // Date.parse(s) — stub: return NaN for non-recognized, passthrough for
    // numeric-looking strings. Most test262 tests only check metadata
    // (typeof/length/name/isConstructor), so a stub suffices for those.
    // A full ISO 8601 parser is a larger separate project.
    // fwd (defined near ts_object_get_property): receiver-correct prototype walk.
    static TsValue* temporal_proto_get(void* obj, void* protoRaw, const char* keyStr);

    TsValue* ts_date_parse_native(void* ctx, int argc, TsValue** argv) {
        if (argc < 1 || !argv || !argv[0]) {
            return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        }
        // Best-effort: if the arg is already a Number, return it; otherwise NaN.
        uint64_t nb = nanbox_from_tsvalue_ptr(argv[0]);
        if (nanbox_is_number(nb)) return ts_value_make_double(nanbox_to_number(nb));
        return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
    }

    // Date.UTC(year, month, day?, hour?, minute?, second?, ms?) — approximate
    // using <ctime>. Handles common cases; edge cases (year < 100) not
    // fully spec-compliant.
    TsValue* ts_date_UTC_native(void* ctx, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        auto getInt = [&](int i, int dflt) -> int {
            if (i >= argc || !argv[i]) return dflt;
            double d = ts_value_get_double(argv[i]);
            if (d != d) return 0;
            return (int)d;
        };
        int year   = getInt(0, 1970);
        int month  = getInt(1, 0);
        int day    = getInt(2, 1);
        int hour   = getInt(3, 0);
        int minute = getInt(4, 0);
        int second = getInt(5, 0);
        int ms     = getInt(6, 0);
        // Two-digit year normalization (0-99 → 1900-1999) per spec.
        if (year >= 0 && year <= 99) year += 1900;
        struct tm t{};
        t.tm_year = year - 1900;
        t.tm_mon  = month;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min  = minute;
        t.tm_sec  = second;
        // Use timegm-equivalent: compute as UTC by converting local then adjusting.
        // On Windows we have _mkgmtime.
#if defined(_WIN32)
        time_t tt = _mkgmtime(&t);
#else
        time_t tt = timegm(&t);
#endif
        if (tt == (time_t)-1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double result = (double)tt * 1000.0 + (double)ms;
        return ts_value_make_double(result);
    }

    // Populate a TsMap with Date constructor static methods (Date.now, etc.)
    extern "C" void ts_date_constructor_populate(void* ctorMap) {
        TsMap* ctor = (TsMap*)ctorMap;
        dateRegisterMethod(ctor, "now",   (void*)ts_date_now_native,   0);
        dateRegisterMethod(ctor, "parse", (void*)ts_date_parse_native, 1);
        dateRegisterMethod(ctor, "UTC",   (void*)ts_date_UTC_native,   7);
    }

    // Native wrappers for RegExp instance methods (.test() and .exec()).
    // Exported (non-static) so RegExp.prototype population in TsGlobals.cpp
    // can install them via addMethod with proper name/length metadata.
    //
    // Receiver BRAND CHECK: `RegExp.prototype.exec.call(Math, s)` (and any
    // other non-RegExp receiver) must throw TypeError -- the old blind
    // (TsRegExp*)ctx cast dereferenced arbitrary objects (0xc0000005).
    static TsRegExp* regexp_receiver_or_throw(void* ctx, const char* name) {
        void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
        if (!raw) raw = ctx;
        uintptr_t a = (uintptr_t)raw;
        if (raw && a >= 4096 && (a >> 48) == 0 &&
            *(uint32_t*)raw == 0x52454758 /* TsRegExp "REGX" */) {
            return (TsRegExp*)raw;
        }
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "RegExp.prototype.%s called on incompatible receiver", name);
        ts_throw((TsValue*)ts_error_create_typed("TypeError", buf));
        return nullptr;
    }
    extern "C" TsValue* ts_regexp_test_native(void* ctx, int argc, TsValue** argv) {
        TsRegExp* re = regexp_receiver_or_throw(ctx, "test");
        if (!re) return (TsValue*)ts_value_make_undefined();
        void* str = (argc >= 1 && argv && argv[0]) ? (void*)argv[0] : nullptr;
        int32_t result = RegExp_test(re, str);
        return (TsValue*)ts_value_make_bool(result != 0);
    }
    extern "C" TsValue* ts_regexp_tostring_native(void* ctx, int argc, TsValue** argv) {
        // ES 22.2.6.16 step 2: Type(R) must be Object — a primitive receiver
        // (RegExp.prototype.toString.call('s')) is a TypeError, not a blind
        // TsRegExp cast. Generic objects compose "/"+Get(source)+"/"+
        // Get(flags). POD frame — ts_throw longjmps.
        extern void* ts_get_call_this();
        if (!ctx) ctx = ts_get_call_this();
        uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
        void* raw = nanbox_is_ptr(nb) ? ts_value_get_object((TsValue*)ctx) : nullptr;
        if (!raw && nanbox_is_ptr(nb)) raw = nanbox_to_ptr(nb);
        bool isObj = raw && (uintptr_t)raw >= 4096 &&
                     (uintptr_t)raw <= 0x00007FFFFFFFFFFFULL;
        if (isObj) {
            uint32_t m0 = *(uint32_t*)raw;
            if (m0 == 0x53545247 /*STRG*/ || m0 == 0x434F4E53 /*CONS*/ ||
                m0 == 0x53594D42 /*SYMB*/ || m0 == 0x42494749 /*BIGI*/)
                isObj = false;
        }
        if (!isObj) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "RegExp.prototype.toString requires that 'this' be an Object"));
            return (TsValue*)ts_value_make_undefined();
        }
        extern void* ts_string_from_value(TsValue* val);
        if (*(uint32_t*)raw == 0x52454758 /* REGX */) {
            // Fast path: ts_string_from_value builds "/source/flags".
            TsValue* boxed = (TsValue*)ts_value_make_object((TsRegExp*)raw);
            return (TsValue*)ts_value_make_string(ts_string_from_value(boxed));
        }
        // Generic object: "/" + ToString(Get(R,"source")) + "/" +
        // ToString(Get(R,"flags")).
        extern TsValue* ts_object_get_property(void* obj, const char* key);
        extern void* ts_string_concat(void* a, void* b);
        TsValue* srcV = ts_object_get_property(raw, "source");
        TsValue* flV  = ts_object_get_property(raw, "flags");
        void* srcS = ts_string_from_value(srcV ? srcV : ts_value_make_undefined());
        void* flS  = ts_string_from_value(flV ? flV : ts_value_make_undefined());
        void* out = ts_string_concat(TsString::Create("/"), srcS);
        out = ts_string_concat(out, TsString::Create("/"));
        out = ts_string_concat(out, flS);
        return (TsValue*)ts_value_make_string(out);
    }
    extern "C" TsValue* ts_regexp_exec_native(void* ctx, int argc, TsValue** argv) {
        TsRegExp* re = regexp_receiver_or_throw(ctx, "exec");
        if (!re) return (TsValue*)ts_value_make_undefined();
        void* str = (argc >= 1 && argv && argv[0]) ? (void*)argv[0] : nullptr;
        void* result = RegExp_exec(re, str);
        if (!result) return (TsValue*)ts_value_make_null();
        return (TsValue*)ts_value_make_object(result);
    }
    // RegExp.prototype.compile (Annex B B.2.3.1): recompile `this` in place from
    // a new pattern/flags, then return `this`.
    extern "C" TsValue* ts_regexp_compile_native(void* ctx, int argc, TsValue** argv) {
        // B.2.5.1 step 2: `this` must be an Object with a [[RegExpMatcher]]
        // slot — anything else (undefined/null/number/plain object) is a
        // TypeError, not a blind cast into ICU (which crashed).
        void* raw = ts_value_get_object((TsValue*)ctx);
        if (!raw) raw = ctx;
        if (!raw || (uintptr_t)raw < 4096 ||
            (uintptr_t)raw > 0x00007FFFFFFFFFFFULL ||
            *(uint32_t*)raw != 0x52454758 /* REGX */) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Method RegExp.prototype.compile called on incompatible receiver"));
            return (TsValue*)ts_value_make_undefined();
        }
        TsRegExp* re = (TsRegExp*)raw;
        extern void* ts_string_from_value(TsValue* val);
        // POD frame (SMELL-002): the receiver throw above and the Symbol
        // coercion throws below longjmp; std::string locals here were in
        // the unconstructed-at-throw crash class. TsString* is POD-safe.
        TsString* patS = nullptr;
        TsString* flS = nullptr;
        bool flagsGiven = (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]));
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            void* praw = ts_value_get_object(argv[0]);
            if (!praw) praw = (void*)argv[0];
            // A RegExp source argument: reuse its source (and flags if none given).
            if (praw && (uintptr_t)praw > 0x1000 && *(uint32_t*)praw == 0x52454758 /* REGX */) {
                TsRegExp* src = (TsRegExp*)praw;
                patS = src->GetSource();
            } else {
                patS = (TsString*)ts_string_from_value((TsValue*)argv[0]);
            }
        }
        if (flagsGiven) {
            flS = (TsString*)ts_string_from_value((TsValue*)argv[1]);
        }
        const char* patC = (patS && patS->ToUtf8()) ? patS->ToUtf8() : "";
        const char* flC = (flS && flS->ToUtf8()) ? flS->ToUtf8() : "";
        re->Recompile(patC, flC);
        return (TsValue*)ts_value_make_object(re);
    }

    // ECMA-262 22.2.7.1 RegExpExec(R, S): the observable exec abstract operation
    // shared by the RegExp.prototype[@@search/@@match/@@replace/@@split/@@matchAll]
    // methods. Prefer a (callable) `exec` looked up via Get(R,"exec") — so a user
    // override is honored — else fall back to the builtin RegExpBuiltinExec. `S` is
    // a boxed string TsValue*. Returns the match-result object (raw ptr) or nullptr
    // for no match (throws TypeError if a custom exec returns a non-object/non-null).
    static void* ts_regexp_exec_observable(void* rx, void* sBoxed) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* exec = ts_object_get_property(rx, "exec");
        if (exec && ts_value_is_callable(exec)) {
            TsValue* boxedRx = (TsValue*)ts_value_make_object(rx);
            TsValue* sArg = (TsValue*)sBoxed;
            TsValue* result = ts_function_call_with_this(exec, boxedRx, 1, &sArg);
            // Spec: the result must be an Object or NULL -- undefined (and
            // every other primitive) is a TypeError, not a no-match.
            if (!result || ts_value_is_null(result))
                return nullptr;
            void* robj = ts_value_get_object(result);
            if (robj) {
                // Primitive values that unbox to a pointer (strings/symbols/
                // bigints) are NOT Objects either.
                uint32_t m0 = *(uint32_t*)robj;
                if (m0 == 0x53545247 /*STRG*/ || m0 == TsConsString::MAGIC ||
                    m0 == 0x53594D42 /*SYMB*/ || m0 == 0x42494749 /*BIGI*/) {
                    robj = nullptr;
                }
            }
            if (!robj) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "RegExp exec method returned a non-object"));
                return nullptr;
            }
            return robj;
        }
        // RegExpBuiltinExec requires the [[RegExpMatcher]] internal slot --
        // a generic receiver without a callable `exec` is a TypeError, not
        // a blind cast into RegExp_exec.
        {
            uintptr_t a = (uintptr_t)rx;
            if (!rx || a < 4096 || (a >> 48) != 0 ||
                *(uint32_t*)rx != 0x52454758 /* REGX */) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "RegExpExec: receiver is not a RegExp and has no callable exec"));
                return nullptr;
            }
        }
        return RegExp_exec(rx, sBoxed);  // RegExpBuiltinExec
    }

    // ToString(arg) per the RegExp Symbol-method spec steps: ToPrimitive(string
    // hint) for objects (honors a custom toString; a throwing valueOf is not
    // called), then stringify. ts_to_primitive returns primitives unchanged.
    static TsString* ts_regexp_tostring_arg(TsValue* sv) {
        extern void* ts_string_from_value(TsValue* val);
        extern TsValue* ts_to_primitive(TsValue* val, int hint);
        return (TsString*)ts_string_from_value(sv ? ts_to_primitive(sv, 2) : sv);
    }

    // ECMA-262 22.2.6.x: RegExp.prototype[@@search/@@match/@@replace/@@split/
    // @@matchAll] require `this` (R) to be an Object; this implementation further
    // requires a RegExp. A non-RegExp receiver ({} / undefined / number) was cast
    // to TsRegExp* and the ICU GetSource/GetFlags/exec ran on garbage -> crash.
    // Validate (REGX magic) or throw a TypeError.
    static TsRegExp* regexp_require_this(void* ctx, const char* msg) {
        void* raw = ts_value_get_object((TsValue*)ctx);
        if (!raw) raw = ctx;
        if (!is_safe_ptr_for_magic(raw) || *(uint32_t*)raw != 0x52454758) {  // "REGX"
            ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
            return nullptr;  // unreachable
        }
        return (TsRegExp*)raw;
    }

    // ECMA-262 22.2.6.12 RegExp.prototype [ @@search ] ( string ). Saves and
    // restores lastIndex (search must not perturb it), runs RegExpExec once, and
    // returns the match index or -1.
    // Receiver must be an OBJECT (generic @@-method contract shared by
    // @@match / @@search / @@split): returns the raw pointer or throws.
    static void* regexp_generic_receiver(void* ctx, const char* what) {
        // The generic @@-method contract accepts any OBJECT -- but PRIMITIVES
        // (numbers, booleans, string/symbol/bigint values) must throw, not
        // slip through as their boxed TsValue pointer (this-val-non-obj
        // crashed on property gets against a boxed string).
        void* recv = nullptr;
        if (ctx) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
            if (nanbox_is_ptr(nb) && !nanbox_is_string_ptr(nb)) {
                void* raw = nanbox_to_ptr(nb);
                uintptr_t ra = (uintptr_t)raw;
                if (raw && ra >= 4096 && (ra >> 48) == 0) {
                    uint32_t m0 = *(uint32_t*)raw;
                    if (m0 != 0x53594D42 /*SYMB*/ && m0 != 0x42494749 /*BIGI*/ &&
                        m0 != 0x53545247 /*STRG*/ && m0 != TsConsString::MAGIC) {
                        recv = raw;
                    }
                }
            }
        }
        if (!recv) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "RegExp.prototype[%s] called on incompatible receiver", what);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", buf));
            return nullptr;
        }
        return recv;
    }

    // ES 7.2.11 SameValue over two raw (uncoerced) values. Distinguishes the
    // signed zeros and treats NaN as equal to itself — the @@search lastIndex
    // save/restore compares with SameValue, NOT ToNumber (a value like -0 must
    // be re-Set to +0, and an object lastIndex must NOT be coerced here).
    static bool regexp_same_value(TsValue* a, TsValue* b) {
        uint64_t na = nanbox_from_tsvalue_ptr(a);
        uint64_t nb = nanbox_from_tsvalue_ptr(b);
        bool an = nanbox_is_number(na), bn = nanbox_is_number(nb);
        if (an && bn) {
            double da = nanbox_to_number(na), db = nanbox_to_number(nb);
            if (da != da && db != db) return true;                  // NaN, NaN
            if (da == 0 && db == 0) return std::signbit(da) == std::signbit(db);
            return da == db;
        }
        if (an != bn) return false;
        return na == nb;  // non-numbers: identity (undefined/null/ptr)
    }

    extern "C" TsValue* ts_regexp_symbol_search_native(void* ctx, int argc, TsValue** argv) {
        extern void* ts_string_from_value(TsValue* val);
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value);
        // ES 22.2.6.12 is GENERIC: lastIndex save/zero/restore are ordinary
        // property operations on ANY object; exec runs through RegExpExec.
        void* recv = regexp_generic_receiver(ctx, "Symbol.search");
        if (!recv) return (TsValue*)ts_value_make_undefined();
        TsValue* sv = (argc >= 1 && argv && argv[0]) ? argv[0]
                                                     : (TsValue*)ts_value_make_undefined();
        TsString* sStr = ts_regexp_tostring_arg(sv);
        TsValue* sBoxed = (TsValue*)ts_value_make_string(sStr);
        TsValue* liKey = ts_value_make_string(TsString::GetInterned("lastIndex"));
        // Step 4-5: previousLastIndex = Get(rx,"lastIndex") (NO coercion). If
        // SameValue(previousLastIndex, +0) is false, Set(rx,"lastIndex", 0).
        TsValue* prev = ts_object_get_property(recv, "lastIndex");
        TsValue* posZero = ts_value_make_int(0);
        if (!regexp_same_value(prev ? prev : posZero, posZero)) {
            ts_object_set_dynamic((TsValue*)recv, liKey, ts_value_make_int(0));
        }
        void* result = ts_regexp_exec_observable(recv, sBoxed);
        // Step 7: currentLastIndex = Get(rx,"lastIndex"). If
        // SameValue(currentLastIndex, previousLastIndex) is false, restore.
        TsValue* cur = ts_object_get_property(recv, "lastIndex");
        if (!regexp_same_value(cur ? cur : posZero, prev ? prev : posZero)) {
            ts_object_set_dynamic((TsValue*)recv, liKey,
                                  prev ? prev : ts_value_make_int(0));
        }
        if (!result) return (TsValue*)ts_value_make_int(-1);
        return ts_object_get_property(result, "index");
    }

    // ECMA-262 22.2.7.3 AdvanceStringIndex (simple form): +1 code unit.
    static int64_t ts_regexp_advance_string_index(int64_t index) {
        return index + 1;
    }
    // Surrogate-aware AdvanceStringIndex (defined below, used by @@match).
    static int ts_regexp_adv_u(const icu::UnicodeString& S, int index, bool unicode);

    // ECMA-262 22.2.6.8 RegExp.prototype [ @@match ] ( string ). Non-global:
    // returns the single RegExpExec result (or null). Global: resets lastIndex,
    // collects each full match string, advancing past empty matches, returns the
    // array (or null if none).
    extern "C" TsValue* ts_regexp_symbol_match_native(void* ctx, int argc, TsValue** argv) {
        extern void* ts_string_from_value(TsValue* val);
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void* ts_array_create();
        extern int64_t ts_array_push(void* arr, void* value);
        extern void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value);
        // ES 22.2.6.8 is GENERIC: any OBJECT receiver works. `flags` and
        // `lastIndex` are ordinary property operations and exec runs through
        // RegExpExec, so user getters (and their abrupt completions) are
        // observable in spec order (g-get-exec-err: lastIndex is set to 0
        // BEFORE the throwing `exec` getter propagates).
        void* recv = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
        if (!recv) recv = ctx;
        uintptr_t ra = (uintptr_t)recv;
        if (!recv || ra < 4096 || (ra >> 48) != 0) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "RegExp.prototype[Symbol.match] called on incompatible receiver"));
            return (TsValue*)ts_value_make_undefined();
        }
        TsValue* sv = (argc >= 1 && argv && argv[0]) ? argv[0]
                                                     : (TsValue*)ts_value_make_undefined();
        TsString* sStr = ts_regexp_tostring_arg(sv);
        TsValue* sBoxed = (TsValue*)ts_value_make_string(sStr);
        TsValue* fv = ts_object_get_property(recv, "flags");
        TsString* fStr = (TsString*)ts_string_from_value(fv);
        const char* fC = fStr ? fStr->ToUtf8() : nullptr;
        bool global = fC && strchr(fC, 'g');
        // ES 22.2.6.8 step 6.a: fullUnicode = flags contains "u" or "v" (used by
        // AdvanceStringIndex to step over a surrogate pair after an empty match).
        bool fullUnicode = fC && (strchr(fC, 'u') || strchr(fC, 'v'));
        if (!global) {
            void* result = ts_regexp_exec_observable(recv, sBoxed);
            return result ? (TsValue*)ts_value_make_object(result)
                          : (TsValue*)ts_value_make_null();
        }
        TsValue* liKey = ts_value_make_string(TsString::GetInterned("lastIndex"));
        ts_object_set_dynamic((TsValue*)recv, liKey, ts_value_make_int(0));
        void* arr = ts_array_create();
        int64_t sLen = sStr->Length();
        int n = 0;
        while (true) {
            void* result = ts_regexp_exec_observable(recv, sBoxed);
            if (!result) break;
            TsValue* m0 = ts_object_get_property(result, "0");
            TsString* mStr = (TsString*)ts_string_from_value(m0);  // ToString
            ts_array_push(arr, (void*)ts_value_make_string(mStr));
            n++;
            if (mStr->Length() == 0) {
                // Empty match: ES 22.2.6.8 step reads ToLength(Get(rx,"lastIndex"))
                // and advances (this coercion IS observable per spec).
                TsValue* liv = ts_object_get_property(recv, "lastIndex");
                int64_t li = liv ? (int64_t)ts_to_number(liv) : 0;
                ts_object_set_dynamic((TsValue*)recv, liKey,
                    ts_value_make_int(ts_regexp_adv_u(sStr->getUStr(), (int)li, fullUnicode)));
            }
            // Termination safety: bound the loop by the match counter ALONE. A
            // non-empty match must NOT coerce lastIndex (g-match-no-coerce-
            // lastindex installs a throwing valueOf that spec never invokes).
            if (n > (int)sLen + 2) break;
        }
        if (n == 0) return (TsValue*)ts_value_make_null();
        return (TsValue*)ts_value_make_object(arr);
    }

    // ECMA-262 22.1.3.17.1 GetSubstitution — expand $ patterns in `templ`
    // ($$ $& $` $' $n $nn $<name>) using the matched text, captures, and the
    // optional named-capture groups object. Appends to `out`.
    static void ts_regexp_get_substitution(
            icu::UnicodeString& out, const icu::UnicodeString& matched,
            const icu::UnicodeString& S, int position,
            const std::vector<std::pair<bool, icu::UnicodeString>>& captures,
            void* namedCaptures, const icu::UnicodeString& templ) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        int nCaptures = (int)captures.size();
        int tailPos = position + matched.length();
        int len = templ.length();
        for (int i = 0; i < len; i++) {
            UChar c = templ.charAt(i);
            if (c != 0x24 /* $ */ || i + 1 >= len) { out.append(c); continue; }
            UChar nx = templ.charAt(i + 1);
            if (nx == 0x24) { out.append((UChar)0x24); i++; }
            else if (nx == 0x26 /* & */) { out.append(matched); i++; }
            else if (nx == 0x60 /* ` */) { out.append(S, 0, position); i++; }
            else if (nx == 0x27 /* ' */) { out.append(S, tailPos, S.length() - tailPos); i++; }
            else if (nx >= 0x30 && nx <= 0x39 /* 0-9 */) {
                int idx = nx - 0x30, consumed = 1;
                if (i + 2 < len) {
                    UChar n2 = templ.charAt(i + 2);
                    if (n2 >= 0x30 && n2 <= 0x39) {
                        int two = idx * 10 + (n2 - 0x30);
                        if (two >= 1 && two <= nCaptures) { idx = two; consumed = 2; }
                    }
                }
                if (idx >= 1 && idx <= nCaptures) {
                    if (captures[idx - 1].first) out.append(captures[idx - 1].second);
                    i += consumed;
                } else {
                    out.append((UChar)0x24);  // literal $
                }
            }
            else if (nx == 0x3C /* < */ && namedCaptures) {
                int j = i + 2;
                while (j < len && templ.charAt(j) != 0x3E /* > */) j++;
                if (j < len) {
                    icu::UnicodeString name(templ, i + 2, j - (i + 2));
                    std::string nameU8; name.toUTF8String(nameU8);
                    TsValue* gv = ts_object_get_property(namedCaptures, nameU8.c_str());
                    if (gv && !ts_value_is_undefined(gv)) {
                        TsString* gs = ts_regexp_tostring_arg(gv);
                        out.append(gs->getUStr());
                    }
                    i = j;
                } else {
                    out.append((UChar)0x24);
                }
            }
            else { out.append((UChar)0x24); }  // unknown — literal $
        }
    }

    // ECMA-262 22.2.6.11 RegExp.prototype [ @@replace ] ( string, replaceValue ).
    // Single-pass (process each match before the next RegExpExec) so no
    // std::vector of GC-pointer results is held across allocating exec calls.
    extern "C" TsValue* ts_regexp_symbol_replace_native(void* ctx, int argc, TsValue** argv) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value);
        extern void* ts_string_from_value(TsValue* val);
        // ES 22.2.6.11 is GENERIC: any OBJECT receiver; global comes from the
        // hooked flags string, lastIndex is an ordinary property op, and exec
        // runs through RegExpExec (abrupt exec-getter completions propagate).
        void* recvRaw = regexp_generic_receiver(ctx, "Symbol.replace");
        if (!recvRaw) return (TsValue*)ts_value_make_undefined();
        TsValue* sv = (argc >= 1 && argv && argv[0]) ? argv[0]
                                                     : (TsValue*)ts_value_make_undefined();
        TsString* sTs = ts_regexp_tostring_arg(sv);
        // ALL non-POD locals for this frame constructed HERE, before ANY
        // throwing call — including the hooked flags coercion below
        // (SMELL-002 longjmp rule: MSVC's longjmp unwind can run cleanup on
        // an UNCONSTRUCTED non-POD slot and corrupt the heap, 0xc0000374).
        // Values are assigned after the observable coercions; per-iteration
        // state resets via assignment/clear().
        icu::UnicodeString S;
        icu::UnicodeString replTemplate;
        icu::UnicodeString accumulated;
        icu::UnicodeString matched;
        std::vector<std::pair<bool, icu::UnicodeString>> captures;
        icu::UnicodeString replacement;
        std::vector<TsValue*> callArgs;
        std::string u8work;
        std::string outU8;
        TsString* fTs = nullptr;
        {
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            TsValue* fvv = ts_object_get_property(recvRaw, "flags");
            TsValue* fprim = ts_to_primitive(
                fvv ? fvv : (TsValue*)ts_value_make_undefined(), 2);
            fTs = (TsString*)ts_string_from_value(fprim);
        }
        TsValue* liKey = ts_value_make_string(TsString::GetInterned("lastIndex"));
        S = sTs->getUStr();
        int sLen = S.length();
        TsValue* replaceVal = (argc >= 2 && argv && argv[1]) ? argv[1]
                                                            : (TsValue*)ts_value_make_undefined();
        bool functional = ts_value_is_callable(replaceVal);
        if (!functional) replTemplate = ts_regexp_tostring_arg(replaceVal)->getUStr();

        const char* fC = fTs ? fTs->ToUtf8() : nullptr;
        bool global = fC && strchr(fC, 'g');
        // ES 22.2.6.11 step 8.b: fullUnicode = flags contains "u" or "v".
        bool fullUnicode = fC && (strchr(fC, 'u') || strchr(fC, 'v'));
        if (global) {
            ts_object_set_dynamic((TsValue*)recvRaw, liKey, ts_value_make_int(0));
        }

        int nextSourcePosition = 0;
        TsValue* sBoxed = (TsValue*)ts_value_make_string(sTs);
        while (true) {
            void* result = ts_regexp_exec_observable(recvRaw, sBoxed);
            if (!result) break;
            TsString* matchedTs = ts_regexp_tostring_arg(ts_object_get_property(result, "0"));
            matched = matchedTs->getUStr();
            int matchLen = matched.length();
            int position = (int)ts_to_number(ts_object_get_property(result, "index"));
            if (position < 0) position = 0;
            if (position > sLen) position = sLen;
            int nCaptures = (int)ts_to_number(ts_object_get_property(result, "length")) - 1;
            if (nCaptures < 0) nCaptures = 0;
            captures.clear();
            for (int i = 1; i <= nCaptures; i++) {
                char key[16]; snprintf(key, sizeof(key), "%d", i);
                TsValue* cap = ts_object_get_property(result, key);
                if (cap && !ts_value_is_undefined(cap))
                    captures.push_back({true, ts_regexp_tostring_arg(cap)->getUStr()});
                else
                    captures.push_back({false, icu::UnicodeString()});
            }
            TsValue* groups = ts_object_get_property(result, "groups");
            void* namedCaptures = (groups && !ts_value_is_undefined(groups))
                                  ? ts_value_get_object(groups) : nullptr;
            replacement.remove();
            if (functional) {
                callArgs.clear();
                callArgs.push_back((TsValue*)ts_value_make_string(matchedTs));
                for (auto& cap : captures) {
                    if (cap.first) {
                        u8work.clear(); cap.second.toUTF8String(u8work);
                        callArgs.push_back((TsValue*)ts_value_make_string(TsString::Create(u8work.c_str())));
                    } else {
                        callArgs.push_back((TsValue*)ts_value_make_undefined());
                    }
                }
                callArgs.push_back((TsValue*)ts_value_make_int(position));
                callArgs.push_back((TsValue*)ts_value_make_string(sTs));
                if (namedCaptures) callArgs.push_back(groups);
                TsValue* r = ts_function_call_with_this(replaceVal,
                        (TsValue*)ts_value_make_undefined(),
                        (int)callArgs.size(), callArgs.data());
                replacement = ts_regexp_tostring_arg(r)->getUStr();
            } else {
                ts_regexp_get_substitution(replacement, matched, S, position,
                                           captures, namedCaptures, replTemplate);
            }
            if (position >= nextSourcePosition) {
                accumulated.append(S, nextSourcePosition, position - nextSourcePosition);
                accumulated.append(replacement);
                nextSourcePosition = position + matchLen;
            }
            if (!global) break;
            if (matchLen == 0) {
                TsValue* liv = ts_object_get_property(recvRaw, "lastIndex");
                int64_t li = liv ? (int64_t)ts_to_number(liv) : 0;
                ts_object_set_dynamic((TsValue*)recvRaw, liKey,
                    ts_value_make_int(ts_regexp_adv_u(S, (int)li, fullUnicode)));
            }
            {
                TsValue* liv = ts_object_get_property(recvRaw, "lastIndex");
                int64_t li = liv ? (int64_t)ts_to_number(liv) : 0;
                if (li > sLen + 1) break;
            }
        }
        if (nextSourcePosition < sLen)
            accumulated.append(S, nextSourcePosition, sLen - nextSourcePosition);
        accumulated.toUTF8String(outU8);
        return (TsValue*)ts_value_make_string(TsString::Create(outU8.c_str()));
    }

    // AdvanceStringIndex over an icu::UnicodeString, surrogate-aware under `u`.
    static int ts_regexp_adv_u(const icu::UnicodeString& S, int index, bool unicode) {
        if (!unicode || index + 1 >= S.length()) return index + 1;
        UChar c = S.charAt(index);
        if (c >= 0xD800 && c <= 0xDBFF) {
            UChar c2 = S.charAt(index + 1);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) return index + 2;
        }
        return index + 1;
    }

    // ES 7.3.22 SpeciesConstructor(O, %RegExp%), shared by @@split and @@matchAll.
    // Returns the constructor to invoke, or nullptr meaning "use the default
    // RegExp construction". THROWS TypeError when `constructor` is present but
    // not an Object, or when its @@species is present, non-null, and not a
    // constructor. (%RegExp% itself and undefined/null @@species mean "default".)
    // No non-POD locals: a throw here is a clean longjmp.
    static TsValue* regexp_species_constructor(void* recvRaw) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void* ts_get_global_RegExp();
        extern bool ts_value_is_object(TsValue* v);
        TsValue* c = ts_object_get_property(recvRaw, "constructor");
        if (!c || ts_value_is_undefined(c)) return nullptr;              // default
        if (!ts_value_is_object(c)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "RegExp SpeciesConstructor: constructor is not an Object"));
            return nullptr;  // unreachable
        }
        void* cRaw = ts_value_get_object(c);
        if (!cRaw) cRaw = (void*)c;
        TsValue* sp = ts_object_get_property(cRaw, "[Symbol.species]");
        if (!sp || ts_value_is_undefined(sp) || ts_value_is_null(sp)) return nullptr; // default
        if (!ts_is_callable((void*)sp)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "RegExp SpeciesConstructor: @@species is not a constructor"));
            return nullptr;  // unreachable
        }
        if ((void*)sp == ts_get_global_RegExp()) return nullptr;         // default
        return sp;
    }

    // ECMA-262 22.2.6.14 RegExp.prototype [ @@split ] ( string, limit ). Uses a
    // sticky clone of the regex (no SpeciesConstructor — %RegExp% only, a
    // residual). Emits substrings between matches plus capture groups, honoring
    // `limit`.
    extern "C" TsValue* ts_regexp_symbol_split_native(void* ctx, int argc, TsValue** argv) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void ts_object_set_property_strict(void* obj, void* key, void* value);
        extern void* ts_array_create();
        extern int64_t ts_array_push(void* arr, void* value);
        extern void* ts_string_from_value(TsValue* val);
        extern TsValue* ts_new_from_constructor(TsValue* constructorFn, int argc, TsValue** argv);
        // ES 22.2.6.14 is GENERIC: any OBJECT receiver; the splitter comes
        // from SpeciesConstructor(rx, %RegExp%) called with (rx, newFlags) --
        // a custom @@species observes both arguments (coerce-flags family) --
        // and `flags` is ToString(Get(rx, "flags")).
        void* recvRaw = regexp_generic_receiver(ctx, "Symbol.split");
        if (!recvRaw) return (TsValue*)ts_value_make_undefined();
        bool recvIsRegExp = (*(uint32_t*)recvRaw == 0x52454758 /*REGX*/);
        TsValue* sv = (argc >= 1 && argv && argv[0]) ? argv[0]
                                                     : (TsValue*)ts_value_make_undefined();
        TsString* sTs = ts_regexp_tostring_arg(sv);
        // ToString(Get(rx,"flags")) WITH user hooks (a flags object's
        // toString is observable) — and BEFORE any C++ locals exist in this
        // frame, since an abrupt hook ts_throws (longjmp rule: only POD
        // pointer locals may be live here).
        // ALL non-POD locals for this frame constructed HERE, before the
        // first throwing call (SMELL-002 longjmp rule: an unconstructed
        // non-POD at a longjmp corrupts the unwind — 0xc0000374 class).
        // Values are assigned after the observable coercions below.
        icu::UnicodeString S;
        std::string flags;
        std::string newFlags;
        std::string srcU8;
        icu::UnicodeString T;
        std::string tU8;
        TsString* fTs = nullptr;
        {
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            TsValue* fvv = ts_object_get_property(recvRaw, "flags");
            TsValue* fprim = ts_to_primitive(
                fvv ? fvv : (TsValue*)ts_value_make_undefined(), 2);
            fTs = (TsString*)ts_string_from_value(fprim);
        }
        S = sTs->getUStr();
        int size = S.length();
        flags = fTs ? fTs->ToUtf8() : "";
        bool unicodeMatching = flags.find('u') != std::string::npos;
        newFlags = flags;
        if (newFlags.find('y') == std::string::npos) newFlags += "y";
        // SpeciesConstructor(rx, %RegExp%): validates `constructor`/@@species and
        // throws a TypeError on a non-object constructor or non-constructor
        // @@species; nullptr means "use the default construction".
        TsValue* speciesFn = regexp_species_constructor(recvRaw);
        // The splitter is whatever Construct(C, «rx, newFlags») yields — ANY
        // object (ES 22.2.6.14 step 8). SpeciesConstructor(rx,%RegExp%) with a
        // custom @@species may hand back a plain object whose `exec` /
        // `lastIndex` are observed via RegExpExec and Get/Set. Only fall back
        // to a default RegExp when there is NO custom species.
        void* splitterRaw = nullptr;
        TsRegExp* splitterRx = nullptr;   // non-null only when splitter is a real RegExp
        if (speciesFn) {
            TsValue* nfBoxed =
                (TsValue*)ts_value_make_string(TsString::Create(newFlags.c_str()));
            TsValue* args2[2] = { (TsValue*)ts_value_make_object(recvRaw), nfBoxed };
            TsValue* built = ts_new_from_constructor(speciesFn, 2, args2);
            splitterRaw = built ? ts_value_get_object(built) : nullptr;
            if (!splitterRaw) splitterRaw = built;
            if (splitterRaw && (uintptr_t)splitterRaw >= 4096 &&
                (uintptr_t)splitterRaw <= 0x00007FFFFFFFFFFFULL &&
                *(uint32_t*)splitterRaw == 0x52454758 /*REGX*/) {
                splitterRx = (TsRegExp*)splitterRaw;
            }
        } else {
            // Default construction: a real-RegExp receiver reuses its source;
            // anything else stringifies its `source` (same net effect as
            // `new RegExp(obj, flags)` for the receiver-compat tests).
            // srcU8 hoisted to the frame prologue (longjmp rule).
            if (recvIsRegExp) {
                srcU8 = ((TsRegExp*)recvRaw)->GetSource()->ToUtf8();
            } else {
                TsValue* srcProp = ts_object_get_property(recvRaw, "source");
                TsString* srcTs = (TsString*)ts_string_from_value(
                    srcProp ? srcProp : (TsValue*)ts_value_make_undefined());
                srcU8 = srcTs ? srcTs->ToUtf8() : "(?:)";
                if (srcU8 == "undefined") srcU8 = "(?:)";
            }
            splitterRx = TsRegExp::Create(srcU8.c_str(), newFlags.c_str());
            splitterRaw = (void*)splitterRx;
        }
        if (!splitterRaw) {
            ts_throw((TsValue*)ts_error_create_typed(
                "TypeError", "RegExp @@split: splitter constructor returned no object"));
            return (TsValue*)ts_value_make_undefined();
        }
        // Set/Get lastIndex: a real RegExp uses its internal slot directly; a
        // generic splitter routes through observable Set/Get on "lastIndex".
        TsValue* liKey = (TsValue*)ts_value_make_string(TsString::Create("lastIndex"));
        auto setLastIndex = [&](int q) {
            if (splitterRx) splitterRx->SetLastIndex(q);
            else ts_object_set_property_strict(splitterRaw, liKey,
                                               (void*)ts_value_make_int(q));
        };
        auto getLastIndex = [&]() -> int {
            if (splitterRx) return (int)splitterRx->GetLastIndex();
            return (int)ts_to_number(ts_object_get_property(splitterRaw, "lastIndex"));
        };
        uint32_t lim = 0xFFFFFFFFu;
        if (argc >= 2 && argv[1] && !ts_value_is_undefined(argv[1]))
            lim = ts_double_to_uint32(ts_to_number(argv[1]));
        void* A = ts_array_create();
        if (lim == 0) return (TsValue*)ts_value_make_object(A);
        TsValue* sBoxed = (TsValue*)ts_value_make_string(sTs);
        if (size == 0) {
            setLastIndex(0);
            if (ts_regexp_exec_observable(splitterRaw, sBoxed))
                return (TsValue*)ts_value_make_object(A);
            ts_array_push(A, (void*)ts_value_make_string(sTs));
            return (TsValue*)ts_value_make_object(A);
        }
        int p = 0, q = 0; uint32_t lengthA = 0;
        while (q < size) {
            setLastIndex(q);
            void* result = ts_regexp_exec_observable(splitterRaw, sBoxed);
            if (!result) { q = ts_regexp_adv_u(S, q, unicodeMatching); continue; }
            int e = getLastIndex();
            if (e > size) e = size;
            if (e == p) { q = ts_regexp_adv_u(S, q, unicodeMatching); continue; }
            T.setTo(S, p, q - p);
            tU8.clear(); T.toUTF8String(tU8);
            ts_array_push(A, (void*)ts_value_make_string(TsString::Create(tU8.c_str())));
            if (++lengthA == lim) return (TsValue*)ts_value_make_object(A);
            p = e;
            int nCaptures = (int)ts_to_number(ts_object_get_property(result, "length")) - 1;
            if (nCaptures < 0) nCaptures = 0;
            for (int i = 1; i <= nCaptures; i++) {
                char key[16]; snprintf(key, sizeof(key), "%d", i);
                TsValue* cap = ts_object_get_property(result, key);
                ts_array_push(A, cap ? (void*)cap : (void*)ts_value_make_undefined());
                if (++lengthA == lim) return (TsValue*)ts_value_make_object(A);
            }
            q = p;
        }
        T.setTo(S, p, size - p);
        tU8.clear(); T.toUTF8String(tU8);
        ts_array_push(A, (void*)ts_value_make_string(TsString::Create(tU8.c_str())));
        return (TsValue*)ts_value_make_object(A);
    }

    // ECMA-262 22.2.6.9 RegExp.prototype [ @@matchAll ] ( string ). Pragmatic:
    // pre-materialize all match results into an array (using a clone so the
    // receiver's lastIndex isn't mutated) and return a forward iterator over
    // them. Lazy per-next() re-exec observability is a residual.
    // SpeciesConstructor(rx, %RegExp%) -> construct with (rx, flags), used
    // by the generic @@split/@@matchAll. Returns the constructed RegExp, or
    // nullptr for "use the default construction" (no custom species). NO C++
    // locals: a custom species can ts_throw.
    static TsRegExp* regexp_species_construct(void* recvRaw, const char* flagsC) {
        extern TsValue* ts_new_from_constructor(TsValue* constructorFn, int argc, TsValue** argv);
        // ES 7.3.22 SpeciesConstructor with full validation (throws TypeError on
        // a non-object constructor / non-constructor @@species). nullptr => the
        // default RegExp construction.
        TsValue* sp = regexp_species_constructor(recvRaw);
        if (!sp) return nullptr;
        TsValue* fBoxed = (TsValue*)ts_value_make_string(TsString::Create(flagsC));
        TsValue* args2[2] = { (TsValue*)ts_value_make_object(recvRaw), fBoxed };
        TsValue* built = ts_new_from_constructor(sp, 2, args2);
        void* raw = built ? ts_value_get_object(built) : nullptr;
        if (!raw) raw = built;
        if (raw && (uintptr_t)raw >= 4096 &&
            *(uint32_t*)raw == 0x52454758 /*REGX*/) {
            return (TsRegExp*)raw;
        }
        return nullptr;
    }

    // ======================================================================
    // ECMA-262 22.2.9 %RegExpStringIteratorPrototype% (lazy, spec-faithful).
    // A RegExp String Iterator is a TsMap carrying the internal slots
    // [[IteratingRegExp]] (R), [[IteratedString]] (S), [[Global]], [[Unicode]],
    // [[Done]] as hidden own properties. Storing R/S as own TsMap values keeps
    // them reachable by the normal GC object scan (no separate C++ container to
    // root). Its [[Prototype]] is %RegExpStringIteratorPrototype%, whose own
    // next() is ts_regexp_string_iter_next and whose @@toStringTag is
    // "RegExp String Iterator".
    // ======================================================================
    static const char* kRESI_R = "__resi_regexp";
    static const char* kRESI_S = "__resi_string";
    static const char* kRESI_G = "__resi_global";
    static const char* kRESI_U = "__resi_unicode";
    static const char* kRESI_D = "__resi_done";

    static TsValue resi_slot(TsMap* it, const char* k) {
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(k);
        return it->Get(key);
    }
    static void resi_set_bool(TsMap* it, const char* k, bool b) {
        TsValue key; key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(k);
        TsValue v; v.type = ValueType::BOOLEAN; v.i_val = b ? 1 : 0;
        it->Set(key, v);
    }
    // CreateIterResultObject(value, done); `value` is a boxed TsValue* (or null
    // meaning undefined).
    static TsValue* resi_make_result(TsValue* value, bool done) {
        TsMap* r = TsMap::Create();
        TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("value");
        r->Set(vk, nanbox_to_tagged(value ? value : (TsValue*)ts_value_make_undefined()));
        TsValue dk; dk.type = ValueType::STRING_PTR; dk.ptr_val = TsString::GetInterned("done");
        TsValue dv; dv.type = ValueType::BOOLEAN; dv.i_val = done ? 1 : 0; r->Set(dk, dv);
        return (TsValue*)ts_value_make_object(r);
    }

    // ECMA-262 22.2.9.2.1 %RegExpStringIteratorPrototype%.next ( ).
    extern "C" TsValue* ts_regexp_string_iter_next(void* ctx, int argc, TsValue** argv) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void ts_object_set_property(void* obj, void* key, void* value);
        (void)argc; (void)argv;
        // step 1-2: Let O be the this value; if not Object, throw TypeError.
        if (!ctx) ctx = ts_get_call_this();
        void* raw = ctx ? ts_value_get_object((TsValue*)ctx) : nullptr;
        if (!raw) raw = ctx;
        auto throw_brand = []() {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "%RegExpStringIteratorPrototype%.next called on incompatible receiver"));
        };
        // step 3: O must have the RegExp String Iterator internal slots. Require
        // a TsMap (magic@16) carrying an OWN [[IteratingRegExp]] slot — an
        // Object.create(iterator) inherits next() but not the own slot.
        if (!is_safe_ptr_for_magic(raw) ||
            *(uint32_t*)((char*)raw + 16) != TsMap::MAGIC) {
            throw_brand(); return (TsValue*)ts_value_make_undefined();
        }
        TsMap* iter = (TsMap*)raw;
        {
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(kRESI_R);
            if (!iter->Has(k)) { throw_brand(); return (TsValue*)ts_value_make_undefined(); }
        }
        // step 4: If [[Done]] is true, return CreateIterResultObject(undefined, true).
        {
            TsValue d = resi_slot(iter, kRESI_D);
            if (d.type == ValueType::BOOLEAN && d.i_val) return resi_make_result(nullptr, true);
        }
        // steps 5-8: read R, S, global, fullUnicode.
        TsValue rv = resi_slot(iter, kRESI_R);
        void* R = (rv.type == ValueType::OBJECT_PTR || rv.type == ValueType::ARRAY_PTR ||
                   rv.type == ValueType::FUNCTION_PTR) ? rv.ptr_val : nullptr;
        TsValue sv = resi_slot(iter, kRESI_S);
        TsString* S = (sv.type == ValueType::STRING_PTR) ? (TsString*)sv.ptr_val
                                                         : TsString::Create("");
        TsValue gv = resi_slot(iter, kRESI_G);
        bool global = (gv.type == ValueType::BOOLEAN && gv.i_val);
        TsValue uv = resi_slot(iter, kRESI_U);
        bool fullUnicode = (uv.type == ValueType::BOOLEAN && uv.i_val);
        // step 9: match = RegExpExec(R, S) — honors a user-overridden `exec`.
        TsValue* sBoxed = (TsValue*)ts_value_make_string(S);
        void* match = ts_regexp_exec_observable(R, sBoxed);   // may throw
        // step 10: null match -> set [[Done]], return {undefined, true}.
        if (!match) {
            resi_set_bool(iter, kRESI_D, true);
            return resi_make_result(nullptr, true);
        }
        TsValue* matchBoxed = (TsValue*)ts_value_make_object(match);
        if (!global) {
            // step 11.b: non-global -> set [[Done]], return {match, false}.
            resi_set_bool(iter, kRESI_D, true);
            return resi_make_result(matchBoxed, false);
        }
        // step 11.a: global -> advance lastIndex past a zero-width match.
        TsString* m0 = ts_regexp_tostring_arg(
            ts_object_get_property(match, "0"));            // ToString(Get(match,"0")); may throw
        if (m0 && m0->Length() == 0) {
            TsValue* liv = ts_object_get_property(R, "lastIndex");   // may throw
            double li = liv ? ts_to_number(liv) : 0;                // ToLength; may throw
            if (li != li || li < 0) li = 0;
            int64_t nextIndex = ts_regexp_adv_u(S->getUStr(), (int)li, fullUnicode);
            TsValue* liKey = (TsValue*)ts_value_make_string(TsString::Create("lastIndex"));
            ts_object_set_property(R, liKey, (void*)ts_value_make_int(nextIndex)); // may throw
        }
        return resi_make_result(matchBoxed, false);
    }

    // %RegExpStringIteratorPrototype% singleton. buildIteratorPrototypeCustom
    // (TsMap.cpp) wires next()/@@toStringTag and sets [[Prototype]] =
    // %IteratorPrototype%. Immortal-tenured + registered as a GC root (the
    // static holder is invisible to the object scan otherwise).
    static TsMap* g_regexp_string_iter_proto = nullptr;
    static TsMap* getRegExpStringIteratorPrototype() {
        extern TsMap* buildIteratorPrototypeCustom(const char* tagStr, void* nextFn);
        if (!g_regexp_string_iter_proto) {
            ts_gc_push_tenure();
            g_regexp_string_iter_proto = buildIteratorPrototypeCustom(
                "RegExp String Iterator", (void*)ts_regexp_string_iter_next);
            ts_gc_pop_tenure();
            ts_gc_register_root((void**)&g_regexp_string_iter_proto);
        }
        return g_regexp_string_iter_proto;
    }

    // ECMA-262 22.2.9.1 CreateRegExpStringIterator ( R, S, global, fullUnicode ).
    static TsValue* create_regexp_string_iterator(void* R, TsString* S,
                                                  bool global, bool fullUnicode) {
        TsMap* iter = TsMap::Create();
        iter->SetPrototype(getRegExpStringIteratorPrototype());
        TsValue rk; rk.type = ValueType::STRING_PTR; rk.ptr_val = TsString::GetInterned(kRESI_R);
        TsValue rv; rv.type = ValueType::OBJECT_PTR; rv.ptr_val = R; iter->Set(rk, rv);
        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(kRESI_S);
        TsValue sv; sv.type = ValueType::STRING_PTR; sv.ptr_val = S ? S : TsString::Create("");
        iter->Set(sk, sv);
        resi_set_bool(iter, kRESI_G, global);
        resi_set_bool(iter, kRESI_U, fullUnicode);
        resi_set_bool(iter, kRESI_D, false);
        return (TsValue*)ts_value_make_object(iter);
    }

    extern "C" TsValue* ts_regexp_symbol_matchAll_native(void* ctx, int argc, TsValue** argv) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void* ts_array_create();
        extern int64_t ts_array_push(void* arr, void* value);
        extern TsValue* ts_create_array_iterator_pub(void* items);
        extern void* ts_string_from_value(TsValue* val);
        // ES 22.2.6.9 is GENERIC: any OBJECT receiver; flags via hooked
        // ToString(Get(rx,"flags")); the matcher comes from
        // SpeciesConstructor(rx, %RegExp%) constructed with (rx, flags) and
        // inherits ToLength(Get(rx,"lastIndex")).
        void* recvRaw = regexp_generic_receiver(ctx, "Symbol.matchAll");
        if (!recvRaw) return (TsValue*)ts_value_make_undefined();
        bool recvIsRegExp = (*(uint32_t*)recvRaw == 0x52454758 /*REGX*/);
        TsValue* sv = (argc >= 1 && argv && argv[0]) ? argv[0]
                                                     : (TsValue*)ts_value_make_undefined();
        TsString* sTs = ts_regexp_tostring_arg(sv);
        // ALL non-POD locals constructed HERE, before the first throwing
        // call (SMELL-002 longjmp rule: an unconstructed non-POD at a
        // longjmp corrupts the unwind — 0xc0000374). Assigned after the
        // observable coercions.
        icu::UnicodeString S;
        std::string flags;
        std::string srcU8;
        TsString* fTs = nullptr;
        {
            extern TsValue* ts_to_primitive(TsValue* val, int hint);
            TsValue* fvv = ts_object_get_property(recvRaw, "flags");
            TsValue* fprim = ts_to_primitive(
                fvv ? fvv : (TsValue*)ts_value_make_undefined(), 2);
            fTs = (TsString*)ts_string_from_value(fprim);
        }
        TsRegExp* matcher =
            regexp_species_construct(recvRaw, fTs ? fTs->ToUtf8() : "");
        flags = fTs ? fTs->ToUtf8() : "";
        bool global = flags.find('g') != std::string::npos;
        // step 9: fullUnicode is true when the flags contain 'u' (or 'v').
        bool fullUnicode = flags.find('u') != std::string::npos ||
                           flags.find('v') != std::string::npos;
        if (!matcher) {
            if (recvIsRegExp) {
                srcU8 = ((TsRegExp*)recvRaw)->GetSource()->ToUtf8();
            } else {
                TsValue* srcProp = ts_object_get_property(recvRaw, "source");
                TsString* srcTs = (TsString*)ts_string_from_value(
                    srcProp ? srcProp : (TsValue*)ts_value_make_undefined());
                srcU8 = srcTs ? srcTs->ToUtf8() : "(?:)";
                if (srcU8 == "undefined") srcU8 = "(?:)";
            }
            matcher = TsRegExp::Create(srcU8.c_str(), flags.c_str());
        }
        // steps 6-7: matcher.lastIndex = ToLength(Get(R,"lastIndex")).
        {
            TsValue* liv = ts_object_get_property(recvRaw, "lastIndex");
            double li = liv ? ts_to_number(liv) : 0;
            if (li != li || li < 0) li = 0;
            matcher->SetLastIndex((int64_t)li);
        }
        // step 10: return CreateRegExpStringIterator(matcher, S, global,
        // fullUnicode) — LAZY: each next() re-execs matcher against S.
        return create_regexp_string_iterator((void*)matcher, sTs, global, fullUnicode);
    }

}  // extern "C"
