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
#include <string>
#include <cstring>

#include <set>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cmath>

// ECMA-262 25.5.2 SerializeJSONObject step 6: emit keys in
// [[OwnPropertyKeys]] order (insertion order for string keys). Default
// nlohmann::ordered_json uses std::map which sorts alphabetically; switch to
// ordered_json which preserves insertion order while keeping the same
// API surface elsewhere in this file.
using json = nlohmann::ordered_json;

static TsValue json_to_ts(const json& j) {
    if (j.is_null()) return TsValue(nullptr);
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

// Signals a revoked Proxy encountered during serialization. Thrown as a C++
// exception (safe through the nlohmann/std frames), converted to a JS TypeError
// by ts_json_stringify's catch (a ts_throw from inside the recursion would
// longjmp through std-object frames and corrupt the unwinder).
struct JsonRevokedProxyError {};

// Signals a TypeError to raise from the clean top-level catch (unwinding
// through nlohmann/std frames first): circular structure, or a BigInt value
// (ECMA-262 25.5.2 SerializeJSONProperty — BigInt is a TypeError).
struct JsonTypeErrorSignal { const char* msg; };

// Signals an ABRUPT completion (user toJSON/getter threw) during
// serialization. Same architecture as JsonRevokedProxyError: the user call
// runs under a setjmp guard (ts_throw pops its own handler — NEVER pop in
// the landing branch), the longjmp is converted to this C++ exception so
// unwinding through the nlohmann/std frames is well-defined, and the clean
// top-level catch re-throws the ORIGINAL error object via ts_throw.
struct JsonAbruptCompletion { TsValue* error; };

// Invoke a user callback under a longjmp guard. Returns the result, or
// null with *errOut set when the callee threw. NO C++ objects live in
// this frame (longjmp rule).
extern "C" bool ts_is_callable(void* v);
extern "C" void* ts_push_exception_handler();
extern "C" void ts_pop_exception_handler();
extern "C" TsValue* ts_get_exception();
extern "C" void ts_set_exception(TsValue* e);

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
    // ts_throw already popped the handler before the longjmp.
    *errOut = ts_get_exception();
    ts_set_exception(nullptr);
    return nullptr;
}

static nlohmann::ordered_json ts_to_json_internal(void* p, std::set<void*>& visited);
static int json_wrapper_num_or_str(void* raw, double* num, TsString** str);
// ECMA-262 25.5.2.2 SerializeJSONProperty step 4: unwrap a primitive wrapper
// object (new Number/String/Boolean) to its primitive JSON form. Defined below
// (after json_wrapper_num_or_str); forward-declared here for ts_to_json_internal.
static bool json_unwrap_primitive_wrapper(void* p, nlohmann::ordered_json& out);

static nlohmann::ordered_json ts_value_to_json(TsValue v, std::set<void*>& visited) {
    switch (v.type) {
        case ValueType::UNDEFINED: return nullptr;
        case ValueType::NUMBER_INT: return v.i_val;
        case ValueType::NUMBER_DBL: {
            // Format whole numbers as integers (10.0 -> 10, not 10.0)
            double d = v.d_val;
            double intPart;
            if (std::modf(d, &intPart) == 0.0 &&
                d >= -9007199254740992.0 && d <= 9007199254740992.0) {
                return (int64_t)d;
            }
            return d;
        }
        case ValueType::BOOLEAN: return v.b_val;
        case ValueType::STRING_PTR: return ts_to_json_internal(v.ptr_val, visited);
        case ValueType::OBJECT_PTR: return ts_to_json_internal(v.ptr_val, visited);
        case ValueType::ARRAY_PTR: return ts_to_json_internal(v.ptr_val, visited);
        // 25.5.2.2 steps 10-11: a callable member is unserializable —
        // omitted from objects / null in arrays (discarded sentinel).
        case ValueType::FUNCTION_PTR:
            return nlohmann::ordered_json(nlohmann::detail::value_t::discarded);
        case ValueType::PROMISE_PTR: return "[Promise]";
        default: return nullptr;
    }
}

