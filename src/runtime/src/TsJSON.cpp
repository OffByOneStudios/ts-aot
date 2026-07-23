#include <csetjmp>
#include "TsJSON.h"
#include "TsNanBox.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsDate.h"
#include "TsRegExp.h"
#include "TsProxy.h"
#include "TsClosure.h"
#include "TsFlatObject.h"
#include "TsBoxedPrimitives.h"
#include "TsTyped.h"
#include "GC.h"
#include <nlohmann/json.hpp>
#include <unicode/unistr.h>
#include <string>
#include <cstring>
#include <cstdio>

#include <set>
#include <vector>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cmath>

// nlohmann is used ONLY by JSON.parse now. ECMA-262 25.5.2 JSON.stringify is
// implemented below as a direct, spec-order serializer over live JS values
// (SerializeJSONProperty / SerializeJSONObject / SerializeJSONArray), because
// function replacers, per-property [[Get]] observability, toJSON ordering and
// abrupt completions cannot be expressed over a materialized JSON tree.
using json = nlohmann::ordered_json;

static TsValue json_to_ts(const json& j) {
    if (j.is_null()) {
        // JS null, NOT undefined: OBJECT_PTR with a null pointer is the
        // struct representation nanbox_from_tagged maps to NANBOX_NULL.
        // (TsValue(nullptr) is the UNDEFINED ctor — JSON.parse("null")
        // returned undefined at every nesting level.)
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));
        v.type = ValueType::OBJECT_PTR;
        v.ptr_val = nullptr;
        return v;
    }
    if (j.is_boolean()) return TsValue(j.get<bool>());
    if (j.is_number_integer()) {
        return TsValue((int64_t)j.get<int64_t>());
    }
    if (j.is_number_float()) {
        return TsValue(j.get<double>());
    }
    if (j.is_string()) {
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::STRING_PTR;
        v.ptr_val = TsString::Create(j.get<std::string>().c_str());
        return v;
    }
    if (j.is_array()) {
        TsArray* arr = TsArray::Create();
        for (const auto& element : j) {
            TsValue val = json_to_ts(element);
            // Convert TsValue struct to NaN-boxed for array storage
            uint64_t nb = (uint64_t)(uintptr_t)nanbox_from_tagged(val);
            arr->Push((int64_t)nb);
        }
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::ARRAY_PTR;
        v.ptr_val = arr;
        return v;
    }
    if (j.is_object()) {
        TsMap* map = TsMap::Create();
        for (auto it = j.begin(); it != j.end(); ++it) {
            map->Set(TsString::Create(it.key().c_str()), json_to_ts(it.value()));
        }
        TsValue v;
        std::memset(&v, 0, sizeof(TsValue));  // Zero-initialize including padding
        v.type = ValueType::OBJECT_PTR;
        v.ptr_val = map;
        return v;
    }
    return TsValue(nullptr);
}

// ---------------------------------------------------------------------------
// JSON.stringify — ECMA-262 25.5.2 spec-order serializer
// ---------------------------------------------------------------------------
//
// Exception architecture: user code (toJSON, replacer, getters, proxy traps,
// ToNumber/ToString coercions) runs under a setjmp guard in a POD-only frame;
// a ts_throw longjmp is converted to the C++ exception JsonAbruptCompletion so
// unwinding through frames holding std::string/std::vector is well-defined.
// The clean top-level catch in ts_json_stringify re-raises via ts_throw.

// Signals a revoked Proxy encountered during serialization (IsArray on a
// revoked proxy is a TypeError per ES 7.2.2).
struct JsonRevokedProxyError {};

// Signals a TypeError raised by the serializer itself: circular structure, or
// a BigInt value (ECMA-262 25.5.2.2 SerializeJSONProperty step 10).
struct JsonTypeErrorSignal { const char* msg; };

// Signals an ABRUPT completion (user toJSON/replacer/getter/coercion threw).
struct JsonAbruptCompletion { TsValue* error; };

// Runtime services (C linkage). Declared at file scope per runtime-safety
// rules. Several are also declared in the included headers; identical
// redeclaration is harmless.
extern "C" bool ts_is_callable(void* v);
extern "C" void* ts_push_exception_handler();
extern "C" void ts_pop_exception_handler();
extern "C" TsValue* ts_get_exception();
extern "C" void ts_set_exception(TsValue* e);
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);
extern "C" TsValue* ts_value_make_undefined();
extern "C" TsValue* ts_value_make_null();
extern "C" TsValue* ts_value_make_double(double d);
extern "C" double ts_to_number(TsValue* v);
extern "C" TsValue* ts_to_primitive(TsValue* val, int hint);
extern "C" void* ts_string_from_value(TsValue* val);
extern "C" void* ts_number_to_string(double value, int64_t radix);
extern "C" bool ts_array_isArray(void* value);
extern TsString* ts_ensure_flat(void* ptr);

