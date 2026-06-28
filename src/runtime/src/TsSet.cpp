#include "TsSet.h"
#include "TsHashTable.h"
#include "TsMap.h"
#include "TsWeakSet.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "GC.h"
#include "TsGC.h"
#include <string>
#include <cstring>
#include <new>
#include <functional>
#include <iostream>
#include <cmath>

void* TsSet_VTable[2] = { nullptr, nullptr };
extern "C" TsValue* ts_set_get_property(void* obj, void* propName);

// ECMA-262 SameValueZero for Set keys: numeric keys compare by value
// regardless of int/double representation; -0 and +0 are the same key (Set.add
// normalizes -0 to +0); NaN equals NaN. The backing hash table keys off the
// TsValue representation, so the same integer as NUMBER_INT vs NUMBER_DBL
// (e.g. `2.5+2.5`) hashed to different slots -> `new Set([5,5.0]).size` was 2,
// and `-0` did not collide with `0`. Canonicalize an integer-valued double
// (incl. -0) to NUMBER_INT; non-integers / NaN keep NUMBER_DBL. (Iteration order
// is now seq-based, so this no longer perturbs Set iteration order.)
static inline TsValue ts_set_canon_key(TsValue v) {
    if (v.type == ValueType::NUMBER_DBL) {
        double d = v.d_val;
        if (d == 0.0) {                      // +0 and -0 -> int 0
            v.type = ValueType::NUMBER_INT; v.i_val = 0;
        } else if (d == (double)(int64_t)d &&
                   d >= -9007199254740992.0 && d <= 9007199254740992.0) {
            v.type = ValueType::NUMBER_INT; v.i_val = (int64_t)d;
        }
    }
    return v;
}

TsSet* TsSet::Create() {
    void* mem = ts_alloc(sizeof(TsSet));
    TsSet* set = new(mem) TsSet();

    if (!TsSet_VTable[1]) {
        TsSet_VTable[1] = (void*)ts_set_get_property;
    }
    set->vtable = TsSet_VTable;

    return set;
}

TsSet::TsSet() {
    TsObject::magic = MAGIC;
    impl = TsHashTable::Create();
}

void TsSet::Add(TsValue value) {
    value = ts_set_canon_key(value);
    // For Set, store value as key with UNDEFINED as the map value
    TsValue undef;
    undef.type = ValueType::UNDEFINED;
    undef.i_val = 0;
    ((TsHashTable*)impl)->Set(value, undef);
}

bool TsSet::Has(TsValue value) {
    return ((TsHashTable*)impl)->Has(ts_set_canon_key(value));
}

bool TsSet::Delete(TsValue value) {
    return ((TsHashTable*)impl)->Delete(ts_set_canon_key(value));
}

void TsSet::Clear() {
    ((TsHashTable*)impl)->Clear();
}

int64_t TsSet::Size() {
    return static_cast<int64_t>(((TsHashTable*)impl)->Size());
}

void* TsSet::GetValues() {
    auto* ht = (TsHashTable*)impl;
    TsArray* values = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        values->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
    });
    return values;
}

void TsSet::ForEach(void* callback, void* thisArg) {
    if (!callback) return;
    TsValue* cbVal = (TsValue*)callback;
    // Per ECMA-262 24.2.3.5 Set.prototype.forEach: bind `this` to thisArg.
    TsValue* thisVal = thisArg ? (TsValue*)thisArg : ts_value_make_undefined();

    auto* ht = (TsHashTable*)impl;
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        TsValue* v1 = nanbox_from_tagged(key);
        TsValue* v2 = nanbox_from_tagged(key);
        TsValue* s = ts_value_make_object(this);
        ts_call_with_this_3(cbVal, thisVal, v1, v2, s);
    });
}