static nlohmann::ordered_json ts_to_json_internal(void* p, std::set<void*>& visited) {
    if (!p) return nullptr;

    // Decode NaN-boxed values
    uint64_t nb = (uint64_t)(uintptr_t)p;
    if (nanbox_is_undefined(nb)) return nullptr;
    if (nanbox_is_null(nb)) return nullptr;
    if (nanbox_is_bool(nb)) return nanbox_to_bool(nb);
    if (nanbox_is_int32(nb)) {
        return (int64_t)nanbox_to_int32(nb);
    }
    if (nanbox_is_double(nb)) {
        double d = nanbox_to_double(nb);
        double intPart;
        if (std::modf(d, &intPart) == 0.0 &&
            d >= -9007199254740992.0 && d <= 9007199254740992.0) {
            return (int64_t)d;
        }
        return d;
    }

    // Must be a pointer (nanbox_is_ptr). Extract the raw pointer.
    // Note: for heap pointers, the NaN-boxed representation IS the raw pointer value.

    // Check magic numbers. Some objects have a vtable (TsObject), some don't.
    // Layout varies:
    //   TsString, TsArray, TsDate, TsRegExp: magic at offset 0 (no vtable)
    //   TsMap, TsSet, TsBuffer, etc: magic at offset 16 (TsObject-derived with vtable)
    uint32_t magic = *(uint32_t*)p;
    uint32_t magic_offset16 = 0;
    if ((uintptr_t)p > 0x1000) {
        magic_offset16 = *(uint32_t*)((char*)p + 16);
    }

    // A revoked Proxy crashes when its traps deref the null target. ECMA-262
    // requires JSON.stringify to throw a TypeError on it. We can't ts_throw
    // (longjmp) from here — this frame holds nlohmann/std objects — so signal
    // via a C++ exception that ts_json_stringify converts to a JS TypeError.
    if (magic_offset16 == 0x4D415053) {  // TsMap (Proxy is a TsMap subclass, vtable)
        if (TsProxy* px = dynamic_cast<TsProxy*>((TsObject*)p)) {
            if (px->revoked) throw JsonRevokedProxyError{};
        }
    }

    if (magic == TsString::MAGIC) {
        return ((TsString*)p)->ToUtf8();
    }

    // TsConsString (lazy string concatenation, magic "CONS"): flatten to a real
    // TsString and serialize as a string. Without this, a runtime-built string
    // (e.g. `"a" + someVar`, which is a cons-string until forced) fell through to
    // the object path and serialized as `{}`. ASCII all-literal concats were
    // unaffected only because the compiler constant-folds them to a flat string.
    if (magic == 0x434F4E53) {  // TsConsString::MAGIC
        extern TsString* ts_ensure_flat(void* ptr);
        TsString* flat = ts_ensure_flat(p);
        if (flat) return flat->ToUtf8();
    }

    if (magic == TsDate::MAGIC) {
        return ((TsDate*)p)->ToISOString()->ToUtf8();
    }

    if (magic == TsRegExp::MAGIC) {
        return nlohmann::ordered_json::object();
    }

    if (magic == TsArray::MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw JsonTypeErrorSignal{"Converting circular structure to JSON"};
        }
        visited.insert(p);
        TsArray* arr = (TsArray*)p;
        nlohmann::ordered_json j = nlohmann::ordered_json::array();
        for (int64_t i = 0; i < arr->Length(); ++i) {
            // TsArray::Get returns raw int64_t, which might be a pointer or a boxed value.
            // ECMA-262 25.5.2: array holes (and undefined) serialize as `null`.
            // Without the hole guard, the NANBOX_HOLE sentinel was dereferenced
            // as a pointer (crash) for sparse arrays, e.g. `JSON.stringify([,,5])`
            // or lodash `_.set({}, 'a[2]', v)`.
            uint64_t raw = (uint64_t)arr->Get(i);
            if (raw == (uint64_t)NANBOX_HOLE || nanbox_is_undefined(raw)) {
                j.push_back(nullptr);
                continue;
            }
            nlohmann::ordered_json el = ts_to_json_internal((void*)raw, visited);
            // 25.5.2.4 SerializeJSONArray step 8.b: unserializable elements
            // (functions, symbols) become null in arrays.
            j.push_back(el.is_discarded() ? nlohmann::ordered_json(nullptr)
                                          : std::move(el));
        }
        visited.erase(p);
        return j;
    }

    // ECMA-262 25.5.2.1 SerializeJSONProperty step 2: if the value has a
    // callable `toJSON` method, invoke it (with the property key) and serialize
    // the RESULT instead. Built-ins with their own JSON form (String, Date,
    // RegExp, Array) returned above; this covers generic objects / class
    // instances / lodash wrappers whose toJSON lives on the prototype. Guarded
    // by `visited` so a toJSON returning `this` degrades to circular-ref, not an
    // infinite loop.
    if (visited.find(p) == visited.end()) {
        TsValue* tj = ts_object_get_property(p, "toJSON");
        if (tj) {
            uint64_t tjnb = nanbox_from_tsvalue_ptr(tj);
            if (nanbox_is_ptr(tjnb)) {
                void* fnp = nanbox_to_ptr(tjnb);
                if (fnp && ts_is_closure(fnp)) {
                    visited.insert(p);
                    TsValue* keyArg = ts_value_make_string(TsString::Create(""));
                    TsValue* boxedThis = ts_value_make_object(p);
                    TsValue* tjErr = nullptr;
                    TsValue* res = json_call_guarded_1(tj, boxedThis, keyArg, &tjErr);
                    if (tjErr) throw JsonAbruptCompletion{tjErr};
                    nlohmann::ordered_json out =
                        ts_to_json_internal((void*)(uintptr_t)nanbox_from_tsvalue_ptr(res), visited);
                    visited.erase(p);
                    return out;
                }
            }
        }
    }

    // ECMA-262 25.5.2.2 SerializeJSONProperty step 4-5: a Number/String/Boolean
    // wrapper object (new Number(x) / new String(x) / new Boolean(x)) serializes
    // as its underlying primitive (ToNumber / ToString / [[BooleanData]]), NOT
    // as a plain object. This runs AFTER toJSON (step 2) but before the generic
    // object serialization below, so `JSON.stringify(new Number(5))` yields "5"
    // instead of leaking the hidden data slot as `{"__NumberData":5}`.
    {
        nlohmann::ordered_json wrapped;
        if (json_unwrap_primitive_wrapper(p, wrapped)) {
            return wrapped;
        }
    }

    // Flat inline-slot object (magic at offset 0)
    if (magic == FLAT_MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw JsonTypeErrorSignal{"Converting circular structure to JSON"};
        }
        visited.insert(p);
        uint32_t shapeId = flat_object_shape_id(p);
        ShapeDescriptor* desc = ts_shape_lookup(shapeId);
        nlohmann::ordered_json j = nlohmann::ordered_json::object();
        if (desc) {
            for (uint32_t i = 0; i < desc->numSlots; i++) {
                uint64_t val = *(uint64_t*)((char*)p + 16 + i * 8);
                // Per ECMA-262 25.5.2.4 SerializeJSONProperty: undefined-valued
                // own properties are OMITTED from the result object. Also skip
                // hole/deleted tombstones so absent slots don't appear as null.
                if (nanbox_is_undefined(val) || val == NANBOX_HOLE ||
                    val == NANBOX_DELETED) continue;
                // Internal storage keys are not JSON-serializable properties:
                // '\x01' slots (privates/symbols) and "__getter_/__setter_"
                // accessor storage (JSON.stringify({} with defineProperty get
                // x) leaked {"__getter_x":null}).
                const char* nm = desc->propNames[i];
                if (nm && (nm[0] == '\x01' ||
                           strncmp(nm, "__getter_", 9) == 0 ||
                           strncmp(nm, "__setter_", 9) == 0)) continue;
                nlohmann::ordered_json pv =
                    ts_to_json_internal((void*)(uintptr_t)val, visited);
                // 25.5.2.5 SerializeJSONObject step 8.b: unserializable
                // members (functions, symbols) are omitted.
                if (pv.is_discarded()) continue;
                j[desc->propNames[i]] = std::move(pv);
            }
            // Check overflow map
            void* overflow = *(void**)((char*)p + 16 + desc->numSlots * 8);
            if (overflow) {
                TsMap* map = (TsMap*)overflow;
                TsArray* keys = (TsArray*)map->GetKeys();
                for (int64_t i = 0; i < keys->Length(); i++) {
                    uint64_t keyNB = (uint64_t)keys->Get(i);
                    if (!nanbox_is_ptr(keyNB)) continue;
                    void* keyPtr = nanbox_to_ptr(keyNB);
                    if (!ts_is<TsString>(keyPtr)) continue;
                    TsString* keyStr = (TsString*)keyPtr;
                    {
                        const char* kc = keyStr->ToUtf8();
                        if (kc && (kc[0] == '\x01' ||
                                   strncmp(kc, "__getter_", 9) == 0 ||
                                   strncmp(kc, "__setter_", 9) == 0)) continue;
                    }
                    TsValue keyTv;
                    std::memset(&keyTv, 0, sizeof(TsValue));
                    keyTv.type = ValueType::STRING_PTR;
                    keyTv.ptr_val = keyStr;
                    TsValue val = map->Get(keyTv);
                    if (val.type == ValueType::UNDEFINED) continue;
                    nlohmann::ordered_json pv = ts_value_to_json(val, visited);
                    if (pv.is_discarded()) continue;  // function/symbol member
                    j[keyStr->ToUtf8()] = std::move(pv);
                }
            }
        }
        visited.erase(p);
        return j;
    }

    // TsMap is TsObject-derived, magic at offset 16
    if (magic_offset16 == TsMap::MAGIC) {
        if (visited.find(p) != visited.end()) {
            throw JsonTypeErrorSignal{"Converting circular structure to JSON"};
        }
        visited.insert(p);
        TsMap* map = (TsMap*)p;
        nlohmann::ordered_json j = nlohmann::ordered_json::object();
        TsArray* keys = (TsArray*)map->GetKeys();
        for (int64_t i = 0; i < keys->Length(); ++i) {
            // keys->Get(i) returns a NaN-boxed value (pointer to TsString)
            uint64_t keyNB = (uint64_t)keys->Get(i);
            if (!nanbox_is_ptr(keyNB)) continue;  // JSON only supports string keys
            void* keyPtr = nanbox_to_ptr(keyNB);
            if (!ts_is<TsString>(keyPtr)) continue;

            TsString* keyStr = (TsString*)keyPtr;
            std::string keyStdStr = keyStr->ToUtf8();
            // Internal storage keys (accessor slots, '\x01' privates/symbols)
            // are not properties — skip them.
            if (!keyStdStr.empty() &&
                (keyStdStr[0] == '\x01' ||
                 keyStdStr.rfind("__getter_", 0) == 0 ||
                 keyStdStr.rfind("__setter_", 0) == 0)) continue;

            // Create a TsValue struct for the key to pass to map->Get
            TsValue keyTv;
            std::memset(&keyTv, 0, sizeof(TsValue));
            keyTv.type = ValueType::STRING_PTR;
            keyTv.ptr_val = keyStr;
            TsValue val = map->Get(keyTv);
            if (val.type == ValueType::UNDEFINED) continue;

            nlohmann::ordered_json pv = ts_value_to_json(val, visited);
            if (pv.is_discarded()) continue;  // function/symbol member
            j[keyStdStr] = std::move(pv);
        }
        visited.erase(p);
        return j;
    }

    // ECMA-262 25.5.2.2 SerializeJSONProperty steps 10-11: a callable object
    // (function/closure) or a Symbol is NOT serializable — the property is
    // omitted from objects, becomes null in arrays, and makes the top-level
    // result undefined. Signal with nlohmann's `discarded` sentinel; every
    // consumer (array/object loops, ts_json_stringify) handles it explicitly
    // and it never reaches dump().
    {
        uint32_t m16sym = ((uintptr_t)p > 0x1000) ? *(uint32_t*)((char*)p + 16) : 0;
        if (ts_is_callable(p) || magic == 0x53594D42 || m16sym == 0x53594D42) {
            return nlohmann::ordered_json(nlohmann::detail::value_t::discarded);
        }
    }

    // Fallback: unknown object type
    return nlohmann::ordered_json::object();
}

