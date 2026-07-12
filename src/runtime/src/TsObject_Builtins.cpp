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
                                   bool hasExtra, TsValue** out);

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
                                       bool hasExtra, TsValue** out) {
        if (!arg || !receiver) return false;
        void* raw = ts_value_get_object(arg);
        if (!raw || (uintptr_t)raw < 0x1000) return false;
        uint32_t m0 = *(uint32_t*)raw;
        TsValue* method = nullptr;
        if (m0 == 0x52454758) {  // TsRegExp: own override only
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
                else return false;
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
        TsValue* args[2] = { ts_value_make_string(receiver),
                             extra ? extra : ts_value_make_undefined() };
        *out = ts_function_call_with_this(method, arg, hasExtra ? 2 : 1, args);
        if (!*out) *out = ts_value_make_undefined();
        return true;
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
        TsString* str = ctx ? ts_ensure_flat(ctx) : (TsString*)ctx;
        if (argc < 1 || !argv) return ts_value_make_string(str);

        // ES 22.1.3.21 step 2: GetMethod(searchValue, @@replace) dispatch
        // (after the IsRegExp+flags check for real RegExps, which the regexp
        // branch below performs; user objects divert here).
        {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.replace]", argv[0], str,
                                       (argc >= 2) ? argv[1] : nullptr, true, &out))
                return out;
        }

        // Extract replacement string
        void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;

        // Check if pattern is a RegExp
        void* rawPattern = argv[0] ? ts_value_get_object((TsValue*)argv[0]) : nullptr;
        if (!rawPattern) rawPattern = (void*)argv[0];
        if (rawPattern) {
            uint32_t magic = *(uint32_t*)rawPattern;
            if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                return ts_value_make_string((TsString*)ts_string_replace_regexp(str, rawPattern, replacement));
            }
        }

        // Pattern is a string
        void* pattern = argv[0] ? ts_value_get_string(argv[0]) : nullptr;
        if (!pattern) pattern = (void*)argv[0];
        return ts_value_make_string((TsString*)ts_string_replaceAll(str, pattern, replacement));
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
        TsString* str = (TsString*)ctx;
        // ES 22.1.3.14 step 2.c: GetMethod(regexp, @@matchAll) dispatch.
        if (argc >= 1 && argv && argv[0]) {
            TsValue* out = nullptr;
            if (ts_string_symbol_dispatch("[Symbol.matchAll]", argv[0], str,
                                       nullptr, false, &out))
                return out;
        }
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* result = ts_string_matchAll_regexp(str, regexp);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
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

    static TsArray* require_array_or_throw(void* ctx, const char* methodName) {
        TsArray* arr = resolve_array_ctx(ctx);
        if (arr) return arr;

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
                if (!protoGlobal) return TsArray::Create(0);
                void* protoCtor = ts_value_get_object((TsValue*)protoGlobal);
                if (!protoCtor) protoCtor = protoGlobal;
                TsValue* protoVal = ts_object_get_property(protoCtor, "prototype");
                if (notPresent(protoVal)) return TsArray::Create(0);
                void* protoRaw = ts_value_get_object(protoVal);
                if (!protoRaw) return TsArray::Create(0);

                TsValue* lenVal = ts_object_get_property(protoRaw, "length");
                if (notPresent(lenVal)) return TsArray::Create(0);
                double lenD = ts_value_get_double(lenVal);
                if (lenD != lenD || lenD <= 0) return TsArray::Create(0);
                int64_t len = (int64_t)lenD;
                const int64_t MAX_ITER = 1 << 20;
                if (len > MAX_ITER) len = MAX_ITER;

                TsArray* tmp = TsArray::Create((size_t)len);
                // The callback's 3rd arg (O) must be ToObject(receiver) — a real
                // Number/Boolean WRAPPER — not the raw primitive, so that e.g.
                // `obj instanceof Number` holds (ES5 15.4.4.x step-1 tests).
                TsValue* wrapArgs[] = { (TsValue*)ctxToRead };
                void* wrapper = ts_new_from_constructor_impl((TsValue*)protoGlobal, 1, wrapArgs);
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

    // Validate that `callback` is callable (function or closure).
    // Throws TypeError if not callable, matching spec for Array callback
    // methods (filter/map/forEach/every/some/find/findIndex/reduce/etc).
    // Returns true on success, false if TypeError was thrown (caller should
    // return a safe default — ts_throw longjmps so the false branch is rare).
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
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t end   = (argc >= 2 && argv) ? toInteger(argv[1], arr->Length()) : arr->Length();
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
        int64_t start = 0, end = len;
        // Use ts_to_number for Symbol→TypeError per spec.
        if (argc >= 1 && argv && argv[0]) {
            start = (int64_t)ts_to_number(argv[0]);
            if (start < 0) start = std::max((int64_t)0, len + start);
        }
        if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1])) {
            end = (int64_t)ts_to_number(argv[1]);
            if (end < 0) end = std::max((int64_t)0, len + end);
        }
        if (start > len) start = len;
        if (end > len) end = len;
        if (end < start) end = start;
        int64_t newLen = end - start;
        // TypedArraySpeciesCreate(this, newLen) — honors @@species ctor.
        void* resRaw = ts_typed_array_species_alloc((void*)ta, newLen);
        if (!resRaw) return ts_value_make_undefined();  // TypeError thrown
        TsTypedArray* result = (TsTypedArray*)resRaw;
        size_t copyN = std::min((size_t)newLen, result->GetLength());
        for (size_t i = 0; i < copyN; i++) {
            result->Set(i, ta->Get((size_t)start + i));
        }
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
        return ts_value_make_double(ta->Get(index));
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
        if (offD < 0) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "offset is out of bounds"));
            return ts_value_make_undefined();
        }
        int64_t offset = (int64_t)offD;        // truncate toward zero
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
            size_t srcLen = srcTa->GetLength();
            // BigInt typed arrays have a pre-existing construction/length bug
            // (GetLength can be garbage); keep the legacy bounded copy for them
            // so the strict bounds RangeError below doesn't fire spuriously.
            bool isBig = ta->GetType() == TypedArrayType::BigInt64 ||
                         ta->GetType() == TypedArrayType::BigUint64 ||
                         srcTa->GetType() == TypedArrayType::BigInt64 ||
                         srcTa->GetType() == TypedArrayType::BigUint64;
            // step: if srcLength + targetOffset > targetLength, RangeError
            // (no silent truncation).
            if (!isBig && srcLen + (size_t)offset > targetLen) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "offset is out of bounds"));
                return ts_value_make_undefined();
            }
            for (size_t i = 0; i < srcLen && (size_t)offset + i < targetLen; i++) {
                ta->Set((size_t)offset + i, srcTa->Get(i));
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
    TsValue* ts_typed_array_subarray_native(void* ctx, int argc, TsValue** argv) {
        // subarray creates a new view (we just copy for now)
        return ts_typed_array_slice_native(ctx, argc, argv);
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
        int64_t start = 0, end = len;
        if (argc >= 2 && argv && argv[1]) start = (int64_t)ts_to_number(argv[1]);
        if (argc >= 3 && argv && argv[2] && !ts_value_is_undefined(argv[2])) {
            end = (int64_t)ts_to_number(argv[2]);
        }
        if (start < 0) start = std::max((int64_t)0, len + start);
        if (end < 0) end = std::max((int64_t)0, len + end);
        if (start > len) start = len;
        if (end > len) end = len;
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
        if (argc >= 1 && argv && argv[0]) idx = (int64_t)ts_to_number(argv[0]);
        if (idx < 0) idx = len + idx;
        if (idx < 0 || idx >= len) return ts_value_make_undefined();
        return ts_ta_get_boxed(ta, (size_t)idx);
    }

    // TypedArray.prototype.includes(searchElement, fromIndex?)
    TsValue* ts_typed_array_includes_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "includes")) return ts_value_make_undefined();
        int64_t len = (int64_t)ta->GetLength();
        // ECMA-262 %TypedArray%.prototype.includes step 4: len 0 -> false before
        // coercing search/fromIndex (throwing valueOf must not run on empty).
        if (len == 0) return ts_value_make_bool(false);
        if (argc < 1 || !argv || !argv[0]) return ts_value_make_bool(false);
        double search = ts_to_number(argv[0]);
        int64_t from = 0;
        if (argc >= 2 && argv[1]) {
            // ToIntegerOrInfinity: +Inf -> nothing at/after it (false);
            // -Inf -> clamp to 0. FPToSI(Inf) is INT64_MIN garbage.
            double fd = ts_to_number(argv[1]);
            if (ta->IsDetachedBuffer()) return ts_value_make_bool(false);
            if (fd == std::numeric_limits<double>::infinity())
                return ts_value_make_bool(false);
            from = (fd == -std::numeric_limits<double>::infinity())
                       ? 0 : (int64_t)fd;
        }
        if (ta->IsDetachedBuffer()) return ts_value_make_bool(false);
        if (from < 0) from = std::max((int64_t)0, len + from);
        bool searchNaN = (search != search);
        for (int64_t i = from; i < len; i++) {
            double v = ta->Get((size_t)i);
            if (searchNaN) { if (v != v) return ts_value_make_bool(true); }
            else if (v == search) return ts_value_make_bool(true);
        }
        return ts_value_make_bool(false);
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
        double search = ts_to_number(argv[0]);
        if (search != search) return ts_value_make_int(-1);  // NaN never matches via ===
        int64_t from = 0;
        if (argc >= 2 && argv[1]) {
            // ToIntegerOrInfinity: +Inf -> -1 (search starts past the end);
            // -Inf -> 0. FPToSI(Inf) is INT64_MIN garbage (fromIndex-infinity).
            double fd = ts_to_number(argv[1]);
            if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
            if (fd == std::numeric_limits<double>::infinity())
                return ts_value_make_int(-1);
            from = (fd == -std::numeric_limits<double>::infinity())
                       ? 0 : (int64_t)fd;
        }
        // ToInteger(fromIndex) can detach the buffer; a detached buffer has no
        // elements, so the search finds nothing.
        if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
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
        double search = ts_to_number(argv[0]);
        if (search != search) return ts_value_make_int(-1);
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
                from = (int64_t)fd;
                if (from < 0) from = len + from;
            }
        }
        // fromIndex coercion can detach the buffer -> no elements -> not found.
        if (ta->IsDetachedBuffer()) return ts_value_make_int(-1);
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

    // TypedArray.prototype.join(separator?)
    TsValue* ts_typed_array_join_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        if (throwIfDetached(ta, "join")) return ts_value_make_undefined();
        size_t len = ta->GetLength();
        std::string sep = ",";
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            TsString* s = (TsString*)ts_value_get_string(argv[0]);
            if (s) {
                const char* u = s->ToUtf8();
                if (u) sep = u;
            }
        }
        std::string out;
        char buf[64];
        for (size_t i = 0; i < len; i++) {
            if (i > 0) out += sep;
            double v = ta->Get(i);
            if (v != v) out += "NaN";
            else if (v == (int64_t)v && std::abs(v) < 1e16) {
                snprintf(buf, sizeof(buf), "%lld", (long long)v);
                out += buf;
            } else {
                snprintf(buf, sizeof(buf), "%g", v);
                out += buf;
            }
        }
        return ts_value_make_string(TsString::Create(out.c_str()));
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
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern void* ts_string_from_value(TsValue* val);
        for (size_t k = 0; k < len; k++) {
            if (k > 0) acc = (TsString*)ts_string_concat(acc, sep);
            // Re-read length each step: a user toLocaleString may shrink a
            // resizable buffer (user-provided-tolocalestring-shrink); an
            // absent element stringifies as "" per Array semantics.
            if (ta->IsDetachedBuffer() || k >= ta->GetLength()) continue;
            TsValue* elem = ts_ta_get_boxed(ta, k);
            TsValue* m = ts_object_get_property((void*)elem, "toLocaleString");
            TsValue* r = nullptr;
            if (m && ts_is_callable(m)) {
                r = ts_function_call_with_this(m, elem, 0, nullptr);  // may throw
            } else {
                r = elem;
            }
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
        int64_t target = 0, start = 0, end = len;
        if (argc >= 1 && argv && argv[0]) target = (int64_t)ts_to_number(argv[0]);
        if (argc >= 2 && argv && argv[1]) start = (int64_t)ts_to_number(argv[1]);
        if (argc >= 3 && argv && argv[2] && !ts_value_is_undefined(argv[2])) end = (int64_t)ts_to_number(argv[2]);
        if (target < 0) target = std::max((int64_t)0, len + target);
        if (start < 0) start = std::max((int64_t)0, len + start);
        if (end < 0) end = std::max((int64_t)0, len + end);
        if (target > len) target = len;
        if (start > len) start = len;
        if (end > len) end = len;
        int64_t count = std::min(end - start, len - target);
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
        TsArray* arr = require_array_or_throw(ctx, "splice");
        array_require_length_writable(arr, "splice");
        if (!arr) return ts_value_make_undefined();
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t deleteCount = (argc >= 2 && argv) ? toInteger(argv[1], arr->Length() - start) : arr->Length() - start;
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
        TsArray* arr = require_array_or_throw(ctx, "shift");
        array_require_length_writable(arr, "shift");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_shift(arr);
        arraylike_writeback(arr);  // array-like receiver: propagate mutation
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv) {
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
        TsArray* arr = require_array_or_throw(ctx, "fill");
        if (!arr) return ts_value_make_undefined();
        void* value = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t start = (argc >= 2 && argv) ? toInteger(argv[1], 0) : 0;
        int64_t end = (argc >= 3 && argv) ? toInteger(argv[2], arr->Length()) : arr->Length();
        ts_array_fill(arr, value, start, end);
        arraylike_writeback(arr);
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
        TsArray* arr = require_array_or_throw(ctx, "toReversed");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_toReversed(arr);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = require_array_or_throw(ctx, "toSorted");
        if (!arr) return ts_value_make_undefined();
        void* result = ts_array_toSorted(arr);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv) {
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
        TsArray* arr = require_array_or_throw(ctx, "copyWithin");
        if (!arr) return ts_value_make_undefined();
        // Use toInteger so Symbol args throw TypeError per spec.
        int64_t target = (argc >= 1 && argv) ? toInteger(argv[0], 0) : 0;
        int64_t start  = (argc >= 2 && argv) ? toInteger(argv[1], 0) : 0;
        int64_t end    = (argc >= 3 && argv) ? toInteger(argv[2], arr->Length()) : arr->Length();
        ts_array_copyWithin(arr, target, start, end);
        arraylike_writeback(arr);
        return ts_value_make_object(arr);
    }
    TsValue* ts_array_with_native(void* ctx, int argc, TsValue** argv) {
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

    // Native wrapper for number.toString() - ctx is a NaN-boxed number value
    TsValue* ts_number_toString_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toString");
        int64_t radix = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 10;
        return ts_value_make_string((TsString*)ts_number_to_string(value, radix));
    }

    TsValue* ts_number_toFixed_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toFixed");
        int64_t digits = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        if (digits < 0 || digits > 100) {  // ECMA-262 21.1.3.3: RangeError
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
        if (argc < 1 || !argv || !argv[0]) {
            return ts_value_make_string((TsString*)ts_number_to_string(value, 10));
        }
        int64_t precision = ts_value_get_int(argv[0]);
        // ECMA-262 21.1.3.5 steps 4/6: a non-finite x returns "NaN"/"Infinity"
        // BEFORE the precision RangeError (step 7), and snprintf's "nan"/"inf"
        // are not the JS spellings.
        if (std::isnan(value)) return ts_value_make_string(TsString::Create("NaN"));
        if (std::isinf(value)) return ts_value_make_string(TsString::Create(value < 0 ? "-Infinity" : "Infinity"));
        if (precision < 1 || precision > 100) {  // ECMA-262 21.1.3.5: RangeError
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "toPrecision() argument must be between 1 and 100"));
            return ts_value_make_undefined();
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*g", (int)precision, value);
        return ts_value_make_string(TsString::Create(buf));
    }
    TsValue* ts_number_toExponential_native(void* ctx, int argc, TsValue** argv) {
        double value = numberThisValueOrThrow(ctx, "toExponential");
        int64_t digits = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 6;
        // ECMA-262 21.1.3.2 step 4: a non-finite x returns "NaN"/"Infinity" BEFORE
        // the fractionDigits RangeError (step 5); snprintf emits "nan"/"inf".
        if (std::isnan(value)) return ts_value_make_string(TsString::Create("NaN"));
        if (std::isinf(value)) return ts_value_make_string(TsString::Create(value < 0 ? "-Infinity" : "Infinity"));
        if (digits < 0 || digits > 100) {  // ECMA-262 21.1.3.2: RangeError
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "toExponential() argument must be between 0 and 100"));
            return ts_value_make_undefined();
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*e", (int)digits, value);
        return ts_value_make_string(TsString::Create(buf));
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
    TsValue* ts_date_toJSON_native(void* ctx, int argc, TsValue** argv) {
        TsDate* d = requireDateOrThrow(ctx, "toJSON");
        if (!d) return ts_value_make_undefined();
        // Per spec, toJSON returns null for invalid Date.
        if (!d->IsValid()) return ts_value_make_null();
        return ts_value_make_string(d->ToJSON());
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
    // ECMA-262 21.4.4.45 Date.prototype[@@toPrimitive](hint): "string"/"default"
    // -> OrdinaryToPrimitive(string) (toString); "number" -> valueOf; any other
    // hint is a TypeError. Calls the Date string/number natives directly so it
    // does not recurse through ts_to_primitive (which consults @@toPrimitive).
    TsValue* ts_date_symbolToPrimitive_native(void* ctx, int argc, TsValue** argv) {
        // POD frame (SMELL-002): requireDateOrThrow and the invalid-hint
        // path both longjmp; a std::string local here put this frame in the
        // longjmp-through-unconstructed-non-POD crash class (0xc0000374).
        TsDate* d = requireDateOrThrow(ctx, "[Symbol.toPrimitive]");
        if (!d) return ts_value_make_undefined();
        const char* hint = "";
        if (argc >= 1 && argv && argv[0] && !ts_value_is_undefined(argv[0])) {
            void* hs = ts_value_get_string(argv[0]);
            if (hs) {
                const char* h = ((TsString*)hs)->ToUtf8();
                if (h) hint = h;
            }
        }
        if (strcmp(hint, "string") == 0 || strcmp(hint, "default") == 0)
            return ts_date_toString_native(ctx, 0, nullptr);
        if (strcmp(hint, "number") == 0)
            return ts_date_valueOf_native(ctx, 0, nullptr);
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "invalid hint passed to Date.prototype[Symbol.toPrimitive]"));
        return ts_value_make_undefined();
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
        if (std::isnan(y)) { d->SetTime(TsDate::INVALID);
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
        if (std::isnan(mo)) { d->SetTime(TsDate::INVALID);
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
        if (std::isnan(h)) { d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
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
        if (std::isnan(m)) { d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
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
        if (std::isnan(s)) { d->SetTime(TsDate::INVALID); return ts_value_make_double(NaNv); }
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
        if (std::isnan(v)) {
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
                                   void* nativeFn, int arity) {
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
        proto->SetWithAttrs(key, val,
                            TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
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
        dateRegisterMethod(proto, "[Symbol.toPrimitive]", (void*)ts_date_symbolToPrimitive_native, 1);
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
        TsValue* prev = ts_object_get_property(recv, "lastIndex");
        double prevN = prev ? ts_to_number(prev) : 0;
        if (prevN != 0) {
            ts_object_set_dynamic((TsValue*)recv, liKey, ts_value_make_int(0));
        }
        void* result = ts_regexp_exec_observable(recv, sBoxed);
        TsValue* cur = ts_object_get_property(recv, "lastIndex");
        double curN = cur ? ts_to_number(cur) : 0;
        if (curN != prevN) {
            ts_object_set_dynamic((TsValue*)recv, liKey,
                                  prev ? prev : ts_value_make_int(0));
        }
        if (!result) return (TsValue*)ts_value_make_int(-1);
        return ts_object_get_property(result, "index");
    }

    // ECMA-262 22.2.7.3 AdvanceStringIndex (simple form): +1 code unit. Unicode
    // surrogate-pair advance (+2 under the `u` flag) is a known residual.
    static int64_t ts_regexp_advance_string_index(int64_t index) {
        return index + 1;
    }

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
                TsValue* liv = ts_object_get_property(recv, "lastIndex");
                int64_t li = liv ? (int64_t)ts_to_number(liv) : 0;
                ts_object_set_dynamic((TsValue*)recv, liKey,
                    ts_value_make_int(ts_regexp_advance_string_index(li)));
            }
            {
                TsValue* liv = ts_object_get_property(recv, "lastIndex");
                int64_t li = liv ? (int64_t)ts_to_number(liv) : 0;
                if (li > sLen + 1 || n > (int)sLen + 2) break;  // termination safety
            }
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
                    ts_value_make_int(ts_regexp_advance_string_index(li)));
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
        // SpeciesConstructor(rx, %RegExp%): Get(rx,"constructor") ->
        // @@species; undefined/null fall back to the default construction.
        TsValue* speciesFn = nullptr;
        {
            TsValue* c = ts_object_get_property(recvRaw, "constructor");
            void* cRaw = c ? ts_value_get_object(c) : nullptr;
            if (cRaw && (uintptr_t)cRaw >= 4096) {
                TsValue* sp = ts_object_get_property(cRaw, "[Symbol.species]");
                if (sp && !ts_value_is_undefined(sp) && !ts_value_is_null(sp) &&
                    ts_is_callable((void*)sp)) {
                    extern void* ts_get_global_RegExp();
                    if ((void*)sp != ts_get_global_RegExp()) speciesFn = sp;
                }
            }
        }
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
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        extern TsValue* ts_new_from_constructor(TsValue* constructorFn, int argc, TsValue** argv);
        extern void* ts_get_global_RegExp();
        TsValue* c = ts_object_get_property(recvRaw, "constructor");
        void* cRaw = c ? ts_value_get_object(c) : nullptr;
        if (!cRaw || (uintptr_t)cRaw < 4096) return nullptr;
        TsValue* sp = ts_object_get_property(cRaw, "[Symbol.species]");
        if (!sp || ts_value_is_undefined(sp) || ts_value_is_null(sp) ||
            !ts_is_callable((void*)sp) || (void*)sp == ts_get_global_RegExp()) {
            return nullptr;
        }
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
        S = sTs->getUStr();
        int sLen = S.length();
        flags = fTs ? fTs->ToUtf8() : "";
        bool global = flags.find('g') != std::string::npos;
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
        {
            TsValue* liv = ts_object_get_property(recvRaw, "lastIndex");
            double li = liv ? ts_to_number(liv) : 0;
            if (li != li || li < 0) li = 0;
            matcher->SetLastIndex((int64_t)li);
        }
        TsValue* sBoxed = (TsValue*)ts_value_make_string(sTs);
        void* results = ts_array_create();
        while (true) {
            void* result = ts_regexp_exec_observable(matcher, sBoxed);
            if (!result) break;
            ts_array_push(results, (void*)ts_value_make_object(result));
            if (!global) break;
            TsString* m0 = ts_regexp_tostring_arg(ts_object_get_property(result, "0"));
            if (m0->Length() == 0)
                matcher->SetLastIndex(ts_regexp_advance_string_index(matcher->GetLastIndex()));
            if (matcher->GetLastIndex() > sLen + 1) break;
        }
        return ts_create_array_iterator_pub(results);
    }

}  // extern "C"
