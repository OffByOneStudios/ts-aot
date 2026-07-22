// Iterator-protocol runtime (ECMA-262 7.4): GetIterator / IteratorNext /
// IteratorComplete / IteratorValue. Extracted from TsPromise.cpp (SMELL-002
// item 9): the protocol serves spread, destructuring, for-of, yield*,
// Map/Set constructors and Array.from -- not just promise/generator code.
// The compiled for-of loop's own step guard is ts_iterator_step_require_object
// (TsObject.cpp).
#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsHashTable.h"
#include "TsGC.h"
#include "TsError.h"
#include "TsConsString.h"
#include "TsObject.h"
#include <cstdio>
#include <cstdlib>

namespace ts {

// {value, done} result builder -- defined in TsPromise.cpp, woven through the
// generator machinery there; shared here for the array-iterator fallback.
TsValue* create_generator_result(TsValue value, bool done);

extern "C" {

// Array.prototype[@@iterator] mutation tracking (defined in TsArray.cpp).
// Declared at file scope so the references in ts_iterator_get link with C
// linkage (block-scope `extern "C"` is illegal).
extern "C" uint64_t g_array_prototype_version;
extern "C" bool g_array_default_iterator_deleted;
extern "C" TsMap* g_array_prototype_map;

// ---------------------------------------------------------------------------
// Iterator-protocol validation (ECMA-262 7.4.2 GetIterator / 7.4.3 IteratorNext)
// shared by ts_iterator_get / ts_iterator_next below (spread, destructuring,
// yield*, Map/Set constructors). The compiled for-of loop has its own guard
// (ts_iterator_step_require_object, TsObject.cpp).
// ---------------------------------------------------------------------------

// Type(x) is Object: a real heap pointer that is not a primitive. Strings are
// NaN-boxed with their own tag (ts_value_get_object -> nullptr); symbols,
// bigints, and rope strings are plain heap pointers, so reject them by magic.
// Functions/arrays/promises/maps/flat objects all pass.
static bool iter_value_is_object(TsValue* v) {
    if (!v) return false;
    void* raw = ts_value_get_object(v);  // nullptr for numbers/specials/strings
    if (!raw) return false;
    uintptr_t p = (uintptr_t)raw;
    if (p < 0x1000 || p >= 0x0000800000000000ULL) return false;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 /* TsString "STRG" */ ||
        m0 == 0x434F4E53 /* TsConsString "CONS" */ ||
        m0 == 0x53594D42 /* TsSymbol "SYMB" */ ||
        m0 == 0x42494749 /* TsBigInt "BIGI" */) return false;
    return true;
}

// GetIterator step 4: if Type(iterator) is not Object, throw TypeError.
// Applied to every @@iterator call result in ts_iterator_get.
static TsValue* iter_require_object_result(TsValue* res) {
    if (!iter_value_is_object(res)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator method returned a non-object"));
    }
    return res;
}

// Presence check for the @@iterator slot covering both the data key and the
// computed-accessor convention (__getter_[Symbol.iterator]).
static bool iter_slot_present(TsMap* obj) {
    TsValue k1; k1.type = ValueType::STRING_PTR;
    k1.ptr_val = TsString::GetInterned("[Symbol.iterator]");
    if (obj->Has(k1)) return true;
    TsValue k2; k2.type = ValueType::STRING_PTR;
    k2.ptr_val = TsString::GetInterned("__getter_[Symbol.iterator]");
    return obj->Has(k2);
}

// @@iterator slot PRESENT on a map-backed object but the raw TsMap::Get read
// something non-callable. The raw Get cannot run accessors (__getter_
// convention), so consult the accessor-aware property read before deciding:
// a callable method is called (this = the iterable) and its result
// object-checked; anything else (null / undefined / primitive @@iterator)
// is a GetIterator TypeError -- never fall back to "not an iterator, treat
// as empty" (spread-err-mult-err-iter-get-value cluster).
static TsValue* iter_noncallable_slot(void* rawObj, TsValue* iterable) {
    extern TsValue* ts_object_get_property(void* o, const char* k);
    TsValue* mv = ts_object_get_property(rawObj, "[Symbol.iterator]");
    if (mv && ts_is_callable((void*)mv)) {
        return iter_require_object_result(ts_call_with_this_0(mv, iterable));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "(intermediate value)[Symbol.iterator] is not a function"));
    return nullptr;  // unreachable (ts_throw longjmps)
}