static nlohmann::ordered_json ts_to_json(void* p) {
    std::set<void*> visited;
    return ts_to_json_internal(p, visited);
}

extern TsString* ts_ensure_flat(void* ptr);

// ECMA-262 25.5.2: format a Number value as a JSON property-list key string
// (used for array-replacer entries and Number wrappers). Integers get no
// fractional part; other finite numbers use the shortest round-trip form.
static std::string json_number_key(double d) {
    // Number::toString, not the JSON number grammar: NaN/Infinity stringify to
    // their names (these are legal PropertyList keys, e.g. an object with a
    // "NaN" property).
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    double intPart;
    if (std::modf(d, &intPart) == 0.0 &&
        d >= -9007199254740992.0 && d <= 9007199254740992.0) {
        return std::to_string((int64_t)d);
    }
    std::ostringstream oss;
    oss << d;
    return oss.str();
}

// If `raw` (a heap object pointer) is a Number or String wrapper object
// (`new Number(x)` / `new String(x)`, stored as an object with a hidden
// __NumberData / __StringData own slot), return its primitive: kind 1 fills
// *num, kind 2 fills *str. Returns 0 for any other object. Only own-slot data
// reads are performed, so no user getters/valueOf are invoked.
static int json_wrapper_num_or_str(void* raw, double* num, TsString** str) {
    if (!raw || (uintptr_t)raw <= 0x1000) return 0;
    {
        TsValue* v = ts_object_get_property(raw, "__NumberData");
        if (v) {
            uint64_t vb = nanbox_from_tsvalue_ptr(v);
            if (nanbox_is_int32(vb)) { *num = (double)nanbox_to_int32(vb); return 1; }
            if (nanbox_is_double(vb)) { *num = nanbox_to_double(vb); return 1; }
        }
    }
    {
        TsValue* v = ts_object_get_property(raw, "__StringData");
        if (v) {
            uint64_t vb = nanbox_from_tsvalue_ptr(v);
            if (nanbox_is_ptr(vb)) {
                void* sp = nanbox_to_ptr(vb);
                if (sp && (uintptr_t)sp > 0x1000 &&
                    *(uint32_t*)sp == TsString::MAGIC) { *str = (TsString*)sp; return 2; }
            }
        }
    }
    return 0;
}