// ---- Guarded user-code invocations -----------------------------------------
// Each helper's frame is POD-only (longjmp rule: no non-trivially-destructible
// locals may live in a frame that a longjmp passes through). ts_throw pops its
// own handler before the longjmp — NEVER pop in the landing branch.

static TsValue* json_call_guarded_1(TsValue* fn, TsValue* thisV,
                                    TsValue* arg, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        TsValue* r = ts_call_with_this_1(fn, thisV, arg);
        ts_pop_exception_handler();
        return r;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

static TsValue* json_call_guarded_2(TsValue* fn, TsValue* thisV,
                                    TsValue* a1, TsValue* a2, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        TsValue* r = ts_call_with_this_2(fn, thisV, a1, a2);
        ts_pop_exception_handler();
        return r;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

// [[Get]] — observable (getters, proxy get trap).
static TsValue* json_get_guarded(TsValue* obj, TsValue* key, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        TsValue* r = ts_object_get_dynamic(obj, key);
        ts_pop_exception_handler();
        return r;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

// EnumerableOwnPropertyNames(KEY) — observable (proxy ownKeys/gOPD traps).
static TsValue* json_keys_guarded(TsValue* obj, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        TsValue* r = ts_object_keys(obj);
        ts_pop_exception_handler();
        return r;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

// ToNumber — observable on objects (valueOf/toString).
static double json_tonumber_guarded(TsValue* v, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        double d = ts_to_number(v);
        ts_pop_exception_handler();
        return d;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return 0.0;
}

// ToString — observable on objects (toString/valueOf are invoked via
// OrdinaryToPrimitive with the string hint; ts_string_from_value alone reads
// wrapper data slots directly and would miss a patched toString).
static TsString* json_tostring_guarded(TsValue* v, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        TsValue* prim = ts_to_primitive(v, 2);  // hint string
        TsString* s = (TsString*)ts_string_from_value(prim ? prim : v);
        ts_pop_exception_handler();
        return s;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

// IsArray — throws TypeError on a revoked proxy (ES 7.2.2 step 3.a).
static bool json_isarray_guarded(void* raw, TsValue** errOut) {
    *errOut = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        bool b = ts_array_isArray(raw);
        ts_pop_exception_handler();
        return b;
    }
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return false;
}

// ---- Value inspection helpers ----------------------------------------------

static void* json_raw(TsValue* v) {
    if (!v) return nullptr;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (!nanbox_is_ptr(nb)) return nullptr;
    void* p = nanbox_to_ptr(nb);
    return ((uintptr_t)p > 0x1000) ? p : nullptr;
}

static bool json_is_proxy(void* raw) {
    if (!raw) return false;
    if (*(uint32_t*)((char*)raw + 16) != 0x4D415053) return false;  // TsMap
    return dynamic_cast<TsProxy*>((TsMap*)raw) != nullptr;
}

// Boxed-primitive wrapper kind for a heap object: 0 = none, 1 = Number,
// 2 = String, 3 = Boolean, 4 = BigInt. Detection only — no user code runs.
// Handles BOTH runtime representations: the dedicated TsNumberObject /
// TsStringObject / TsBooleanObject classes (magic at offset 16) and TsMap /
// flat-backed objects carrying a hidden __NumberData/__StringData/
// __BooleanData/__BigIntData own slot. Proxies are never probed.
static int json_wrapper_kind(void* raw) {
    if (!raw) return 0;
    uint32_t m0 = *(uint32_t*)raw;
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 == TsNumberObject::MAGIC) return 1;
    if (m16 == TsStringObject::MAGIC) return 2;
    if (m16 == TsBooleanObject::MAGIC) return 3;
    bool isMapLike = (m16 == 0x4D415053);
    bool isFlat = (m0 == FLAT_MAGIC);
    if (!isMapLike && !isFlat) return 0;
    if (isMapLike && dynamic_cast<TsProxy*>((TsMap*)raw) != nullptr) return 0;
    static const char* slots[4] = { "__NumberData", "__StringData",
                                    "__BooleanData", "__BigIntData" };
    for (int k = 0; k < 4; k++) {
        TsValue* v = ts_object_get_property(raw, slots[k]);
        if (!v) continue;
        uint64_t vb = nanbox_from_tsvalue_ptr(v);
        if (!nanbox_is_undefined(vb)) return k + 1;
    }
    return 0;
}

// [[BooleanData]] read (internal slot, not observable).
static bool json_boolean_data(void* raw) {
    if (*(uint32_t*)((char*)raw + 16) == TsBooleanObject::MAGIC)
        return ((TsBooleanObject*)raw)->value;
    TsValue* v = ts_object_get_property(raw, "__BooleanData");
    if (v) {
        uint64_t vb = nanbox_from_tsvalue_ptr(v);
        if (nanbox_is_bool(vb)) return nanbox_to_bool(vb);
    }
    return false;
}

// ---- Output builders --------------------------------------------------------

// ECMA-262 25.5.2.3 QuoteJSONString over UTF-16 code units. Well-formed
// stringify (ES2019): lone surrogates escape as \udXXX (lowercase hex); valid
// pairs and other units pass through as UTF-8.
static void json_quote_us(const icu::UnicodeString& us, std::string& out) {
    out.push_back('"');
    int32_t n = us.length();
    char buf[8];
    for (int32_t i = 0; i < n; i++) {
        char16_t c = us.charAt(i);
        switch (c) {
            case 0x22: out += "\\\""; break;
            case 0x5C: out += "\\\\"; break;
            case 0x08: out += "\\b";  break;
            case 0x0C: out += "\\f";  break;
            case 0x0A: out += "\\n";  break;
            case 0x0D: out += "\\r";  break;
            case 0x09: out += "\\t";  break;
            default:
                if (c < 0x20) {
                    std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
                    out += buf;
                } else if (c >= 0xD800 && c <= 0xDBFF) {
                    if (i + 1 < n) {
                        char16_t d = us.charAt(i + 1);
                        if (d >= 0xDC00 && d <= 0xDFFF) {
                            uint32_t cp = 0x10000u +
                                (((uint32_t)(c - 0xD800)) << 10) + (d - 0xDC00);
                            out += (char)(0xF0 | (cp >> 18));
                            out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                            i++;
                            break;
                        }
                    }
                    std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
                    out += buf;
                } else if (c >= 0xDC00 && c <= 0xDFFF) {
                    std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)c);
                    out += buf;
                } else if (c < 0x80) {
                    out += (char)c;
                } else if (c < 0x800) {
                    out += (char)(0xC0 | (c >> 6));
                    out += (char)(0x80 | (c & 0x3F));
                } else {
                    out += (char)(0xE0 | (c >> 12));
                    out += (char)(0x80 | ((c >> 6) & 0x3F));
                    out += (char)(0x80 | (c & 0x3F));
                }
        }
    }
    out.push_back('"');
}

static void json_quote(TsString* str, std::string& out) {
    if (!str) { out += "\"\""; return; }
    // NUL-safe: ToUnicodeString() can lazily rebuild from the C-string UTF-8
    // buffer and truncate at an embedded '\0' (JSON/stringify
    // value-string-escape-ascii serializes control-char strings).
    // AppendUtf8 is
    // length-aware.
    std::string utf8;
    str->AppendUtf8(utf8);
    json_quote_us(icu::UnicodeString::fromUTF8(
        icu::StringPiece(utf8.data(), (int32_t)utf8.size())), out);
}

// NUL-safe: keys are stored as std::string of UTF-8 BYTES (length-aware);
// StringPiece keeps embedded NULs intact.
static void json_quote_utf8(const std::string& key, std::string& out) {
    json_quote_us(icu::UnicodeString::fromUTF8(
        icu::StringPiece(key.data(), (int32_t)key.size())), out);
}

// Length-aware TsString for a stored key (Create(const char*) truncates at NUL).
static TsString* json_key_string(const std::string& key) {
    return TsString::Create(key.data(), key.size());
}

// ECMA-262 25.5.2.2 step 9: Number serializes via ToString(number); non-finite
// values become "null".
static void json_number_out(double d, std::string& out) {
    if (!std::isfinite(d)) { out += "null"; return; }
    TsString* s = (TsString*)ts_number_to_string(d, 10);
    if (s) out += s->ToUtf8();
    else out += "0";
}

// ---- Serializer state -------------------------------------------------------

struct JsonSer {
    std::vector<void*> stack;                 // circular detection (raw ptrs)
    std::string gap;                          // ES 25.5.2 step 8
    std::vector<std::string>* propList = nullptr;  // array-replacer PropertyList
    TsValue* replacer = nullptr;              // callable replacer (boxed)
};

static bool json_serialize_property(JsonSer& st, const std::string& key,
                                    TsValue* holderBoxed, TsValue* valueBoxed,
                                    const std::string& indent, std::string& out);

// ECMA-262 25.5.2.5 SerializeJSONObject
static void json_serialize_object(JsonSer& st, TsValue* valueBoxed, void* raw,
                                  const std::string& indent, std::string& out) {
    if (std::find(st.stack.begin(), st.stack.end(), raw) != st.stack.end())
        throw JsonTypeErrorSignal{"Converting circular structure to JSON"};
    st.stack.push_back(raw);
    const std::string& stepback = indent;
    std::string ind = indent + st.gap;

    std::vector<std::string> keys;
    if (st.propList) {
        keys = *st.propList;
    } else {
        TsValue* err = nullptr;
        TsValue* kv = json_keys_guarded(valueBoxed, &err);
        if (err) throw JsonAbruptCompletion{err};
        void* karr = kv ? ts_value_get_object(kv) : nullptr;
        if (karr && *(uint32_t*)karr == TsArray::MAGIC) {
            TsArray* ka = (TsArray*)karr;
            int64_t kn = ka->Length();
            for (int64_t i = 0; i < kn; i++) {
                uint64_t knb = (uint64_t)ka->Get(i);
                if (!nanbox_is_ptr(knb)) continue;
                void* kp = nanbox_to_ptr(knb);
                if (!kp || (uintptr_t)kp <= 0x1000) continue;
                uint32_t km = *(uint32_t*)kp;
                TsString* ks = nullptr;
                if (km == TsString::MAGIC) ks = (TsString*)kp;
                else if (km == 0x434F4E53) ks = ts_ensure_flat(kp);  // cons
                if (!ks) continue;
                std::string kstr;
                ks->AppendUtf8(kstr);  // NUL-safe (ToUtf8 truncates at '\0')
                // Internal storage keys are not JSON-serializable properties.
                if (!kstr.empty() &&
                    (kstr[0] == '\x01' ||
                     kstr.rfind("__getter_", 0) == 0 ||
                     kstr.rfind("__setter_", 0) == 0)) continue;
                keys.push_back(std::move(kstr));
            }
        }
    }

    std::vector<std::string> partial;
    for (const std::string& k : keys) {
        TsValue* err = nullptr;
        TsValue* keyV = ts_value_make_string(json_key_string(k));
        TsValue* v = json_get_guarded(valueBoxed, keyV, &err);
        if (err) throw JsonAbruptCompletion{err};
        std::string sub;
        if (json_serialize_property(st, k, valueBoxed, v, ind, sub)) {
            std::string member;
            json_quote_utf8(k, member);
            member += ':';
            if (!st.gap.empty()) member += ' ';
            member += sub;
            partial.push_back(std::move(member));
        }
    }

    if (partial.empty()) {
        out += "{}";
    } else if (st.gap.empty()) {
        out += '{';
        for (size_t i = 0; i < partial.size(); i++) {
            if (i) out += ',';
            out += partial[i];
        }
        out += '}';
    } else {
        out += "{\n";
        for (size_t i = 0; i < partial.size(); i++) {
            if (i) out += ",\n";
            out += ind;
            out += partial[i];
        }
        out += '\n';
        out += stepback;
        out += '}';
    }
    st.stack.pop_back();
}

// ECMA-262 25.5.2.4 SerializeJSONArray
static void json_serialize_array(JsonSer& st, TsValue* valueBoxed, void* raw,
                                 const std::string& indent, std::string& out) {
    if (std::find(st.stack.begin(), st.stack.end(), raw) != st.stack.end())
        throw JsonTypeErrorSignal{"Converting circular structure to JSON"};
    st.stack.push_back(raw);
    const std::string& stepback = indent;
    std::string ind = indent + st.gap;

    // LengthOfArrayLike: plain arrays read length directly; anything else
    // (proxy for an array) does an observable Get("length") + ToLength.
    int64_t len = 0;
    if (*(uint32_t*)raw == TsArray::MAGIC || *(uint32_t*)raw == 0x524D4154) {
        len = (*(uint32_t*)raw == TsArray::MAGIC)
                  ? ((TsArray*)raw)->Length()
                  : ((TsRegExpMatchArray*)raw)->Length();
    } else {
        TsValue* err = nullptr;
        TsValue* keyV = ts_value_make_string(TsString::GetInterned("length"));
        TsValue* lv = json_get_guarded(valueBoxed, keyV, &err);
        if (err) throw JsonAbruptCompletion{err};
        double d = json_tonumber_guarded(lv, &err);
        if (err) throw JsonAbruptCompletion{err};
        if (!(d > 0)) len = 0;                       // NaN / negative / 0
        else if (d > 9007199254740991.0) len = 9007199254740991LL;
        else len = (int64_t)d;
    }

    std::vector<std::string> partial;
    for (int64_t i = 0; i < len; i++) {
        TsValue* err = nullptr;
        TsValue* elem = json_get_guarded(valueBoxed, ts_value_make_int(i), &err);
        if (err) throw JsonAbruptCompletion{err};
        std::string sub;
        std::string ks = std::to_string(i);
        bool present = json_serialize_property(st, ks, valueBoxed, elem, ind, sub);
        // 25.5.2.4 step 8.b: unserializable elements become "null".
        partial.push_back(present ? std::move(sub) : std::string("null"));
    }

    if (partial.empty()) {
        out += "[]";
    } else if (st.gap.empty()) {
        out += '[';
        for (size_t i = 0; i < partial.size(); i++) {
            if (i) out += ',';
            out += partial[i];
        }
        out += ']';
    } else {
        out += "[\n";
        for (size_t i = 0; i < partial.size(); i++) {
            if (i) out += ",\n";
            out += ind;
            out += partial[i];
        }
        out += '\n';
        out += stepback;
        out += ']';
    }
    st.stack.pop_back();
}

// ECMA-262 25.5.2.2 SerializeJSONProperty. `valueBoxed` is Get(holder, key),
// performed by the caller. Returns false when the value is unserializable
// (undefined / function / symbol) — the JSON "absent" completion.
static bool json_serialize_property(JsonSer& st, const std::string& key,
                                    TsValue* holderBoxed, TsValue* valueBoxed,
                                    const std::string& indent, std::string& out) {
    if (!valueBoxed) valueBoxed = ts_value_make_undefined();

    // Step 2: if Type(value) is Object or BigInt, Get(value, "toJSON") and,
    // if callable, value = Call(toJSON, value, [key]). Strings and Symbols
    // are excluded (not Objects); BigInt primitives ARE included (the BigInt
    // proposal allows BigInt receivers for toJSON).
    {
        void* raw = json_raw(valueBoxed);
        bool eligible = false;
        if (raw) {
            uint32_t m0 = *(uint32_t*)raw;
            if (m0 != TsString::MAGIC && m0 != 0x434F4E53 /*cons*/ &&
                m0 != 0x53594D42 /*symbol*/) {
                eligible = true;
            }
        }
        if (eligible) {
            TsValue* err = nullptr;
            TsValue* tjKey = ts_value_make_string(TsString::GetInterned("toJSON"));
            TsValue* tj = json_get_guarded(valueBoxed, tjKey, &err);
            if (err) throw JsonAbruptCompletion{err};
            void* tjRaw = json_raw(tj);
            if (tjRaw && ts_is_callable(tjRaw)) {
                TsValue* keyV = ts_value_make_string(json_key_string(key));
                TsValue* r = json_call_guarded_1(tj, valueBoxed, keyV, &err);
                if (err) throw JsonAbruptCompletion{err};
                valueBoxed = r ? r : ts_value_make_undefined();
            }
        }
    }

    // Step 3: function replacer — value = Call(replacer, holder, [key, value]).
    // Runs for EVERY property (even undefined values), AFTER toJSON.
    if (st.replacer) {
        TsValue* err = nullptr;
        TsValue* keyV = ts_value_make_string(json_key_string(key));
        TsValue* r = json_call_guarded_2(st.replacer, holderBoxed, keyV,
                                         valueBoxed, &err);
        if (err) throw JsonAbruptCompletion{err};
        valueBoxed = r ? r : ts_value_make_undefined();
    }

    // Step 4: unwrap Number/String/Boolean/BigInt wrapper objects. ToNumber /
    // ToString are OBSERVABLE (valueOf/toString are invoked and may throw).
    {
        void* raw = json_raw(valueBoxed);
        int wk = raw ? json_wrapper_kind(raw) : 0;
        if (wk == 1) {
            TsValue* err = nullptr;
            double d = json_tonumber_guarded(valueBoxed, &err);
            if (err) throw JsonAbruptCompletion{err};
            valueBoxed = ts_value_make_double(d);
        } else if (wk == 2) {
            TsValue* err = nullptr;
            TsString* s = json_tostring_guarded(valueBoxed, &err);
            if (err) throw JsonAbruptCompletion{err};
            valueBoxed = ts_value_make_string(s ? s : TsString::Create(""));
        } else if (wk == 3) {
            valueBoxed = ts_value_make_bool(json_boolean_data(raw));
        } else if (wk == 4) {
            // [[BigIntData]] -> BigInt -> step 10 TypeError (no user code runs).
            throw JsonTypeErrorSignal{"Do not know how to serialize a BigInt"};
        }
    }

    // Steps 5-12: emit by final type.
    uint64_t nb = nanbox_from_tsvalue_ptr(valueBoxed);
    if (nanbox_is_undefined(nb)) return false;
    if (nanbox_is_null(nb)) { out += "null"; return true; }
    if (nanbox_is_bool(nb)) { out += nanbox_to_bool(nb) ? "true" : "false"; return true; }
    if (nanbox_is_int32(nb)) { json_number_out((double)nanbox_to_int32(nb), out); return true; }
    if (nanbox_is_double(nb)) { json_number_out(nanbox_to_double(nb), out); return true; }
    if (!nanbox_is_ptr(nb)) return false;

    void* raw = nanbox_to_ptr(nb);
    if (!raw || (uintptr_t)raw <= 0x1000) return false;
    uint32_t m0 = *(uint32_t*)raw;

    if (m0 == TsString::MAGIC) { json_quote((TsString*)raw, out); return true; }
    if (m0 == 0x434F4E53) { json_quote(ts_ensure_flat(raw), out); return true; }
    if (m0 == 0x42494749) {  // TsBigInt — step 10
        throw JsonTypeErrorSignal{"Do not know how to serialize a BigInt"};
    }
    if (m0 == 0x53594D42) return false;              // Symbol — unserializable
    if (ts_is_callable(raw)) return false;           // callable — unserializable

    // Revoked proxy: IsArray throws TypeError (surface via signal).
    if (json_is_proxy(raw)) {
        TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw);
        if (px && px->revoked) throw JsonRevokedProxyError{};
    }

    {
        TsValue* err = nullptr;
        bool isArr = json_isarray_guarded(raw, &err);
        if (err) throw JsonAbruptCompletion{err};
        if (isArr) {
            json_serialize_array(st, valueBoxed, raw, indent, out);
            return true;
        }
    }

    // Date without a reachable toJSON (step 2 already ran): ISO string, matching
    // Date.prototype.toJSON.
    if (m0 == TsDate::MAGIC) {
        json_quote(((TsDate*)raw)->ToISOString(), out);
        return true;
    }
    // RegExp: ordinary object with no own enumerable properties.
    if (m0 == TsRegExp::MAGIC) { out += "{}"; return true; }

    json_serialize_object(st, valueBoxed, raw, indent, out);
    return true;
}