// yield* delegation support - get an iterator from an iterable
TsValue* ts_iterator_get(TsValue* iterable) {
    if (!iterable) {
        return nullptr;
    }
    static const bool s_dbgIter = getenv("TS_DEBUG_ITER") != nullptr;
    if (s_dbgIter) {
        fprintf(stderr, "[ITER] get in=%p nb=%016llx\n", (void*)iterable,
                (unsigned long long)nanbox_from_tsvalue_ptr(iterable));
    }

    // First, try to extract the raw object pointer using ts_value_get_object
    // This handles both boxed TsValue* and raw object pointers
    void* rawObj = ts_value_get_object(iterable);
    // ts_value_get_object returns nullptr for primitive strings (which are
    // valid iterables per spec). Fall back to extracting the string pointer
    // directly so the TsString branch below can fire. ONLY for pointer-shaped
    // values: ts_value_get_string ToString-COERCES numbers/booleans, which
    // made `for (v of 12)` iterate the string "12" instead of throwing the
    // GetIterator TypeError (the primitive tail below never ran).
    if (!rawObj && nanbox_is_ptr(nanbox_from_tsvalue_ptr(iterable))) {
        void* maybeStr = ts_value_get_string(iterable);
        if (maybeStr) {
            uint32_t m = *(uint32_t*)maybeStr;
            if (m == 0x53545247) rawObj = maybeStr;  // TsString::MAGIC
            else if (ts_is_cons_string(maybeStr)) rawObj = ts_ensure_flat(maybeStr);
        }
    }
    // A concat result >= 64 chars is a TsConsString ROPE (magic "CONS"), which
    // no branch below recognizes — for-of over one threw "iterator method
    // returned a non-object" (post-guard; previously an infinite loop).
    // Flatten so the TsString branch fires.
    if (rawObj && ts_is_cons_string(rawObj)) rawObj = ts_ensure_flat(rawObj);

    // Check if we have a TsMap-based object (TsMap, TsGenerator, TsAsyncGenerator)
    if (rawObj) {
        // Set/Map COLLECTIONS: their [Symbol.iterator] is exposed through the
        // Set/Map property getter (ts_set/map_get_property), NOT the raw
        // TsMap::Get lookups below, so the old code returned the collection
        // itself -> next() reported done immediately -> spread `[...set]` /
        // `[...map]`, Array.from(set/map), and Map.groupBy came out empty.
        // Build the proper iterator directly (Set -> values; Map -> entries).
        // Exclude objects that are already iterators (they carry __iter_items).
        if ((uintptr_t)rawObj >= 0x1000) {
            uint32_t m16 = *(uint32_t*)((char*)rawObj + 16);
            bool isIterAlready = false;
            if (m16 == 0x4D415053) {  // TsMap "MAPS"
                TsValue ik; ik.type = ValueType::STRING_PTR;
                ik.ptr_val = TsString::GetInterned("__iter_items");
                isIterAlready = ((TsMap*)rawObj)->Has(ik);
            }
            if (!isIterAlready) {
                if (m16 == 0x53455453) {  // TsSet "SETS"
                    extern void* ts_set_values(void* set);
                    extern void* ts_create_set_iterator(void* items);
                    return (TsValue*)ts_create_set_iterator(ts_set_values(rawObj));
                }
                if (m16 == 0x4D415053 && ((TsMap*)rawObj)->IsExplicitMap()) {
                    extern void* ts_map_entries(void* map);
                    extern void* ts_create_map_iterator(void* items);
                    return (TsValue*)ts_create_map_iterator(ts_map_entries(rawObj));
                }
            }
        }

        // Map, Generator, or AsyncGenerator (all TsMap-based, tag at offset 16).
        if (ts_is_unchecked<TsMap>(rawObj) ||
            ts_is_unchecked<TsGenerator>(rawObj) ||
            ts_is_unchecked<TsAsyncGenerator>(rawObj)) {
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                // Call via the proper ts_call_0 dispatcher so user-written
                // [Symbol.iterator]() native functions (which expect
                // (ctx, argc, argv)) are invoked correctly. Direct
                // `funcPtr(context)` only worked for functions that ignore
                // argc/argv like TsGenerator's identity [Symbol.iterator].
                TsValue* boxedFn = (TsValue*)iterMethod.ptr_val;
                // ES GetIterator step 4: the @@iterator call result must be
                // an Object (null/number/string/Symbol -> TypeError).
                return iter_require_object_result(
                    ts_call_with_this_0(boxedFn, iterable));
            }
            // @@iterator slot present (own data key or computed-accessor
            // slot) but the raw map read found it non-callable: run the
            // accessor-aware read; a null/undefined/primitive @@iterator is
            // a GetIterator TypeError instead of a silent empty iteration.
            if (iter_slot_present(obj)) {
                return iter_noncallable_slot(rawObj, iterable);
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }

        // Also try magic at offset 8 for some TsMap layouts
        uint32_t magic8 = *(uint32_t*)((char*)rawObj + 8);
        if (magic8 == 0x4D415053) { // TsMap::MAGIC
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                // Call via the proper ts_call_0 dispatcher so user-written
                // [Symbol.iterator]() native functions (which expect
                // (ctx, argc, argv)) are invoked correctly. Direct
                // `funcPtr(context)` only worked for functions that ignore
                // argc/argv like TsGenerator's identity [Symbol.iterator].
                TsValue* boxedFn = (TsValue*)iterMethod.ptr_val;
                // ES GetIterator step 4: the @@iterator call result must be
                // an Object (null/number/string/Symbol -> TypeError).
                return iter_require_object_result(
                    ts_call_with_this_0(boxedFn, iterable));
            }
            // @@iterator slot present (own data key or computed-accessor
            // slot) but the raw map read found it non-callable: run the
            // accessor-aware read; a null/undefined/primitive @@iterator is
            // a GetIterator TypeError instead of a silent empty iteration.
            if (iter_slot_present(obj)) {
                return iter_noncallable_slot(rawObj, iterable);
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                return iterable;
            }
        }
    }

    // Check for raw TsArray* pointer (TsArray has no vtable, magic at offset 0)
    TsValue iterDecoded = nanbox_to_tagged(iterable);
    if (rawObj) {
        if (ts_is_unchecked<TsArray>(rawObj)) { // TsArray (tag at offset 0)
            TsArray* arr = (TsArray*)rawObj;
            // ECMA-262 GetIterator(obj): honor an OWN overridden @@iterator
            // (`arr[Symbol.iterator] = fn`) instead of the built-in array
            // iterator. The default lives on Array.prototype (inherited), so an
            // entry in the instance's own `properties` side-map is necessarily
            // a user override — call it with `this` = the array. Without this,
            // `var [a,b] = arrWithCustomIter` read the raw elements, ignoring
            // the override (and never ran a custom next()).
            extern TsValue* ts_call_with_this_0(TsValue*, TsValue*);
            if (arr->properties) {
                TsValue ik; ik.type = ValueType::STRING_PTR;
                ik.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                if (arr->properties->Has(ik)) {
                    TsValue m = arr->properties->Get(ik);
                    if ((m.type == ValueType::OBJECT_PTR ||
                         m.type == ValueType::FUNCTION_PTR) && m.ptr_val) {
                        return iter_require_object_result(
                            ts_call_with_this_0((TsValue*)m.ptr_val, iterable));
                    }
                    // Own @@iterator present but undefined/non-callable:
                    // GetIterator throws TypeError (no fall-back to the
                    // built-in array iterator).
                    ts_throw((TsValue*)ts_error_create_typed(
                        "TypeError", "(intermediate value)[Symbol.iterator] is not a function"));
                    return nullptr;  // unreachable (ts_throw longjmps)
                }
            }
            // ECMA-262 GetIterator also consults Array.prototype[@@iterator].
            // ts-aot serves the default iterator from a built-in fast path, so
            // a user mutation of Array.prototype[Symbol.iterator] (override or
            // `delete`) is only observable via the prototype map / deleted
            // flag. Gate on the version counter so the unmutated path stays hot.
            {
                if (g_array_prototype_version != 0 && g_array_prototype_map) {
                    TsValue pk; pk.type = ValueType::STRING_PTR;
                    pk.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                    if (g_array_prototype_map->Has(pk)) {
                        TsValue pm = g_array_prototype_map->Get(pk);
                        if ((pm.type == ValueType::OBJECT_PTR ||
                             pm.type == ValueType::FUNCTION_PTR) && pm.ptr_val) {
                            // Overridden Array.prototype[@@iterator]. A re-assigned
                            // default reads back as the array `values` native, so
                            // this also restores normal behavior after a restore.
                            return iter_require_object_result(
                                ts_call_with_this_0((TsValue*)pm.ptr_val, iterable));
                        }
                        ts_throw((TsValue*)ts_error_create_typed(
                            "TypeError", "Array.prototype[Symbol.iterator] is not a function"));
                        return nullptr;  // unreachable
                    }
                    if (g_array_default_iterator_deleted) {
                        // `delete Array.prototype[Symbol.iterator]` -> no iterator.
                        ts_throw((TsValue*)ts_error_create_typed(
                            "TypeError", "Array.prototype[Symbol.iterator] is not a function"));
                        return nullptr;  // unreachable
                    }
                }
            }
            // Update decoded value for array iterator path below
            iterDecoded.type = ValueType::ARRAY_PTR;
            iterDecoded.ptr_val = arr;
            // Fall through to ARRAY_PTR check below
        }
        // TsString: per spec, String.prototype[@@iterator] yields each
        // Unicode code point as a one-character string. We decode the
        // string into a TsArray of one-codepoint strings up-front, then
        // reuse the array-iterator path below. This is O(n) memory but
        // correct for surrogate pairs (e.g. "a😀b" yields "a","😀","b").
        // Without this branch, for-of on a string previously fell through
        // to `return iterable` and the resulting non-iterator caused an
        // infinite alloc loop in the for-of next() polling.
        if (ts_is_unchecked<TsString>(rawObj)) { // TsString (tag at offset 0)
            TsString* s = (TsString*)rawObj;
            int64_t len = s->Length();  // code-unit length
            // Pre-size to code-unit length (>= code-point count) so Push never
            // grows mid-loop, and REGISTER the raw locals as GC roots: this
            // loop allocates per code point, a minor GC mid-loop MOVES nursery
            // objects, and C++ frame locals are not precise roots. Unrooted,
            // a multi-megabyte string (test262 CharacterClassEscapes builds
            // ~2.1M-char subjects) corrupted the heap — nondeterministic
            // 0xC0000409/0xC0000005 crashes that vanish under TS_GC_NURSERY=0.
            TsArray* arr = TsArray::Create((size_t)(len > 0 ? len : 0));
            TsString* part = nullptr;
            ts_gc_register_root((void**)&s);
            ts_gc_register_root((void**)&arr);
            ts_gc_register_root((void**)&part);
            int64_t i = 0;
            while (i < len) {
                int64_t cp = s->CodePointAt(i);
                int64_t cps[1] = {cp};
                part = TsString::FromCodePoint(cps, 1);
                TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = part;
                arr->Push((int64_t)(uintptr_t)nanbox_from_tagged(v));
                // Advance past surrogate pair if needed.
                i += (cp > 0xFFFF) ? 2 : 1;
            }
            ts_gc_unregister_root((void**)&part);
            ts_gc_unregister_root((void**)&arr);
            ts_gc_unregister_root((void**)&s);
            iterDecoded.type = ValueType::ARRAY_PTR;
            iterDecoded.ptr_val = arr;
        }
    }

    if (s_dbgIter)
        fprintf(stderr, "[ITER] decoded type=%d rawObj=%p\n",
                (int)iterDecoded.type, rawObj);
    // Fall back to type-based check for explicit OBJECT_PTR values
    if (iterDecoded.type == ValueType::OBJECT_PTR) {
        TsMap* obj = (TsMap*)iterDecoded.ptr_val;
        if (obj) {
            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                TsFunction* func = (TsFunction*)iterMethod.ptr_val;
                if (func->funcPtr) {
                    typedef TsValue* (*IterFunc)(void*);
                    // ES GetIterator step 4: result must be an Object.
                    return iter_require_object_result(
                        ((IterFunc)func->funcPtr)(func->context));
                }
            }
            // @@iterator slot present but not callable via the raw map read
            // (accessor or null/undefined/primitive). Only trust the TsMap
            // reads when the layout is verified MAPS -- this fallback path
            // also sees non-TsMap layouts.
            if ((uintptr_t)obj >= 0x1000 &&
                *(uint32_t*)((char*)obj + 16) == 0x4D415053 /* MAPS */ &&
                iter_slot_present(obj)) {
                return iter_noncallable_slot((void*)obj, iterable);
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }
    }

    // For arrays, create an array iterator
    if (iterDecoded.type == ValueType::ARRAY_PTR) {
        TsArray* arr = (TsArray*)iterDecoded.ptr_val;
        if (arr) {
            // Create a simple array iterator object
            TsMap* iterator = TsMap::Create();

            // Create properly typed keys for internal properties
            TsValue arrKey, idxKey;
            arrKey.type = ValueType::STRING_PTR;
            arrKey.ptr_val = TsString::Create("__arr");
            idxKey.type = ValueType::STRING_PTR;
            idxKey.ptr_val = TsString::Create("__idx");

            iterator->Set(arrKey, iterDecoded);
            iterator->Set(idxKey, TsValue((int64_t)0));

            // Create the next function that iterates over the array
            TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
                TsMap* self = (TsMap*)ctx;
                if (!self) return create_generator_result(TsValue(), true);

                TsValue arrKey, idxKey;
                arrKey.type = ValueType::STRING_PTR;
                arrKey.ptr_val = TsString::Create("__arr");
                idxKey.type = ValueType::STRING_PTR;
                idxKey.ptr_val = TsString::Create("__idx");

                TsValue arrVal = self->Get(arrKey);
                TsValue idxVal = self->Get(idxKey);

                TsArray* arr = (TsArray*)arrVal.ptr_val;
                int64_t idx = idxVal.i_val;

                if (!arr) {
                    return create_generator_result(TsValue(), true);
                }

                size_t len = arr->Length();

                if (idx >= (int64_t)len) {
                    return create_generator_result(TsValue(), true);
                }

                // Get the value and increment index
                // Use GetElementBoxed which returns a proper TsValue*, not Get() which returns raw int64_t
                TsValue* elem = arr->GetElementBoxed(idx);
                self->Set(idxKey, TsValue(idx + 1));

                if (elem) {
                    return create_generator_result(nanbox_to_tagged(elem), false);
                } else {
                    TsValue undef;
                    undef.type = ValueType::UNDEFINED;
                    return create_generator_result(undef, false);
                }
            }, iterator));

            TsValue nextKey;
            nextKey.type = ValueType::STRING_PTR;
            nextKey.ptr_val = TsString::Create("next");
            iterator->Set(nextKey, nextFunc);
            return ts_value_make_object(iterator);
        }
    }

    // ES GetIterator: nothing above recognized the value and primitives
    // (other than strings, handled early) carry no @@iterator — TypeError,
    // not the old pass-through that iterated bool/number/symbol/bigint as
    // silently EMPTY (for-of / array-destructuring over `true`/`5`).
    // Unrecognized OBJECTS keep the permissive pass-through for now.
    {
        uint64_t nb = nanbox_from_tsvalue_ptr(iterable);
        if (s_dbgIter)
            fprintf(stderr, "[ITER] tail nb=%016llx is_ptr=%d\n",
                    (unsigned long long)nb, (int)nanbox_is_ptr(nb));
        bool primitive = !nanbox_is_ptr(nb);
        if (!primitive) {
            void* p = nanbox_to_ptr(nb);
            if (p && (uintptr_t)p >= 0x1000) {
                uint32_t m0 = *(uint32_t*)p;
                if (m0 == 0x53594D42 /*SYMB*/ || m0 == 0x42494749 /*BIGI*/)
                    primitive = true;
            }
        }
        if (primitive) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "value is not iterable"));
            return nullptr;  // unreachable (ts_throw longjmps)
        }
    }
    return iterable;
}