extern "C" {

void* ts_set_create() {
    return TsSet::Create();
}

// Per ECMA-262 24.2.1.1 Set ( [ iterable ] ): create a Set, then iterate
// the iterable and call set.add(item) for each. Used by `new Set([...])`.
// Currently only handles TsArray iterables (the common case in test262);
// general iterators (custom @@iterator) fall through to empty Set.
void* ts_set_create_from_iterable(TsValue* iterable) {
    void* set = TsSet::Create();
    if (!iterable) return set;
    uint64_t nb = (uint64_t)(uintptr_t)iterable;
    if (nb <= NANBOX_UNDEFINED) return set;  // null / undefined → empty
    if (!nanbox_is_ptr(nb)) return set;
    void* raw = nanbox_to_ptr(nb);
    if (!raw) return set;
    // Unbox if it's a TsValue wrapper
    void* unboxed = ts_value_get_object(iterable);
    if (unboxed) raw = unboxed;
    uint32_t magic = *(uint32_t*)raw;
    // Handle TsArray (most common iterable in tests)
    if (magic == 0x41525259) {  // ARRY
        TsArray* arr = (TsArray*)raw;
        int64_t len = arr->Length();
        for (int64_t i = 0; i < len; i++) {
            int64_t elem = arr->GetUnchecked((size_t)i);
            TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)elem);
            ((TsSet*)set)->Add(v);
        }
    }
    // TODO: TsString iterables (each char becomes an element)
    // TODO: Generic iterator protocol (call @@iterator + next loop)
    return set;
}

extern "C" bool ts_can_be_held_weakly(TsValue* key);  // defined in TsMap.cpp
extern "C" void* ts_error_create_typed(const char* type, const char* message);

void ts_set_add(void* set, TsValue* value) {
    if (!set) return;
    // A WeakSet receiver (WSET magic at offset 16) requires a weakly-holdable
    // value, else TypeError. Checked here so both the compiler-direct lowering
    // and the branded wrapper are covered.
    if ((uintptr_t)set >= 0x1000 &&
        *(uint32_t*)((char*)set + 16) == 0x57534554 /* WEAKSET_MAGIC "WSET" */ &&
        !ts_can_be_held_weakly(value)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Invalid value used in weak set"));
        return;
    }
    TsValue v = nanbox_to_tagged(value);
    ((TsSet*)set)->Add(v);
}

bool ts_set_has(void* set, TsValue* value) {
    if (!set) return false;
    TsValue v = nanbox_to_tagged(value);
    return ((TsSet*)set)->Has(v);
}

bool ts_set_delete(void* set, TsValue* value) {
    if (!set) return false;
    TsValue v = nanbox_to_tagged(value);
    return ((TsSet*)set)->Delete(v);
}

void ts_set_clear(void* set) {
    if (!set) return;
    ((TsSet*)set)->Clear();
}

int64_t ts_set_size(void* set) {
    if (!set) return 0;
    return ((TsSet*)set)->Size();
}

void* ts_set_values(void* set) {
    if (!set) return nullptr;
    return ((TsSet*)set)->GetValues();
}

// ECMA-262 24.2.3.6: Set.prototype.keys === Set.prototype.values
void* ts_set_keys(void* set) {
    return ts_set_values(set);
}

// ECMA-2024 Set composition methods (24.2.3.*). Minimal Set-Set
// implementations: for non-Set `other`, treat as empty (rather than
// the spec-mandated GetSetRecord/SetLike protocol, which would
// require calling JS-level .has/.keys/.size). Sufficient to convert
// link-error compile failures into runtime test outcomes; passes the
// Set-Set subset of staging/sm/Set tests at minimum.
static TsSet* setFromArg(void* other) {
    if (!other) return nullptr;
    uint64_t nb = (uint64_t)(uintptr_t)other;
    if (nb <= NANBOX_UNDEFINED) return nullptr;
    void* raw = other;
    // Unbox if NaN-boxed
    if (nanbox_is_ptr(nb)) {
        void* unboxed = ts_value_get_object((TsValue*)other);
        if (unboxed) raw = unboxed;
        else raw = nanbox_to_ptr(nb);
    } else if ((nb & 0xFFFF000000000000ULL) != 0) {
        // Primitive nanbox values (numbers/strings) — not a Set
        return nullptr;
    }
    if (!raw) return nullptr;
    // TsSet sets TsObject::magic at the canonical offset 16.
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 == TsSet::MAGIC) {
        return (TsSet*)raw;
    }
    return nullptr;
}

void* ts_set_union(void* set, void* other) {
    if (!set) return TsSet::Create();
    TsSet* a = (TsSet*)set;
    TsSet* result = TsSet::Create();
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        result->Add(v);
    }
    TsSet* b = setFromArg(other);
    if (b) {
        TsArray* bVals = (TsArray*)b->GetValues();
        int64_t bLen = bVals ? bVals->Length() : 0;
        for (int64_t i = 0; i < bLen; i++) {
            TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)bVals->Get(i));
            result->Add(v);
        }
    }
    return result;
}