// Format a double as its JSON number form (whole numbers as integers, e.g.
// 8.5 -> 8.5, 10.0 -> 10). Non-finite values (NaN/Infinity) become JSON null,
// matching the primitive-number path (nlohmann also dumps these as null).
static nlohmann::ordered_json json_num_to_json(double d) {
    if (!std::isfinite(d)) return nullptr;
    double intPart;
    if (std::modf(d, &intPart) == 0.0 &&
        d >= -9007199254740992.0 && d <= 9007199254740992.0) {
        return (int64_t)d;
    }
    return d;
}

// ECMA-262 25.5.2.2 SerializeJSONProperty step 4: if `p` is a Number/String/
// Boolean wrapper object (new Number(x) / new String(x) / new Boolean(x)),
// fill `out` with its primitive JSON form (ToNumber / ToString /
// [[BooleanData]]) and return true. Handles BOTH runtime representations: the
// dedicated TsNumberObject/TsStringObject/TsBooleanObject (vtable at 0, magic
// at offset 16) and TsMap/flat-backed objects carrying a hidden
// __NumberData/__StringData/__BooleanData own slot. Only own-slot / internal
// reads are performed (no user getters/valueOf), and a Proxy is never probed.
static bool json_unwrap_primitive_wrapper(void* p, nlohmann::ordered_json& out) {
    if (!p || (uintptr_t)p <= 0x1000) return false;
    uint32_t magic0 = *(uint32_t*)p;
    uint32_t magic16 = *(uint32_t*)((char*)p + 16);

    // Dedicated wrapper classes.
    if (magic16 == TsNumberObject::MAGIC) {
        out = json_num_to_json(((TsNumberObject*)p)->value);
        return true;
    }
    if (magic16 == TsStringObject::MAGIC) {
        TsString* s = ((TsStringObject*)p)->value;
        out = s ? std::string(s->ToUtf8()) : std::string();
        return true;
    }
    if (magic16 == TsBooleanObject::MAGIC) {
        out = ((TsBooleanObject*)p)->value;
        return true;
    }

    // TsMap/flat-backed wrappers with a hidden data slot. Only probe map-like
    // (non-Proxy) or flat objects; a dynamic_cast is only well-defined on the
    // polymorphic TsMap family, and a Proxy's get trap must not be tripped.
    bool isMapLike = (magic16 == 0x4D415053);  // TsMap
    bool isFlat = (magic0 == FLAT_MAGIC);
    if (!isMapLike && !isFlat) return false;
    if (isMapLike && dynamic_cast<TsProxy*>((TsObject*)p) != nullptr) return false;

    double wn = 0.0; TsString* ws = nullptr;
    int wk = json_wrapper_num_or_str(p, &wn, &ws);
    if (wk == 1) { out = json_num_to_json(wn); return true; }
    if (wk == 2) { out = ws ? std::string(ws->ToUtf8()) : std::string(); return true; }

    // Boolean wrapper: __BooleanData slot (json_wrapper_num_or_str covers only
    // Number/String). A plain object without the slot yields boxed undefined,
    // which fails the bool check below, so this is safe on arbitrary objects.
    TsValue* bv = ts_object_get_property(p, "__BooleanData");
    if (bv) {
        uint64_t bb = nanbox_from_tsvalue_ptr(bv);
        if (nanbox_is_bool(bb)) { out = nanbox_to_bool(bb); return true; }
    }
    return false;
}

