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
#include "TsTyped.h"
#include "GC.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstring>

#include <set>
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
            j.push_back(ts_to_json_internal((void*)raw, visited));
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
                j[desc->propNames[i]] = ts_to_json_internal((void*)(uintptr_t)val, visited);
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
                    j[keyStr->ToUtf8()] = ts_value_to_json(val, visited);
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

            j[keyStdStr] = ts_value_to_json(val, visited);
        }
        visited.erase(p);
        return j;
    }

    // Fallback: unknown object type
    return nlohmann::ordered_json::object();
}

static nlohmann::ordered_json ts_to_json(void* p) {
    std::set<void*> visited;
    return ts_to_json_internal(p, visited);
}

// JS exception machinery (defined elsewhere in the runtime). Declared at file
// scope per runtime-safety rules (block-scope extern "C" is illegal).
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

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
        try {
            nlohmann::ordered_json j = ts_to_json(obj);

            int indent = -1;
            if (space) {
                nlohmann::ordered_json s = ts_to_json(space);
                if (s.is_number()) {
                    indent = s.get<int>();
                } else if (s.is_string()) {
                    indent = (int)s.get<std::string>().length();
                }
            }

            if (replacer) {
                nlohmann::ordered_json r = ts_to_json(replacer);
                if (r.is_array() && j.is_object()) {
                    nlohmann::ordered_json filtered = nlohmann::ordered_json::object();
                    for (auto& key : r) {
                        if (key.is_string()) {
                            std::string k = key.get<std::string>();
                            if (j.contains(k)) {
                                filtered[k] = j[k];
                            }
                        }
                    }
                    j = filtered;
                }
            }

            std::string s = (indent >= 0) ? j.dump(indent) : j.dump();
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