void* ts_set_intersection(void* set, void* other) {
    if (!set) return TsSet::Create();
    TsSet* a = (TsSet*)set;
    TsSet* result = TsSet::Create();
    TsSet* b = setFromArg(other);
    if (!b) return result;
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        if (b->Has(v)) result->Add(v);
    }
    return result;
}

void* ts_set_difference(void* set, void* other) {
    if (!set) return TsSet::Create();
    TsSet* a = (TsSet*)set;
    TsSet* result = TsSet::Create();
    TsSet* b = setFromArg(other);
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        if (!b || !b->Has(v)) result->Add(v);
    }
    return result;
}

void* ts_set_symmetricDifference(void* set, void* other) {
    if (!set) return TsSet::Create();
    TsSet* a = (TsSet*)set;
    TsSet* result = TsSet::Create();
    TsSet* b = setFromArg(other);
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        if (!b || !b->Has(v)) result->Add(v);
    }
    if (b) {
        TsArray* bVals = (TsArray*)b->GetValues();
        int64_t bLen = bVals ? bVals->Length() : 0;
        for (int64_t i = 0; i < bLen; i++) {
            TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)bVals->Get(i));
            if (!a->Has(v)) result->Add(v);
        }
    }
    return result;
}

// NB: registry-dispatched Set methods are IR-declared with `ptr` return type
// regardless of the registry's HIRType hint, so boolean results must be
// returned as NaN-boxed TsValue* (via ts_value_make_bool), not raw `bool`.
void* ts_set_isSubsetOf(void* set, void* other) {
    if (!set) return ts_value_make_bool(true);
    TsSet* a = (TsSet*)set;
    TsSet* b = setFromArg(other);
    if (!b) return ts_value_make_bool(a->Size() == 0);
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        if (!b->Has(v)) return ts_value_make_bool(false);
    }
    return ts_value_make_bool(true);
}

void* ts_set_isSupersetOf(void* set, void* other) {
    if (!set) return ts_value_make_bool(false);
    TsSet* a = (TsSet*)set;
    TsSet* b = setFromArg(other);
    if (!b) return ts_value_make_bool(true);
    TsArray* bVals = (TsArray*)b->GetValues();
    int64_t bLen = bVals ? bVals->Length() : 0;
    for (int64_t i = 0; i < bLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)bVals->Get(i));
        if (!a->Has(v)) return ts_value_make_bool(false);
    }
    return ts_value_make_bool(true);
}

void* ts_set_isDisjointFrom(void* set, void* other) {
    if (!set) return ts_value_make_bool(true);
    TsSet* a = (TsSet*)set;
    TsSet* b = setFromArg(other);
    if (!b) return ts_value_make_bool(true);
    TsArray* aVals = (TsArray*)a->GetValues();
    int64_t aLen = aVals ? aVals->Length() : 0;
    for (int64_t i = 0; i < aLen; i++) {
        TsValue v = nanbox_to_tagged((TsValue*)(uintptr_t)aVals->Get(i));
        if (b->Has(v)) return ts_value_make_bool(false);
    }
    return ts_value_make_bool(true);
}

// Collection brand for receiver validation. Set/WeakSet share the TsSet impl,
// and a method from one must throw TypeError when called with a receiver of the
// other (ECMA-262 brand check). SetLike is the permissive default (Set|WeakSet)
// preserving the legacy shared-wrapper behavior the compiler's typed path and
// the WeakSet.prototype registration both depend on.
enum class SetBrand { Set, WeakSet, SetLike };

// Forward decl (defined below). Used by ts_set_forEach to validate
// the receiver before dereferencing as a TsSet pointer. The brand selects which
// magic the receiver must carry; the underlying ops are shared and unchanged.
static void* requireSet(void* context, const char* methodName,
                        SetBrand brand = SetBrand::SetLike);

// Forward decls for TypeError throw helpers (defined later in this TU).
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