// Call next on an iterator
TsValue* ts_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) return nullptr;

    void* rawObj = ts_value_get_object(iterator);
    if (!rawObj) rawObj = iterator;
    if (rawObj) {
        // Look up `next` via the PROTOTYPE-WALKING property getter. Built-in
        // Map/Set/Array iterators keep next() on their prototype, which the old
        // raw TsMap::Get did not traverse -> it found nothing and returned done
        // immediately (spread/Array.from over those iterators came out empty).
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* nextFn = ts_object_get_property(rawObj, "next");
        if (nextFn) {
            TsValue nm = nanbox_to_tagged(nextFn);
            if ((nm.type == ValueType::OBJECT_PTR || nm.type == ValueType::FUNCTION_PTR) && nm.ptr_val) {
                // ECMA-262 IteratorNext: call next() with the ITERATOR as `this`
                // (these iterators read their state from ts_get_call_this()).
                TsValue* res = value ? ts_call_with_this_1(nextFn, iterator, value)
                                     : ts_call_with_this_0(nextFn, iterator);
                // ES 7.4.3 IteratorNext step 3: Type(result) must be Object.
                // A primitive result previously read done=false forever ->
                // spread/destructure infinite loop (OOM) or silent mis-bind.
                if (!iter_value_is_object(res)) {
                    ts_throw((TsValue*)ts_error_create_typed("TypeError",
                        "iterator result is not an object"));
                }
                return res;
            }
        }
    }

    // Fallback: return done
    return create_generator_result(TsValue(), true);
}