// ECMA-262 25.5.2 step 5: compute the `gap` string from the `space` argument.
//   Number  -> min(10, ToInteger(space)) SPACE code units (empty if < 1)
//   String  -> first min(10, length) code units of the string
//   other   -> empty (ignored)
// Handles nanbox int/double primitives, TsString / cons-string pointers, and
// Number/String wrapper objects (new Number(x) / new String(x)).
static std::string json_compute_gap(void* space) {
    if (!space) return "";
    uint64_t nb = (uint64_t)(uintptr_t)space;

    double num = 0.0;
    bool isNum = false;
    TsString* str = nullptr;

    if (nanbox_is_int32(nb)) { num = (double)nanbox_to_int32(nb); isNum = true; }
    else if (nanbox_is_double(nb)) { num = nanbox_to_double(nb); isNum = true; }
    else if (nanbox_is_bool(nb) || nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
        return "";
    } else if (nanbox_is_ptr(nb)) {
        void* p = nanbox_to_ptr(nb);
        if (!p) return "";
        uint32_t magic0 = *(uint32_t*)p;
        uint32_t magic16 = ((uintptr_t)p > 0x1000) ? *(uint32_t*)((char*)p + 16) : 0;
        if (magic0 == TsString::MAGIC) {
            str = (TsString*)p;
        } else if (magic0 == 0x434F4E53) {  // TsConsString
            str = ts_ensure_flat(p);
        } else if (magic16 == TsNumberObject::MAGIC) {
            num = ((TsNumberObject*)p)->value; isNum = true;
        } else if (magic16 == TsStringObject::MAGIC) {
            str = ((TsStringObject*)p)->value;
        } else {
            // new Number(x) / new String(x) wrapper objects (hidden data slot)
            double wn = 0.0; TsString* ws = nullptr;
            int wk = json_wrapper_num_or_str(p, &wn, &ws);
            if (wk == 1) { num = wn; isNum = true; }
            else if (wk == 2) { str = ws; }
            else return "";  // any other object type: ignored
        }
    } else {
        return "";
    }

    if (isNum) {
        if (std::isnan(num)) num = 0.0;
        long n = (long)num;  // ToInteger truncates toward zero
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

// Custom pretty-printer over a fully-materialized nlohmann tree, using an
// arbitrary `gap` string for indentation (nlohmann::dump only supports a
// single repeated fill CHARACTER, so it cannot emit e.g. "\t" or "--"). For a
// numeric space the gap is N spaces, producing output byte-identical to
// nlohmann::dump(N). Scalars delegate to nlohmann so number/string escaping
// stays consistent with the compact path.
static void json_pp(const nlohmann::ordered_json& j, const std::string& gap,
                    const std::string& curIndent, std::string& out) {
    if (j.is_object()) {
        if (j.empty()) { out += "{}"; return; }
        std::string childIndent = curIndent + gap;
        out += "{\n";
        bool first = true;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!first) out += ",\n";
            first = false;
            out += childIndent;
            out += nlohmann::ordered_json(it.key()).dump();
            out += ": ";
            json_pp(it.value(), gap, childIndent, out);
        }
        out += "\n";
        out += curIndent;
        out += "}";
    } else if (j.is_array()) {
        if (j.empty()) { out += "[]"; return; }
        std::string childIndent = curIndent + gap;
        out += "[\n";
        bool first = true;
        for (const auto& e : j) {
            if (!first) out += ",\n";
            first = false;
            out += childIndent;
            json_pp(e, gap, childIndent, out);
        }
        out += "\n";
        out += curIndent;
        out += "]";
    } else {
        out += j.dump();
    }
}