// Helper: returns true iff val is a callable function/closure.
// Per ECMA-262, callbacks must be IsCallable; otherwise throw TypeError.
// IsCallable is the canonical ts_is_callable (TsObject.cpp), declared in
// TsRuntime.h; call sites below bind to it directly.

void ts_set_forEach(void* set, void* callback, void* thisArg) {
    void* rawCtx = requireSet(set, "forEach");
    if (!rawCtx) return;
    if (!ts_is_callable(callback)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set.prototype.forEach callback must be callable"));
        return;
    }
    ((TsSet*)rawCtx)->ForEach(callback, thisArg);
}

// Validate that ctx points to a TsSet (has [[SetData]] internal slot).
// Per ES spec, Set methods must throw TypeError if `this` is not a Set.
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

static void* requireSet(void* context, const char* methodName, SetBrand brand) {
    if (!context) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    uint64_t nb = (uint64_t)(uintptr_t)context;
    // Reject NaN-boxed primitives:
    //   - Special values (null=0x02, undefined=0x0A, true=0x06, false=0x04)
    //   - Numbers/strings (top 16 bits set)
    // Only raw pointers OR true pointer-tagged NaN-box values can be Sets.
    if (nb <= NANBOX_UNDEFINED ||
        (!nanbox_is_ptr(nb) && (nb & 0xFFFF000000000000ULL) != 0)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    void* rawCtx = context;
    if (nanbox_is_ptr(nb) && nb > NANBOX_UNDEFINED) {
        rawCtx = nanbox_to_ptr(nb);
    }
    if (!rawCtx) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    // Brand check: the receiver must carry the magic of THIS method's own
    // collection. A method from one collection (e.g. Set.prototype.has) called
    // on a sibling (e.g. a WeakSet) must throw TypeError per spec. WeakSet's
    // ctor overrides TsObject::magic to WEAKSET_MAGIC, so the two are
    // distinguishable even though TsWeakSet IS-A TsSet.
    constexpr uint32_t WEAKSET_MAGIC = 0x57534554; // "WSET"
    uint32_t m16 = *(uint32_t*)((char*)rawCtx + 16);  // canonical TsObject::magic
    bool hasSetMagic = (m16 == TsSet::MAGIC);
    bool hasWeakSetMagic = (m16 == WEAKSET_MAGIC);
    bool isValid = false;
    switch (brand) {
        case SetBrand::Set:     isValid = hasSetMagic; break;
        case SetBrand::WeakSet: isValid = hasWeakSetMagic; break;
        case SetBrand::SetLike: isValid = hasSetMagic || hasWeakSetMagic; break;
    }
    if (!isValid) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    return rawCtx;
}

TsValue* ts_set_add_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "add");
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_add(rawCtx, value);
    return ts_value_make_object(rawCtx);
}

TsValue* ts_set_has_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "has");
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_has(rawCtx, value));
}

TsValue* ts_set_delete_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "delete");
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_delete(rawCtx, value));
}

TsValue* ts_set_clear_wrapper(void* context) {
    void* rawCtx = requireSet(context, "clear");
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_clear(rawCtx);
    return ts_value_make_undefined();
}

TsValue* ts_set_size_wrapper(void* context) {
    void* rawCtx = requireSet(context, "size");
    if (!rawCtx) return ts_value_make_int(0);
    return ts_value_make_int(ts_set_size(rawCtx));
}

// ============================================================
// Brand-checked prototype-method entry points
// ============================================================
// Set.prototype.* and WeakSet.prototype.* are distinct collections sharing the
// TsSet-backed ops; a method from one must throw TypeError when invoked with a
// receiver of the OTHER (ECMA-262 brand check; e.g.
// `Set.prototype.add.call(new WeakSet())`).
//
// The plain ts_set_*_wrapper functions above stay permissive (SetBrand::SetLike
// = Set|WeakSet): the compiler's typed Set dispatch and the WeakSet.prototype
// registration both route through them. These branded entry points are what the
// TsGlobals.cpp Set/WeakSet prototype-method lambdas call, so a wrong-collection
// receiver is rejected. After validation the receiver is a verified raw TsSet/
// TsWeakSet pointer, so they delegate to the brandless raw ops (ts_set_*).
//   brand codes (must match SetBrand order with offset): 0=Set, 1=WeakSet

