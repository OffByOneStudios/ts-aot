#include "TsObject_Internal.h"
// #66 (defined extern "C" in TsObject.cpp)
extern "C" { extern void* g_object_proto_map; extern bool g_object_proto_dirty; }
#include "TsProxy.h"

// Integrity levels for EXOTIC objects (functions, Dates, RegExps, TsArrays,
// arguments objects...) that are neither flat objects nor TsMaps and so have
// no header flag bits to record seal/freeze/preventExtensions. Weak side-
// table keyed by object pointer: 1 = non-extensible, 2 = sealed, 3 = frozen.
// GC rule (runtime-safety): keys are weak (never marked — marking would pin
// dead objects), but promoted keys MUST be re-pointed via the minor fixup or
// a stale nursery address aliases a reused slot -> type confusion.
static std::unordered_map<void*, uint8_t> g_obj_integrity;
static struct ObjIntegrityFixup {
    ObjIntegrityFixup() {
        ts_gc_register_minor_fixup([](void*) {
            std::vector<std::pair<void*, uint8_t>> reinserts;
            for (auto it = g_obj_integrity.begin(); it != g_obj_integrity.end(); ) {
                void* fixed = ts_gc_minor_lookup_forward(it->first);
                if (fixed && fixed != it->first) {
                    reinserts.emplace_back(fixed, it->second);
                    it = g_obj_integrity.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& kv : reinserts) {
                uint8_t& lvl = g_obj_integrity[kv.first];
                if (kv.second > lvl) lvl = kv.second;
            }
        }, nullptr);
    }
} g_obj_integrity_fixup;

static bool integrity_plausible_ptr(void* p) {
    uintptr_t u = (uintptr_t)p;
    return u > 0x1000 && u < 0x0000800000000000ULL;
}
static void integrity_set(void* raw, uint8_t lvl) {
    if (!integrity_plausible_ptr(raw)) return;
    uint8_t& cur = g_obj_integrity[raw];
    if (lvl > cur) cur = lvl;
}
extern "C" uint8_t ts_integrity_get(void* raw) {
    if (g_obj_integrity.empty() || !integrity_plausible_ptr(raw)) return 0;
    auto it = g_obj_integrity.find(raw);
    return it != g_obj_integrity.end() ? it->second : 0;
}

// Object.* static methods + property-descriptor machinery extracted from
// TsObject.cpp: keys/values/entries/assign/is/getOwnPropertyNames/getPrototypeOf/
// create/setPrototypeOf/freeze/seal/preventExtensions/isFrozen/isSealed/
// isExtensible/defineProperty/defineProperties/getOwnPropertyDescriptor(s)/
// hasOwn/fromEntries/groupBy plus their local descriptor helpers.
extern "C" {


    // Revoked-proxy guard (defined in TsProxy.cpp): throws TypeError for a
    // revoked-proxy receiver in the Object.* operations below.
    extern "C" void ts_proxy_throw_if_revoked(void* boxed);
    static TsValue* ts_object_keys_impl(TsValue* obj);
    TsValue* ts_object_keys(TsValue* obj) {
        TsValue* r = ts_object_keys_impl(obj);
        extern void ts_keys_spec_order(void* keysRaw);
        void* arr = r ? ts_value_get_object(r) : nullptr;
        if (arr && *(uint32_t*)arr == TsArray::MAGIC) ts_keys_spec_order(arr);
        return r;
    }
    static TsValue* ts_object_keys_impl(TsValue* obj) {
        // Proxy: route through the ownKeys trap (ES 10.5.11), then apply
        // Object.keys' EnumerableOwnPropertyNames filtering: STRING keys
        // whose [[GetOwnProperty]] (the gOPD trap — observable) reports
        // enumerable. Only divert when the trap exists or revoked.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && (uintptr_t)raw0 > 0x1000 &&
                *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    // Trap-less proxy: [[OwnPropertyKeys]] forwards to the
                    // TARGET's ordinary behavior (was: fell to the proxy's
                    // own empty map -> []).
                    if (!px->revoked && !px->getTrap("ownKeys") && px->target) {
                        return ts_object_keys(ts_value_box_any(px->target));
                    }
                    if (px->revoked || px->getTrap("ownKeys")) {
                        TsValue* all = px->ownKeys();
                        void* aRaw = all ? ts_value_get_object(all) : nullptr;
                        TsArray* aKeys = (aRaw && *(uint32_t*)aRaw == 0x41525259)
                            ? (TsArray*)aRaw : nullptr;
                        if (!aKeys) return all;
                        TsArray* outArr = TsArray::Create();
                        int64_t an = aKeys->Length();
                        for (int64_t i = 0; i < an; i++) {
                            TsValue* k = (TsValue*)aKeys->Get(i);
                            uint64_t knb = k ? nanbox_from_tsvalue_ptr(k) : 0;
                            void* kp = (k && nanbox_is_ptr(knb)) ? nanbox_to_ptr(knb) : nullptr;
                            if (!kp || *(uint32_t*)kp != 0x53545247 /*STRG*/) continue;
                            TsValue* d = px->getOwnPropertyDescriptorTrap(k);
                            if (!d || ts_value_is_undefined(d)) continue;
                            void* dRaw2 = ts_value_get_object(d);
                            if (!dRaw2) continue;
                            TsValue* ev = ts_object_get_property(dRaw2, "enumerable");
                            if (ev && ts_value_to_bool(ev))
                                outArr->Push((int64_t)k);
                        }
                        return ts_value_make_array(outArr);
                    }
                }
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        // ECMA-262 20.1.2.17: ToObject(O) is performed first -> TypeError on
        // null/undefined. (Object.values/entries already do this; keys did not,
        // returning []). Other primitives coerce to a wrapper with an empty own-key
        // set and are handled by the fall-through below.
        uint64_t nbK = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nbK) || nanbox_is_undefined(nbK)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_array(TsArray::Create(0));
        }

        // Unbox if needed. ts_value_get_object returns null for non-object
        // NaN-boxed primitives (number / bool / undefined / null). Per
        // ECMA-262 §19.1.2.16, Object.keys(primitive) ToObjects to a wrapper
        // whose own-property set is empty (the wrapper's accessible members
        // live on its prototype). Falling back to `rawPtr = obj` here would
        // treat the NaN-box bits (e.g. 0xFFFE000000000001 for int 1) as a
        // heap pointer and deref into garbage for the magic-at-offset-0
        // dispatch below — lodash _.isEmpty(1) calls Object.keys(1) and hit
        // this. Strings retain their pointer via nanbox_is_string_ptr, so
        // ts_value_get_object returns the TsString* for them — they still
        // reach the magic0=STRG branch.
        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) return ts_value_make_array(TsArray::Create(0));

        // Guard against small integer values stored directly (undefined=0,
        // null=1, etc.) that aren't NaN-boxed.
        if ((uintptr_t)rawPtr < 0x10000) return ts_value_make_array(TsArray::Create(0));

        // Check string — for...in on a string enumerates character indices
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x53545247) { // TsString::MAGIC "STRG"
            TsString* str = (TsString*)rawPtr;
            int64_t len = str->Length();
            TsArray* arr = TsArray::Create(len);
            for (int64_t i = 0; i < len; i++) {
                arr->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
            }
            return ts_value_make_array(arr);
        }

        // Check array (magic at offset 0). Own enumerable keys = present index
        // strings (holes skipped, per ECMA-262 Object.keys) followed by custom
        // string-keyed own properties from the side map (`arr.foo = 1`). Without
        // this an array fell through to the empty default, so Object.keys /
        // for-in / getOwnPropertyNames returned [] for arrays (the lodash
        // "keys methods" cluster: keys for custom properties on arrays, etc.).
        if (magic0 == 0x41525259) { // TsArray "ARRY"
            TsArray* a = (TsArray*)rawPtr;
            int64_t len = a->Length();
            TsArray* out = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++) {
                if (a->IsHole((size_t)i)) continue;
                // Skip non-enumerable indices defined via Object.defineProperty.
                // A recorded "__arr_attrs_<i>" with its enumerable bit (0x01)
                // clear is non-enumerable; an accessor index with no recorded
                // attrs defaults non-enumerable. A plain element (no side entry)
                // is enumerable. ECMA-262 EnumerateObjectProperties / Object.keys
                // yield only enumerable own keys.
                if (a->properties) {
                    char ak[40];
                    snprintf(ak, sizeof(ak), "__arr_attrs_%lld", (long long)i);
                    TsValue atk; atk.type = ValueType::STRING_PTR; atk.ptr_val = TsString::GetInterned(ak);
                    if (a->properties->Has(atk)) {
                        TsValue av = a->properties->Get(atk);
                        if (!(((uint64_t)av.i_val) & 0x01)) continue;  // non-enumerable
                    } else {
                        snprintf(ak, sizeof(ak), "__arr_getter_%lld", (long long)i);
                        TsValue gk; gk.type = ValueType::STRING_PTR; gk.ptr_val = TsString::GetInterned(ak);
                        snprintf(ak, sizeof(ak), "__arr_setter_%lld", (long long)i);
                        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(ak);
                        if (a->properties->Has(gk) || a->properties->Has(sk)) continue;  // accessor, default non-enum
                    }
                }
                out->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
            }
            if (a->properties) {
                extern void* ts_map_enumerable_keys(void*);
                TsArray* extra = (TsArray*)ts_map_enumerable_keys(a->properties);
                if (extra) {
                    int64_t n = extra->Length();
                    for (int64_t i = 0; i < n; i++) {
                        // Skip internal per-index bookkeeping keys
                        // (__arr_getter_/__arr_setter_/__arr_attrs_) — they are
                        // implementation storage, not user-visible properties.
                        int64_t kraw = extra->Get((size_t)i);
                        TsString* ks = (TsString*)ts_value_get_string(
                            (TsValue*)(uintptr_t)kraw);
                        if (ks) {
                            const char* kc = ks->ToUtf8();
                            if (kc && strncmp(kc, "__arr_", 6) == 0) continue;
                        }
                        out->Push(kraw);
                    }
                }
            }
            return ts_value_make_array(out);
        }

        // Check flat object (magic at offset 0)
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_keys(rawPtr));
        }

        // Check TsMap::magic at offset 16 (after vptr + explicit vtable field)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            // Object.keys() returns only enumerable own properties
            extern void* ts_map_enumerable_keys(void*);
            TsMap* m = (TsMap*)rawPtr;
            // Primitive String wrapper (TsMap with __StringData): its own
            // enumerable keys are the character indices '0'..'length-1' first,
            // then any other own enumerable string props (ECMA-262 String
            // exotic OwnPropertyKeys order).
            {
                TsValue sdKey; sdKey.type = ValueType::STRING_PTR;
                sdKey.ptr_val = TsString::GetInterned("__StringData");
                TsValue sd = m->Get(sdKey);
                if (sd.type == ValueType::STRING_PTR && sd.ptr_val) {
                    TsString* str = (TsString*)sd.ptr_val;
                    int64_t len = str->Length();
                    TsArray* out = TsArray::Create(0);
                    for (int64_t i = 0; i < len; i++) {
                        out->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
                    }
                    TsArray* extra = (TsArray*)ts_map_enumerable_keys(rawPtr);
                    if (extra) {
                        for (int64_t i = 0; i < extra->Length(); i++) out->Push(extra->Get((size_t)i));
                    }
                    return ts_value_make_array(out);
                }
            }
            return ts_value_make_array(ts_map_enumerable_keys(rawPtr));
        }

        // Function / closure objects store their own properties in a side
        // TsMap. Object.keys/for-in/`in` must enumerate them too — lodash
        // assigns ~300 methods onto the `lodash` function and relies on
        // keys(lodash) (mixin) to copy them to the wrapper prototype.
        if (magic == 0x46554E43) { // TsFunction::MAGIC "FUNC"
            TsMap* props = ((TsFunction*)rawPtr)->properties;
            if (props) {
                extern void* ts_map_enumerable_keys(void*);
                return ts_value_make_array(ts_map_enumerable_keys(props));
            }
            return ts_value_make_array(TsArray::Create(0));
        }
        if (magic == 0x434C5352) { // TsClosure::MAGIC "CLSR"
            TsMap* props = ((TsClosure*)rawPtr)->properties;
            if (props) {
                extern void* ts_map_enumerable_keys(void*);
                return ts_value_make_array(ts_map_enumerable_keys(props));
            }
            return ts_value_make_array(TsArray::Create(0));
        }

        // Check if this is a Proxy - only attempt dynamic_cast on known TsObject types
        // (TsString, TsArray, etc. are NOT TsObject subclasses — dynamic_cast crashes)
        if (magic == 0x50524F58) { // TsProxy::MAGIC "PROX"
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawPtr);
            if (proxy) {
                return proxy->ownKeys();
            }
        }

        // Not a map - return empty array
        return ts_value_make_array(TsArray::Create(0));
    }

    // ECMA-262 14.6 (CopyDataProperties / object-rest): build a fresh plain
    // object holding all of `source`'s own enumerable properties EXCEPT those
    // whose keys appear in `excludedKeys` (the keys already consumed by the
    // destructuring pattern). Used by object-rest destructuring `{a, ...rest}`
    // (assignment and binding forms). Implemented as own-enumerable copy
    // (ts_object_assign) then delete of each excluded key — the result's props
    // are plain data properties {writable, enumerable, configurable}: true,
    // which matches the spec's CreateDataPropertyOrThrow.
    extern void* ts_array_get_unchecked(void* arr, int64_t index);
    TsValue* ts_object_rest_exclude(TsValue* source, TsValue* excludedKeys) {
        TsMap* result = TsMap::Create();
        TsValue* resultBoxed = ts_value_make_object(result);
        if (!source || !ts_value_get_object(source)) return resultBoxed;
        ts_object_assign(resultBoxed, source);
        if (excludedKeys) {
            void* exclRaw = ts_value_get_object(excludedKeys);
            if (exclRaw) {
                int64_t m = ts_array_length(exclRaw);
                for (int64_t j = 0; j < m; ++j) {
                    void* ek = ts_array_get_unchecked(exclRaw, j);
                    if (ek) ts_object_delete_prop(resultBoxed, (TsValue*)ek);
                }
            }
        }
        return resultBoxed;
    }

    // for-in enumeration: own enumerable string keys PLUS inherited enumerable
    // string keys from the prototype chain (deduped, first occurrence wins).
    // ECMA-262 14.7.5.9. Object.keys (own only) is the wrong source for
    // `for (k in obj)` when obj has a prototype with enumerable properties
    // (Object.create({a:1}); a `new Foo()` whose Foo.prototype has enumerable
    // methods; lodash keysIn/assignIn/defaults). Built-in and function/class
    // prototype `constructor` back-pointers are non-enumerable so they do not
    // appear here.
    TsValue* ts_object_for_in_keys(TsValue* obj) {
        TsValue* ownVal = ts_object_keys(obj);
        TsArray* result = (TsArray*)ts_value_get_object(ownVal);
        if (!result) return ownVal;

        std::unordered_map<std::string, char> seen;
        for (int64_t i = 0; i < result->Length(); i++) {
            void* sp = ts_value_get_string((TsValue*)(uintptr_t)result->Get(i));
            if (sp) { const char* k = ((TsString*)sp)->ToUtf8(); if (k) seen[k] = 1; }
        }

        TsValue* cur = ts_object_getPrototypeOf(obj);
        for (int depth = 0; cur && depth < 100; depth++) {
            uint64_t pnb = nanbox_from_tsvalue_ptr(cur);
            if (nanbox_is_null(pnb) || nanbox_is_undefined(pnb)) break;
            if (!ts_value_get_object(cur)) break;
            TsValue* pkVal = ts_object_keys(cur);
            TsArray* pk = (TsArray*)ts_value_get_object(pkVal);
            if (pk) {
                for (int64_t i = 0; i < pk->Length(); i++) {
                    int64_t boxed = pk->Get(i);
                    void* sp = ts_value_get_string((TsValue*)(uintptr_t)boxed);
                    if (!sp) continue;
                    const char* kc = ((TsString*)sp)->ToUtf8();
                    if (!kc) continue;
                    std::string k(kc);
                    if (seen.count(k)) continue;
                    seen[k] = 1;
                    result->Push(boxed);
                }
            }
            cur = ts_object_getPrototypeOf(cur);
        }
        return ts_value_make_array(result);
    }

    // Object.values(obj) - returns array of values
    TsValue* ts_object_values(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        // ECMA-262 20.1.2.23: ToObject(O) first -> TypeError on null/undefined;
        // other primitives coerce to a wrapper with no own enumerable string
        // keys (-> []). Without this the magic read below dereferenced a
        // NaN-boxed primitive (e.g. Object.values(true)) and crashed.
        uint64_t nb_v = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nb_v) || nanbox_is_undefined(nb_v)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_array(TsArray::Create(0));  // unreachable
        }
        if (!nanbox_is_ptr(nb_v)) return ts_value_make_array(TsArray::Create(0));

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check flat object (magic at offset 0)
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_values(rawPtr));
        }
        if (magic0 == 0x53545247) { // TsString "STRG": values = the characters
            extern int64_t ts_string_length(void* str);
            extern void* ts_string_charAt(void* str, int64_t index);
            int64_t len = ts_string_length(rawPtr);
            TsArray* arr = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++)
                arr->Push((int64_t)(uintptr_t)ts_value_make_string(
                    (TsString*)ts_string_charAt(rawPtr, i)));
            return ts_value_make_array(arr);
        }
        if (magic0 == 0x41525259) { // TsArray "ARRY": values = enumerable element values
            TsArray* a = (TsArray*)rawPtr;
            int64_t len = a->Length();
            TsArray* out = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++) {
                if (a->IsHole((size_t)i)) continue;
                if (a->properties) {  // skip non-enumerable / accessor indices (mirror Object.keys)
                    char ak[40]; snprintf(ak, sizeof(ak), "__arr_attrs_%lld", (long long)i);
                    TsValue atk; atk.type = ValueType::STRING_PTR; atk.ptr_val = TsString::GetInterned(ak);
                    if (a->properties->Has(atk)) {
                        TsValue av = a->properties->Get(atk);
                        if (!(((uint64_t)av.i_val) & 0x01)) continue;
                    } else {
                        snprintf(ak, sizeof(ak), "__arr_getter_%lld", (long long)i);
                        TsValue gk; gk.type = ValueType::STRING_PTR; gk.ptr_val = TsString::GetInterned(ak);
                        snprintf(ak, sizeof(ak), "__arr_setter_%lld", (long long)i);
                        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(ak);
                        if (a->properties->Has(gk) || a->properties->Has(sk)) continue;
                    }
                }
                out->Push((int64_t)a->Get(i));
            }
            return ts_value_make_array(out);
        }

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_values(rawPtr));
        }
        if (magic == 0x46554E43) { // TsFunction::MAGIC "FUNC"
            return ts_func_props_view(((TsFunction*)rawPtr)->properties, false);
        }
        if (magic == 0x434C5352) { // TsClosure::MAGIC "CLSR"
            return ts_func_props_view(((TsClosure*)rawPtr)->properties, false);
        }
        return ts_value_make_array(TsArray::Create(0));
    }

    // Object.entries(obj) - returns array of [key, value] pairs
    static TsValue* ts_object_entries_impl(TsValue* obj);
    TsValue* ts_object_entries(TsValue* obj) {
        TsValue* r = ts_object_entries_impl(obj);
        extern void ts_entries_spec_order(void* entriesRaw);
        void* arr = r ? ts_value_get_object(r) : nullptr;
        if (arr && *(uint32_t*)arr == TsArray::MAGIC) ts_entries_spec_order(arr);
        return r;
    }
    static TsValue* ts_object_entries_impl(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        // ECMA-262 20.1.2.5: ToObject(O) first -> TypeError on null/undefined;
        // other primitives coerce to a wrapper with no own enumerable string
        // keys (-> []). Without this the magic read below dereferenced a
        // NaN-boxed primitive (e.g. Object.entries(true)) and crashed.
        uint64_t nb_e = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nb_e) || nanbox_is_undefined(nb_e)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_array(TsArray::Create(0));  // unreachable
        }
        if (!nanbox_is_ptr(nb_e)) return ts_value_make_array(TsArray::Create(0));

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check flat object (magic at offset 0)
        uint32_t magic0_e = *(uint32_t*)rawPtr;
        if (magic0_e == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_entries(rawPtr));
        }
        if (magic0_e == 0x53545247) { // TsString "STRG": entries = [["0","a"],...]
            extern int64_t ts_string_length(void* str);
            extern void* ts_string_charAt(void* str, int64_t index);
            int64_t len = ts_string_length(rawPtr);
            TsArray* arr = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++) {
                TsArray* pair = TsArray::Create(0);
                pair->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
                pair->Push((int64_t)(uintptr_t)ts_value_make_string(
                    (TsString*)ts_string_charAt(rawPtr, i)));
                arr->Push((int64_t)(uintptr_t)ts_value_make_array(pair));
            }
            return ts_value_make_array(arr);
        }
        if (magic0_e == 0x41525259) { // TsArray "ARRY": entries = [[String(i), value], ...]
            TsArray* a = (TsArray*)rawPtr;
            int64_t len = a->Length();
            TsArray* arr = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++) {
                if (a->IsHole((size_t)i)) continue;
                if (a->properties) {  // skip non-enumerable / accessor indices (mirror Object.keys)
                    char ak[40]; snprintf(ak, sizeof(ak), "__arr_attrs_%lld", (long long)i);
                    TsValue atk; atk.type = ValueType::STRING_PTR; atk.ptr_val = TsString::GetInterned(ak);
                    if (a->properties->Has(atk)) {
                        TsValue av = a->properties->Get(atk);
                        if (!(((uint64_t)av.i_val) & 0x01)) continue;
                    } else {
                        snprintf(ak, sizeof(ak), "__arr_getter_%lld", (long long)i);
                        TsValue gk; gk.type = ValueType::STRING_PTR; gk.ptr_val = TsString::GetInterned(ak);
                        snprintf(ak, sizeof(ak), "__arr_setter_%lld", (long long)i);
                        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(ak);
                        if (a->properties->Has(gk) || a->properties->Has(sk)) continue;
                    }
                }
                TsArray* pair = TsArray::Create(0);
                pair->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
                pair->Push((int64_t)a->Get(i));
                arr->Push((int64_t)(uintptr_t)ts_value_make_array(pair));
            }
            return ts_value_make_array(arr);
        }

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_entries(rawPtr));
        }
        if (magic == 0x46554E43) { // TsFunction::MAGIC "FUNC"
            return ts_func_props_view(((TsFunction*)rawPtr)->properties, true);
        }
        if (magic == 0x434C5352) { // TsClosure::MAGIC "CLSR"
            return ts_func_props_view(((TsClosure*)rawPtr)->properties, true);
        }

        return ts_value_make_array(TsArray::Create(0));
    }

    // Object.is(value1, value2) - ES6 SameValue comparison
    // Differs from === in that:
    // - Object.is(NaN, NaN) returns true
    // - Object.is(0, -0) returns false
    bool ts_object_is(TsValue* val1, TsValue* val2) {
        if (!val1 && !val2) return true;
        if (!val1 || !val2) return false;

        uint64_t nb1 = nanbox_from_tsvalue_ptr(val1);
        uint64_t nb2 = nanbox_from_tsvalue_ptr(val2);

        // Same bits = same value (except for doubles: NaN and -0)
        if (nb1 == nb2) return true;

        // Both int32: already handled by nb1==nb2
        // Both double: need special NaN/-0 handling
        if (nanbox_is_double(nb1) && nanbox_is_double(nb2)) {
            double d1 = nanbox_to_double(nb1);
            double d2 = nanbox_to_double(nb2);
            // NaN === NaN in SameValue
            if (d1 != d1 && d2 != d2) return true;
            if (d1 != d1 || d2 != d2) return false;
            // +0 !== -0 in SameValue
            if (d1 == 0.0 && d2 == 0.0) {
                return (1.0 / d1) > 0.0 == (1.0 / d2) > 0.0;
            }
            return d1 == d2;
        }

        // Both string pointers: compare by content
        if (nanbox_is_string_ptr(nb1) && nanbox_is_string_ptr(nb2)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nb1);
            TsString* s2 = (TsString*)nanbox_to_ptr(nb2);
            if (!s1 && !s2) return true;
            if (!s1 || !s2) return false;
            return strcmp(s1->ToUtf8(), s2->ToUtf8()) == 0;
        }

        // Different types or different pointers = not equal
        return false;
    }

    // Object.getOwnPropertyNames(obj) - returns array of all own property names
    // In our runtime, this is the same as Object.keys() since we don't have
    // non-enumerable properties
    static TsValue* ts_object_getOwnPropertyNames_impl(TsValue* obj);
    TsValue* ts_object_getOwnPropertyNames(TsValue* obj) {
        TsValue* r = ts_object_getOwnPropertyNames_impl(obj);
        extern void ts_keys_spec_order(void* keysRaw);
        void* arr = r ? ts_value_get_object(r) : nullptr;
        if (arr && *(uint32_t*)arr == TsArray::MAGIC) ts_keys_spec_order(arr);
        return r;
    }
    static TsValue* ts_object_getOwnPropertyNames_impl(TsValue* obj) {
        // Proxy: ownKeys trap (string keys; see ts_object_keys).
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && (uintptr_t)raw0 > 0x1000 &&
                *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    if (!px->revoked && !px->getTrap("ownKeys") && px->target)
                        return ts_object_getOwnPropertyNames(
                            ts_value_box_any(px->target));
                    if (px->revoked || px->getTrap("ownKeys"))
                        return px->ownKeys();
                }
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        // ECMA-262 19.1.2.10: ToObject(O) is performed first, which throws
        // TypeError on null/undefined. Primitives coerce to wrapper objects
        // with no own keys (effectively empty array — match V8 behavior).
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_array(TsArray::Create(0));  // unreachable
        }
        if (!nanbox_is_ptr(nb)) {
            return ts_value_make_array(TsArray::Create(0));
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) return ts_value_make_array(TsArray::Create(0));

        // String wrapper: own property names are "0".."len-1" then "length".
        if ((uintptr_t)rawPtr >= 0x10000 && *(uint32_t*)rawPtr == 0x53545247) { // STRG
            extern int64_t ts_string_length(void* str);
            int64_t len = ts_string_length(rawPtr);
            TsArray* arr = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++)
                arr->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
            arr->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::Create("length")));
            return ts_value_make_array(arr);
        }

        // Handle flat objects
        if (is_flat_object(rawPtr)) {
            return ts_value_make_array((TsArray*)ts_flat_object_keys(rawPtr));
        }

        // All own string keys incl. non-enumerable, but never internal '\x01'
        // storage keys (user-symbol slots, private-method "\x01#m" slots) —
        // those are not property keys per ECMA-262.
        extern void* ts_map_own_string_keys(void*);

        // TsArray (magic "ARRY" at offset 0): getOwnPropertyNames includes
        // non-enumerable keys, so unlike Object.keys it must emit every present
        // index (ascending), the own non-enumerable "length", then the side-map
        // string keys in insertion order (skipping internal __arr_* bookkeeping).
        // Was missing entirely — arrays fell through to an empty array.
        if (*(uint32_t*)((char*)rawPtr) == 0x41525259) {
            TsArray* a = (TsArray*)rawPtr;
            int64_t len = a->Length();
            TsArray* out = TsArray::Create(0);
            for (int64_t i = 0; i < len; i++) {
                if (a->IsHole((size_t)i)) continue;
                out->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
            }
            out->Push((int64_t)(uintptr_t)ts_value_make_string(
                TsString::GetInterned("length")));
            if (a->properties) {
                TsArray* extra = (TsArray*)ts_map_own_string_keys(a->properties);
                if (extra) {
                    int64_t n = extra->Length();
                    for (int64_t i = 0; i < n; i++) {
                        int64_t kraw = extra->Get((size_t)i);
                        TsString* ks = (TsString*)ts_value_get_string(
                            (TsValue*)(uintptr_t)kraw);
                        if (ks) {
                            const char* kc = ks->ToUtf8();
                            if (kc && strncmp(kc, "__arr_", 6) == 0) continue;
                        }
                        out->Push(kraw);
                    }
                }
            }
            return ts_value_make_array(out);
        }

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_own_string_keys(rawPtr));
        }

        // Handle TsFunction and TsClosure - delegate to their properties map
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsMap* props = ((TsFunction*)rawPtr)->properties;
            if (props) return ts_value_make_array(ts_map_own_string_keys(props));
        }
        if (magic == 0x434C5352) { // TsClosure magic
            TsMap* props = ((TsClosure*)rawPtr)->properties;
            if (props) return ts_value_make_array(ts_map_own_string_keys(props));
        }

        return ts_value_make_array(TsArray::Create(0));
    }

    // Object.getPrototypeOf(obj) - returns the prototype of an object
    TsValue* ts_object_getPrototypeOf(TsValue* obj) {
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        // Proxy: route through the getPrototypeOf trap (ES 10.5.1); a
        // trap-less proxy forwards to the TARGET's ordinary behavior.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0))
                    return px->getPrototypeOfTrap();
            }
        }
        // Subclass-of-builtin instance: its recorded [[Prototype]]
        // (Subclass.prototype from ts_subclass_builtin_alloc) wins over the
        // per-magic builtin dispatch below.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0) {
                extern void* ts_native_object_get_proto(void* o);
                void* p = ts_native_object_get_proto(raw0);
                if (p) return ts_value_make_object(p);
            }
        }
        // Per spec 19.1.2.12: ToObject(O) is performed first, which
        // throws TypeError on null/undefined. Without this guard, the
        // magic-check below dereferences a tagged primitive and crashes.
        if (!obj || ts_value_is_nullish(obj)) {
            // ToObject(null/undefined) throws a real TypeError (was a generic
            // Error whose constructor is Object, so assert.throws(TypeError) failed).
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object.getPrototypeOf called on null or undefined"));
            return ts_value_make_undefined();
        }

        // Unbox obj if needed. For NaN-boxed primitives (numbers/strings/
        // booleans) ts_value_get_object returns nullptr; per spec we should
        // ToObject-box and return the wrapper prototype, but absent that
        // path we return null to avoid faulting on the magic check below.
        void* objRaw = ts_value_get_object(obj);
        if (!objRaw) {
            uint64_t nb = nanbox_from_tsvalue_ptr(obj);
            if (!nanbox_is_ptr(nb)) return ts_value_make_null();
            objRaw = obj;
        }

        // Helper: walk to ConstructorGlobal.prototype and return its TsMap*.
        // ctorGetter is the runtime accessor (e.g. ts_get_global_Array).
        auto getCtorPrototype = [](void* ctor) -> TsValue* {
            if (!ctor) return ts_value_make_null();
            void* raw = ts_value_get_object((TsValue*)ctor);
            if (!raw) raw = ctor;
            if (!raw) return ts_value_make_null();
            uint32_t fmagic = *(uint32_t*)((char*)raw + 16);
            if (fmagic != TsFunction::MAGIC) return ts_value_make_null();
            TsFunction* fctor = (TsFunction*)raw;
            if (!fctor->properties) return ts_value_make_null();
            TsValue protoKey;
            protoKey.type = ValueType::STRING_PTR;
            protoKey.ptr_val = TsString::GetInterned("prototype");
            TsValue protoVal = fctor->properties->Get(protoKey);
            if (protoVal.type == ValueType::OBJECT_PTR && protoVal.ptr_val) {
                return ts_value_make_object(protoVal.ptr_val);
            }
            return ts_value_make_null();
        };

        // Check offset 0 magic first — TsArray/TsRegExp/TsDate don't have
        // the TsObject prefix so their magic lives at offset 0.
        uint32_t magic0 = *(uint32_t*)objRaw;
        if (magic0 == 0x41525259) { // TsArray "ARRY"
            extern void* ts_get_global_Array();
            return getCtorPrototype(ts_get_global_Array());
        }
        if (magic0 == 0x52454758) { // TsRegExp "REGX"
            extern void* ts_get_global_RegExp();
            return getCtorPrototype(ts_get_global_RegExp());
        }
        if (magic0 == 0x44415445) { // TsDate "DATE"
            extern void* ts_get_global_Date();
            return getCtorPrototype(ts_get_global_Date());
        }
        if (magic0 == 0x464C4154) { // FLAT_MAGIC — class instance
            // Use ShapeDescriptor::constructorSlot (compiler-emitted
            // back-pointer to __closure_cache_<ClassName>_constructor) to
            // find the class's constructor, then return its `prototype`
            // property. The slot may hold either a TsFunction or a
            // TsClosure depending on how the constructor was lowered;
            // ts_object_get_property handles both magic types.
            // ECMA-262 §10.1.1 [[GetPrototypeOf]] for an ordinary object.
            uint32_t shapeId = flat_object_shape_id(objRaw);
            ShapeDescriptor* desc = ts_shape_lookup(shapeId);
            if (desc && desc->constructorSlot) {
                TsValue* ctorVal = *(TsValue**)desc->constructorSlot;
                if (ctorVal) {
                    uint64_t cnb = nanbox_from_tsvalue_ptr(ctorVal);
                    if (nanbox_is_ptr(cnb)) {
                        void* ctorRaw = nanbox_to_ptr(cnb);
                        TsValue* protoVal = ts_object_get_property(ctorRaw, "prototype");
                        if (protoVal && !ts_value_is_undefined(protoVal)) {
                            return protoVal;
                        }
                    }
                }
            }
            // No back-pointer (e.g. object literal shape) — return null.
            // TODO: fall back to Object.prototype for plain object literals.
            return ts_value_make_null();
        }

        // Check if obj is a TsMap
        uint32_t magic = *(uint32_t*)((char*)objRaw + 16);
        if (magic == 0x504C5449) { // TsPlainTime "PLTI" (Temporal) -> Temporal.PlainTime.prototype
            extern void* ts_temporal_get_plaintime_ctor();
            return getCtorPrototype(ts_temporal_get_plaintime_ctor());
        }
        if (magic == 0x54445552) { // TsDuration "TDUR" -> Temporal.Duration.prototype
            extern void* ts_temporal_get_duration_ctor();
            return getCtorPrototype(ts_temporal_get_duration_ctor());
        }
        if (magic == 0x504C4454) { // TsPlainDate "PLDT" -> Temporal.PlainDate.prototype
            extern void* ts_temporal_get_plaindate_ctor();
            return getCtorPrototype(ts_temporal_get_plaindate_ctor());
        }
        if (magic == 0x504C594D) { // TsPlainYearMonth "PLYM"
            extern void* ts_temporal_get_plainyearmonth_ctor();
            return getCtorPrototype(ts_temporal_get_plainyearmonth_ctor());
        }
        if (magic == 0x504C4D44) { // TsPlainMonthDay "PLMD"
            extern void* ts_temporal_get_plainmonthday_ctor();
            return getCtorPrototype(ts_temporal_get_plainmonthday_ctor());
        }
        if (magic == 0x50444D54) { // TsPlainDateTime "PDMT"
            extern void* ts_temporal_get_plaindatetime_ctor();
            return getCtorPrototype(ts_temporal_get_plaindatetime_ctor());
        }
        if (magic == 0x494E5354) { // TsInstant "INST"
            extern void* ts_temporal_get_instant_ctor();
            return getCtorPrototype(ts_temporal_get_instant_ctor());
        }
        if (magic == 0x5A44544D) { // TsZonedDateTime "ZDTM"
            extern void* ts_temporal_get_zoneddatetime_ctor();
            return getCtorPrototype(ts_temporal_get_zoneddatetime_ctor());
        }
        if (magic == 0x4D415053) { // TsMap::MAGIC
            TsMap* objMap = (TsMap*)objRaw;
            // Object.create(null): a genuinely prototype-less object.
            if (objMap->HasNullPrototype()) {
                return ts_value_make_null();
            }
            TsMap* proto = objMap->GetPrototype();
            if (proto) {
                return ts_value_make_object(proto);
            }
            // For explicit Map instances with no user-set prototype,
            // return Map.prototype.
            if (objMap->IsExplicitMap()) {
                extern void* ts_get_global_Map();
                return getCtorPrototype(ts_get_global_Map());
            }
            // Plain object literals: return Object.prototype per spec.
            // Use ts_get_global_Object() — the same Object constructor that
            // JS code sees as the bare `Object` identifier (via load_global).
            // Going through globalThis.Object would return a DIFFERENT
            // Object constructor (set up separately in ts_runtime_init),
            // so `Object.getPrototypeOf({}) === Object.prototype` would be
            // false.
            extern void* ts_get_global_Object();
            TsValue* objProtoVal = getCtorPrototype(ts_get_global_Object());
            // ECMA-262 §20.1.3: Object.prototype's own [[Prototype]] is null.
            // Object.prototype is itself a plain TsMap with no stored
            // prototype and IsExplicitMap()==false, so without this guard it
            // would fall into this same branch and return *itself* — an
            // infinite self-cycle. That cycle hangs any consumer that walks
            // the chain via `while (o) o = getPrototypeOf(o)` (e.g. lodash
            // getSymbolsIn / getAllKeysIn, exercised by `_.omit`). Plain
            // object literals never hit this (they carry FLAT magic and
            // return null earlier), but a TsMap instance whose [[Prototype]]
            // resolves up to Object.prototype does.
            if (objProtoVal) {
                void* opRaw = ts_value_get_object(objProtoVal);
                if (opRaw && opRaw == objRaw) return ts_value_make_null();
            }
            return objProtoVal;
        }
        if (magic == 0x53455453) { // TsSet "SETS"
            extern void* ts_get_global_Set();
            return getCtorPrototype(ts_get_global_Set());
        }
        if (magic == 0x574D4150) { // TsWeakMap "WMAP"
            extern void* ts_get_global_WeakMap();
            return getCtorPrototype(ts_get_global_WeakMap());
        }
        if (magic == 0x57534554) { // TsWeakSet "WSET"
            extern void* ts_get_global_WeakSet();
            return getCtorPrototype(ts_get_global_WeakSet());
        }
        if (magic == 0x42554646) { // TsBuffer "BUFF" (ArrayBuffer)
            extern void* ts_get_global_ArrayBuffer();
            return getCtorPrototype(ts_get_global_ArrayBuffer());
        }
        if (magic == 0x44564945) { // TsDataView "DVIE"
            extern void* ts_get_global_DataView();
            return getCtorPrototype(ts_get_global_DataView());
        }
        if (magic == 0x54415252) { // TsTypedArray "TARR"
            // Per-class dispatch by element type.
            extern void* ts_get_global_Int8Array();
            extern void* ts_get_global_Uint8Array();
            extern void* ts_get_global_Uint8ClampedArray();
            extern void* ts_get_global_Int16Array();
            extern void* ts_get_global_Uint16Array();
            extern void* ts_get_global_Int32Array();
            extern void* ts_get_global_Uint32Array();
            extern void* ts_get_global_Float32Array();
            extern void* ts_get_global_Float64Array();
            extern void* ts_get_global_BigInt64Array();
            extern void* ts_get_global_BigUint64Array();
            TsTypedArray* ta = (TsTypedArray*)objRaw;
            switch (ta->GetType()) {
                case TypedArrayType::Int8:    return getCtorPrototype(ts_get_global_Int8Array());
                case TypedArrayType::Uint8:   return getCtorPrototype(ts_get_global_Uint8Array());
                case TypedArrayType::Uint8Clamped: return getCtorPrototype(ts_get_global_Uint8ClampedArray());
                case TypedArrayType::Int16:   return getCtorPrototype(ts_get_global_Int16Array());
                case TypedArrayType::Uint16:  return getCtorPrototype(ts_get_global_Uint16Array());
                case TypedArrayType::Int32:   return getCtorPrototype(ts_get_global_Int32Array());
                case TypedArrayType::Uint32:  return getCtorPrototype(ts_get_global_Uint32Array());
                case TypedArrayType::Float32: return getCtorPrototype(ts_get_global_Float32Array());
                case TypedArrayType::Float64: return getCtorPrototype(ts_get_global_Float64Array());
                case TypedArrayType::BigInt64:  return getCtorPrototype(ts_get_global_BigInt64Array());
                case TypedArrayType::BigUint64: return getCtorPrototype(ts_get_global_BigUint64Array());
                default: return ts_value_make_null();
            }
        }

        // ECMA-262: Function/Closure objects' [[Prototype]] is
        // %FunctionPrototype% (i.e., Function.prototype). Read the
        // .prototype property of the global Function constructor.
        if (magic == 0x46554E43 /* FUNC */ || magic == 0x434C5352 /* CLSR */) {
            extern void* ts_get_global_Function();
            return getCtorPrototype(ts_get_global_Function());
        }

        // For non-TsMap objects, return null (no prototype chain for them yet)
        return ts_value_make_null();
    }

    // Object.create(proto) - creates new object with specified prototype
    // Creates a new empty object with its [[Prototype]] set to proto
    TsValue* ts_object_create(TsValue* proto) {
        // Create a new empty map
        TsMap* newObj = TsMap::Create();
        TsValue* thisVal = ts_value_make_object(newObj);

        // If proto is null/undefined, return object with no prototype. Mark it
        // null-prototype so `in` / `.constructor` / getPrototypeOf don't fall
        // back to Object.prototype (a plain `{}` also has prototype==nullptr but
        // logically inherits Object.prototype). lodash's Hash cache is
        // `Object.create(null)` and relies on `'constructor' in cache` === false.
        if (!proto || ts_value_is_nullish(proto)) {
            newObj->SetPrototype(nullptr);
            newObj->SetNullPrototype(true);
            return thisVal;
        }

        // Object.create(O): O must be Object or null (ECMA-262 20.1.2.2 step 1). A
        // non-null primitive proto is a TypeError. Numbers/booleans (non-pointer
        // nanboxes) already throw downstream; a STRING or SYMBOL proto is heap-
        // backed and was silently accepted as an "object". Reject those brands here.
        {
            void* protoRaw = ts_value_get_object(proto);
            uintptr_t pp = (uintptr_t)protoRaw;
            if (protoRaw && pp >= 0x1000 && pp <= 0x00007FFFFFFFFFFFULL) {
                uint32_t pm0 = *(uint32_t*)protoRaw;
                uint32_t pm16 = *(uint32_t*)((char*)protoRaw + 16);
                if (pm0 == 0x53545247 /*TsString "STRG"*/ ||
                    pm0 == 0x434F4E53 /*TsConsString "CONS"*/ ||
                    pm0 == 0x53594D42 || pm16 == 0x53594D42 /*TsSymbol "SYMB"*/) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Object prototype may only be an Object or null"));
                    return ts_value_make_undefined();
                }
            }
        }

        // Link the prototype via ts_object_setPrototypeOf, which handles BOTH
        // TsMap and FLAT-object prototypes (object literals — it converts a
        // flat proto to a map). The old code only matched magic-at-+16 == MAPS,
        // so a flat-object prototype was silently dropped: Object.create({a:1})
        // inherited nothing (src.a undefined, getPrototypeOf !== proto). This is
        // the same path `new Foo()` uses for Foo.prototype.
        ts_object_setPrototypeOf(thisVal, proto);
        return thisVal;
    }

    // Object.setPrototypeOf(obj, proto) - sets the prototype of an object
    TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto) {
        // Proxy: route through the setPrototypeOf trap (ES 10.5.2); a
        // trap-less proxy forwards to the target.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    px->setPrototypeOfTrap(proto);
                    return obj;
                }
            }
        }
        if (!obj) return ts_value_make_undefined();

        // ECMA-262 20.1.2.22: RequireObjectCoercible(O) -> TypeError on
        // null/undefined; a primitive O is returned unchanged. Without this the
        // FLAT magic read below dereferenced a NaN-boxed primitive and crashed
        // (Object.setPrototypeOf(true, null)).
        uint64_t nbObj = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nbObj) || nanbox_is_undefined(nbObj)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object.setPrototypeOf called on null or undefined"));
            return ts_value_make_undefined();  // unreachable
        }
        if (!nanbox_is_ptr(nbObj)) return obj;  // primitive: no-op, return O

        // Step 2: proto must be Object or Null; any other primitive (number,
        // boolean) throws TypeError — and must do so before the magic reads on
        // protoRaw below would dereference a NaN-boxed value. undefined keeps
        // the runtime's existing "clear prototype" behavior.
        uint64_t nbProto = nanbox_from_tsvalue_ptr(proto);
        if (proto && !nanbox_is_null(nbProto) && !nanbox_is_undefined(nbProto)
            && !nanbox_is_ptr(nbProto)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object prototype may only be an Object or null"));
            return ts_value_make_undefined();  // unreachable
        }
        // A string/symbol proto is heap-backed (passes nanbox_is_ptr above) but is
        // still a primitive -> TypeError (proto must be Object or null). Mirror
        // Object.create's primitive-proto reject.
        if (proto && nanbox_is_ptr(nbProto)) {
            void* protoRaw = nanbox_to_ptr(nbProto);
            uintptr_t ppr = (uintptr_t)protoRaw;
            if (protoRaw && ppr >= 0x1000 && ppr <= 0x00007FFFFFFFFFFFULL) {
                uint32_t pm0 = *(uint32_t*)protoRaw;
                uint32_t pm16 = *(uint32_t*)((char*)protoRaw + 16);
                if (pm0 == 0x53545247 /*STRG*/ || pm0 == 0x434F4E53 /*CONS*/ ||
                    pm0 == 0x53594D42 || pm16 == 0x53594D42 /*SYMB*/) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Object prototype may only be an Object or null"));
                    return ts_value_make_undefined();  // unreachable
                }
            }
        }

        // Unbox obj if needed
        void* objRaw = ts_value_get_object(obj);
        if (!objRaw) objRaw = obj;

        // Flat-object instances cannot have their [[Prototype]] mutated
        // because the prototype is derived from ShapeDescriptor.constructorSlot
        // (a static back-pointer to the class constructor's closure cache).
        // Silently no-op for them — matches Object.setPrototypeOf returning
        // the receiver per ECMA-262 19.1.2.22 (the value is unchanged).
        if (*(uint32_t*)objRaw == 0x464C4154) {  // FLAT_MAGIC
            return obj;
        }

        // Check if obj is a TsMap or TsClosure
        uint32_t magic = *(uint32_t*)((char*)objRaw + 16);

        // Handle TsClosure: set prototype pointer on closure->properties
        if (magic == 0x434C5352) { // TsClosure::MAGIC
            TsClosure* closure = (TsClosure*)objRaw;

            if (!proto || ts_value_is_nullish(proto)) {
                if (closure->properties) closure->properties->SetPrototype(nullptr);
                return obj;
            }

            void* protoRaw = ts_value_get_object(proto);
            if (!protoRaw) protoRaw = proto;

            // Extract source TsMap from proto (convert flat objects)
            TsMap* sourceMap = nullptr;
            if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);
            uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
            if (protoMagic == 0x4D415053) {
                sourceMap = (TsMap*)protoRaw;
            } else if (protoMagic == 0x434C5352) {
                sourceMap = ((TsClosure*)protoRaw)->properties;
            }

            if (sourceMap) {
                if (!closure->properties) {
                    closure->properties = TsMap::Create();
                    ts_gc_write_barrier(&closure->properties, closure->properties);
                }
                if (!closure->properties->WouldCreateCycle(sourceMap)) {
                    closure->properties->SetPrototype(sourceMap);
                }
            }
            return obj;
        }

        if (magic != 0x4D415053) { // TsMap::MAGIC
            // Generic TsObject subclass (native C++ objects like TsServerResponse):
            // set prototype pointer on the side-map instead of copying properties.
            if (!proto || ts_value_is_nullish(proto)) {
                TsMap* props = getNativeProps(objRaw);
                if (props) props->SetPrototype(nullptr);
                return obj;
            }

            void* protoRaw = ts_value_get_object(proto);
            if (!protoRaw) protoRaw = proto;

            // Extract source TsMap from proto (convert flat objects)
            TsMap* sourceMap = nullptr;
            if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);
            uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
            if (protoMagic == 0x4D415053) {
                sourceMap = (TsMap*)protoRaw;
            } else if (protoMagic == 0x434C5352) {
                sourceMap = ((TsClosure*)protoRaw)->properties;
            }

            if (sourceMap) {
                TsMap* props = getOrCreateNativeProps(objRaw);
                if (!props->WouldCreateCycle(sourceMap)) {
                    props->SetPrototype(sourceMap);
                }
            }
            return obj;
        }

        TsMap* objMap = (TsMap*)objRaw;

        // If proto is null/undefined, clear the prototype
        if (!proto || ts_value_is_nullish(proto)) {
            objMap->SetPrototype(nullptr);
            return obj;
        }

        // Unbox proto if needed
        void* protoRaw = ts_value_get_object(proto);
        if (!protoRaw) protoRaw = proto;

        // Convert flat objects to TsMap
        if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);

        // Check if proto is a TsMap
        uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
        if (protoMagic == 0x4D415053) { // TsMap::MAGIC
            TsMap* protoMap = (TsMap*)protoRaw;

            // Check for prototype chain cycles
            if (objMap->WouldCreateCycle(protoMap)) {
                // TypeError: Cyclic __proto__ value - just return obj unchanged
                return obj;
            }

            objMap->SetPrototype(protoMap);
        } else if (*(uint32_t*)protoRaw == 0x41525259 /*ARRY*/) {
            // A real ARRAY as [[Prototype]] (`Foo.prototype = new Array(...)`,
            // the test262 subclassed-Array pattern). Store the array pointer
            // in the prototype slot; the property-get/has walks detect the
            // ARRY magic and delegate to the array's own lookup (indices,
            // length, Array.prototype methods).
            objMap->SetPrototype((TsMap*)protoRaw);
        }

        return obj;
    }

    // Object.freeze(obj) - freezes an object, preventing modifications
    TsValue* ts_object_freeze(TsValue* obj) {
        if (!obj) return obj;

        // ES2015+: Object.freeze of a non-object returns the input unchanged.
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (!nanbox_is_ptr(nb)) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) return obj;

        // Flat objects: freeze in place via shapeId flag bits. The previous
        // demote-to-TsMap approach returned a new TsMap pointer but the
        // caller still held the original flat-object pointer, so the freeze
        // was effectively invisible.
        if (is_flat_object(rawPtr)) {
            flat_object_set_frozen(rawPtr);
            // If there's an overflow map, freeze it too so existing dynamic
            // properties also become read-only.
            uint32_t sid = flat_object_shape_id(rawPtr);
            if (ShapeDescriptor* desc = ts_shape_lookup(sid)) {
                void* overflow = *(void**)((char*)rawPtr + 16 + desc->numSlots * 8);
                if (overflow) {
                    TsMap* overflowMap = (TsMap*)overflow;
                    overflowMap->Freeze();
                    overflowMap->PreventExtensions();
                }
            }
            return obj;
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            // Per ES spec, Object.freeze:
            //   1. SetIntegrityLevel(O, "frozen") which iterates own keys
            //   2. For each, set [[Configurable]]:false; for data props
            //      also set [[Writable]]:false.
            //   3. SetExtensible(false).
            void* keysPtr = map->GetKeys();
            if (keysPtr) {
                TsArray* keys = (TsArray*)keysPtr;
                int64_t len = keys->Length();
                for (int64_t i = 0; i < len; i++) {
                    int64_t kRaw = keys->Get(i);
                    TsValue keyVal = nanbox_to_tagged((TsValue*)(uintptr_t)kRaw);
                    if (keyVal.type != ValueType::STRING_PTR) continue;
                    uint8_t a = map->GetPropertyAttrs(keyVal);
                    // Clear ATTR_CONFIGURABLE (0x04) and ATTR_WRITABLE (0x02);
                    // preserve ATTR_ENUMERABLE (0x01).
                    a &= ~(uint8_t)(0x04 | 0x02);
                    map->SetPropertyAttrs(keyVal, a);
                }
            }
            map->Freeze();
            map->PreventExtensions();
        } else {
            // Exotic object (function/Date/RegExp/array/arguments): record
            // the integrity level in the weak side-table.
            integrity_set(rawPtr, 3);
        }

        return obj;  // Return the same object (frozen)
    }

    // Object.seal(obj) - seals an object, preventing new properties
    TsValue* ts_object_seal(TsValue* obj) {
        if (!obj) return obj;

        // Per ECMA-262 (ES2015+), Object.seal of a non-object returns the
        // input unchanged. Guard against passing primitives where we would
        // dereference a NaN-box as a pointer and crash.
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (!nanbox_is_ptr(nb)) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) return obj;

        // Flat objects: seal IN PLACE via the shape flag bits (the demote-
        // to-TsMap approach sealed a detached copy).
        if (is_flat_object(rawPtr)) {
            flat_object_set_sealed(rawPtr);
            uint32_t sid = flat_object_shape_id(rawPtr);
            if (ShapeDescriptor* desc = ts_shape_lookup(sid)) {
                void* overflow = *(void**)((char*)rawPtr + 16 + desc->numSlots * 8);
                if (overflow) ((TsMap*)overflow)->PreventExtensions();
            }
            return obj;
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            // Per ES spec, Object.seal:
            //   1. SetIntegrityLevel(O, "sealed") — clear [[Configurable]]
            //      on all own properties (writable preserved).
            //   2. SetExtensible(false).
            void* keysPtr = map->GetKeys();
            if (keysPtr) {
                TsArray* keys = (TsArray*)keysPtr;
                int64_t len = keys->Length();
                for (int64_t i = 0; i < len; i++) {
                    int64_t kRaw = keys->Get(i);
                    TsValue keyVal = nanbox_to_tagged((TsValue*)(uintptr_t)kRaw);
                    if (keyVal.type != ValueType::STRING_PTR) continue;
                    uint8_t a = map->GetPropertyAttrs(keyVal);
                    a &= ~(uint8_t)0x04;  // clear ATTR_CONFIGURABLE only
                    map->SetPropertyAttrs(keyVal, a);
                }
            }
            map->Seal();
            map->PreventExtensions();
        } else {
            integrity_set(rawPtr, 2);
        }

        return obj;  // Return the same object (sealed)
    }

    // Object.preventExtensions(obj) - prevents new properties from being added
    TsValue* ts_object_preventExtensions(TsValue* obj) {
        // Proxy: route through the preventExtensions trap (ES 10.5.4); a
        // trap-less proxy forwards to the target.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    if (!px->preventExtensionsTrap()) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "'preventExtensions' on proxy: trap returned falsish"));
                    }
                    return obj;
                }
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        if (!obj) return obj;

        // ES2015+: Object.preventExtensions of a non-object returns the
        // input unchanged.
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (!nanbox_is_ptr(nb)) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) return obj;

        // Flat objects: set the non-extensible flag IN PLACE. The old
        // demote-to-TsMap approach marked a detached copy — the caller's
        // pointer (and any proxy target) stayed extensible.
        if (is_flat_object(rawPtr)) {
            flat_object_set_non_extensible(rawPtr);
            uint32_t sid = flat_object_shape_id(rawPtr);
            if (ShapeDescriptor* desc = ts_shape_lookup(sid)) {
                void* overflow = *(void**)((char*)rawPtr + 16 + desc->numSlots * 8);
                if (overflow) ((TsMap*)overflow)->PreventExtensions();
            }
            return obj;
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            map->PreventExtensions();
        } else {
            integrity_set(rawPtr, 1);
        }

        return obj;
    }

    // Object.isFrozen(obj) - returns true if object is frozen
    // ES TestIntegrityLevel structural check for a TsMap: non-extensible AND
    // every own property non-configurable (frozen additionally requires data
    // properties be non-writable; __getter_/__setter_ mangles are accessor
    // halves where writability does not apply).
    static bool map_integrity_level(TsMap* map, bool frozen) {
        if (!map->IsExtensible()) {
            void* keysPtr = map->GetKeys();
            if (keysPtr) {
                TsArray* keys = (TsArray*)keysPtr;
                int64_t len = keys->Length();
                for (int64_t i = 0; i < len; i++) {
                    TsValue keyVal = nanbox_to_tagged((TsValue*)(uintptr_t)keys->Get(i));
                    if (keyVal.type != ValueType::STRING_PTR) continue;
                    uint8_t a = map->GetPropertyAttrs(keyVal);
                    if (a & 0x04) return false;  // configurable
                    if (frozen) {
                        const char* k = ((TsString*)keyVal.ptr_val)->ToUtf8();
                        bool accessor = k && (strncmp(k, "__getter_", 9) == 0 ||
                                              strncmp(k, "__setter_", 9) == 0);
                        if (!accessor && (a & 0x02)) return false;  // writable
                    }
                }
            }
            return true;
        }
        return false;
    }

    TsValue* ts_object_isFrozen(TsValue* obj) {
        if (!obj) return ts_value_make_bool(true);  // null/undefined considered frozen
        // ES2015+: non-object args return true (don't throw).
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb) ||
            nanbox_is_int32(nb) || nanbox_is_double(nb) || nanbox_is_bool(nb)) {
            return ts_value_make_bool(true);
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Flat objects: frozen flag, or trivially frozen when non-extensible
        // with zero own properties (ES TestIntegrityLevel).
        if (is_flat_object(rawPtr)) {
            if (flat_object_is_frozen(rawPtr)) return ts_value_make_bool(true);
            if (!flat_object_is_extensible(rawPtr)) {
                uint32_t sid = flat_object_shape_id(rawPtr);
                ShapeDescriptor* desc = ts_shape_lookup(sid);
                bool empty = desc && desc->numSlots == 0;
                if (empty) {
                    void* overflow = *(void**)((char*)rawPtr + 16);
                    TsMap* om = (TsMap*)overflow;
                    void* okeys = om ? om->GetKeys() : nullptr;
                    if (!okeys || ((TsArray*)okeys)->Length() == 0)
                        return ts_value_make_bool(true);
                }
            }
            return ts_value_make_bool(false);
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            if (map->IsFrozen()) return ts_value_make_bool(true);
            return ts_value_make_bool(map_integrity_level(map, true));
        }

        return ts_value_make_bool(ts_integrity_get(rawPtr) >= 3);
    }

    // Object.isSealed(obj) - returns true if object is sealed
    TsValue* ts_object_isSealed(TsValue* obj) {
        if (!obj) return ts_value_make_bool(true);  // null/undefined considered sealed
        // ES2015+: non-object args return true (don't throw).
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb) ||
            nanbox_is_int32(nb) || nanbox_is_double(nb) || nanbox_is_bool(nb)) {
            return ts_value_make_bool(true);
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Flat objects honor the in-place sealed/frozen flag bits
        if (is_flat_object(rawPtr)) {
            return ts_value_make_bool(flat_object_is_sealed(rawPtr));
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            if (map->IsSealed() || map->IsFrozen()) return ts_value_make_bool(true);
            return ts_value_make_bool(map_integrity_level(map, false));
        }

        return ts_value_make_bool(ts_integrity_get(rawPtr) >= 2);
    }

    // Object.isExtensible(obj) - returns true if object is extensible
    TsValue* ts_object_isExtensible(TsValue* obj) {
        // Proxy: route through the isExtensible trap (ES 10.5.3); a
        // trap-less proxy forwards to the target.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0))
                    return ts_value_make_bool(px->isExtensibleTrap());
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        if (!obj) return ts_value_make_bool(false);  // null/undefined not extensible
        // Per ES2015+ spec: non-object arguments return false (don't throw).
        // NaN-boxed undefined/null/numbers/bools are not objects.
        uint64_t nb = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb) ||
            nanbox_is_int32(nb) || nanbox_is_double(nb) || nanbox_is_bool(nb)) {
            return ts_value_make_bool(false);
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Flat objects honor the in-place non-extensible/sealed/frozen flags
        if (is_flat_object(rawPtr)) {
            return ts_value_make_bool(flat_object_is_extensible(rawPtr));
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            return ts_value_make_bool(map->IsExtensible());
        }

        return ts_value_make_bool(ts_integrity_get(rawPtr) == 0);
    }

    // ---- Per-array-index property descriptor attributes ----
    // ECMA-262 10.4.2.1 Array [[DefineOwnProperty]] runs OrdinaryDefineOwnProperty
    // (ValidateAndApplyPropertyDescriptor) for an array index. An array index has
    // a real element slot (so arr[i]/iteration/length see it) AND, when defined
    // via Object.defineProperty with non-default attributes, descriptor attrs that
    // must be validated on redefinition and reported by getOwnPropertyDescriptor.
    // We keep the value in the element slot and store the attribute byte in
    // arr->properties under "__arr_attrs_<i>" (bit0 enumerable, bit1 writable,
    // bit2 configurable). Absence of the key for a present element means a plain
    // element: enumerable+writable+configurable all true. (Accessor indices use
    // __arr_getter_<i>/__arr_setter_<i>, handled separately.)
    bool array_index_attrs_get(TsArray* a, size_t idx, uint8_t* outAttrs) {
        if (!a || !a->properties) return false;
        char k[40]; snprintf(k, sizeof(k), "__arr_attrs_%zu", idx);
        TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
        if (!a->properties->Has(kk)) return false;
        TsValue v = a->properties->Get(kk);
        *outAttrs = (uint8_t)((uint64_t)v.i_val & 0x07);
        return true;
    }
    static void array_index_attrs_set(TsArray* a, size_t idx, uint8_t attrs) {
        if (!a->properties) {
            a->properties = TsMap::Create();
            ts_gc_write_barrier(&a->properties, a->properties);
        }
        char k[40]; snprintf(k, sizeof(k), "__arr_attrs_%zu", idx);
        TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
        TsValue vv; vv.type = ValueType::NUMBER_INT; vv.i_val = (int64_t)attrs;
        a->properties->Set(kk, vv);
    }
    void array_index_attrs_clear(TsArray* a, size_t idx) {
        if (!a || !a->properties) return;
        char k[40]; snprintf(k, sizeof(k), "__arr_attrs_%zu", idx);
        TsValue kk; kk.type = ValueType::STRING_PTR; kk.ptr_val = TsString::GetInterned(k);
        if (a->properties->Has(kk)) a->properties->Delete(kk);
    }
    static bool array_index_has_accessor_obj(TsArray* a, size_t idx) {
        if (!a || !a->properties) return false;
        char k[40];
        snprintf(k, sizeof(k), "__arr_getter_%zu", idx);
        TsValue gk; gk.type = ValueType::STRING_PTR; gk.ptr_val = TsString::GetInterned(k);
        if (a->properties->Has(gk)) return true;
        snprintf(k, sizeof(k), "__arr_setter_%zu", idx);
        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(k);
        return a->properties->Has(sk);
    }

    // Object.defineProperty(obj, prop, descriptor) - defines a property on an object
    // Supports: value, get, set, writable (partial), enumerable (partial), configurable (partial)
    extern "C" void ts_array_prototype_bump_version();
    extern "C" bool ts_array_is_prototype_map(void* maybeMap);
    extern "C" bool g_array_default_iterator_deleted;  // defined in TsArray.cpp

    // ES 6.2.5.5 ToPropertyDescriptor: descriptor fields are read with
    // [[Get]]/HasProperty — the PROTOTYPE CHAIN counts, field getters are
    // invoked, and ANY object kind qualifies (array, function, class
    // instance, object with inherited fields). The legacy paths below read
    // descriptor fields as OWN TsMap entries only, so normalize any
    // descriptor object into a plain own-fields TsMap first. A non-object
    // descriptor throws TypeError.
    extern "C" bool ts_object_has_property(void* objArg, void* keyArg);
    // ES 7.1.21 CanonicalNumericIndexString + classification of a property
    // key against a TypedArray. Returns:
    //   0 — key is not a canonical numeric index (symbol, "1.0", "01", "+1",
    //       plain names) → caller takes the ORDINARY property path
    //   1 — canonical numeric index, VALID for this TA (integral, not -0,
    //       0 <= i < length, not detached); *outIdx set
    //   2 — canonical numeric index but INVALID (OOB, -0, fractional,
    //       NaN/Infinity, detached)
    static int ta_classify_index(TsTypedArray* ta, TsValue* prop, size_t* outIdx) {
        uint64_t knb = nanbox_from_tsvalue_ptr(prop);
        double d;
        bool haveNum = false;
        if (nanbox_is_int32(knb)) { d = (double)nanbox_to_int32(knb); haveNum = true; }
        else if (nanbox_is_double(knb)) { d = nanbox_to_double(knb); haveNum = true; }
        else if (nanbox_is_ptr(knb)) {
            void* kraw = nanbox_to_ptr(knb);
            uint32_t m0 = (kraw && (uintptr_t)kraw >= 4096) ? *(uint32_t*)kraw : 0;
            if (m0 == 0x53545247 /*STRG*/ || m0 == TsConsString::MAGIC) {
                const char* ks = ((TsString*)kraw)->ToUtf8();
                if (!ks || !*ks) return 0;
                if (strcmp(ks, "-0") == 0) { d = -0.0; haveNum = true; }
                else {
                    char* end = nullptr;
                    double parsed = strtod(ks, &end);
                    if (end && *end == '\0') {
                        // Canonical iff ToString(parsed) round-trips exactly
                        // ("1.0" -> "1" != "1.0" → not canonical; "1.5" is).
                        TsString* back = TsString::FromDouble(parsed);
                        const char* bs = back ? back->ToUtf8() : nullptr;
                        if (bs && strcmp(bs, ks) == 0) { d = parsed; haveNum = true; }
                    } else if (strcmp(ks, "NaN") == 0 || strcmp(ks, "Infinity") == 0 ||
                               strcmp(ks, "-Infinity") == 0) {
                        d = (ks[0] == 'N') ? std::numeric_limits<double>::quiet_NaN()
                            : (ks[0] == '-') ? -std::numeric_limits<double>::infinity()
                                             : std::numeric_limits<double>::infinity();
                        haveNum = true;
                    }
                }
            }
        }
        if (!haveNum) return 0;
        // IsValidIntegerIndex: integral, not -0, in [0, length), live buffer.
        if (d != d || std::isinf(d)) return 2;
        if (d != std::trunc(d)) return 2;
        if (d == 0 && std::signbit(d)) return 2;
        if (d < 0) return 2;
        if (ta->IsDetachedBuffer() || ta->IsOutOfBounds()) return 2;
        if (d >= (double)ta->GetLength()) return 2;
        if (outIdx) *outIdx = (size_t)d;
        return 1;
    }

    // extern shim: key classification for other TUs ([[Delete]]/[[Has]] in
    // TsObject.cpp). Same return codes as ta_classify_index.
    extern "C" int ts_ta_classify_index_c(void* taRaw, TsValue* prop) {
        return ta_classify_index((TsTypedArray*)taRaw, prop, nullptr);
    }

    // ES 13.15.2 PutValue on an immutable binding: assignment to `const`.
    extern "C" TsValue* ts_throw_const_assign() {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Assignment to constant variable."));
        return ts_value_make_undefined();  // unreachable
    }

    // ES 9.2.2: a derived-class constructor completing without super().
    // Emitted by the compiler at the implicit-return point of derived ctors
    // that provably contain no super call.
    extern "C" TsValue* ts_throw_super_not_called() {
        ts_throw((TsValue*)ts_error_create_typed("ReferenceError",
            "Must call super constructor in derived class before returning "
            "from derived constructor"));
        return ts_value_make_undefined();  // unreachable
    }

    // ES 10.4.5.3 [[DefineOwnProperty]] for integer-indexed exotic objects.
    // `descriptor` must already be normalized to a TsMap. Returns:
    //   -1 — key is not a canonical numeric index (caller: ordinary define)
    //    0 — define refused (Object.defineProperty throws, Reflect returns false)
    //    1 — define succeeded
    extern "C" int ts_ta_define_own_property(void* taRaw, TsValue* prop,
                                             TsValue* descriptor) {
        TsTypedArray* ta = (TsTypedArray*)taRaw;
        size_t idx = 0;
        int cls = ta_classify_index(ta, prop, &idx);
        if (cls == 0) return -1;
        if (cls == 2) return 0;   // invalid index → false
        void* dRaw = ts_nanbox_safe_unbox(descriptor);
        TsMap* dm = (dRaw && *(uint32_t*)((char*)dRaw + 16) == 0x4D415053)
                        ? (TsMap*)dRaw : nullptr;
        if (!dm) return 1;        // no recognizable fields → nothing to refuse
        auto field = [&](const char* name, TsValue* out) -> bool {
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(name);
            if (!dm->Has(k)) return false;
            if (out) *out = dm->Get(k);
            return true;
        };
        TsValue v;
        // Steps iii-vi: configurable:false, enumerable:false, any accessor
        // field, writable:false → false. Absent fields are fine (indices are
        // configurable+enumerable+writable per ES2021).
        if (field("configurable", &v) && !ts_value_to_bool(nanbox_from_tagged(v))) return 0;
        if (field("enumerable", &v) && !ts_value_to_bool(nanbox_from_tagged(v))) return 0;
        if (field("get", nullptr) || field("set", nullptr)) return 0;
        if (field("writable", &v) && !ts_value_to_bool(nanbox_from_tagged(v))) return 0;
        // Step vii: IntegerIndexedElementSet — conversion runs user code
        // (valueOf) which may detach the buffer; a now-invalid index is a
        // silent success per spec.
        if (field("value", &v)) {
            TsValue* boxedVal = nanbox_from_tagged(v);
            double num = ts_to_number(boxedVal);   // throws on Symbol / poisoned valueOf
            if (!ta->IsDetachedBuffer() && !ta->IsOutOfBounds() &&
                idx < ta->GetLength()) {
                ta->Set(idx, num);
            }
        }
        return 1;
    }

    static TsValue* ts_descriptor_normalize(TsValue* descriptor) {
        uint64_t nb = descriptor ? nanbox_from_tsvalue_ptr(descriptor) : 0;
        void* raw = (descriptor && nanbox_is_ptr(nb)) ? nanbox_to_ptr(nb) : nullptr;
        bool isObj = false;
        if (raw && (uintptr_t)raw >= 4096) {
            uint32_t m0 = *(uint32_t*)raw;
            if (m0 != 0x53545247 /*STRG*/ && m0 != 0x434F4E53 /*CONS*/ &&
                m0 != 0x53594D42 /*SYMB*/ && m0 != 0x42494749 /*BIGI*/)
                isObj = true;
        }
        if (!isObj) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Property description must be an object"));
            return descriptor;  // unreachable
        }
        // Fast path: a plain TsMap with no prototype and no ACCESSOR fields
        // already behaves as own-fields — keep its identity (the
        // overwhelmingly common case). A `{ get value() {...} }` descriptor
        // stores __getter_value and must go through the [[Get]] path.
        uint32_t m16 = *(uint32_t*)((char*)raw + 16);
        if (m16 == 0x4D415053 /*MAPS*/ && !((TsMap*)raw)->GetPrototype()) {
            static const char* accessorKeys[6] = {
                "__getter_value", "__getter_writable", "__getter_get",
                "__getter_set", "__getter_enumerable", "__getter_configurable" };
            bool hasAccessorField = false;
            for (int i = 0; i < 6; i++) {
                TsValue ak; ak.type = ValueType::STRING_PTR;
                ak.ptr_val = TsString::GetInterned(accessorKeys[i]);
                if (((TsMap*)raw)->Has(ak)) { hasAccessorField = true; break; }
            }
            if (!hasAccessorField) return descriptor;
        }
        TsMap* norm = TsMap::Create();
        static const char* fields[6] =
            { "value", "writable", "get", "set", "enumerable", "configurable" };
        for (int i = 0; i < 6; i++) {
            TsValue key; key.type = ValueType::STRING_PTR;
            key.ptr_val = TsString::GetInterned(fields[i]);
            TsValue* boxedKey = nanbox_from_tagged(key);
            if (!ts_object_has_property(raw, (void*)boxedKey)) continue;
            TsValue* v = ts_object_get_property(raw, fields[i]);
            norm->Set(key, v ? nanbox_to_tagged(v) : TsValue());
        }
        return ts_value_make_object(norm);
    }

    TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor) {
        // #66: defineProperty on %Object.prototype% flips the dirty bit.
        if (g_object_proto_map && obj) {
            void* r0 = ts_value_get_object(obj);
            if (!r0) r0 = (void*)obj;
            if (r0 == g_object_proto_map) g_object_proto_dirty = true;
        }
        // Proxy: route through the defineProperty trap (ES 10.5.6) when
        // present (or revoked). A trap-less proxy keeps the legacy define-
        // on-the-proxy behavior (our ordinary define can't reach an Array
        // target; the concat/filter/map species tests define accessors on a
        // trap-less array-target proxy).
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    if (px->revoked || px->getTrap("defineProperty")) {
                        if (!px->definePropertyTrap(prop, descriptor)) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "'defineProperty' on proxy: trap returned falsish"));
                        }
                        return obj;
                    }
                }
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        // Spec step 1: If Type(O) is not Object, throw a TypeError exception.
        // Throws on null/undefined/number/bool/string/symbol — anything
        // that isn't an object reference. Unknown raw pointers (e.g.
        // native HTTP req objects) are kept as the legacy silent no-op
        // path so existing integrations don't break.
        if (!obj) return ts_value_make_undefined();  // C-null: ignore silently
        {
            uint64_t objNb = nanbox_from_tsvalue_ptr(obj);
            if (nanbox_is_int32(objNb) || nanbox_is_double(objNb) ||
                nanbox_is_true(objNb)  || nanbox_is_false(objNb) ||
                nanbox_is_undefined(objNb) || nanbox_is_null(objNb)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Object.defineProperty called on non-object"));
                return ts_value_make_undefined();
            }
            // String primitives also count as "not an object" per spec.
            if (nanbox_is_ptr(objNb)) {
                void* p = nanbox_to_ptr(objNb);
                if (p) {
                    uint32_t m = *(uint32_t*)p;
                    if (m == 0x53545247 /* TsString::MAGIC */) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Object.defineProperty called on non-object"));
                        return ts_value_make_undefined();
                    }
                }
            }
        }
        descriptor = ts_descriptor_normalize(descriptor);
        if (!prop) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object.defineProperty: property key required"));
            return ts_value_make_undefined();
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) {
            // Unknown raw pointer (native object, exotic) — legacy no-op.
            return obj;
        }

        // ES 10.4.5.3: integer-indexed exotic [[DefineOwnProperty]]. A refusal
        // is a TypeError here (DefinePropertyOrThrow); non-index keys fall
        // through to the ordinary paths below.
        if ((uintptr_t)rawPtr >= 4096 &&
            *(uint32_t*)((char*)rawPtr + 16) == 0x54415252 /* TARR */) {
            int r = ts_ta_define_own_property(rawPtr, prop, descriptor);
            if (r == 0) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Cannot define property on a TypedArray index"));
                return ts_value_make_undefined();
            }
            if (r == 1) return obj;
        }

        // For flat objects, use the overflow TsMap directly (creating if
        // needed) rather than migrating. Migration creates a new TsMap that
        // the caller's obj pointer doesn't reference, so the getter wasn't
        // visible on subsequent reads. The overflow map is already checked
        // by ts_flat_object_get_property (for value properties), and we
        // teach it below to also invoke __getter_<key> from overflow.
        if (is_flat_object(rawPtr)) {
            uint32_t shapeId = flat_object_shape_id(rawPtr);
            ShapeDescriptor* desc = ts_shape_lookup(shapeId);
            if (!desc) {
                rawPtr = ts_flat_object_to_map(rawPtr);
            } else {
                void** overflowPtr = flat_object_overflow_ptr(rawPtr, desc->numSlots);
                if (!*overflowPtr) {
                    TsMap* newMap = TsMap::Create();
                    *overflowPtr = newMap;
                    ts_gc_write_barrier(overflowPtr, newMap);
                }
                rawPtr = *overflowPtr;
            }
        }

        // Check if it's a TsMap (or extract properties map from function/closure)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)rawPtr;
            if (!func->properties) func->properties = TsMap::Create();
                ts_gc_write_barrier(&func->properties, func->properties);
            rawPtr = func->properties;
            magic = 0x4D415053;
        } else if (magic == 0x434C5352) { // TsClosure magic
            TsClosure* clos = (TsClosure*)rawPtr;
            if (!clos->properties) {
                clos->properties = TsMap::Create();
                ts_gc_write_barrier(&clos->properties, clos->properties);
            }
            rawPtr = clos->properties;
            magic = 0x4D415053;
        }
        if (magic != 0x4D415053) {  // TsMap::MAGIC
            // TsArray has magic at offset 0. If the target is a TsArray
            // with a canonical numeric key in [0, length), promote the
            // storage slot from hole to "present with value undefined"
            // so HasProperty(arr, key) returns true after defineProperty.
            // This matches spec: defining an accessor at an array index
            // makes the index a present own property, even if only a setter
            // is provided (reads fall through to undefined).
            //
            // Accepted simplification: descriptor's get/set/value are
            // discarded here. Reads return undefined regardless. Full
            // accessor invocation on array indices would require a per-
            // array (index → descriptor) side-map. The test262 cluster
            // that triggers this pattern uses set-only accessors and
            // only checks HasProperty + undefined-read.
            uint32_t magic0 = *(uint32_t*)rawPtr;
            if (magic0 == 0x41525259) {  // TsArray::MAGIC ("ARRY")
                TsArray* arr = (TsArray*)rawPtr;
                const char* keyStr = nullptr;
                char intBuf[32];
                uint64_t propNb = nanbox_from_tsvalue_ptr(prop);
                if (nanbox_is_int32(propNb)) {
                    snprintf(intBuf, sizeof(intBuf), "%d",
                             nanbox_to_int32(propNb));
                    keyStr = intBuf;
                } else if (nanbox_is_ptr(propNb)) {
                    TsString* ps = (TsString*)ts_value_get_string(prop);
                    if (ps) {
                        TsString* flat = ts_ensure_flat(ps);
                        if (flat) keyStr = flat->ToUtf8();
                    }
                }
                // ECMA-262 10.4.2.1 Array [[DefineOwnProperty]] for "length"
                // runs ArraySetLength: ToUint32-validate the descriptor value
                // (RangeError on a non-canonical length) and truncate/extend,
                // rather than storing "length" as a generic string property.
                // Only the data-`value` case is intercepted here; descriptors
                // without a value (accessor / attrs-only / invalid) fall through
                // to the generic path below (which validates + throws TypeError).
                if (keyStr && strcmp(keyStr, "length") == 0) {
                    void* dRaw = ts_value_get_object(descriptor);
                    if (dRaw) {
                        if (is_flat_object(dRaw)) dRaw = ts_flat_object_to_map(dRaw);
                        uint32_t dMag = 0;
                        if ((uintptr_t)dRaw > 0x1000 &&
                            (uintptr_t)dRaw < 0x0000800000000000ULL)
                            dMag = *(uint32_t*)((char*)dRaw + 16);
                        if (dMag == 0x46554E43) {  // TsFunction
                            TsFunction* f = (TsFunction*)dRaw;
                            if (f->properties) { dRaw = f->properties; dMag = 0x4D415053; }
                        } else if (dMag == 0x434C5352) {  // TsClosure
                            TsClosure* c = (TsClosure*)dRaw;
                            if (c->properties) { dRaw = c->properties; dMag = 0x4D415053; }
                        }
                        if (dMag == 0x4D415053) {  // TsMap
                            TsMap* dm = (TsMap*)dRaw;
                            TsValue vk; vk.type = ValueType::STRING_PTR;
                            vk.ptr_val = TsString::GetInterned("value");
                            // Value-LESS length descriptors run the ES 10.4.2.4
                            // invariants directly (the value-present branch runs
                            // them after ToUint32 so a throwing valueOf wins).
                            if (!dm->Has(vk)) {
                                TsValue ckA; ckA.type = ValueType::STRING_PTR;
                                ckA.ptr_val = TsString::GetInterned("configurable");
                                if (dm->Has(ckA) &&
                                    ts_value_to_bool(nanbox_from_tagged(dm->Get(ckA)))) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as configurable"));
                                    return ts_value_make_undefined();
                                }
                                TsValue ekA; ekA.type = ValueType::STRING_PTR;
                                ekA.ptr_val = TsString::GetInterned("enumerable");
                                if (dm->Has(ekA) &&
                                    ts_value_to_bool(nanbox_from_tagged(dm->Get(ekA)))) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as enumerable"));
                                    return ts_value_make_undefined();
                                }
                                TsValue gkA; gkA.type = ValueType::STRING_PTR;
                                gkA.ptr_val = TsString::GetInterned("get");
                                TsValue skA; skA.type = ValueType::STRING_PTR;
                                skA.ptr_val = TsString::GetInterned("set");
                                if (dm->Has(gkA) || dm->Has(skA)) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as an accessor"));
                                    return ts_value_make_undefined();
                                }
                            }
                            if (dm->Has(vk)) {
                                double num = ts_to_number(nanbox_from_tagged(dm->Get(vk)));
                                uint32_t u = ts_double_to_uint32(num);
                                if ((double)u != num) {
                                    ts_throw((TsValue*)ts_error_create_typed(
                                        "RangeError", "Invalid array length"));
                                    return ts_value_make_undefined();
                                }
                            // ES 10.4.2.4: ToUint32(Desc.[[Value]]) ran FIRST (step 3 —
                                // a throwing valueOf wins); now the invariants for the
                            // non-configurable, non-enumerable "length":
                            // configurable:true / enumerable:true -> TypeError;
                            // an accessor redefinition of length -> TypeError.
                            {
                                TsValue ck2; ck2.type = ValueType::STRING_PTR;
                                ck2.ptr_val = TsString::GetInterned("configurable");
                                if (dm->Has(ck2) &&
                                    ts_value_to_bool(nanbox_from_tagged(dm->Get(ck2)))) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as configurable"));
                                    return ts_value_make_undefined();
                                }
                                TsValue ek2; ek2.type = ValueType::STRING_PTR;
                                ek2.ptr_val = TsString::GetInterned("enumerable");
                                if (dm->Has(ek2) &&
                                    ts_value_to_bool(nanbox_from_tagged(dm->Get(ek2)))) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as enumerable"));
                                    return ts_value_make_undefined();
                                }
                                TsValue gk2; gk2.type = ValueType::STRING_PTR;
                                gk2.ptr_val = TsString::GetInterned("get");
                                TsValue sk2; sk2.type = ValueType::STRING_PTR;
                                sk2.ptr_val = TsString::GetInterned("set");
                                if (dm->Has(gk2) || dm->Has(sk2)) {
                                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                        "Cannot redefine array length as an accessor"));
                                    return ts_value_make_undefined();
                                }
                            }
                                // Honor a non-writable length set by a prior
                                // defineProperty(arr,"length",{writable:false})
                                // — recorded in the props map (ATTR_WRITABLE=0x02).
                                // ECMA-262 10.4.2.4 step 3.f/3.g: changing the
                                // length of a non-writable length is a TypeError.
                                if (arr->properties) {
                                    TsValue lk; lk.type = ValueType::STRING_PTR;
                                    lk.ptr_val = TsString::GetInterned("length");
                                    if (arr->properties->Has(lk) &&
                                        !(arr->properties->GetPropertyAttrs(lk) & 0x02) &&
                                        (size_t)u != arr->Length()) {
                                        ts_throw((TsValue*)ts_error_create_typed(
                                            "TypeError",
                                            "Cannot redefine property: length"));
                                        return ts_value_make_undefined();
                                    }
                                }
                                // ECMA-262 10.4.2.4 ArraySetLength: reducing
                                // length deletes elements top-down; a
                                // non-configurable element cannot be deleted —
                                // length stops just above it and a TypeError is
                                // thrown. Scan from the top: the first
                                // non-configurable index found is the highest.
                                if ((size_t)u < arr->Length()) {
                                    for (size_t i = arr->Length(); i > (size_t)u; ) {
                                        --i;
                                        uint8_t ea;
                                        if (array_index_attrs_get(arr, i, &ea) &&
                                            !(ea & 0x04 /*ATTR_CONFIGURABLE*/)) {
                                            arr->SetLength(i + 1);
                                            ts_throw((TsValue*)ts_error_create_typed(
                                                "TypeError",
                                                "Cannot delete non-configurable array "
                                                "element while shrinking length"));
                                            return ts_value_make_undefined();
                                        }
                                    }
                                }
                                arr->SetLength((size_t)u);
                                return obj;
                            }
                        }
                    }
                    // else: fall through to generic path (TypeError / no-op).
                }
                bool routedToProps = false;
                if (keyStr && keyStr[0] != '\0') {
                    int64_t idx = 0;
                    // ECMA-262 array-index test (strict): rejects "-1",
                    // "4294967295" (2^32-1), "01", "  3", "1e21", "1.5" — those
                    // are ordinary string properties, not elements. (Was a loose
                    // strtoul that mis-routed all of these.)
                    if (parse_canonical_array_index(keyStr, &idx)) {
                        // ECMA-262 10.4.2.1 step 2.c: defining an index >=
                        // length EXTENDS length; when length is non-writable
                        // (prior defineProperty(arr,"length",{writable:false}))
                        // that is a TypeError and nothing is defined.
                        if ((size_t)idx >= arr->Length() && arr->properties) {
                            TsValue lkNW; lkNW.type = ValueType::STRING_PTR;
                            lkNW.ptr_val = TsString::GetInterned("length");
                            if (arr->properties->Has(lkNW) &&
                                !(arr->properties->GetPropertyAttrs(lkNW) & 0x02)) {
                                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                    "Cannot define property: array length is not writable"));
                                return ts_value_make_undefined();
                            }
                        }
                        // Canonical array index. ECMA-262 10.4.2.1 Array
                        // [[DefineOwnProperty]]: a DATA descriptor's `value` is
                        // stored at the index and length extends to idx+1 when
                        // idx >= length. (Previously the value was discarded and
                        // length never grew, so `defineProperty(arr,"0",{value})`
                        // left arr[0] undefined / length 0.) Accessor and
                        // attrs-only descriptors keep the existing "promote hole
                        // to present-undefined" behavior — full accessor-on-index
                        // read support needs a per-index descriptor side-map.
                        void* dRaw = ts_value_get_object(descriptor);
                        if (dRaw && is_flat_object(dRaw)) dRaw = ts_flat_object_to_map(dRaw);
                        uint32_t dMag = 0;
                        if ((uintptr_t)dRaw > 0x1000 &&
                            (uintptr_t)dRaw < 0x0000800000000000ULL)
                            dMag = *(uint32_t*)((char*)dRaw + 16);
                        if (dMag == 0x46554E43) {  // TsFunction
                            TsFunction* f = (TsFunction*)dRaw;
                            if (f->properties) { dRaw = f->properties; dMag = 0x4D415053; }
                        } else if (dMag == 0x434C5352) {  // TsClosure
                            TsClosure* c = (TsClosure*)dRaw;
                            if (c->properties) { dRaw = c->properties; dMag = 0x4D415053; }
                        }
                        if (dMag == 0x4D415053) {  // TsMap descriptor
                            TsMap* dm = (TsMap*)dRaw;
                            TsValue vk; vk.type = ValueType::STRING_PTR;
                            vk.ptr_val = TsString::GetInterned("value");
                            TsValue wkD; wkD.type = ValueType::STRING_PTR;
                            wkD.ptr_val = TsString::GetInterned("writable");
                            TsValue ekD; ekD.type = ValueType::STRING_PTR;
                            ekD.ptr_val = TsString::GetInterned("enumerable");
                            TsValue ckD; ckD.type = ValueType::STRING_PTR;
                            ckD.ptr_val = TsString::GetInterned("configurable");
                            TsValue gkD; gkD.type = ValueType::STRING_PTR;
                            gkD.ptr_val = TsString::GetInterned("get");
                            TsValue skD; skD.type = ValueType::STRING_PTR;
                            skD.ptr_val = TsString::GetInterned("set");
                            auto descBool = [&](const TsValue& key) -> bool {
                                TsValue v = dm->Get(key);
                                return v.type == ValueType::BOOLEAN ? (v.i_val != 0)
                                    : (v.type != ValueType::UNDEFINED && v.ptr_val);
                            };
                            bool hasVal = dm->Has(vk);
                            bool hasW = dm->Has(wkD), hasE = dm->Has(ekD), hasC = dm->Has(ckD);
                            bool descAccessor = dm->Has(gkD) || dm->Has(skD);
                            // DATA descriptor on an array index: validate against the
                            // existing index (ValidateAndApplyPropertyDescriptor) then
                            // store the value in the element slot and record attrs.
                            if (!descAccessor && (hasVal || hasW || hasE || hasC)) {
                                constexpr uint8_t A_ENUM = 0x01, A_WRIT = 0x02, A_CONF = 0x04;
                                bool curAccessor = array_index_has_accessor_obj(arr, (size_t)idx);
                                bool curPresent = curAccessor ||
                                    ((size_t)idx < arr->Length() && !arr->IsHole((size_t)idx));
                                uint8_t curAttrs = 0x07;  // plain element default
                                if (!array_index_attrs_get(arr, (size_t)idx, &curAttrs)) {
                                    curAttrs = curAccessor ? 0x00 : 0x07;
                                }
                                // ValidateAndApplyPropertyDescriptor: a non-configurable
                                // existing index rejects incompatible redefinitions.
                                if (curPresent && !(curAttrs & A_CONF)) {
                                    if (hasC && descBool(ckD)) {
                                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                            "Cannot redefine property: non-configurable"));
                                        return ts_value_make_undefined();
                                    }
                                    if (hasE && (descBool(ekD) != ((curAttrs & A_ENUM) != 0))) {
                                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                            "Cannot redefine property: non-configurable (enumerable)"));
                                        return ts_value_make_undefined();
                                    }
                                    if (curAccessor) {
                                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                            "Cannot redefine property: non-configurable (accessor->data)"));
                                        return ts_value_make_undefined();
                                    }
                                    if (!(curAttrs & A_WRIT)) {
                                        if (hasW && descBool(wkD)) {
                                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                                "Cannot redefine property: non-configurable (writable)"));
                                            return ts_value_make_undefined();
                                        }
                                        if (hasVal) {
                                            TsValue* newVp = nanbox_from_tagged(dm->Get(vk));
                                            int64_t curRaw = arr->Get((size_t)idx);
                                            TsValue* curVp = (TsValue*)(uintptr_t)curRaw;
                                            if (!ts_object_is(curVp, newVp)) {
                                                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                                    "Cannot redefine property: non-configurable (value)"));
                                                return ts_value_make_undefined();
                                            }
                                        }
                                    }
                                }
                                // APPLY. Compute the new attribute byte: absent
                                // fields keep the existing value for an existing
                                // property, or default to false for a new one.
                                uint8_t newAttrs = curPresent ? curAttrs : 0x00;
                                if (hasW) newAttrs = descBool(wkD) ? (newAttrs | A_WRIT) : (newAttrs & ~A_WRIT);
                                if (hasE) newAttrs = descBool(ekD) ? (newAttrs | A_ENUM) : (newAttrs & ~A_ENUM);
                                if (hasC) newAttrs = descBool(ckD) ? (newAttrs | A_CONF) : (newAttrs & ~A_CONF);
                                // Converting a former accessor index to data: drop
                                // the getter/setter slots so reads see the value.
                                if (curAccessor) {
                                    char ak[40];
                                    snprintf(ak, sizeof(ak), "__arr_getter_%zu", (size_t)idx);
                                    TsValue gk2; gk2.type = ValueType::STRING_PTR;
                                    gk2.ptr_val = TsString::GetInterned(ak);
                                    if (arr->properties && arr->properties->Has(gk2)) arr->properties->Delete(gk2);
                                    snprintf(ak, sizeof(ak), "__arr_setter_%zu", (size_t)idx);
                                    TsValue sk2; sk2.type = ValueType::STRING_PTR;
                                    sk2.ptr_val = TsString::GetInterned(ak);
                                    if (arr->properties && arr->properties->Has(sk2)) arr->properties->Delete(sk2);
                                }
                                if (hasVal) {
                                    arr->Set((size_t)idx,
                                        (int64_t)(uintptr_t)nanbox_from_tagged(dm->Get(vk)));
                                } else if ((size_t)idx >= arr->Length()) {
                                    arr->Set((size_t)idx,
                                        (int64_t)(uintptr_t)ts_value_make_undefined());
                                }
                                // Record non-default attrs; a plain (all-true)
                                // element keeps no side entry.
                                if (newAttrs == 0x07) array_index_attrs_clear(arr, (size_t)idx);
                                else array_index_attrs_set(arr, (size_t)idx, newAttrs);
                                return obj;
                            }
                            if (hasVal) {
                                TsValue val = dm->Get(vk);
                                arr->Set((size_t)idx,
                                    (int64_t)(uintptr_t)nanbox_from_tagged(val));
                                return obj;
                            }
                            // ACCESSOR descriptor at an array index: store get/set
                            // in the array's properties side-map under
                            // __arr_getter_<i>/__arr_setter_<i> (TsArray Get/IsHole
                            // consult these), keep the slot a hole, extend length.
                            TsValue gk; gk.type = ValueType::STRING_PTR;
                            gk.ptr_val = TsString::GetInterned("get");
                            TsValue sk; sk.type = ValueType::STRING_PTR;
                            sk.ptr_val = TsString::GetInterned("set");
                            bool hasGet = dm->Has(gk), hasSet = dm->Has(sk);
                            if (hasGet || hasSet) {
                                // Capture hole-ness BEFORE storing the accessor
                                // (afterwards IsHole returns false for this index).
                                bool wasHole = ((size_t)idx >= arr->Length()) ||
                                               arr->IsHole((size_t)idx);
                                if (!arr->properties) {
                                    arr->properties = TsMap::Create();
                                    ts_gc_write_barrier(&arr->properties, arr->properties);
                                }
                                char akey[40];
                                if (hasGet) {
                                    snprintf(akey, sizeof(akey), "__arr_getter_%zu", (size_t)idx);
                                    TsValue ak; ak.type = ValueType::STRING_PTR;
                                    ak.ptr_val = TsString::GetInterned(akey);
                                    arr->properties->Set(ak, dm->Get(gk));
                                }
                                if (hasSet) {
                                    snprintf(akey, sizeof(akey), "__arr_setter_%zu", (size_t)idx);
                                    TsValue ak; ak.type = ValueType::STRING_PTR;
                                    ak.ptr_val = TsString::GetInterned(akey);
                                    arr->properties->Set(ak, dm->Get(sk));
                                }
                                if ((size_t)idx >= arr->Length()) {
                                    arr->SetLength((size_t)idx + 1);  // pads with holes
                                } else if (!wasHole) {
                                    arr->SetHole((size_t)idx);  // clear the old data value
                                }
                                // Record enumerable/configurable (default false
                                // when absent) so for-in/Object.keys honor
                                // enumerability and getOwnPropertyDescriptor
                                // reports the accessor's attrs.
                                // ValidateAndApplyPropertyDescriptor: on a
                                // REDEFINE, fields absent from the descriptor
                                // RETAIN their current values (starting from
                                // 0x00 reset configurable:true accessors to
                                // non-configurable — dp-matrix a-25x tests).
                                uint8_t accAttrs = 0x00;
                                if (!wasHole) {
                                    uint8_t cur = 0;
                                    if (array_index_attrs_get(arr, (size_t)idx, &cur))
                                        accAttrs = cur & 0x05;  // enumerable|configurable
                                }
                                TsValue ekA; ekA.type = ValueType::STRING_PTR;
                                ekA.ptr_val = TsString::GetInterned("enumerable");
                                TsValue ckA; ckA.type = ValueType::STRING_PTR;
                                ckA.ptr_val = TsString::GetInterned("configurable");
                                if (dm->Has(ekA)) {
                                    TsValue v = dm->Get(ekA);
                                    bool b = v.type == ValueType::BOOLEAN ? (v.i_val != 0)
                                        : (v.type != ValueType::UNDEFINED && v.ptr_val);
                                    if (b) accAttrs |= 0x01; else accAttrs &= ~(uint8_t)0x01;
                                }
                                if (dm->Has(ckA)) {
                                    TsValue v = dm->Get(ckA);
                                    bool b = v.type == ValueType::BOOLEAN ? (v.i_val != 0)
                                        : (v.type != ValueType::UNDEFINED && v.ptr_val);
                                    if (b) accAttrs |= 0x04; else accAttrs &= ~(uint8_t)0x04;
                                }
                                array_index_attrs_set(arr, (size_t)idx, accAttrs);
                                return obj;
                            }
                        }
                        if (idx < (unsigned long)arr->Length() &&
                            arr->IsHole((size_t)idx)) {
                            arr->SetUnchecked((size_t)idx,
                                (int64_t)(uintptr_t)ts_value_make_undefined());
                        }
                        return obj;
                    }
                    // For string-keyed (non-numeric) properties on arrays,
                    // route through the array's properties TsMap so the
                    // TsMap branch below enforces descriptor validation
                    // (TypeError on non-configurable redefinitions, etc).
                    if (!arr->properties) {
                        arr->properties = TsMap::Create();
                        ts_gc_write_barrier(&arr->properties, arr->properties);
                    }
                    rawPtr = arr->properties;
                    magic = 0x4D415053;
                    routedToProps = true;
                }
                if (!routedToProps) {
                    // Non-index string key on an array (e.g. Object.defineProperty(
                    // arr, "x", {get/set/value})). Was returned without storing, so
                    // accessors never applied (arr.x === undefined, desc.get wrong).
                    // Route into arr->properties and fall through to the generic
                    // TsMap [[DefineOwnProperty]] path (which stores __getter_/
                    // __setter_, validates non-configurable, and records attrs).
                    if (!arr->properties) {
                        arr->properties = TsMap::Create();
                        ts_gc_write_barrier(&arr->properties, arr->properties);
                    }
                    rawPtr = arr->properties;
                    magic = 0x4D415053;
                }
                // else: fall through to TsMap branch below with rawPtr reassigned.
            } else {
                // Native / exotic object (RegExp, Date, native C++ objects, …):
                // route the write into the per-object side-map
                // (g_native_object_props), which ts_object_get/has_property
                // already consult for dynamically-assigned props. This lets the
                // TsMap branch below apply real [[DefineOwnProperty]] validation
                // (TypeError on a non-configurable redefinition, etc.) and stores
                // the value so it reads back. TsString and primitives were
                // already handled (no-op / throw) above, so they don't reach here.
                rawPtr = getOrCreateNativeProps(rawPtr);
                magic = 0x4D415053;
                // fall through to the TsMap branch with rawPtr reassigned.
            }
        }

        TsMap* map = (TsMap*)rawPtr;

        // If the target is Array.prototype, bump the version counter so
        // Array iteration methods switch to the spec-compliant slow path.
        if (ts_array_is_prototype_map(map)) {
            ts_array_prototype_bump_version();
        }

        // Spec step 2: Property descriptor must itself be an object.
        // ToPropertyDescriptor: If Type(Obj) is not Object, throw TypeError.
        // Approach: try to extract a raw pointer. If extraction yields
        // something with TsMap magic, it's an object descriptor — proceed.
        // If extraction fails AND we can prove the value is a primitive
        // (NaN-boxed null/undefined/int/double/bool/string), throw TypeError.
        // Otherwise (raw-pointer internal caller, or unknown shape) fall
        // through to the existing magic check which silently no-ops.
        if (!descriptor) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Property description must be an object"));
            return ts_value_make_undefined();
        }
        void* descRaw = ts_value_get_object(descriptor);
        if (!descRaw) {
            // Could be: (a) primitive, (b) raw pointer from internal caller.
            // Distinguish via NaN-box tag.
            uint64_t descNb = nanbox_from_tsvalue_ptr(descriptor);
            if (nanbox_is_undefined(descNb) || nanbox_is_null(descNb) ||
                nanbox_is_int32(descNb)     || nanbox_is_double(descNb) ||
                nanbox_is_true(descNb)      || nanbox_is_false(descNb)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Property description must be an object"));
                return ts_value_make_undefined();
            }
            // Raw pointer fallback
            descRaw = descriptor;
        }

        // Convert flat descriptor to TsMap
        if (is_flat_object(descRaw)) {
            descRaw = ts_flat_object_to_map(descRaw);
        }

        // Per ECMA-262 ToPropertyDescriptor: any object can serve as a
        // property descriptor; the algorithm uses HasProperty/Get on it.
        // Functions and closures are objects too (e.g. `funObj.value =
        // "X"; Object.defineProperty(o, "p", funObj)` is legal). For
        // those, route through the function/closure's own `properties`
        // map which holds user-set fields like .value, .writable.
        uint32_t descMagic = *(uint32_t*)((char*)descRaw + 16);
        if (descMagic == 0x46554E43) { // TsFunction
            TsFunction* fnDesc = (TsFunction*)descRaw;
            if (!fnDesc->properties) {
                // Function with no user-set properties — empty
                // descriptor. Per spec, an empty descriptor is a
                // generic descriptor (no fields), so defineProperty
                // becomes a no-op.
                return obj;
            }
            descRaw = fnDesc->properties;
            descMagic = 0x4D415053;
        } else if (descMagic == 0x434C5352) { // TsClosure
            TsClosure* closDesc = (TsClosure*)descRaw;
            if (!closDesc->properties) {
                return obj;
            }
            descRaw = closDesc->properties;
            descMagic = 0x4D415053;
        }
        if (descMagic != 0x4D415053) {
            // Object that isn't a TsMap (TsArray, TsString, etc.) — preserve
            // the legacy silent no-op rather than throw.
            return obj;
        }

        // Spec ToPropertyDescriptor step: data and accessor descriptor fields
        // are mutually exclusive — having both [value|writable] and [get|set]
        // is a TypeError.
        TsMap* descCheck = (TsMap*)descRaw;
        // ECMA-262 ToPropertyDescriptor reads each descriptor field with
        // HasProperty/Get, which WALK THE PROTOTYPE CHAIN. descCheck (the
        // flattened descriptor map) only exposes OWN slots, so a field inherited
        // from the descriptor object's prototype — e.g.
        // Object.defineProperties(o, { p: new Con() }) with Con.prototype.value —
        // was missed (the property became undefined). Materialize any inherited
        // descriptor field into descCheck once, using the prototype-walking
        // getter on the ORIGINAL descriptor object (Get also invokes an inherited
        // or own accessor field, per spec).
        {
            extern bool ts_object_has_prop(TsValue* obj, TsValue* key);
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            void* origDesc = ts_value_get_object(descriptor);
            if (origDesc) {
                static const char* kDescFields[] = {
                    "value", "get", "set", "writable", "enumerable", "configurable"};
                for (const char* fld : kDescFields) {
                    TsValue fk; fk.type = ValueType::STRING_PTR;
                    fk.ptr_val = TsString::GetInterned(fld);
                    TsValue* fkBoxed = ts_value_make_string(TsString::GetInterned(fld));
                    // HasProperty (walks prototype, recognizes accessors). When
                    // present, (re)read via Get — which invokes an own/inherited
                    // accessor field and walks the chain — overriding any stale
                    // raw own-data slot (an accessor leaves a placeholder "value"
                    // slot beside __getter_value, so Has on the map alone lies).
                    if (ts_object_has_prop(descriptor, fkBoxed)) {
                        TsValue* fv = ts_object_get_property(origDesc, fld);
                        descCheck->Set(fk, fv ? nanbox_to_tagged(fv)
                                              : nanbox_to_tagged((TsValue*)ts_value_make_undefined()));
                    }
                }
            }
        }
        TsValue valueKeyChk;  valueKeyChk.type = ValueType::STRING_PTR;
        valueKeyChk.ptr_val = TsString::GetInterned("value");
        TsValue writableKeyChk; writableKeyChk.type = ValueType::STRING_PTR;
        writableKeyChk.ptr_val = TsString::GetInterned("writable");
        TsValue getKeyChk; getKeyChk.type = ValueType::STRING_PTR;
        getKeyChk.ptr_val = TsString::GetInterned("get");
        TsValue setKeyChk; setKeyChk.type = ValueType::STRING_PTR;
        setKeyChk.ptr_val = TsString::GetInterned("set");
        // Per spec ToPropertyDescriptor: a field is "present" only if it
        // is *defined* (HasProperty) — but for the data/accessor exclusivity
        // check, the spec consistently treats `get: undefined`/`set: undefined`
        // as STILL marking the descriptor as accessor-shaped. However, for
        // OUR use, descriptors produced by Object.getOwnPropertyDescriptors
        // include all four keys with undefined for the absent ones, and the
        // spec is careful that those don't trigger ToPropertyDescriptor's
        // exclusivity check (because Object.fromOwnPropertyDescriptors
        // round-trips). The practical fix: treat get/set as "present" only
        // if they are not undefined OR are explicitly assigned via the
        // accessor-form descriptor literal. Since we can't tell those apart
        // post-hoc in our flat representation, we conservatively only flag
        // the conflict when get/set are not undefined.
        auto isPresentAndDefined = [&](const TsValue& key) -> bool {
            if (!descCheck->Has(key)) return false;
            TsValue v = descCheck->Get(key);
            return v.type != ValueType::UNDEFINED;
        };
        bool hasValue    = isPresentAndDefined(valueKeyChk) || descCheck->Has(valueKeyChk);
        bool hasWritable = descCheck->Has(writableKeyChk);
        bool hasGetDef   = isPresentAndDefined(getKeyChk);
        bool hasSetDef   = isPresentAndDefined(setKeyChk);
        if ((hasValue || hasWritable) && (hasGetDef || hasSetDef)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Invalid property descriptor. Cannot both specify accessors and a value or writable attribute"));
            return ts_value_make_undefined();
        }
        // Spec ToPropertyDescriptor: get and set, when present and not
        // undefined, must be callable. Note that nanbox_to_tagged maps
        // JS null to ValueType::OBJECT_PTR with ptr_val=nullptr (see
        // TsObject.h:62), so an explicit null check is required — the
        // earlier `type != OBJECT_PTR` test alone accepts null silently.
        // Must be actually callable (a function/closure/callable proxy), not merely
        // an object: `{get: {a:1}}` is a non-callable object and must throw a
        // TypeError (ToPropertyDescriptor step 7.b / 9.b). The old "any non-null
        // OBJECT_PTR" check accepted it. ts_is_callable wants the NaN-boxed value,
        // so round-trip through nanbox_from_tagged. (ts_is_callable: TsRuntime.h)
        auto isCallableValue = [](const TsValue& v) -> bool {
            if (v.type != ValueType::OBJECT_PTR && v.type != ValueType::FUNCTION_PTR)
                return false;
            if (!v.ptr_val) return false;
            return ts_is_callable(nanbox_from_tagged(v));
        };
        if (hasGetDef) {
            TsValue gv = descCheck->Get(getKeyChk);
            if (!isCallableValue(gv)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Getter must be a function"));
                return ts_value_make_undefined();
            }
        }
        if (hasSetDef) {
            TsValue sv = descCheck->Get(setKeyChk);
            if (!isCallableValue(sv)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Setter must be a function"));
                return ts_value_make_undefined();
            }
        }

        // The non-extensible / frozen / sealed checks happen below, AFTER
        // we've materialized propKey from the property name argument. See
        // the "Spec [[DefineOwnProperty]] non-extensible check" block.

        TsMap* descMap = (TsMap*)descRaw;

        // Get property key as string. ECMA-262 ToPropertyKey accepts
        // strings and symbols. For Symbol keys we encode them as
        // `[<description>]` to match the rest of TsMap's symbol-key
        // convention (see ts_object_set_prop_v site for the canonical
        // form). ts_value_get_string on a Symbol throws; pre-check the
        // nanbox + magic to avoid the throw and route through the symbol
        // encoding instead.
        TsString* propStr = nullptr;
        {
            uint64_t propNb = nanbox_from_tsvalue_ptr(prop);
            if (nanbox_is_ptr(propNb)) {
                void* ptr = nanbox_to_ptr(propNb);
                if (ptr) {
                    uint32_t pmagic = *(uint32_t*)ptr;
                    if (pmagic == 0x53594D42) {  // TsSymbol::MAGIC "SYMB"
                        propStr = ts_symbol_storage_key((TsSymbol*)ptr);
                    }
                }
            }
        }
        if (!propStr) {
            propStr = (TsString*)ts_value_get_string(prop);
        }
        if (!propStr) {
            // Try number-to-string coercion
            uint64_t propNb = nanbox_from_tsvalue_ptr(prop);
            if (nanbox_is_int32(propNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", nanbox_to_int32(propNb));
                propStr = TsString::Create(buf);
            } else if (nanbox_is_double(propNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.15g", nanbox_to_double(propNb));
                propStr = TsString::Create(buf);
            }
        }
        if (!propStr) return obj;

        const char* propName = propStr->ToUtf8();
        if (!propName) return obj;

        TsValue propKey;
        propKey.type = ValueType::STRING_PTR;
        propKey.ptr_val = propStr;

        // Spec [[DefineOwnProperty]] non-extensible check: if the property
        // does not currently exist on the object and the object is not
        // extensible (preventExtensions / seal / freeze), throw a TypeError.
        // Frozen objects also cannot have existing properties redefined in
        // incompatible ways, but we surface that as a structural reject below
        // (the simple "frozen → silent ignore" was wrong; we now throw).
        bool propExistsForExtCheck = map->Has(propKey);
        if (!propExistsForExtCheck && !map->IsExtensible()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot define property on non-extensible object"));
            return ts_value_make_undefined();
        }
        if (map->IsFrozen()) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot redefine property on frozen object"));
            return ts_value_make_undefined();
        }

        // ECMA-262 10.1.6.3 ValidateAndApplyPropertyDescriptor: a
        // non-configurable existing property rejects (TypeError) a change of
        // KIND (data<->accessor) and any change to an accessor's [[Get]]/
        // [[Set]]. This MUST run before the get/set storage below, which would
        // otherwise mutate the accessor as a side effect even when rejected.
        {
            TsValue exPropKey; exPropKey.type = ValueType::STRING_PTR; exPropKey.ptr_val = propStr;
            if (map->Has(exPropKey) && !(map->GetPropertyAttrs(exPropKey) & 0x04 /*ATTR_CONFIGURABLE*/)) {
                TsValue exGk; exGk.type = ValueType::STRING_PTR;
                exGk.ptr_val = TsString::GetInterned((std::string("__getter_") + propName).c_str());
                TsValue exSk; exSk.type = ValueType::STRING_PTR;
                exSk.ptr_val = TsString::GetInterned((std::string("__setter_") + propName).c_str());
                bool existingIsAccessor = map->Has(exGk) || map->Has(exSk);
                TsValue gKx; gKx.type = ValueType::STRING_PTR; gKx.ptr_val = TsString::GetInterned("get");
                TsValue sKx; sKx.type = ValueType::STRING_PTR; sKx.ptr_val = TsString::GetInterned("set");
                TsValue vKx; vKx.type = ValueType::STRING_PTR; vKx.ptr_val = TsString::GetInterned("value");
                TsValue wKx; wKx.type = ValueType::STRING_PTR; wKx.ptr_val = TsString::GetInterned("writable");
                bool newHasGet = descMap->Has(gKx), newHasSet = descMap->Has(sKx);
                bool newIsAccessor = newHasGet || newHasSet;
                bool newIsData = descMap->Has(vKx) || descMap->Has(wKx);
                bool reject = false;
                if (existingIsAccessor) {
                    if (newIsData) reject = true;                 // accessor -> data
                    else {
                        if (newHasGet) {                          // accessor -> accessor: [[Get]] must match
                            TsValue ng = descMap->Get(gKx);
                            TsValue eg = map->Has(exGk) ? map->Get(exGk) : TsValue();
                            if (!(ng.type == eg.type && ng.i_val == eg.i_val)) reject = true;
                        }
                        if (newHasSet) {                          // [[Set]] must match
                            TsValue ns = descMap->Get(sKx);
                            TsValue es = map->Has(exSk) ? map->Get(exSk) : TsValue();
                            if (!(ns.type == es.type && ns.i_val == es.i_val)) reject = true;
                        }
                    }
                } else if (newIsAccessor) {
                    reject = true;                                // data -> accessor
                }
                if (reject) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Cannot redefine property: non-configurable (kind change)"));
                    return ts_value_make_undefined();
                }
            }
        }

        // Check for getter
        TsValue getKey;
        getKey.type = ValueType::STRING_PTR;
        getKey.ptr_val = TsString::GetInterned("get");

        if (descMap->Has(getKey)) {
            TsValue getter = descMap->Get(getKey);
            if (getter.type != ValueType::UNDEFINED) {
                // Store getter as __getter_<propName>
                std::string getterKey = std::string("__getter_") + propName;
                TsValue gk;
                gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterKey.c_str());
                map->Set(gk, getter);
            }
        }

        // Check for setter
        TsValue setKey;
        setKey.type = ValueType::STRING_PTR;
        setKey.ptr_val = TsString::GetInterned("set");

        if (descMap->Has(setKey)) {
            TsValue setter = descMap->Get(setKey);
            if (setter.type != ValueType::UNDEFINED) {
                // Store setter as __setter_<propName>
                std::string setterKey = std::string("__setter_") + propName;
                TsValue sk;
                sk.type = ValueType::STRING_PTR;
                sk.ptr_val = TsString::GetInterned(setterKey.c_str());
                map->Set(sk, setter);
            }
        }

        // Extract property attribute flags from descriptor.
        // Per JS spec: missing flags default to false for new properties via defineProperty,
        // but preserve existing value for properties that already exist.
        // Attribute flag constants (match TsHashTable::ATTR_*)
        constexpr uint8_t ATTR_ENUMERABLE   = 0x01;
        constexpr uint8_t ATTR_WRITABLE     = 0x02;
        constexpr uint8_t ATTR_CONFIGURABLE = 0x04;

        uint8_t existingAttrs = map->GetPropertyAttrs(propKey);
        bool propertyExists = map->Has(propKey);
        uint8_t attrs = 0;

        // ECMA-262 ToBoolean for descriptor attribute values: -0/0/NaN/""/
        // null/undefined are FALSE. The previous `v.ptr_val`-bit test treated
        // -0 (raw bits 0x8000000000000000) and "" as true, so e.g.
        // `Object.defineProperty(o,p,{configurable:-0})` wrongly produced a
        // *configurable* property (ToBoolean(-0) must be false).
        auto descToBool = [](const TsValue& v) -> bool {
            switch (v.type) {
                case ValueType::BOOLEAN:    return v.i_val != 0;
                case ValueType::NUMBER_INT: return v.i_val != 0;
                case ValueType::NUMBER_DBL: return v.d_val != 0.0 && v.d_val == v.d_val;
                case ValueType::STRING_PTR: return v.ptr_val && ((TsString*)v.ptr_val)->Length() > 0;
                case ValueType::UNDEFINED:  return false;
                default:                    return v.ptr_val != nullptr;
            }
        };

        // enumerable
        TsValue enumKey;
        enumKey.type = ValueType::STRING_PTR;
        enumKey.ptr_val = TsString::GetInterned("enumerable");
        if (descMap->Has(enumKey)) {
            if (descToBool(descMap->Get(enumKey)))
                attrs |= ATTR_ENUMERABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_ENUMERABLE);
        }

        // writable
        TsValue writableKey;
        writableKey.type = ValueType::STRING_PTR;
        writableKey.ptr_val = TsString::GetInterned("writable");
        if (descMap->Has(writableKey)) {
            if (descToBool(descMap->Get(writableKey)))
                attrs |= ATTR_WRITABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_WRITABLE);
        }

        // configurable
        TsValue configKey;
        configKey.type = ValueType::STRING_PTR;
        configKey.ptr_val = TsString::GetInterned("configurable");
        if (descMap->Has(configKey)) {
            if (descToBool(descMap->Get(configKey)))
                attrs |= ATTR_CONFIGURABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_CONFIGURABLE);
        }

        // Spec [[DefineOwnProperty]] validation: if the existing property is
        // non-configurable, most descriptor changes must be rejected with
        // TypeError. Applies when property exists and was non-configurable.
        if (propertyExists && !(existingAttrs & ATTR_CONFIGURABLE)) {
            // 1. Cannot go non-configurable → configurable.
            if (descMap->Has(configKey)) {
                bool newConfig = descToBool(descMap->Get(configKey));
                if (newConfig) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Cannot redefine property: non-configurable"));
                    return ts_value_make_undefined();
                }
            }
            // 2. Cannot change enumerable.
            if (descMap->Has(enumKey)) {
                bool newEnum = descToBool(descMap->Get(enumKey));
                bool oldEnum = (existingAttrs & ATTR_ENUMERABLE) != 0;
                if (newEnum != oldEnum) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Cannot redefine property: non-configurable (enumerable)"));
                    return ts_value_make_undefined();
                }
            }
            // 3. If data descriptor with writable:false, cannot go writable:true
            //    and cannot change value.
            if (!(existingAttrs & ATTR_WRITABLE)) {
                if (descMap->Has(writableKey)) {
                    bool newWritable = descToBool(descMap->Get(writableKey));
                    if (newWritable) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Cannot redefine property: non-configurable (writable)"));
                        return ts_value_make_undefined();
                    }
                }
                if (descMap->Has(valueKeyChk)) {
                    TsValue newV = descMap->Get(valueKeyChk);
                    TsValue oldV = map->Get(propKey);
                    // Simple inequality check — SameValue is over-engineered
                    // for this. If either pointer/int/double differs, reject.
                    bool sameType = (newV.type == oldV.type);
                    bool sameBits = (newV.i_val == oldV.i_val);
                    if (!(sameType && sameBits)) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Cannot redefine property: non-configurable (value)"));
                        return ts_value_make_undefined();
                    }
                }
            }
        }

        // Store getters/setters with non-enumerable attrs (they're synthetic)
        // (getter/setter code above already stored them — mark them non-enumerable)
        if (descMap->Has(getKey)) {
            std::string gk2 = std::string("__getter_") + propName;
            TsValue gk2v;
            gk2v.type = ValueType::STRING_PTR;
            gk2v.ptr_val = TsString::GetInterned(gk2.c_str());
            map->SetPropertyAttrs(gk2v, 0); // non-enumerable
        }
        if (descMap->Has(setKey)) {
            std::string sk2 = std::string("__setter_") + propName;
            TsValue sk2v;
            sk2v.type = ValueType::STRING_PTR;
            sk2v.ptr_val = TsString::GetInterned(sk2.c_str());
            map->SetPropertyAttrs(sk2v, 0); // non-enumerable
        }

        // Check for value (data descriptor) — store with extracted attributes
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::GetInterned("value");

        if (descMap->Has(valueKey)) {
            TsValue value = descMap->Get(valueKey);
            map->SetWithAttrs(propKey, value, attrs);
        } else if (!propertyExists) {
            // Property doesn't exist and no value was provided. Per spec,
            // still create the property — as a data property with
            // value=undefined (if no getter/setter) or as an accessor
            // property (handled above by the __getter_/__setter_ storage).
            // In either case, materialize the "outward-facing" property key
            // so hasOwnProperty / getOwnPropertyDescriptor / `in` see it.
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.i_val = 0;
            map->SetWithAttrs(propKey, undef, attrs);
        } else {
            // Property exists, descriptor has no value — update attributes.
            map->SetPropertyAttrs(propKey, attrs);
        }

        return obj;
    }

    // Object.defineProperties(obj, descriptors) - defines multiple properties
    TsValue* ts_object_defineProperties(TsValue* obj, TsValue* descriptors) {
        if (!obj || !descriptors) return obj;

        // ECMA-262 20.1.2.3 step 1: if Type(O) is not Object, throw TypeError.
        // Without this, is_flat_object() below dereferenced a NaN-boxed primitive
        // (Object.defineProperties(true, {})) and crashed.
        uint64_t objNb = nanbox_from_tsvalue_ptr(obj);
        if (!nanbox_is_ptr(objNb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object.defineProperties called on non-object"));
            return obj;  // unreachable
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // A String/Symbol primitive is pointer-shaped but NOT an Object —
        // ECMA-262 20.1.2.3 step 1 requires Type(O) is Object, else TypeError
        // (Object.defineProperties("abc", {}) must throw). The nanbox_is_ptr
        // gate above only catches number/bool/null/undefined.
        if (rawPtr && (uintptr_t)rawPtr > 0x1000) {
            uint32_t m0p = *(uint32_t*)rawPtr;
            if (m0p == 0x53545247 /*TsString STRG*/ || m0p == 0x53594D42 /*TsSymbol SYMB*/) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Object.defineProperties called on non-object"));
                return obj;  // unreachable
            }
        }

        // Convert flat objects to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Accept any pointer-shaped target — ts_object_defineProperty (called
        // below per-descriptor) handles TsMap / TsFunction / TsClosure paths
        // and throws TypeError for primitives. Previously this silently
        // no-op'd on non-TsMap targets, which regressed spec tests like
        // Object.defineProperties(fun, {...}) where fun is a TsFunction.

        // Per ECMA-262 19.1.2.3.1 ObjectDefineProperties step 2:
        // ToObject(Properties) — throws TypeError on null/undefined.
        uint64_t descNb = nanbox_from_tsvalue_ptr(descriptors);
        if (nanbox_is_null(descNb) || nanbox_is_undefined(descNb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return obj;  // unreachable
        }
        if (!nanbox_is_ptr(descNb)) {
            // Primitive (number/bool) coerces to a wrapper with no own
            // property keys — defineProperties is then a no-op per spec.
            return obj;
        }

        // Get the descriptors object
        void* descRaw = ts_value_get_object(descriptors);
        if (!descRaw) descRaw = descriptors;

        // Convert flat descriptor object to TsMap
        if (is_flat_object(descRaw)) {
            descRaw = ts_flat_object_to_map(descRaw);
        }

        uint32_t descMagic = *(uint32_t*)((char*)descRaw + 16);
        if (descMagic != 0x4D415053) {
            return obj;
        }

        TsMap* descMap = (TsMap*)descRaw;

        // Iterate the Properties bag per ES 20.1.2.3.1: for each ENUMERABLE
        // own key, the descriptor is obtained with [[Get]] — an ACCESSOR
        // entry's getter is invoked (stored under __getter_<name>), and its
        // logical name (not the storage key) is defined on the target.
        // NO std::string locals here: ts_object_defineProperty below can
        // ts_throw, and the longjmp corrupts when unwinding a frame that
        // holds a heap-backed std::string.
        TsArray* keys = (TsArray*)descMap->GetKeys();
        int64_t len = keys->Length();
        TsMap* seen = TsMap::Create();  // logical keys already processed

        for (int64_t i = 0; i < len; i++) {
            TsValue* key = (TsValue*)keys->Get(i);
            TsValue keyTag = nanbox_to_tagged(key);
            const char* kc = (keyTag.type == ValueType::STRING_PTR && keyTag.ptr_val)
                ? ((TsString*)keyTag.ptr_val)->ToUtf8() : nullptr;
            if (kc && kc[0] == '') continue;  // private storage keys

            // Map accessor STORAGE keys to the logical property name; the
            // enumerable attribute lives on the storage key that exists.
            TsValue logicalKey = keyTag;
            bool isAccessor = false;
            if (kc && (strncmp(kc, "__getter_", 9) == 0 ||
                       strncmp(kc, "__setter_", 9) == 0)) {
                isAccessor = true;
                logicalKey.type = ValueType::STRING_PTR;
                logicalKey.ptr_val = TsString::GetInterned(kc + 9);
            }

            // Internal storage keys (boxed-primitive slots, array-index
            // side-maps, brands) are not user properties — a String-wrapper
            // Properties bag leaked its primitive slot into the iteration
            // and the slot's string value hit the descriptor TypeError.
            if (!isAccessor && kc && kc[0] == '_' && kc[1] == '_') continue;

            // Dedupe (a get+set pair yields two storage keys; a plain key
            // could coexist with stale accessor storage).
            if (seen->Has(logicalKey)) continue;
            TsValue seenVal; seenVal.type = ValueType::BOOLEAN; seenVal.i_val = 1;
            seen->Set(logicalKey, seenVal);

            {
                // Attributes live on the LOGICAL key (the accessor define
                // stores a plain marker entry alongside __getter_<name>).
                uint8_t attrs = descMap->GetPropertyAttrs(logicalKey);
                if (!(attrs & TsHashTable::ATTR_ENUMERABLE)) continue;
            }

            // [[Get]] on the LOGICAL name — invokes accessor getters and
            // resolves plain entries alike.
            TsValue* descNb;
            if (isAccessor) {
                const char* ln = ((TsString*)logicalKey.ptr_val)->ToUtf8();
                descNb = ts_object_get_property(descRaw, ln);
            } else {
                TsValue desc = descMap->Get(keyTag);
                if (desc.type == ValueType::UNDEFINED) {
                    // A PRESENT key whose value is undefined must throw
                    // (ToPropertyDescriptor step 1); a Get miss here is a
                    // key-encoding artifact and is skipped as before.
                    if (descMap->Has(keyTag)) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Property description must be an object"));
                        return obj;  // unreachable
                    }
                    continue;
                }
                descNb = nanbox_from_tagged(desc);
            }
            if (!descNb) continue;
            // ToPropertyDescriptor step 1: the descriptor must be an Object.
            // null / boolean / number / string / symbol all throw.
            {
                uint64_t dnb = nanbox_from_tsvalue_ptr(descNb);
                void* dptr = nanbox_is_ptr(dnb) ? nanbox_to_ptr(dnb) : nullptr;
                bool isObj = dptr && (uintptr_t)dptr > 0x1000;
                if (isObj) {
                    uint32_t m0d = *(uint32_t*)dptr;
                    if (m0d == 0x53545247 /*STRG*/ || m0d == 0x53594D42 /*SYMB*/)
                        isObj = false;
                }
                if (!isObj) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "Property description must be an object"));
                    return obj;  // unreachable
                }
            }
            TsValue* logicalKeyNb = nanbox_from_tagged(logicalKey);
            ts_object_defineProperty(obj, logicalKeyNb, descNb);
        }

        return obj;
    }

    // Object.getOwnPropertyDescriptor(obj, prop) - gets the descriptor for a property
    // Returns { value: ..., writable: true, enumerable: true, configurable: true }
    // Helper: build a property descriptor {value, writable, enumerable, configurable}
    static TsValue* buildPropertyDescriptor(TsValue* value, bool writable, bool enumerable, bool configurable) {
        TsMap* desc = TsMap::Create();
        TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("value");
        desc->Set(vk, nanbox_to_tagged(value));
        TsValue wk; wk.type = ValueType::STRING_PTR; wk.ptr_val = TsString::GetInterned("writable");
        TsValue wv; wv.type = ValueType::BOOLEAN; wv.i_val = writable ? 1 : 0;
        desc->Set(wk, wv);
        TsValue ek; ek.type = ValueType::STRING_PTR; ek.ptr_val = TsString::GetInterned("enumerable");
        TsValue ev; ev.type = ValueType::BOOLEAN; ev.i_val = enumerable ? 1 : 0;
        desc->Set(ek, ev);
        TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("configurable");
        TsValue cv; cv.type = ValueType::BOOLEAN; cv.i_val = configurable ? 1 : 0;
        desc->Set(ck, cv);
        return ts_value_make_object(desc);
    }

    // ---- %ThrowTypeError% (ES 10.2.4) ------------------------------------
    // One frozen, non-extensible, anonymous, zero-length function per realm;
    // it is the get AND set of the "callee" accessor on unmapped (strict)
    // arguments objects and of Function.prototype's "caller"/"arguments".
    static TsValue* tte_call(void* ctx, int argc, TsValue** argv) {
        (void)ctx; (void)argc; (void)argv;
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "'caller', 'callee', and 'arguments' properties may not be accessed on "
            "strict mode functions or the arguments objects for calls to them"));
        return ts_value_make_undefined();
    }
    extern "C" TsValue* ts_get_throw_type_error() {
        static TsValue* tte = nullptr;
        if (!tte) {
            tte = ts_value_make_native_function((void*)tte_call, nullptr);
            TsFunction* fn = (TsFunction*)tte;
            fn->name = TsString::Create("");
            fn->arity = 0;
            fn->is_constructor = false;
            if (!fn->properties) {
                fn->properties = TsMap::Create();
                ts_gc_write_barrier(&fn->properties, fn->properties);
            }
            TsValue k, v;
            k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned("length");
            v.type = ValueType::NUMBER_INT; v.i_val = 0;
            fn->properties->SetWithAttrs(k, v, 0);  // non-writable/enum/config
            k.ptr_val = TsString::GetInterned("name");
            v.type = ValueType::STRING_PTR; v.ptr_val = TsString::Create("");
            fn->properties->SetWithAttrs(k, v, 0);
            // frozen + non-extensible (Object.isFrozen/isExtensible read this)
            void* raw = ts_value_get_object(tte);
            if (!raw) raw = (void*)tte;
            g_obj_integrity[raw] = 3;  // 3 = frozen (implies sealed + non-extensible)
            static bool rooted = false;
            if (!rooted) { rooted = true; ts_gc_register_root((void**)&tte); }
        }
        return tte;
    }

    // Accessor descriptor { get: TTE, set: TTE, enumerable:false,
    // configurable:false } — shared by the branches below.
    static TsValue* tte_accessor_descriptor() {
        TsValue* tte = ts_get_throw_type_error();
        TsMap* d = TsMap::Create();
        TsValue k, v;
        k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned("get");
        v = nanbox_to_tagged(tte); d->Set(k, v);
        k.ptr_val = TsString::GetInterned("set");
        d->Set(k, v);
        k.ptr_val = TsString::GetInterned("enumerable");
        v.type = ValueType::BOOLEAN; v.i_val = 0; d->Set(k, v);
        k.ptr_val = TsString::GetInterned("configurable");
        d->Set(k, v);
        return ts_value_make_object(d);
    }

    static TsValue* ts_object_getOwnPropertyDescriptor_impl(TsValue* obj, TsValue* prop);
    extern "C" TsValue* ts_to_property_key_spec(TsValue* key);  // TsObject.cpp

    TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop) {
        // ES 20.1.2.8 order in a std::string-FREE frame (the impl holds
        // std::string scopes; a ToPropertyKey hook may throw/longjmp):
        // 1. ToObject(O) — TypeError on null/undefined; 2. ToPropertyKey(P)
        // — runs toString/valueOf; both-non-primitive throws TypeError
        // (Object/getOwnPropertyDescriptor/15.2.3.3-2-46).
        if (obj) {
            uint64_t nbO = nanbox_from_tsvalue_ptr(obj);
            if (nanbox_is_null(nbO) || nanbox_is_undefined(nbO)) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "Cannot convert undefined or null to object"));
                return ts_value_make_undefined();
            }
        }
        if (prop) prop = ts_to_property_key_spec(prop);
        return ts_object_getOwnPropertyDescriptor_impl(obj, prop);
    }

    static TsValue* ts_object_getOwnPropertyDescriptor_impl(TsValue* obj, TsValue* prop) {
        // Proxy: route through the getOwnPropertyDescriptor trap (ES 10.5.5)
        // when present (or revoked); trap-less proxies keep legacy behavior.
        {
            void* raw0 = obj ? ts_value_get_object(obj) : nullptr;
            if (raw0 && *(uint32_t*)((char*)raw0 + 16) == 0x4D415053) {
                if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)raw0)) {
                    if (px->revoked || px->getTrap("getOwnPropertyDescriptor"))
                        return px->getOwnPropertyDescriptorTrap(prop);
                }
            }
        }
        ts_proxy_throw_if_revoked(obj);  // revoked proxy -> TypeError
        // Per ECMA-262 19.1.2.6: returns undefined when the property does
        // not exist (or the receiver isn't an object). Previously returned
        // ts_value_make_object(nullptr) which is the *null* TsValue.
        if (!obj || !prop) return ts_value_make_undefined();

        // ECMA-262 20.1.2.8: ToObject(O) first -> TypeError on null/undefined
        // (undefined is returned only when a property is genuinely absent).
        uint64_t nbO = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nbO) || nanbox_is_undefined(nbO)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_undefined();
        }

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) {
            return ts_value_make_undefined();
        }

        // ES 20.2.3 / 10.2.4: %Function.prototype%'s "caller" and "arguments"
        // are the %ThrowTypeError% accessor (get === set, non-configurable).
        {
            // ONLY inspect a REAL string key here: ts_value_get_string on a
            // Symbol key routes through ToString and THROWS ("Cannot convert
            // a Symbol value to a string") — this pre-check runs on EVERY
            // getOwnPropertyDescriptor call and took out 139 symbol-keyed
            // descriptor tests in sweep p29 (@@species/@@match prop-desc,
            // class computed keys...).
            const char* k0 = nullptr;
            uint64_t pNb0 = nanbox_from_tsvalue_ptr(prop);
            if (nanbox_is_ptr(pNb0)) {
                void* pp0 = nanbox_to_ptr(pNb0);
                if (pp0) {
                    uint32_t m0 = *(uint32_t*)pp0;
                    if (m0 == 0x53545247 /*STRG*/ || m0 == 0x434F4E53 /*CONS*/) {
                        k0 = ((TsString*)pp0)->ToUtf8();
                    }
                }
            }
            if (k0 && (strcmp(k0, "caller") == 0 || strcmp(k0, "arguments") == 0)) {
                extern void* ts_get_global_Function();
                void* g = ts_get_global_Function();
                void* fraw = g ? ts_value_get_object((TsValue*)g) : nullptr;
                if (!fraw) fraw = g;
                if (fraw && *(uint32_t*)((char*)fraw + 16) == 0x46554E43 /*FUNC*/) {
                    TsFunction* fctor = (TsFunction*)fraw;
                    if (fctor->properties) {
                        TsValue pk; pk.type = ValueType::STRING_PTR;
                        pk.ptr_val = TsString::GetInterned("prototype");
                        TsValue pv = fctor->properties->Get(pk);
                        if (pv.type == ValueType::OBJECT_PTR && pv.ptr_val == rawPtr) {
                            return tte_accessor_descriptor();
                        }
                    }
                }
            }
        }

        // Canonicalize a Symbol key to its "\x01@@sym\x01<i>" storage-key string
        // so symbol property descriptors resolve (getOwnPropertyDescriptor(o,
        // sym) returned null, which broke lodash clone of symbol properties).
        {
            uint64_t pNb = nanbox_from_tsvalue_ptr(prop);
            if (nanbox_is_ptr(pNb)) {
                void* pp = nanbox_to_ptr(pNb);
                if (pp && *(uint32_t*)pp == 0x53594D42) { // TsSymbol "SYMB"
                    TsString* sk = ts_symbol_storage_key((TsSymbol*)pp);
                    if (sk) prop = ts_value_make_string(sk);
                }
            }
        }

        // ECMA-262 §7.1.19 ToPropertyKey -> ToString for a non-string,
        // non-symbol key. getOwnPropertyDescriptor(o, null) must look up "null",
        // (o, 123) -> "123", (o, 1.5) -> "1.5", (o, true) -> "true",
        // (o, new String("x")) -> "x", (o, {}) -> "[object Object]". Without this
        // the key was used as-is and the lookup missed -> desc came back
        // undefined (Object/getOwnPropertyDescriptor/15.2.3.3-2-* cluster).
        // Symbols are already canonicalized above; a TsString is left untouched.
        {
            uint64_t pNb = nanbox_from_tsvalue_ptr(prop);
            TsString* ks = nullptr;
            if (nanbox_is_int32(pNb))
                ks = TsString::Create(std::to_string(nanbox_to_int32(pNb)).c_str());
            else if (nanbox_is_double(pNb))
                ks = (TsString*)ts_number_to_string(nanbox_to_double(pNb), 10);
            else if (nanbox_is_bool(pNb))
                ks = TsString::Create(nanbox_to_bool(pNb) ? "true" : "false");
            else if (nanbox_is_null(pNb))      ks = TsString::Create("null");
            else if (nanbox_is_undefined(pNb)) ks = TsString::Create("undefined");
            else if (nanbox_is_ptr(pNb)) {
                void* pp = nanbox_to_ptr(pNb);
                if (pp) {
                    uint32_t m = *(uint32_t*)pp;
                    if (m != 0x53545247 /* TsString: already a string */ &&
                        m != 0x53594D42 /* TsSymbol: handled above */) {
                        extern void* ts_string_from_value(TsValue* val);
                        TsValue kt = nanbox_to_tagged(prop);
                        ks = (TsString*)ts_string_from_value(&kt);
                    }
                }
            }
            if (ks) prop = ts_value_make_string(ks);
        }

        // Convert flat object to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Check if it's a TsMap (or extract properties map from function/closure)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);

        // TsFunction/TsClosure: .length/.name are now stored in the
        // properties TsMap with correct attributes (by ts_closure_set_arity/
        // set_name and makeNamedNativeFunction). Fall through to the
        // properties-TsMap extraction below — no synthetic override needed.
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)rawPtr;
            if (!func->properties) return ts_value_make_undefined();
            rawPtr = func->properties;
            magic = 0x4D415053;
        } else if (magic == 0x434C5352) { // TsClosure magic
            TsClosure* clos = (TsClosure*)rawPtr;
            if (!clos->properties) return ts_value_make_undefined();
            rawPtr = clos->properties;
            magic = 0x4D415053;
        }
        // Native objects (RegExp, Date, native C++ objects) keep their
        // dynamically-assigned / Object.defineProperty'd own properties in the
        // per-object side-map (g_native_object_props). Route to it so the TsMap
        // descriptor path below reports the stored value AND attributes —
        // previously getOwnPropertyDescriptor returned undefined for these
        // receivers, so verifyProperty (writable/enumerable/configurable checks)
        // failed even after defineProperty stored the property. TsArray is
        // excluded (its indices/length are synthesized just below); an absent
        // key still falls through to the map path's undefined return.
        if (magic != 0x4D415053) {
            uint32_t nm0 = *(uint32_t*)rawPtr;
            if (nm0 != 0x41525259) {  // not TsArray
                if (TsMap* nprops = getNativeProps(rawPtr)) {
                    rawPtr = nprops;
                    magic = 0x4D415053;
                }
            }
        }
        // TsArray: synthesize descriptors for length, numeric indices, and
        // user-set named properties. Spec: arr.length is
        // {value: arr.length, writable: true, enumerable: false, configurable: false}.
        // Indexed reads return {value, writable: true, enumerable: true,
        // configurable: true}.
        {
            uint32_t magic0 = *(uint32_t*)rawPtr;
            if (magic0 == 0x41525259) {  // TsArray::MAGIC
                TsArray* arr = (TsArray*)rawPtr;
                // Resolve key string
                TsString* keyStr = nullptr;
                {
                    uint64_t propNb = nanbox_from_tsvalue_ptr(prop);
                    if (nanbox_is_int32(propNb)) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", nanbox_to_int32(propNb));
                        keyStr = TsString::Create(buf);
                    } else if (nanbox_is_ptr(propNb)) {
                        keyStr = (TsString*)ts_value_get_string(prop);
                    }
                }
                if (!keyStr) return ts_value_make_undefined();
                const char* keyCStr = ts_ensure_flat(keyStr)->ToUtf8();
                if (!keyCStr) return ts_value_make_undefined();

                // ES 10.4.4.6 CreateUnmappedArgumentsObject: "callee" is the
                // %ThrowTypeError% accessor (get === set, non-configurable).
                if (arr->isArguments && strcmp(keyCStr, "callee") == 0) {
                    return tte_accessor_descriptor();
                }

                auto buildDataDesc = [](TsValue val, bool writable, bool enumerable, bool configurable) -> TsValue* {
                    TsMap* d = TsMap::Create();
                    TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::GetInterned("value");
                    d->Set(vk, val);
                    TsValue wk; wk.type = ValueType::STRING_PTR; wk.ptr_val = TsString::GetInterned("writable");
                    TsValue wv; wv.type = ValueType::BOOLEAN; wv.i_val = writable ? 1 : 0;
                    d->Set(wk, wv);
                    TsValue ek; ek.type = ValueType::STRING_PTR; ek.ptr_val = TsString::GetInterned("enumerable");
                    TsValue ev; ev.type = ValueType::BOOLEAN; ev.i_val = enumerable ? 1 : 0;
                    d->Set(ek, ev);
                    TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("configurable");
                    TsValue cv; cv.type = ValueType::BOOLEAN; cv.i_val = configurable ? 1 : 0;
                    d->Set(ck, cv);
                    return ts_value_make_object(d);
                };

                if (strcmp(keyCStr, "length") == 0) {
                    TsValue lenVal; lenVal.type = ValueType::NUMBER_INT;
                    lenVal.i_val = (int64_t)arr->Length();
                    return buildDataDesc(lenVal, true, false, false);
                }
                // Numeric index (strict ECMA array-index test; must match the
                // defineProperty/hasOwnProperty branches so define and readback
                // agree on whether a key is an element or a string property).
                int64_t idx = 0;
                if (parse_canonical_array_index(keyCStr, &idx)) {
                    // Accessor index (defineProperty installed __arr_getter_/
                    // __arr_setter_): report an accessor descriptor, do NOT invoke
                    // the getter.
                    if (arr->properties) {
                        char ak[40];
                        snprintf(ak, sizeof(ak), "__arr_getter_%lu", idx);
                        TsValue gk; gk.type = ValueType::STRING_PTR; gk.ptr_val = TsString::GetInterned(ak);
                        snprintf(ak, sizeof(ak), "__arr_setter_%lu", idx);
                        TsValue sk; sk.type = ValueType::STRING_PTR; sk.ptr_val = TsString::GetInterned(ak);
                        bool hg = arr->properties->Has(gk), hs = arr->properties->Has(sk);
                        if (hg || hs) {
                            uint8_t a = 0x00;
                            array_index_attrs_get(arr, (size_t)idx, &a);
                            TsMap* d = TsMap::Create();
                            TsValue getK; getK.type = ValueType::STRING_PTR; getK.ptr_val = TsString::GetInterned("get");
                            TsValue setK; setK.type = ValueType::STRING_PTR; setK.ptr_val = TsString::GetInterned("set");
                            TsValue undef; undef.type = ValueType::UNDEFINED; undef.i_val = 0;
                            d->Set(getK, hg ? arr->properties->Get(gk) : undef);
                            d->Set(setK, hs ? arr->properties->Get(sk) : undef);
                            TsValue ek2; ek2.type = ValueType::STRING_PTR; ek2.ptr_val = TsString::GetInterned("enumerable");
                            TsValue ev2; ev2.type = ValueType::BOOLEAN; ev2.i_val = (a & 0x01) ? 1 : 0;
                            d->Set(ek2, ev2);
                            TsValue ck2; ck2.type = ValueType::STRING_PTR; ck2.ptr_val = TsString::GetInterned("configurable");
                            TsValue cv2; cv2.type = ValueType::BOOLEAN; cv2.i_val = (a & 0x04) ? 1 : 0;
                            d->Set(ck2, cv2);
                            return ts_value_make_object(d);
                        }
                    }
                    if (idx < (unsigned long)arr->Length() && !arr->IsHole((size_t)idx)) {
                        int64_t raw = arr->Get((size_t)idx);
                        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)raw);
                        // Recorded attrs (defineProperty) override the plain-element
                        // default of writable+enumerable+configurable.
                        uint8_t a = 0x07;
                        array_index_attrs_get(arr, (size_t)idx, &a);
                        return buildDataDesc(v, (a & 0x02) != 0, (a & 0x01) != 0, (a & 0x04) != 0);
                    }
                }
                // Named property in side map
                if (arr->properties) {
                    // Accessor (__getter_/__setter_<key>) installed via
                    // Object.defineProperty(arr, "x", {get/set}) — report a
                    // {get, set, enumerable, configurable} accessor descriptor.
                    const char* propC = keyStr ? keyStr->ToUtf8() : nullptr;
                    if (propC) {
                        TsValue agk; agk.type = ValueType::STRING_PTR;
                        agk.ptr_val = TsString::GetInterned((std::string("__getter_") + propC).c_str());
                        TsValue ask; ask.type = ValueType::STRING_PTR;
                        ask.ptr_val = TsString::GetInterned((std::string("__setter_") + propC).c_str());
                        bool hg = arr->properties->Has(agk), hs = arr->properties->Has(ask);
                        if (hg || hs) {
                            TsMap* desc = TsMap::Create();
                            TsValue undef; undef.type = ValueType::UNDEFINED; undef.i_val = 0;
                            TsValue getK; getK.type = ValueType::STRING_PTR; getK.ptr_val = TsString::GetInterned("get");
                            TsValue setK; setK.type = ValueType::STRING_PTR; setK.ptr_val = TsString::GetInterned("set");
                            desc->Set(getK, hg ? arr->properties->Get(agk) : undef);
                            desc->Set(setK, hs ? arr->properties->Get(ask) : undef);
                            TsValue pk; pk.type = ValueType::STRING_PTR; pk.ptr_val = keyStr;
                            uint8_t a = arr->properties->Has(pk) ? arr->properties->GetPropertyAttrs(pk)
                                      : (hg ? arr->properties->GetPropertyAttrs(agk)
                                            : arr->properties->GetPropertyAttrs(ask));
                            TsValue ek; ek.type = ValueType::STRING_PTR; ek.ptr_val = TsString::GetInterned("enumerable");
                            TsValue ev; ev.type = ValueType::BOOLEAN; ev.i_val = (a & 0x01) ? 1 : 0; desc->Set(ek, ev);
                            TsValue ck; ck.type = ValueType::STRING_PTR; ck.ptr_val = TsString::GetInterned("configurable");
                            TsValue cv; cv.type = ValueType::BOOLEAN; cv.i_val = (a & 0x04) ? 1 : 0; desc->Set(ck, cv);
                            return ts_value_make_object(desc);
                        }
                    }
                    TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = keyStr;
                    if (arr->properties->Has(k)) {
                        TsValue v = arr->properties->Get(k);
                        uint8_t a = arr->properties->GetPropertyAttrs(k);
                        return buildDataDesc(v, (a & 0x02) != 0, (a & 0x01) != 0, (a & 0x04) != 0);
                    }
                }
                return ts_value_make_undefined();
            }
        }

        if (magic != 0x4D415053) {
            return ts_value_make_undefined();  // undefined for non-objects
        }

        TsMap* map = (TsMap*)rawPtr;

        // Get property key via NaN-box decode
        TsValue propKey = nanbox_to_tagged(prop);

        // Per spec, accessor properties yield {get, set, enumerable,
        // configurable} — no value/writable. We store accessors via
        // __getter_<name> / __setter_<name> keys in the same map, so
        // detect them via the property's string name and synthesize an
        // accessor descriptor.
        TsString* propStr = (propKey.type == ValueType::STRING_PTR)
            ? (TsString*)propKey.ptr_val : nullptr;
        if (propStr) {
            const char* propC = propStr->ToUtf8();
            if (propC) {
                std::string getterName = std::string("__getter_") + propC;
                std::string setterName = std::string("__setter_") + propC;
                TsValue gk; gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterName.c_str());
                TsValue sk; sk.type = ValueType::STRING_PTR;
                sk.ptr_val = TsString::GetInterned(setterName.c_str());
                bool hasGetter = map->Has(gk);
                bool hasSetter = map->Has(sk);
                if (hasGetter || hasSetter) {
                    TsMap* desc = TsMap::Create();
                    TsValue getKey; getKey.type = ValueType::STRING_PTR;
                    getKey.ptr_val = TsString::GetInterned("get");
                    TsValue setKey; setKey.type = ValueType::STRING_PTR;
                    setKey.ptr_val = TsString::GetInterned("set");
                    if (hasGetter) {
                        desc->Set(getKey, map->Get(gk));
                    } else {
                        TsValue u; u.type = ValueType::UNDEFINED; u.i_val = 0;
                        desc->Set(getKey, u);
                    }
                    if (hasSetter) {
                        desc->Set(setKey, map->Get(sk));
                    } else {
                        TsValue u; u.type = ValueType::UNDEFINED; u.i_val = 0;
                        desc->Set(setKey, u);
                    }
                    // Read attrs from the named property slot if a data
                    // slot under the same name carries flags. Otherwise
                    // read from the __getter_/__setter_ slot, where the
                    // accessor was actually installed. For pure object-
                    // literal accessors no data slot exists, so falling
                    // back to 0 would report enumerable=configurable=false
                    // — wrong per ECMA-262 §10.1.6.3 (object-literal
                    // accessors default to enumerable+configurable).
                    uint8_t attrs;
                    if (map->Has(propKey)) {
                        attrs = map->GetPropertyAttrs(propKey);
                    } else if (hasGetter) {
                        attrs = map->GetPropertyAttrs(gk);
                    } else if (hasSetter) {
                        attrs = map->GetPropertyAttrs(sk);
                    } else {
                        attrs = 0;
                    }
                    TsValue enumKey; enumKey.type = ValueType::STRING_PTR;
                    enumKey.ptr_val = TsString::GetInterned("enumerable");
                    TsValue enumVal; enumVal.type = ValueType::BOOLEAN;
                    enumVal.i_val = (attrs & 0x01) ? 1 : 0;
                    desc->Set(enumKey, enumVal);
                    TsValue configKey; configKey.type = ValueType::STRING_PTR;
                    configKey.ptr_val = TsString::GetInterned("configurable");
                    TsValue configVal; configVal.type = ValueType::BOOLEAN;
                    configVal.i_val = (attrs & 0x04) ? 1 : 0;
                    desc->Set(configKey, configVal);
                    return ts_value_make_object(desc);
                }
            }
        }

        // Check if property exists
        if (!map->Has(propKey)) {
            return ts_value_make_undefined();  // per ECMA-262 19.1.2.6
        }

        TsValue value = map->Get(propKey);

        // Create descriptor object
        TsMap* desc = TsMap::Create();

        // Set value — use interned strings for keys so property lookup matches
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::GetInterned("value");
        desc->Set(valueKey, value);

        // Read back actual property attribute flags
        uint8_t attrs = map->GetPropertyAttrs(propKey);

        TsValue writableKey;
        writableKey.type = ValueType::STRING_PTR;
        writableKey.ptr_val = TsString::GetInterned("writable");
        TsValue writableVal;
        writableVal.type = ValueType::BOOLEAN;
        writableVal.i_val = (attrs & 0x02) ? 1 : 0; // ATTR_WRITABLE
        desc->Set(writableKey, writableVal);

        TsValue enumKey;
        enumKey.type = ValueType::STRING_PTR;
        enumKey.ptr_val = TsString::GetInterned("enumerable");
        TsValue enumVal;
        enumVal.type = ValueType::BOOLEAN;
        enumVal.i_val = (attrs & 0x01) ? 1 : 0; // ATTR_ENUMERABLE
        desc->Set(enumKey, enumVal);

        TsValue configKey;
        configKey.type = ValueType::STRING_PTR;
        configKey.ptr_val = TsString::GetInterned("configurable");
        TsValue configVal;
        configVal.type = ValueType::BOOLEAN;
        configVal.i_val = (attrs & 0x04) ? 1 : 0; // ATTR_CONFIGURABLE
        desc->Set(configKey, configVal);

        return ts_value_make_object(desc);
    }

    // Object.getOwnPropertyDescriptors(obj) - gets descriptors for all own properties
    // Returns { prop1: descriptor1, prop2: descriptor2, ... }
    TsValue* ts_object_getOwnPropertyDescriptors(TsValue* obj) {
        // Create result object
        TsMap* result = TsMap::Create();

        if (!obj) return ts_value_make_object(result);

        // ECMA-262 20.1.2.9: ToObject(O) first -> TypeError on null/undefined;
        // other primitives coerce to a wrapper with no own props (-> {}).
        // Without this is_flat_object() below dereferenced a NaN-boxed primitive.
        uint64_t nb_d = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nb_d) || nanbox_is_undefined(nb_d)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return ts_value_make_object(result);  // unreachable
        }
        if (!nanbox_is_ptr(nb_d)) return ts_value_make_object(result);

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Convert flat object to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {
            return ts_value_make_object(result);  // empty object for non-objects
        }

        TsMap* map = (TsMap*)rawPtr;

        // Iterate over all own properties
        TsArray* keys = (TsArray*)ts_map_keys(map);
        if (!keys) return ts_value_make_object(result);

        int64_t len = keys->Length();
        for (int64_t i = 0; i < len; i++) {
            int64_t keyRaw = keys->Get(i);
            TsValue* keyVal = (TsValue*)keyRaw;

            // Get the descriptor for this property
            TsValue* descriptor = ts_object_getOwnPropertyDescriptor(obj, keyVal);

            // Store descriptor in result with the property name as key. keyVal and
            // descriptor are NaN-boxed TsValue* (the box IS the value), so convert
            // to a tagged TsValue with nanbox_to_tagged — dereferencing them (*keyVal)
            // read the TsString/descriptor object's bytes as a TsValue (garbage key),
            // which is why getOwnPropertyDescriptors returned {}.
            if (descriptor && keyVal) {
                result->Set(nanbox_to_tagged(keyVal), nanbox_to_tagged(descriptor));
            }
        }

        return ts_value_make_object(result);
    }

    // Object.assign(target, source) - copies properties from source to target
    TsValue* ts_object_assign(TsValue* target, TsValue* source) {
        if (!target) return target;
        if (!source) return target;

        // ECMA-262 19.1.2.1 step 1: ToObject(target) — null/undefined throws.
        uint64_t tnb = nanbox_from_tsvalue_ptr(target);
        if (nanbox_is_null(tnb) || nanbox_is_undefined(tnb)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return target;  // unreachable
        }
        // Step 2: For each source, if null/undefined, skip silently.
        uint64_t snb = nanbox_from_tsvalue_ptr(source);
        if (nanbox_is_null(snb) || nanbox_is_undefined(snb)) {
            return target;
        }

        void* targetRaw = ts_value_get_object(target);
        if (!targetRaw) targetRaw = target;

        void* sourceRaw = ts_value_get_object(source);
        if (!sourceRaw) sourceRaw = source;

        // If targetRaw isn't a real heap pointer (e.g., target is a NaN-boxed
        // number/string), reading magic at offset 16 would crash. The
        // primitive-boxing-to-wrapper path isn't implemented here; return
        // the target unchanged rather than fault.
        uint64_t targetCheck = (uint64_t)(uintptr_t)targetRaw;
        if ((targetCheck & 0xFFFF000000000000ULL) != 0 || targetCheck < 0x1000) {
            return target;
        }
        uint64_t sourceCheck = (uint64_t)(uintptr_t)sourceRaw;
        if ((sourceCheck & 0xFFFF000000000000ULL) != 0 || sourceCheck < 0x1000) {
            return target;
        }

        // Check for flat source object
        uint32_t sourceMagic0 = *(uint32_t*)sourceRaw;
        bool sourceIsFlat = (sourceMagic0 == 0x464C4154); // FLAT_MAGIC

        // Check for flat target object
        uint32_t targetMagic0 = *(uint32_t*)targetRaw;
        bool targetIsFlat = (targetMagic0 == 0x464C4154); // FLAT_MAGIC

        if (sourceIsFlat) {
            // Copy from flat source to target
            uint32_t shapeId = flat_object_shape_id(sourceRaw);
            ShapeDescriptor* desc = ts_shape_lookup(shapeId);
            if (desc) {
                for (uint32_t i = 0; i < desc->numSlots; i++) {
                    uint64_t val = *(uint64_t*)((char*)sourceRaw + 16 + i * 8);
                    TsString* keyStr = TsString::Create(desc->propNames[i]);
                    if (targetIsFlat) {
                        ts_flat_object_set_property(targetRaw, desc->propNames[i], (void*)(uintptr_t)val);
                    } else {
                        uint32_t targetMagic16 = *(uint32_t*)((char*)targetRaw + 16);
                        if (targetMagic16 == 0x4D415053) {
                            TsMap* targetMap = (TsMap*)targetRaw;
                            TsValue tv = nanbox_to_tagged((TsValue*)(uintptr_t)val);
                            targetMap->Set(TsValue(keyStr), tv);
                        }
                    }
                }
            }
            return target;
        }

        // Check both are TsMaps (magic at offset 16 - see TsObject.h layout)
        uint32_t targetMagic = *(uint32_t*)((char*)targetRaw + 16);
        uint32_t sourceMagic = *(uint32_t*)((char*)sourceRaw + 16);

        if (sourceMagic != 0x4D415053) {
            return target;
        }

        TsMap* sourceMap = (TsMap*)sourceRaw;

        // Get entries from source and copy to target
        TsArray* entries = (TsArray*)sourceMap->GetEntries();
        int64_t len = entries->Length();
        for (int64_t i = 0; i < len; i++) {
            TsArray* entry = (TsArray*)entries->Get(i);
            TsValue* key = (TsValue*)entry->Get(0);
            TsValue* val = (TsValue*)entry->Get(1);
            // ES CopyDataProperties/Object.assign use [[Get]]: an ACCESSOR on
            // the source must be INVOKED and its VALUE copied as a data
            // property — the __getter_/__setter_ storage keys must never be
            // copied verbatim (rest/spread/assign of `{get v(){...}}` put the
            // accessor itself on the result; gen-meth-dflt-obj-ptrn-rest-getter
            // family). This frame is std::string-free: the getter may throw.
            {
                TsString* keyStr0 = (TsString*)ts_nanbox_safe_unbox(key);
                const char* kc = keyStr0 ? keyStr0->ToUtf8() : nullptr;
                if (kc && strncmp(kc, "__setter_", 9) == 0) {
                    // A get/set pair is handled at its __getter_ entry; a
                    // SET-ONLY accessor's [[Get]] is undefined.
                    char gk[288];
                    if (strlen(kc + 9) < 260) {
                        snprintf(gk, sizeof(gk), "__getter_%s", kc + 9);
                        TsValue gkv; gkv.type = ValueType::STRING_PTR;
                        gkv.ptr_val = TsString::GetInterned(gk);
                        if (!sourceMap->Has(gkv)) {
                            TsValue* undef = ts_value_make_undefined();
                            if (targetIsFlat) {
                                ts_flat_object_set_property(targetRaw, kc + 9, undef);
                            } else if (targetMagic == 0x4D415053) {
                                TsValue rk; rk.type = ValueType::STRING_PTR;
                                rk.ptr_val = TsString::GetInterned(kc + 9);
                                ((TsMap*)targetRaw)->Set(rk, nanbox_to_tagged(undef));
                            }
                        }
                    }
                    continue;
                }
                if (kc && strncmp(kc, "__getter_", 9) == 0) {
                    const char* realKey = kc + 9;
                    // [[Get]] on the source invokes the accessor (may throw).
                    TsValue* got = ts_object_get_property(sourceRaw, realKey);
                    if (targetIsFlat) {
                        ts_flat_object_set_property(targetRaw, realKey, got);
                    } else if (targetMagic == 0x4D415053) {
                        TsValue rk; rk.type = ValueType::STRING_PTR;
                        rk.ptr_val = TsString::GetInterned(realKey);
                        ((TsMap*)targetRaw)->Set(rk, nanbox_to_tagged(got));
                    }
                    continue;
                }
            }
            if (targetIsFlat) {
                TsString* keyStr = (TsString*)ts_nanbox_safe_unbox(key);
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) ts_flat_object_set_property(targetRaw, k, val);
                }
            } else if (targetMagic == 0x4D415053) {
                TsMap* targetMap = (TsMap*)targetRaw;
                targetMap->Set(nanbox_to_tagged(key), nanbox_to_tagged(val));
            }
        }

        return target;
    }
    
    // Object.hasOwn(obj, prop) - check if object has own property
    bool ts_object_has_own(TsValue* obj, TsValue* prop) {
        if (!obj || !prop) return false;

        // ECMA-262 20.1.2.13: ToObject(O) happens BEFORE ToPropertyKey(P), so
        // null/undefined must throw TypeError here; other primitives coerce to a
        // wrapper with no own string-keyed props (-> false). Without this the
        // magic read below dereferenced a NaN-boxed primitive (Object.hasOwn(null)).
        uint64_t nb_h = nanbox_from_tsvalue_ptr(obj);
        if (nanbox_is_null(nb_h) || nanbox_is_undefined(nb_h)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert undefined or null to object"));
            return false;  // unreachable
        }
        if (!nanbox_is_ptr(nb_h)) return false;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Symbol key: the string/TsMap fast paths below would ToUtf8 / STRING_PTR-hash
        // the symbol (-> crash for a description-less symbol, and never matches the
        // symbol-keyed storage, which lives in a side-map). Route through
        // getOwnPropertyDescriptor, which resolves symbol-keyed own properties
        // correctly (the same path the Object.hasOwn native wrapper and
        // Object.prototype.hasOwnProperty use).
        {
            void* propRawSym = ts_nanbox_safe_unbox(prop);
            if (propRawSym && *(uint32_t*)propRawSym == 0x53594D42 /* TsSymbol "SYMB" */) {
                extern TsValue* ts_object_getOwnPropertyDescriptor(TsValue*, TsValue*);
                TsValue* desc = ts_object_getOwnPropertyDescriptor(obj, prop);
                return desc != nullptr && !ts_value_is_undefined(desc);
            }
        }

        // Check for flat object first
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            void* propRaw = ts_nanbox_safe_unbox(prop);
            if (!propRaw) return false;
            TsString* keyStr = (TsString*)propRaw;
            const char* k = keyStr->ToUtf8();
            if (!k) return false;
            return ts_flat_object_has_property(rawPtr, k);
        }

        // Validated, offset-derived TsMap tag check (ts_cast<T>, TsTyped.h).
        if (TsMap* map = ts_cast_unchecked<TsMap>(rawPtr)) {

            // Get the property name as a string
            void* propRaw = ts_nanbox_safe_unbox(prop);

            TsValue propVal;
            propVal.type = ValueType::STRING_PTR;
            propVal.ptr_val = propRaw;
            return ts_map_has_v(map, propVal);
        }

        return false;
    }

    // Object.fromEntries(iterable) - create object from key-value pairs
    TsValue* ts_object_from_entries(TsValue* entries) {
        TsMap* result = TsMap::Create();
        if (!entries) return ts_value_make_object(result);
        
        void* rawPtr = ts_value_get_object(entries);
        if (!rawPtr) rawPtr = entries;

        // RequireObjectCoercible + GetIterator (Object.fromEntries step 1-3): a
        // primitive (null/undefined/number/boolean) is not iterable -> TypeError.
        // Without this guard the magic read below dereferenced a NaN-boxed primitive
        // (Object.fromEntries(null) -> access violation).
        uintptr_t pp = (uintptr_t)rawPtr;
        if (pp < 0x1000 || pp > 0x00007FFFFFFFFFFFULL) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Object.fromEntries requires an iterable of [key, value] entries"));
            return ts_value_make_undefined();
        }

        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != 0x41525259) { // TsArray::MAGIC
            return ts_value_make_object(result);
        }
        
        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();
        
        for (int64_t i = 0; i < len; i++) {
            void* entry = (void*)arr->Get(i);
            if (!entry) continue;
            
            // Unbox entry if needed
            void* entryRaw = ts_nanbox_safe_unbox(entry);
            
            uint32_t entryMagic = *(uint32_t*)entryRaw;
            if (entryMagic != 0x41525259) continue;
            
            TsArray* pair = (TsArray*)entryRaw;
            if (pair->Length() < 2) continue;
            
            TsValue* key = (TsValue*)pair->Get(0);
            TsValue* val = (TsValue*)pair->Get(1);
            if (key && val) {
                result->Set(nanbox_to_tagged(key), nanbox_to_tagged(val));
            }
        }
        
        return ts_value_make_object(result);
    }

    // ES2024 Object.groupBy(iterable, callbackFn)
    // Groups elements by the key returned from the callback
    TsValue* ts_object_groupBy(TsValue* iterable, TsValue* callbackFn) {
        TsMap* result = TsMap::Create();

        if (!iterable || !callbackFn) return ts_value_make_object(result);

        // Get raw array pointer
        void* rawPtr = ts_value_get_object(iterable);
        if (!rawPtr) rawPtr = iterable;

        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != TsArray::MAGIC) {
            return ts_value_make_object(result);
        }

        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();

        // Get the callback function
        void* cbRaw = ts_value_get_object(callbackFn);
        if (!cbRaw) cbRaw = callbackFn;

        for (int64_t i = 0; i < len; i++) {
            // Get raw element value
            int64_t rawVal = arr->Get(i);

            // Box properly using ts_value_box_any to detect strings, arrays, etc.
            TsValue* elem;
            if (rawVal > 0xFFFFFFFF || rawVal < 0) {
                // Looks like a pointer - use ts_value_box_any for proper type detection
                elem = ts_value_box_any((void*)rawVal);
            } else {
                // Small value - likely an integer
                elem = ts_value_make_int(rawVal);
            }
            if (!elem) continue;

            // Call callback with (element, index)
            TsValue* indexVal = ts_value_make_int(i);
            TsValue* keyResult = tsCall(callbackFn, elem, indexVal);

            if (!keyResult) continue;

            // Convert result to string key via NaN-box decode
            TsValue keyVal;
            uint64_t krNb = nanbox_from_tsvalue_ptr(keyResult);
            if (nanbox_is_string_ptr(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = nanbox_to_ptr(krNb);
            } else if (nanbox_is_int32(krNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", nanbox_to_int32(krNb));
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (nanbox_is_double(krNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", nanbox_to_double(krNb));
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (nanbox_is_bool(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(nanbox_to_bool(krNb) ? "true" : "false");
            } else if (nanbox_is_undefined(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create("undefined");
            } else {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create("[object Object]");
            }

            // Check if group already exists
            TsValue existing = result->Get(keyVal);
            TsArray* group;

            if (existing.type == ValueType::ARRAY_PTR && existing.ptr_val) {
                group = (TsArray*)existing.ptr_val;
            } else {
                // Create new group array
                group = TsArray::Create();
                TsValue groupVal;
                groupVal.type = ValueType::ARRAY_PTR;
                groupVal.ptr_val = group;
                result->Set(keyVal, groupVal);
            }

            // Add element to group
            group->Push((int64_t)elem);
        }

        return ts_value_make_object(result);
    }

}  // extern "C"