// ---- Replacer PropertyList (array replacer) ---------------------------------

// ECMA-262 25.5.2 step 4.b: build the PropertyList. String and Number
// elements contribute directly; String/Number WRAPPER objects contribute via
// an observable ToString (abrupt completions propagate). Elements are read
// with observable Gets (proxy replacer arrays route their traps). Duplicates
// are dropped, order preserved.
static bool json_build_property_list(void* replacer, std::vector<std::string>& keys) {
    if (!replacer) return false;
    uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)replacer);
    if (!nanbox_is_ptr(nb)) return false;
    void* p = nanbox_to_ptr(nb);
    if (!p || (uintptr_t)p <= 0x1000) return false;

    // IsArray(replacer) on a revoked Proxy throws a TypeError.
    if (*(uint32_t*)((char*)p + 16) == 0x4D415053) {
        if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)p)) {
            if (px->revoked) throw JsonRevokedProxyError{};
        }
    }
    {
        TsValue* err = nullptr;
        bool isArr = json_isarray_guarded(p, &err);
        if (err) throw JsonAbruptCompletion{err};
        if (!isArr) return false;
    }

    TsValue* rb = (TsValue*)replacer;
    int64_t len = 0;
    if (*(uint32_t*)p == TsArray::MAGIC) {
        len = ((TsArray*)p)->Length();
    } else {
        TsValue* err = nullptr;
        TsValue* keyV = ts_value_make_string(TsString::GetInterned("length"));
        TsValue* lv = json_get_guarded(rb, keyV, &err);
        if (err) throw JsonAbruptCompletion{err};
        double d = json_tonumber_guarded(lv, &err);
        if (err) throw JsonAbruptCompletion{err};
        if (!(d > 0)) len = 0;
        else if (d > 9007199254740991.0) len = 9007199254740991LL;
        else len = (int64_t)d;
    }

    std::set<std::string> seen;
    for (int64_t i = 0; i < len; i++) {
        TsValue* err = nullptr;
        TsValue* e = json_get_guarded(rb, ts_value_make_int(i), &err);
        if (err) throw JsonAbruptCompletion{err};
        if (!e) continue;
        uint64_t enb = nanbox_from_tsvalue_ptr(e);
        std::string key;
        bool have = false;
        if (nanbox_is_int32(enb)) {
            TsString* s = (TsString*)ts_number_to_string((double)nanbox_to_int32(enb), 10);
            if (s) { key = s->ToUtf8(); have = true; }
        } else if (nanbox_is_double(enb)) {
            TsString* s = (TsString*)ts_number_to_string(nanbox_to_double(enb), 10);
            if (s) { key = s->ToUtf8(); have = true; }
        } else if (nanbox_is_ptr(enb)) {
            void* ep = nanbox_to_ptr(enb);
            if (ep && (uintptr_t)ep > 0x1000) {
                uint32_t m0 = *(uint32_t*)ep;
                if (m0 == TsString::MAGIC) {
                    ((TsString*)ep)->AppendUtf8(key); have = true;
                } else if (m0 == 0x434F4E53) {
                    TsString* f = ts_ensure_flat(ep);
                    if (f) { f->AppendUtf8(key); have = true; }
                } else {
                    int wk = json_wrapper_kind(ep);
                    if (wk == 1 || wk == 2) {
                        // 4.b.iii.3: item = ? ToString(v) — observable.
                        TsString* s = json_tostring_guarded(e, &err);
                        if (err) throw JsonAbruptCompletion{err};
                        if (s) { s->AppendUtf8(key); have = true; }
                    }
                }
            }
        }
        if (have && seen.insert(key).second) keys.push_back(std::move(key));
    }
    return true;
}