// ECMA-262 25.5.2 step 4: build the PropertyList from an array replacer.
// Returns true if `replacer` is an Array (so key filtering applies), filling
// `keys` with the ordered, de-duplicated string keys. Only String and Number
// elements (and their wrapper objects) contribute a key, per spec.
static bool json_build_property_list(void* replacer, std::vector<std::string>& keys) {
    if (!replacer) return false;
    uint64_t nb = (uint64_t)(uintptr_t)replacer;
    if (!nanbox_is_ptr(nb)) return false;
    void* p = nanbox_to_ptr(nb);
    if (!p || (uintptr_t)p <= 0x1000) return false;
    // ECMA-262 25.5.2 step 4.b: IsArray(replacer) on a revoked Proxy throws a
    // TypeError. Signal via the C++ exception the top-level catch converts.
    if (*(uint32_t*)((char*)p + 16) == 0x4D415053) {  // TsMap (Proxy subclass)
        if (TsProxy* px = dynamic_cast<TsProxy*>((TsObject*)p)) {
            if (px->revoked) throw JsonRevokedProxyError{};
        }
    }
    if (*(uint32_t*)p != TsArray::MAGIC) return false;

    TsArray* arr = (TsArray*)p;
    std::set<std::string> seen;
    int64_t len = arr->Length();
    for (int64_t i = 0; i < len; ++i) {
        uint64_t e = (uint64_t)arr->Get(i);
        std::string key;
        bool have = false;
        if (nanbox_is_int32(e)) { key = std::to_string(nanbox_to_int32(e)); have = true; }
        else if (nanbox_is_double(e)) { key = json_number_key(nanbox_to_double(e)); have = true; }
        else if (nanbox_is_ptr(e)) {
            void* ep = nanbox_to_ptr(e);
            if (ep && (uintptr_t)ep > 0x1000) {
                uint32_t m0 = *(uint32_t*)ep;
                uint32_t m16 = *(uint32_t*)((char*)ep + 16);
                if (m0 == TsString::MAGIC) { key = ((TsString*)ep)->ToUtf8(); have = true; }
                else if (m0 == 0x434F4E53) {
                    TsString* f = ts_ensure_flat(ep);
                    if (f) { key = f->ToUtf8(); have = true; }
                } else if (m16 == TsStringObject::MAGIC) {
                    TsString* v = ((TsStringObject*)ep)->value;
                    if (v) { key = v->ToUtf8(); have = true; }
                } else if (m16 == TsNumberObject::MAGIC) {
                    key = json_number_key(((TsNumberObject*)ep)->value); have = true;
                } else {
                    // new Number(x) / new String(x) wrapper objects carry a
                    // hidden __NumberData / __StringData own slot. Only probe
                    // flat objects and (non-Proxy) TsMap-backed objects — a
                    // dynamic_cast is only well-defined on the polymorphic
                    // TsMap family, and Proxies must not have a get trap tripped.
                    bool isMapLike = (m16 == 0x4D415053);
                    bool isFlat = (m0 == FLAT_MAGIC);
                    bool isProxy = isMapLike &&
                        (dynamic_cast<TsProxy*>((TsObject*)ep) != nullptr);
                    if ((isFlat || isMapLike) && !isProxy) {
                        double wn = 0.0; TsString* ws = nullptr;
                        int wk = json_wrapper_num_or_str(ep, &wn, &ws);
                        if (wk == 1) { key = json_number_key(wn); have = true; }
                        else if (wk == 2 && ws) { key = ws->ToUtf8(); have = true; }
                    }
                }
            }
        }
        if (have && seen.insert(key).second) keys.push_back(key);
    }
    return true;
}