// Check if an iterator result is done.
// ECMA-262 IteratorComplete: ToBoolean(Get(result, "done")). The result object
// may be a TsMap (generator/Map/Set iterators) OR a flat inline-slot object
// returned from a user-written `next()`. The raw TsMap::Get used previously did
// not see flat-object slots (or prototype-walked), so a `{value, done:false}`
// returned by a user iterator read no BOOLEAN -> fell back to done=true ->
// spread/destructuring over user iterables came out empty. Use the general
// property getter (the same one ts_iterator_next uses to find `next`).
bool ts_iterator_result_done(TsValue* result) {
    if (!result) return true;

    void* rawObj = ts_value_get_object(result);
    if (!rawObj) rawObj = result;
    if ((uintptr_t)rawObj >= 0x1000) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* doneV = ts_object_get_property(rawObj, "done");
        if (doneV) return ts_value_to_bool(doneV);  // ToBoolean, undefined -> false
    }

    return true;
}

// Get value from an iterator result (ECMA-262 IteratorValue: Get(result,"value")).
// Same flat-object/prototype concern as ts_iterator_result_done above.
TsValue* ts_iterator_result_value(TsValue* result) {
    if (!result) {
        return ts_value_make_undefined();
    }

    void* rawObj = ts_value_get_object(result);
    if (!rawObj) rawObj = result;
    if ((uintptr_t)rawObj >= 0x1000) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* val = ts_object_get_property(rawObj, "value");
        if (val) return val;
    }

    return ts_value_make_undefined();
}

} // extern "C"

} // namespace ts