// ---- space -> gap -----------------------------------------------------------

// ECMA-262 25.5.2 steps 5-8. Number/String wrapper objects convert via
// OBSERVABLE ToNumber/ToString (space-number-object / space-string-object).
static std::string json_compute_gap(void* space) {
    if (!space) return "";
    uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)space);

    double num = 0.0;
    bool isNum = false;
    TsString* str = nullptr;

    if (nanbox_is_int32(nb)) { num = (double)nanbox_to_int32(nb); isNum = true; }
    else if (nanbox_is_double(nb)) { num = nanbox_to_double(nb); isNum = true; }
    else if (nanbox_is_ptr(nb)) {
        void* p = nanbox_to_ptr(nb);
        if (!p || (uintptr_t)p <= 0x1000) return "";
        uint32_t m0 = *(uint32_t*)p;
        if (m0 == TsString::MAGIC) {
            str = (TsString*)p;
        } else if (m0 == 0x434F4E53) {
            str = ts_ensure_flat(p);
        } else {
            int wk = json_wrapper_kind(p);
            if (wk == 1) {
                TsValue* err = nullptr;
                num = json_tonumber_guarded((TsValue*)space, &err);
                if (err) throw JsonAbruptCompletion{err};
                isNum = true;
            } else if (wk == 2) {
                TsValue* err = nullptr;
                str = json_tostring_guarded((TsValue*)space, &err);
                if (err) throw JsonAbruptCompletion{err};
            } else {
                return "";  // any other object: ignored
            }
        }
    } else {
        return "";  // undefined / null / bool
    }

    if (isNum) {
        if (std::isnan(num)) num = 0.0;
        long n = (long)num;  // ToIntegerOrInfinity truncates toward zero
        if (n < 1) return "";
        if (n > 10) n = 10;
        return std::string((size_t)n, ' ');
    }
    if (str) {
        int64_t len = str->Length();
        if (len <= 0) return "";
        if (len > 10) {
            TsString* trimmed = str->Substring(0, 10);
            if (trimmed) return std::string(trimmed->ToUtf8());
        }
        return std::string(str->ToUtf8());
    }
    return "";
}