// Recursively restrict every object in the tree to the PropertyList `keys`,
// emitting members in list order (ECMA-262 25.5.2 SerializeJSONObject with a
// PropertyList). Applies at every nesting level, including objects inside
// arrays.
static nlohmann::ordered_json json_apply_property_list(
        const nlohmann::ordered_json& j, const std::vector<std::string>& keys) {
    if (j.is_object()) {
        nlohmann::ordered_json out = nlohmann::ordered_json::object();
        for (const std::string& k : keys) {
            auto it = j.find(k);
            if (it != j.end()) out[k] = json_apply_property_list(it.value(), keys);
        }
        return out;
    }
    if (j.is_array()) {
        nlohmann::ordered_json out = nlohmann::ordered_json::array();
        for (const auto& e : j) out.push_back(json_apply_property_list(e, keys));
        return out;
    }
    return j;
}

// JS exception machinery (defined elsewhere in the runtime). Declared at file
// scope per runtime-safety rules (block-scope extern "C" is illegal).
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);
extern "C" TsValue* ts_value_make_undefined();
extern "C" TsValue* ts_value_make_double(double d);

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
        extern void* ts_string_from_value(TsValue* val);
        TsString* s = (TsString*)ts_string_from_value((TsValue*)json_str);
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

    void* ts_json_stringify(void* obj, void* replacer, void* space) {
        // ECMA-262 25.5.2 steps 11-12 via SerializeJSONProperty step 11:
        // JSON.stringify(undefined), a bare function, or a Symbol at the top
        // level returns undefined (NOT the string "null" / "{}").
        // NOTE: a C-level nullptr is NOT undefined here — internal callers
        // (util.isDeepStrictEqual, legacy paths) pass nullptr for JS null and
        // expect the string "null" back; only a genuine NANBOX_UNDEFINED input
        // takes the undefined path.
        if (obj && nanbox_is_undefined((uint64_t)(uintptr_t)obj))
            return ts_value_make_undefined();
        try {
            nlohmann::ordered_json j = ts_to_json(obj);
            if (j.is_discarded()) return ts_value_make_undefined();

            // ECMA-262 25.5.2 step 4: an Array replacer is a PropertyList that
            // filters/orders object keys at EVERY nesting level (not just the
            // top). A function replacer is not yet supported and is ignored.
            if (replacer) {
                std::vector<std::string> keys;
                if (json_build_property_list(replacer, keys)) {
                    j = json_apply_property_list(j, keys);
                }
            }

            // ECMA-262 25.5.2 step 5-6: derive the indentation gap from `space`
            // (number clamped to 0..10 spaces; string truncated to 10 code
            // units). Empty gap => compact output.
            std::string gap = json_compute_gap(space);

            std::string s;
            if (!gap.empty()) {
                json_pp(j, gap, "", s);
            } else {
                s = j.dump();
            }
            return TsString::Create(s.c_str());
        } catch (const JsonAbruptCompletion& a) {
            // C++ unwinding cleaned the recursion frames; re-throw the
            // ORIGINAL user error from this clean frame.
            ts_throw(a.error);
            return TsString::Create("null");  // unreachable
        } catch (const JsonRevokedProxyError&) {
            // The C++ exception unwound the nlohmann/std frames; this catch
            // frame is clean, so the ts_throw longjmp is safe here.
            extern void* ts_error_create_typed(const char* type, const char* message);
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot serialize a revoked Proxy with JSON.stringify"));
            return TsString::Create("null");  // unreachable
        } catch (const JsonTypeErrorSignal& s) {
            // Circular structure or BigInt: raise a JS TypeError from this
            // clean frame (the swallowing catch(...) below returned "null").
            extern void* ts_error_create_typed(const char* type, const char* message);
            ts_throw((TsValue*)ts_error_create_typed("TypeError", s.msg));
            return TsString::Create("null");  // unreachable
        } catch (...) {
            return TsString::Create("null");
        }
    }
}