TsValue* ts_set_add_wrapper_branded(void* context, TsValue* value, int brand) {
    void* rawCtx = requireSet(context, "add", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    // WeakSet CanBeHeldWeakly validation lives in ts_set_add (covers all paths).
    ts_set_add(rawCtx, value);
    return ts_value_make_object(rawCtx);
}

TsValue* ts_set_has_wrapper_branded(void* context, TsValue* value, int brand) {
    void* rawCtx = requireSet(context, "has", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_has(rawCtx, value));
}

TsValue* ts_set_delete_wrapper_branded(void* context, TsValue* value, int brand) {
    void* rawCtx = requireSet(context, "delete", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_delete(rawCtx, value));
}

TsValue* ts_set_clear_wrapper_branded(void* context, int brand) {
    void* rawCtx = requireSet(context, "clear", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_clear(rawCtx);
    return ts_value_make_undefined();
}

TsValue* ts_set_size_wrapper_branded(void* context, int brand) {
    void* rawCtx = requireSet(context, "size", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_int(0);
    return ts_value_make_int(ts_set_size(rawCtx));
}

// forEach with brand check (Set brand only; WeakSet has no forEach in spec).
TsValue* ts_set_forEach_wrapper_branded(void* context, TsValue* callback, TsValue* thisArg, int brand) {
    void* rawCtx = requireSet(context, "forEach", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_forEach(rawCtx, (void*)callback, (void*)thisArg);
    return ts_value_make_undefined();
}

// Iterator entry points with brand check (Set brand). Delegate to the existing
// permissive iterator wrappers on the already-validated raw receiver.
extern TsValue* ts_set_values_iter_wrapper(void* context, int argc, TsValue** argv);
extern TsValue* ts_set_entries_iter_wrapper(void* context, int argc, TsValue** argv);
TsValue* ts_set_values_iter_wrapper_branded(void* context, int argc, TsValue** argv, int brand) {
    void* rawCtx = requireSet(context, "values", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    return ts_set_values_iter_wrapper(rawCtx, argc, argv);
}
TsValue* ts_set_entries_iter_wrapper_branded(void* context, int argc, TsValue** argv, int brand) {
    void* rawCtx = requireSet(context, "entries", (SetBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    return ts_set_entries_iter_wrapper(rawCtx, argc, argv);
}

// Iterators: values() and keys() are identical for Set; entries() yields
// [v, v] pairs per spec. All return a SetIteratorPrototype-backed iterator.
extern "C" void* ts_create_set_iterator(void* items);

// ts_set_entries — runtime symbol referenced by typed TS lowering
// (BuiltinRegistry.cpp:508). Returns a TsArray of [v, v] pair arrays so
// the typed path can iterate it directly as an array. The untyped-JS path
// goes through ts_set_entries_iter_wrapper (below) which builds a real
// iterator using SetIteratorPrototype.
extern "C" void* ts_set_entries(void* set) {
    if (!set) return ts_array_create();
    TsArray* vals = (TsArray*)((TsSet*)set)->GetValues();
    TsArray* pairs = (TsArray*)ts_array_create();
    int64_t n = vals ? vals->Length() : 0;
    for (int64_t i = 0; i < n; i++) {
        TsArray* pair = (TsArray*)ts_array_create();
        int64_t raw = vals->Get(i);
        pair->Push(raw);
        pair->Push(raw);
        pairs->Push((int64_t)(uintptr_t)pair);
    }
    return pairs;
}

TsValue* ts_set_values_iter_wrapper(void* context, int argc, TsValue** argv) {
    void* rawCtx = requireSet(context, "values");
    if (!rawCtx) return ts_value_make_undefined();
    void* items = ts_set_values(rawCtx);
    return (TsValue*)ts_create_set_iterator(items);
}

TsValue* ts_set_entries_iter_wrapper(void* context, int argc, TsValue** argv) {
    void* rawCtx = requireSet(context, "entries");
    if (!rawCtx) return ts_value_make_undefined();
    // Build [[v,v], [v,v], ...] pairs.
    TsArray* vals = (TsArray*)ts_set_values(rawCtx);
    TsArray* pairs = (TsArray*)ts_array_create();
    int64_t n = vals ? vals->Length() : 0;
    for (int64_t i = 0; i < n; i++) {
        TsArray* pair = (TsArray*)ts_array_create();
        int64_t raw = vals->Get(i);
        pair->Push(raw);
        pair->Push(raw);
        pairs->Push((int64_t)(uintptr_t)pair);
    }
    return (TsValue*)ts_create_set_iterator(pairs);
}

// Set.prototype.forEach(callback[, thisArg]). Dynamic (any-typed receiver)
// dispatch lands here; the static path uses a compiler fast path. Without
// this, `set.forEach` on an any-typed value was undefined -- lodash baseClone
// `value.forEach(...)` silently copied nothing, so _.clone(set) was empty.
TsValue* ts_set_forEach_wrapper(void* context, TsValue* callback, TsValue* thisArg) {
    void* rawCtx = requireSet(context, "forEach");
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_forEach(rawCtx, (void*)callback, (void*)thisArg);
    return ts_value_make_undefined();
}

// Helper: create a TsFunction with name, arity, and properties TsMap
static TsValue* makeSetMethod(void* funcPtr, void* ctx, const char* methodName, int arity) {
    TsValue* val = ts_value_make_function(funcPtr, ctx);
    TsFunction* func = (TsFunction*)val;
    func->name = TsString::Create(methodName);
    func->arity = arity;
    if (!func->properties) func->properties = TsMap::Create();
    TsValue lengthKey; lengthKey.type = ValueType::STRING_PTR;
    lengthKey.ptr_val = TsString::GetInterned("length");
    TsValue lengthVal; lengthVal.type = ValueType::NUMBER_INT; lengthVal.i_val = arity;
    func->properties->SetWithAttrs(lengthKey, lengthVal, TsHashTable::ATTR_CONFIGURABLE);
    TsValue nameKey; nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal; nameVal.type = ValueType::STRING_PTR; nameVal.ptr_val = func->name;
    func->properties->SetWithAttrs(nameKey, nameVal, TsHashTable::ATTR_CONFIGURABLE);
    return val;
}

TsValue* ts_set_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();

    if (strcmp(name, "add") == 0) {
        return makeSetMethod((void*)ts_set_add_wrapper, obj, "add", 1);
    } else if (strcmp(name, "has") == 0) {
        return makeSetMethod((void*)ts_set_has_wrapper, obj, "has", 1);
    } else if (strcmp(name, "delete") == 0) {
        return makeSetMethod((void*)ts_set_delete_wrapper, obj, "delete", 1);
    } else if (strcmp(name, "clear") == 0) {
        return makeSetMethod((void*)ts_set_clear_wrapper, obj, "clear", 0);
    } else if (strcmp(name, "size") == 0) {
        return ts_value_make_int(ts_set_size(obj));
    } else if (strcmp(name, "values") == 0 || strcmp(name, "keys") == 0 ||
               strcmp(name, "[Symbol.iterator]") == 0) {
        // Per spec, Set.prototype.keys === Set.prototype.values, and
        // Set.prototype[@@iterator] === Set.prototype.values.
        return makeSetMethod((void*)ts_set_values_iter_wrapper, obj, name, 0);
    } else if (strcmp(name, "entries") == 0) {
        return makeSetMethod((void*)ts_set_entries_iter_wrapper, obj, "entries", 0);
    } else if (strcmp(name, "forEach") == 0) {
        return makeSetMethod((void*)ts_set_forEach_wrapper, obj, "forEach", 1);
    } else if (strcmp(name, "constructor") == 0) {
        // Set.prototype.constructor === Set. Enables `s.constructor === Set`
        // and lodash baseClone `new set.constructor`.
        extern void* ts_get_global_Set();
        void* ctor = ts_get_global_Set();
        if (ctor) return (TsValue*)ts_value_make_object(ctor);
    }

    return ts_value_make_undefined();
}

}

// ============================================================================
// TsWeakSet Implementation
// ============================================================================

TsWeakSet* TsWeakSet::Create() {
    void* mem = ts_alloc(sizeof(TsWeakSet));
    TsWeakSet* set = new(mem) TsWeakSet();

    if (!TsSet_VTable[1]) {
        TsSet_VTable[1] = (void*)ts_set_get_property;
    }
    set->vtable = TsSet_VTable;

    return set;
}

TsWeakSet::TsWeakSet() : TsSet() {
    TsObject::magic = MAGIC;
}