extern "C" {
    void* ts_json_parse(void* json_str) {
        if (!json_str) {
            // JSON.parse(undefined) -> SyntaxError ("undefined" is not valid JSON)
            ts_throw((TsValue*)ts_error_create_typed(
                "SyntaxError", "Unexpected token u in JSON"));
            return nullptr;
        }
        // ECMA-262 25.5.1: JSON.parse(text) does ToString(text) FIRST. A non-string
        // arg (true, 123, null, undefined, ...) was cast straight to TsString and
        // ToUtf8'd -> access violation. Coerce to its string form; an unparseable
        // result ("undefined", "true" is valid -> true) surfaces below as a
        // SyntaxError via the parse step.
        // ToString(object) is OBSERVABLE (OrdinaryToPrimitive invokes user
        // toString/valueOf; abrupt completions propagate via ts_throw before
        // any non-POD local is constructed in this frame) —
        // JSON/parse/text-object and text-object-abrupt.
        TsValue* prim = ts_to_primitive((TsValue*)json_str, 2);
        TsString* s = (TsString*)ts_string_from_value(prim ? prim : (TsValue*)json_str);
        if (!s) {
            ts_throw((TsValue*)ts_error_create_typed("SyntaxError", "Unexpected token in JSON"));
            return nullptr;
        }
        // Parse the text. Malformed input must surface as a JS SyntaxError
        // (ECMA-262 25.5.1), not as an internal C++ exception escaping the
        // runtime. Only the parse step can throw on bad input; isolate it.
        // ECMA-404/ECMA-262 25.5.1: the text "-0" parses to the Number -0.
        // nlohmann parses it as integer 0, losing the sign — special-case the
        // (whitespace-trimmed) top-level form.
        {
            const char* t = s->ToUtf8();
            while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r') t++;
            if (t[0] == '-' && t[1] == '0') {
                const char* rest = t + 2;
                while (*rest == ' ' || *rest == '\t' || *rest == '\n' ||
                       *rest == '\r') rest++;
                if (*rest == '\0')
                    return (void*)ts_value_make_double(-0.0);
            }
        }
        // ECMA-262 25.5.1 / ECMA-404: U+FEFF (BOM) is NOT JSONWhitespace —
        // "﻿1234" must be a SyntaxError. nlohmann silently skips a
        // leading UTF-8 BOM (EF BB BF), so reject it here (test262
        // JSON/parse/15.12.1.1-0-6).
        {
            const unsigned char* b = (const unsigned char*)s->ToUtf8();
            if (b && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
                ts_throw((TsValue*)ts_error_create_typed(
                    "SyntaxError", "Unexpected token \\ufeff in JSON"));
                return nullptr;
            }
        }
        nlohmann::ordered_json j;
        try {
            j = nlohmann::ordered_json::parse(s->ToUtf8());
        } catch (const std::exception& e) {
            ts_throw((TsValue*)ts_error_create_typed("SyntaxError", e.what()));
            return nullptr;
        } catch (...) {
            ts_throw((TsValue*)ts_error_create_typed(
                "SyntaxError", "Unexpected token in JSON"));
            return nullptr;
        }
        // Valid parse: build the runtime value (unchanged behavior).
        TsValue val = json_to_ts(j);
        // Convert TsValue struct to NaN-boxed representation
        return (void*)nanbox_from_tagged(val);
    }

    // ECMA-262 25.5.2 JSON.stringify ( value [ , replacer [ , space ] ] ).
    // Direct spec-order serializer; see SerializeJSONProperty above.
    // NOTE: a C-level nullptr `obj` is NOT undefined here — internal callers
    // (IPC serialization, legacy paths) pass nullptr for JS null and expect
    // the string "null" back.
    void* ts_json_stringify(void* obj, void* replacer, void* space) {
        try {
            JsonSer st;
            std::vector<std::string> plist;

            // Step 4: replacer — callable => ReplacerFunction; array (incl.
            // proxy-for-array) => PropertyList.
            if (replacer) {
                uint64_t rnb = nanbox_from_tsvalue_ptr((TsValue*)replacer);
                if (nanbox_is_ptr(rnb)) {
                    void* rp = nanbox_to_ptr(rnb);
                    if (rp && (uintptr_t)rp > 0x1000) {
                        if (ts_is_callable(rp)) {
                            st.replacer = (TsValue*)replacer;
                        } else if (json_build_property_list(replacer, plist)) {
                            st.propList = &plist;
                        }
                    }
                }
            }

            // Steps 5-8: gap from space.
            st.gap = json_compute_gap(space);

            // Steps 9-11: wrapper = { "": value }; serialize the empty key.
            TsValue* valueBoxed = obj ? (TsValue*)obj : ts_value_make_null();
            TsMap* wrapper = TsMap::Create();
            wrapper->Set(TsString::Create(""), nanbox_to_tagged(valueBoxed));
            TsValue* wrapperBoxed = ts_value_make_object(wrapper);

            std::string out;
            bool present = json_serialize_property(st, "", wrapperBoxed,
                                                   valueBoxed, "", out);
            if (!present) return ts_value_make_undefined();
            return TsString::Create(out.c_str());
        } catch (const JsonAbruptCompletion& a) {
            // C++ unwinding cleaned the recursion frames; re-throw the
            // ORIGINAL user error from this clean frame.
            ts_throw(a.error);
            return TsString::Create("null");  // unreachable
        } catch (const JsonRevokedProxyError&) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot serialize a revoked Proxy with JSON.stringify"));
            return TsString::Create("null");  // unreachable
        } catch (const JsonTypeErrorSignal& s) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError", s.msg));
            return TsString::Create("null");  // unreachable
        } catch (...) {
            return TsString::Create("null");
        }
    }
}
