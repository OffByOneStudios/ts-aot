#include "TsMap.h"
#include "TsHashTable.h"
#include "TsWeakMap.h"
#include "TsSet.h"
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
#include <utility>
#include <atomic>
#include <cstdio>
#include <cstdlib>

// Define TsHashTable static members (declared in TsHashTable.h)
TsValueHash TsHashTable::hasher_;
TsValueEqual TsHashTable::equal_;

void* TsMap_VTable[2] = { nullptr, nullptr };
extern "C" TsValue* ts_map_get_property(void* obj, void* propName);

// === Off-by-8 receiver tripwire (permanent, zero-cost when cold) ===
// The ±8-misaligned-receiver condition that TsMap::self() corrects is believed
// VESTIGIAL: no current codegen or GC path produces it (compiler IR-verified
// clean; 0 corrections across Map/churn/lodash workloads, 2026-06-01). This
// tripwire records any recurrence so that (a) the compensating hacks — self(),
// the multi-offset 0/8/16/20/24 magic scan, and the shadow `magic` field — can
// be removed once proven cold across the full suite + GC stress, and (b) a real
// recurrence (e.g. a moving-GC forwarding regression — see GC-001) is caught
// rather than silently masked. Costs nothing unless an off-by-8 actually fires.
static std::atomic<uint64_t> g_offby8_corrections{0};
// Shared off-canonical tripwire: records any time a type tag is found at a
// non-canonical offset (the layout-uncertainty tolerance the multi-offset magic
// scans and self() exist to handle). `where` identifies the call site. Used
// both by TsMap::self() and by the classifier scans as they are collapsed to
// canonical-offset-only reads, so a sweep can prove the tolerance is dead.
extern "C" void ts_offcanon_note(const char* where, void* p) {
    uint64_t n = g_offby8_corrections.fetch_add(1, std::memory_order_relaxed) + 1;
    if (const char* path = getenv("TS_OFFBY8_LOG")) {
        if (FILE* f = fopen(path, "a")) {
            fprintf(f, "[OFFCANON] %s #%llu ptr=%p\n", where, (unsigned long long)n, p);
            fclose(f);
        }
    }
}
// Query hook (e.g. for an in-process test assertion).
extern "C" uint64_t ts_offby8_correction_count() {
    return g_offby8_corrections.load(std::memory_order_relaxed);
}

// BUG 7: correct an off-by-8 receiver. A valid C++ TsMap has its C++ vftable at
// offset 0; an off-by-8 pointer (pointing at the `vtable` member, offset 8) has
// the runtime TsMap_VTable there. That sentinel is unique to the off-by-8 case
// (no correctly-based map stores TsMap_VTable at offset 0), so it is safe to
// rebase by -8. Returns `this` unchanged for a correct receiver.
TsMap* TsMap::self() {
    if ((uintptr_t)this >= 0x10000 && *(void**)this == (void*)TsMap_VTable) {
        ts_offcanon_note("TsMap::self", (void*)this);  // tripwire: should never fire
        return (TsMap*)((char*)this - 8);
    }
    return this;
}

TsMap* TsMap::Create() {
    void* mem = ts_alloc(sizeof(TsMap));
    TsMap* map = new(mem) TsMap();

    if (!TsMap_VTable[1]) {
        TsMap_VTable[1] = (void*)ts_map_get_property;
    }
    map->vtable = TsMap_VTable;

    return map;
}

void TsMap::InitInPlace(void* mem) {
    if (!mem) return;
    TsMap* map = new(mem) TsMap();
    if (!TsMap_VTable[1]) {
        TsMap_VTable[1] = (void*)ts_map_get_property;
    }
    map->vtable = TsMap_VTable;
}

TsMap::TsMap() {
    TsObject::magic = MAGIC;
    impl = TsHashTable::Create();
}

// ECMA-262 Map key SameValueZero: numeric keys compare by value regardless of
// int/double representation; -0 and +0 are the same key; NaN equals NaN. Only
// applied to explicit Map instances (object-property maps key on strings, so
// this never runs there). `new Map([[5,a],[5.0,b]]).size` was 2; `map.get(-0)`
// for a 0 key missed. Canonicalize an integer-valued double (incl -0) to
// NUMBER_INT; non-integers / NaN keep NUMBER_DBL.
static inline TsValue ts_map_canon_key(TsValue v) {
    if (v.type == ValueType::NUMBER_DBL) {
        double d = v.d_val;
        if (d == 0.0) { v.type = ValueType::NUMBER_INT; v.i_val = 0; }
        else if (d == (double)(int64_t)d &&
                 d >= -9007199254740992.0 && d <= 9007199254740992.0) {
            v.type = ValueType::NUMBER_INT; v.i_val = (int64_t)d;
        }
    }
    return v;
}

void TsMap::Set(TsValue key, TsValue value) {
    { TsMap* m = self(); if (m != this) { m->Set(key, value); return; } }
    if (IsExplicitMap()) key = ts_map_canon_key(key);
    // Guard: skip if impl is corrupt (GC may have collected this TsMap)
    if ((uintptr_t)impl < 0x10000) return;
    if (frozen) return;

    if (sealed || !extensible) {
        auto* ht = (TsHashTable*)impl;
        if (!ht->Has(key)) return;  // Don't add new properties
    }

    ((TsHashTable*)impl)->Set(key, value);

    // Write barrier: if the stored value is a pointer, notify the GC
    // so nursery pointers stored in old-gen hash tables are tracked.
    // Without this, minor GC Phase 3 won't fix up the pointer after
    // the nursery object is promoted.
    if (value.type == ValueType::OBJECT_PTR || value.type == ValueType::STRING_PTR ||
        value.type == ValueType::FUNCTION_PTR || value.type == ValueType::ARRAY_PTR) {
        if (value.ptr_val) {
            // The slot address is inside the hash table's bucket array.
            // We don't know the exact slot, so use the hash table pointer
            // itself as a conservative slot address for the card table.
            ts_gc_write_barrier(impl, value.ptr_val);
        }
    }
}

void TsMap::SetWithAttrs(TsValue key, TsValue value, uint8_t attrs) {
    { TsMap* m = self(); if (m != this) { m->SetWithAttrs(key, value, attrs); return; } }
    if ((uintptr_t)impl < 0x10000) return;
    if (frozen) return;
    if (sealed || !extensible) {
        auto* ht = (TsHashTable*)impl;
        if (!ht->Has(key)) return;
    }
    ((TsHashTable*)impl)->SetWithAttrs(key, value, attrs);
    if (value.type == ValueType::OBJECT_PTR || value.type == ValueType::STRING_PTR ||
        value.type == ValueType::FUNCTION_PTR || value.type == ValueType::ARRAY_PTR) {
        if (value.ptr_val) {
            ts_gc_write_barrier(impl, value.ptr_val);
        }
    }
}

// A corrupt/null backing hash table (impl < 0x10000) indicates the TsMap was
// not properly initialized or its impl field was clobbered. TsMap::Set already
// guards this; the read paths must too, or they segfault dereferencing a null
// TsHashTable (e.g. find_slot reading [impl+0x20] with impl==0).
TsValue TsMap::Get(TsValue key) {
    { TsMap* m = self(); if (m != this) return m->Get(key); }
    if (IsExplicitMap()) key = ts_map_canon_key(key);
    if ((uintptr_t)impl < 0x10000) {
        TsValue undef; undef.type = ValueType::UNDEFINED; undef.ptr_val = nullptr;
        return undef;
    }
    if (!((TsHashTable*)impl)->looks_valid()) {
        // Residual GC-staleness diagnostic: a corrupt impl means this TsMap (or
        // its holder) is stale. Print whether `this` is a valid TsMap (magic@16
        // == MAPS) vs a garbage object, plus impl, to name the source. Bounded.
        if (getenv("TS_MAP_CORRUPT_TRACE")) {
            static int n = 0;
            if (n++ < 20) {
                fprintf(stderr, "[TsMap] CORRUPT-GET this=%p magic@16=0x%08X impl=%p self=%p\n",
                        (void*)this, *(uint32_t*)((char*)this + 16), impl, (void*)self());
                fflush(stderr);
            }
        }
        TsValue undef; undef.type = ValueType::UNDEFINED; undef.ptr_val = nullptr;
        return undef;
    }
    return ((TsHashTable*)impl)->Get(key);
}

bool TsMap::Has(TsValue key) {
    { TsMap* m = self(); if (m != this) return m->Has(key); }
    if (IsExplicitMap()) key = ts_map_canon_key(key);
    if ((uintptr_t)impl < 0x10000) return false;
    return ((TsHashTable*)impl)->Has(key);
}

bool TsMap::Delete(TsValue key) {
    { TsMap* m = self(); if (m != this) return m->Delete(key); }
    if (IsExplicitMap()) key = ts_map_canon_key(key);
    if (frozen || sealed) return false;
    if ((uintptr_t)impl < 0x10000) return false;
    return ((TsHashTable*)impl)->Delete(key);
}

void TsMap::Clear() {
    { TsMap* m = self(); if (m != this) { m->Clear(); return; } }
    if (frozen || sealed) return;
    if ((uintptr_t)impl < 0x10000) return;
    ((TsHashTable*)impl)->Clear();
}

int64_t TsMap::Size() {
    { TsMap* m = self(); if (m != this) return m->Size(); }
    if ((uintptr_t)impl < 0x10000) return 0;
    return static_cast<int64_t>(((TsHashTable*)impl)->Size());
}

void* TsMap::GetKeys() {
    { TsMap* m = self(); if (m != this) return m->GetKeys(); }
    if ((uintptr_t)impl < 0x10000) return TsArray::Create(0);
    auto* ht = (TsHashTable*)impl;
    TsArray* keys = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        keys->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
    });
    return keys;
}

void* TsMap::GetEnumerableKeys() {
    { TsMap* m = self(); if (m != this) return m->GetEnumerableKeys(); }
    if ((uintptr_t)impl < 0x10000) return TsArray::Create(0);
    auto* ht = (TsHashTable*)impl;
    TsArray* keys = TsArray::Create(ht->Size());
    ht->ForEachEnumerable([&](const TsValue& key, const TsValue& val) {
        // Filter out synthetic __getter_/__setter_ keys
        if (key.type == ValueType::STRING_PTR && key.ptr_val) {
            const char* s = ((TsString*)key.ptr_val)->ToUtf8();
            if (s && (strncmp(s, "__getter_", 9) == 0 || strncmp(s, "__setter_", 9) == 0)) {
                return;
            }
        }
        keys->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
    });
    return keys;
}

uint8_t TsMap::GetPropertyAttrs(TsValue key) {
    { TsMap* m = self(); if (m != this) return m->GetPropertyAttrs(key); }
    if ((uintptr_t)impl < 0x10000) return 0;
    return ((TsHashTable*)impl)->GetAttrs(key);
}

void TsMap::SetPropertyAttrs(TsValue key, uint8_t attrs) {
    { TsMap* m = self(); if (m != this) { m->SetPropertyAttrs(key, attrs); return; } }
    if ((uintptr_t)impl < 0x10000) return;
    ((TsHashTable*)impl)->SetAttrs(key, attrs);
}

void* TsMap::GetValues() {
    { TsMap* m = self(); if (m != this) return m->GetValues(); }
    if ((uintptr_t)impl < 0x10000) return TsArray::Create(0);
    auto* ht = (TsHashTable*)impl;
    TsArray* values = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        values->Push((int64_t)(uintptr_t)nanbox_from_tagged(val));
    });
    return values;
}

void* TsMap::GetEntries() {
    { TsMap* m = self(); if (m != this) return m->GetEntries(); }
    if ((uintptr_t)impl < 0x10000) return TsArray::Create(0);
    auto* ht = (TsHashTable*)impl;
    TsArray* entries = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        TsArray* entry = TsArray::Create(2);
        entry->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
        entry->Push((int64_t)(uintptr_t)nanbox_from_tagged(val));
        entries->Push((int64_t)entry);
    });
    return entries;
}

void TsMap::ForEach(void* callback, void* thisArg) {
    if (!callback) return;
    TsValue* cbVal = (TsValue*)callback;
    // Per ECMA-262 23.1.3.6 Map.prototype.forEach: invoke callback with
    // `this` = thisArg if provided, else undefined.
    TsValue* thisVal = thisArg ? (TsValue*)thisArg : ts_value_make_undefined();

    auto* ht = (TsHashTable*)impl;
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        TsValue* v = nanbox_from_tagged(val);
        TsValue* k = nanbox_from_tagged(key);
        TsValue* m = ts_value_make_object(this);
        ts_call_with_this_3(cbVal, thisVal, v, k, m);
    });
}

TsMap* TsMap::CopyExcluding(std::vector<TsString*>& excluded) {
    TsMap* dest = TsMap::Create();
    auto* ht = (TsHashTable*)impl;

    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        if (key.type != ValueType::STRING_PTR) {
            dest->Set(key, val);
            return;
        }

        TsString* sKey = (TsString*)key.ptr_val;
        bool found = false;
        for (auto ex : excluded) {
            if (std::strcmp(sKey->ToUtf8(), ex->ToUtf8()) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            dest->Set(key, val);
        }
    });
    return dest;
}

bool TsMap::WouldCreateCycle(TsMap* proto) const {
    TsMap* current = proto;
    while (current != nullptr) {
        if (current == this) {
            return true;
        }
        current = current->prototype;
    }
    return false;
}

extern "C" {

// Debug hook from TsObject.cpp: TsMap* backing lodash's module object.
extern void* g_debug_lodash_module_map;

void* ts_map_create() {
    return TsMap::Create();
}

void ts_map_init_inplace(void* mem) {
    TsMap::InitInPlace(mem);
}

// Per ECMA-262 24.1.1.1 Map ( [ iterable ] ): create a Map, then iterate
// the iterable expecting [key, value] pairs and call map.set(k, v) for each.
// Currently handles TsArray-of-TsArrays iterables (the common test262 case).
void* ts_map_create_from_iterable(TsValue* iterable) {
    extern void* ts_map_create_explicit();
    extern void* ts_error_create_typed(const char* type, const char* message);
    void* map = ts_map_create_explicit();
    if (!iterable) return map;
    uint64_t nb = (uint64_t)(uintptr_t)iterable;
    if (nb <= NANBOX_UNDEFINED) return map;  // null / undefined → empty map (no throw)
    // A non-pointer primitive (number/boolean) is not iterable: new Map(5) must
    // throw a TypeError (ECMA-262 24.1.1.1 GetIterator), not silently return empty.
    if (!nanbox_is_ptr(nb)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "object is not iterable (cannot read property Symbol(Symbol.iterator))"));
        return map;  // unreachable
    }
    void* raw = nanbox_to_ptr(nb);
    if (!raw) return map;
    void* unboxed = ts_value_get_object(iterable);
    if (unboxed) raw = unboxed;
    uint32_t magic = *(uint32_t*)raw;
    if (magic == 0x41525259) {  // ARRY
        TsArray* arr = (TsArray*)raw;
        int64_t len = arr->Length();
        TsMap* m = (TsMap*)map;
        for (int64_t i = 0; i < len; i++) {
            int64_t elem = arr->GetUnchecked((size_t)i);
            // Each element should be a 2-element array [key, value]
            uint64_t enb = (uint64_t)elem;
            if (!nanbox_is_ptr(enb)) continue;
            void* eraw = nanbox_to_ptr(enb);
            if (!eraw) continue;
            uint32_t emagic = *(uint32_t*)eraw;
            if (emagic != 0x41525259) continue;
            TsArray* pair = (TsArray*)eraw;
            if (pair->Length() < 2) continue;
            int64_t k = pair->GetUnchecked(0);
            int64_t v = pair->GetUnchecked(1);
            TsValue kv = nanbox_to_tagged((TsValue*)(uintptr_t)k);
            TsValue vv = nanbox_to_tagged((TsValue*)(uintptr_t)v);
            m->Set(kv, vv);
        }
    }
    return map;
}

void* ts_map_create_explicit() {
    TsMap* map = TsMap::Create();
    map->SetExplicitMap(true);
    // Pre-size explicit Maps (new Map()) to 64 to reduce rehashing
    if (map->impl) {
        ((TsHashTable*)map->impl)->Resize(64);
    }
    return map;
}

void ts_map_set_cstr(void* map, const char* key, void* value) {
    if (!map || !key) return;
    TsMap* tsMap = (TsMap*)map;
    TsValue keyVal;
    keyVal.type = ValueType::STRING_PTR;
    keyVal.ptr_val = TsString::Create(key);
    TsValue valVal;
    valVal.type = ValueType::OBJECT_PTR;
    valVal.ptr_val = value;
    tsMap->Set(keyVal, valVal);
}

void ts_map_set_cstr_string(void* map, const char* key, void* stringValue) {
    if (!map || !key) return;
    TsMap* tsMap = (TsMap*)map;
    TsValue keyVal;
    keyVal.type = ValueType::STRING_PTR;
    keyVal.ptr_val = TsString::Create(key);
    TsValue valVal;
    valVal.type = ValueType::STRING_PTR;
    valVal.ptr_val = stringValue;
    tsMap->Set(keyVal, valVal);
}

void ts_map_set_v(void* map, TsValue key, TsValue value) {
    if (!map) return;
    if (g_debug_lodash_module_map && map == g_debug_lodash_module_map && key.type == ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)key.ptr_val;
        const char* keyUtf8 = keyStr ? keyStr->ToUtf8() : nullptr;
        if (keyUtf8 && std::strcmp(keyUtf8, "exports") == 0) {
            std::printf("[ts_map_set_v] module.exports write: value.type=%d value.ptr=%p\n",
                        (int)value.type, value.ptr_val);
        }
    }
    ((TsMap*)map)->Set(key, value);
}

TsValue ts_map_get_v(void* map, TsValue key) {
    if (!map) {
        TsValue undef;
        undef.type = ValueType::UNDEFINED;
        undef.ptr_val = nullptr;
        return undef;
    }
    return ((TsMap*)map)->Get(key);
}

bool ts_map_has_v(void* map, TsValue key) {
    if (!map) return false;
    return ((TsMap*)map)->Has(key);
}

bool ts_map_delete_v(void* map, TsValue key) {
    if (!map) return false;
    return ((TsMap*)map)->Delete(key);
}

bool ts_map_has(void* map, TsValue* key) {
    if (!map || !key) return false;
    TsValue vk = nanbox_to_tagged(key);
    return ((TsMap*)map)->Has(vk);
}

bool ts_map_delete(void* map, TsValue* key) {
    if (!map || !key) return false;
    TsValue vk = nanbox_to_tagged(key);
    return ((TsMap*)map)->Delete(vk);
}

void ts_map_clear(void* map) {
    if (!map) return;
    ((TsMap*)map)->Clear();
}

int64_t ts_map_size(void* map) {
    if (!map) return 0;
    return ((TsMap*)map)->Size();
}

extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

// Receiver guard for Map.prototype.{keys,values,entries}: a non-string-keyed
// nanbox primitive (false/1/''/undefined/null) or a non-Map pointer must throw
// TypeError, not crash in Get*(). Accepts ANY TsMap (real Map AND plain-object
// backing) because Object.keys/values/entries share these on plain objects, so
// we must NOT require IsExplicitMap here. ECMA-262 24.1.3.{4,8,10}.
static bool ts_map_receiver_is_map(void* map) {
    if (!map) return false;
    uint64_t nb = (uint64_t)(uintptr_t)map;
    if (nb <= NANBOX_UNDEFINED ||
        (!nanbox_is_ptr(nb) && (nb & 0xFFFF000000000000ULL) != 0)) {
        return false;  // nanbox primitive: undefined/null/bool/number
    }
    return *(uint32_t*)((char*)map + 16) == TsMap::MAGIC;  // string/etc -> wrong magic
}

void* ts_map_keys(void* map) {
    if (!map) return nullptr;
    if (!ts_map_receiver_is_map(map)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.keys called on incompatible receiver"));
        return nullptr;
    }
    return ((TsMap*)map)->GetKeys();
}

// Defined in TsObject.cpp.
extern "C" int ts_is_user_symbol_storage_key(const char* k);
extern "C" void* ts_value_get_string(TsValue* v);

// Partition an object's enumerable own property keys into string keys
// (symbolsOnly=false) and user-Symbol storage keys (symbolsOnly=true). User
// symbols are stored under "\x01@@sym\x01<index>" marker strings; Object.keys /
// for-in must exclude them, Object.getOwnPropertySymbols collects them.
static void* map_keys_filtered(void* map, bool symbolsOnly, bool allKeys) {
    if (!map) return nullptr;
    // Object.keys/for-in use ENUMERABLE keys; Object.getOwnPropertySymbols
    // returns ALL own symbols regardless of enumerability (ECMA-262 §20.1.2.10).
    TsArray* all = (TsArray*)(allKeys ? ((TsMap*)map)->GetKeys()
                                      : ((TsMap*)map)->GetEnumerableKeys());
    if (!all) return all;
    TsArray* out = TsArray::Create(0);
    for (int64_t i = 0; i < all->Length(); i++) {
        int64_t boxed = all->Get(i);
        void* sp = ts_value_get_string((TsValue*)(intptr_t)boxed);
        const char* kc = sp ? ((TsString*)sp)->ToUtf8() : nullptr;
        // Hidden internal slots ([[NumberData]]/[[StringData]]/[[BooleanData]]
        // on primitive wrapper objects) must never appear in Object.keys /
        // for-in / getOwnPropertySymbols.
        if (kc && (!strcmp(kc, "__NumberData") || !strcmp(kc, "__StringData") ||
                   !strcmp(kc, "__BooleanData"))) continue;
        bool isSym = kc && ts_is_user_symbol_storage_key(kc) != 0;
        // Any other '\x01'-prefixed key is an implementation-internal storage
        // key (e.g. "\x01#m" private-method slots) — invisible to all key
        // enumeration. Symbol storage keys ("\x01@@sym\x01...") are handled by
        // the isSym partition above.
        if (kc && kc[0] == '\x01' && !isSym) continue;
        if (isSym != symbolsOnly) continue;
        out->Push(boxed);
    }
    return out;
}

// ALL own string keys (enumerable + non-enumerable), excluding user-Symbol
// storage keys and internal '\x01' slots — the Object.getOwnPropertyNames set.
void* ts_map_own_string_keys(void* map) {
    return map_keys_filtered(map, false, true);
}

void* ts_map_enumerable_keys(void* map) {
    return map_keys_filtered(map, false, false);
}

// Own user-Symbol storage keys (as strings) of an object-backing map.
void* ts_map_symbol_keys(void* map) {
    return map_keys_filtered(map, true, true);
}

void* ts_map_values(void* map) {
    if (!map) return nullptr;
    if (!ts_map_receiver_is_map(map)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.values called on incompatible receiver"));
        return nullptr;
    }
    return ((TsMap*)map)->GetValues();
}

void* ts_map_entries(void* map) {
    if (!map) return nullptr;
    if (!ts_map_receiver_is_map(map)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.entries called on incompatible receiver"));
        return nullptr;
    }
    return ((TsMap*)map)->GetEntries();
}

// Forward declarations for iterator creation (defined later)
static TsValue* ts_create_iterator(TsArray* items);
TsMap* getMapIteratorPrototype();
static TsValue* ts_create_iterator_with_proto(TsArray* items, TsMap* proto);

// Iterator-returning versions (return TsMap-based iterator with
// MapIteratorPrototype so toStringTag reads "Map Iterator" per spec).
void* ts_map_keys_iter(void* map) {
    if (!map) return nullptr;
    TsArray* keys = (TsArray*)((TsMap*)map)->GetKeys();
    return (void*)ts_create_iterator_with_proto(keys, getMapIteratorPrototype());
}

void* ts_map_values_iter(void* map) {
    if (!map) return nullptr;
    TsArray* values = (TsArray*)((TsMap*)map)->GetValues();
    return (void*)ts_create_iterator_with_proto(values, getMapIteratorPrototype());
}

void* ts_map_entries_iter(void* map) {
    if (!map) return nullptr;
    TsArray* entries = (TsArray*)((TsMap*)map)->GetEntries();
    return (void*)ts_create_iterator_with_proto(entries, getMapIteratorPrototype());
}

// Public: wrap a TsArray of items in a basic forward iterator (for
// RegExp.prototype[@@matchAll], which pre-materializes its match results).
extern "C" TsValue* ts_create_array_iterator_pub(void* items) {
    if (!items) return nullptr;
    return ts_create_iterator((TsArray*)items);
}

// Collection brand for receiver validation. Map/Set/WeakMap/WeakSet share the
// TsMap/TsSet impl but a method from one collection must throw TypeError when
// called with a receiver of a different collection (ECMA-262 brand check).
//   Map/Set/WeakMap/WeakSet : strict single-brand (used by the prototype-method
//                             dynamic-dispatch sites in TsGlobals.cpp).
//   MapLike                 : permissive Map|Set|WeakMap|WeakSet — the legacy
//                             default (matches the pre-brand-check behavior) for
//                             the shared wrappers that several non-prototype
//                             dispatch paths route through:
//                               * the compiler's *typed* Map/Set dispatch
//                                 (ts_map_has/delete/clear_wrapper);
//                               * the fast TsMap-vtable property accessor
//                                 ts_map_get_property, which is also the vtable
//                                 of WeakMap instances (so weakmap.get(k) lands
//                                 here with a WEAKMAP_MAGIC receiver).
//                             These paths only ever see a same-type receiver in
//                             practice; the cross-receiver spec check is done by
//                             the brand-explicit entry points below. Keeping
//                             MapLike fully permissive avoids regressing them.
enum class CollBrand { Map, Set, WeakMap, WeakSet, MapLike };

// Forward decl: defined later in this TU. The brand selects which magic the
// receiver must carry; the underlying ops are shared and unchanged.
static void* requireMapData(void* context, const char* methodName,
                            CollBrand brand = CollBrand::MapLike);

// Forward decls for TypeError throw helpers (defined later in this TU
// at line ~415).
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

// Helper: returns true iff val is a callable function/closure.
static bool ts_is_callable_map(void* val) {
    return ts_is_callable(val);  // canonical IsCallable (TsObject.cpp)
}

void ts_map_forEach(void* map, void* callback, void* thisArg) {
    void* rawCtx = requireMapData(map, "forEach");
    if (!rawCtx) return;
    if (!ts_is_callable_map(callback)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.forEach callback must be callable"));
        return;
    }
    ((TsMap*)rawCtx)->ForEach(callback, thisArg);
}

void* ts_map_copy_excluding_v2(void* obj, void* excluded_keys_array) {
    TsMap* map = (TsMap*)obj;
    TsArray* excluded = (TsArray*)excluded_keys_array;

    std::vector<TsString*> excluded_vec;
    for (int i = 0; i < excluded->Length(); i++) {
        TsValue decoded = nanbox_to_tagged((TsValue*)excluded->Get(i));
        if (decoded.type == ValueType::STRING_PTR && decoded.ptr_val) {
            excluded_vec.push_back((TsString*)decoded.ptr_val);
        }
    }

    return map->CopyExcluding(excluded_vec);
}

// Validate that context points to a TsMap (has [[MapData]] internal slot).
// Per ES spec, Map methods must throw TypeError if `this` is not a Map.
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

static void* requireMapData(void* context, const char* methodName, CollBrand brand) {
    if (!context) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Method Map.prototype.get called on incompatible receiver"));
        return nullptr;
    }
    uint64_t nb = (uint64_t)(uintptr_t)context;
    // Reject NaN-boxed primitives:
    //   - Special values (null=0x02, undefined=0x0A, true=0x06, false=0x04)
    //   - Numbers/strings (top 16 bits set)
    if (nb <= NANBOX_UNDEFINED ||
        (!nanbox_is_ptr(nb) && (nb & 0xFFFF000000000000ULL) != 0)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Method Map.prototype.get called on incompatible receiver"));
        return nullptr;
    }
    void* rawCtx = context;
    if (nanbox_is_ptr(nb) && nb > NANBOX_UNDEFINED) {
        rawCtx = nanbox_to_ptr(nb);
    }
    if (!rawCtx) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Method Map.prototype.get called on incompatible receiver"));
        return nullptr;
    }
    // Check for TsMap/TsSet/TsWeakMap/TsWeakSet magic at multiple offsets
    // (extends TsObject → magic at 16, 20, or 24).
    // NOTE: plain object literals {} are also TsMap internally, so we
    // additionally require IsExplicitMap() for TsMap::MAGIC matches —
    // new Map() sets that flag, plain objects do not.
    constexpr uint32_t WEAKMAP_MAGIC = 0x574D4150; // "WMAP"
    constexpr uint32_t WEAKSET_MAGIC = 0x57534554; // "WSET"
    // Canonical: all of Map/Set/WeakMap/WeakSet set TsObject::magic at offset 16
    // (verified in their ctors). The former m16||m20||m24 scan was off-by-N
    // misalignment tolerance, now proven vestigial.
    uint32_t m16 = *(uint32_t*)((char*)rawCtx + 16);
    bool hasMapMagic = (m16 == TsMap::MAGIC);
    bool hasSetMagic = (m16 == TsSet::MAGIC);
    bool hasWeakMapMagic = (m16 == WEAKMAP_MAGIC);
    bool hasWeakSetMagic = (m16 == WEAKSET_MAGIC);
    // Tripwire: would the dropped off-canonical (20/24) tolerance have matched?
    if (!hasMapMagic && !hasSetMagic && !hasWeakMapMagic && !hasWeakSetMagic) {
        uint32_t m20 = *(uint32_t*)((char*)rawCtx + 20);
        uint32_t m24 = *(uint32_t*)((char*)rawCtx + 24);
        if (m20 == TsMap::MAGIC || m24 == TsMap::MAGIC || m20 == TsSet::MAGIC || m24 == TsSet::MAGIC ||
            m20 == WEAKMAP_MAGIC || m24 == WEAKMAP_MAGIC || m20 == WEAKSET_MAGIC || m24 == WEAKSET_MAGIC)
            ts_offcanon_note("requireMapData", rawCtx);
    }
    // Brand check: the receiver must carry the magic of THIS method's own
    // collection. A method from one collection (e.g. Map.prototype.get) called
    // on a sibling (e.g. a Set) must throw TypeError per spec. Plain object
    // literals are also TsMap::MAGIC, so the Map brand additionally requires
    // IsExplicitMap() to exclude them.
    bool isValid = false;
    switch (brand) {
        case CollBrand::Map:
            isValid = hasMapMagic && ((TsMap*)rawCtx)->IsExplicitMap();
            break;
        case CollBrand::Set:
            isValid = hasSetMagic;
            break;
        case CollBrand::WeakMap:
            isValid = hasWeakMapMagic;
            break;
        case CollBrand::WeakSet:
            isValid = hasWeakSetMagic;
            break;
        case CollBrand::MapLike:
            // Permissive: any explicit collection (Set/WeakMap/WeakSet) OR an
            // explicit Map — the legacy pre-brand-check behavior for the shared
            // dispatch paths (typed compiler path + the TsMap-vtable property
            // accessor that WeakMap instances also use). Cross-receiver brand
            // enforcement happens in the brand-explicit entry points.
            isValid = hasSetMagic || hasWeakMapMagic || hasWeakSetMagic ||
                      (hasMapMagic && ((TsMap*)rawCtx)->IsExplicitMap());
            break;
    }
    if (!isValid) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Method Map.prototype.get called on incompatible receiver"));
        return nullptr;
    }
    return rawCtx;
}

TsValue* ts_map_set_wrapper(void* context, TsValue* key, TsValue* value) {
    void* rawCtx = requireMapData(context, "set");
    if (!rawCtx) return ts_value_make_undefined();
    TsValue keyDecoded = nanbox_to_tagged(key);
    TsValue valDecoded = nanbox_to_tagged(value);
    uint64_t hash = (uint64_t)keyDecoded.i_val;
    __ts_map_set_at(rawCtx, hash, (uint8_t)keyDecoded.type, keyDecoded.i_val, (uint8_t)valDecoded.type, valDecoded.i_val);
    return ts_value_make_undefined();
}

TsValue* ts_map_get_wrapper(void* context, TsValue* key) {
    void* rawCtx = requireMapData(context, "get");
    if (!rawCtx) return ts_value_make_undefined();
    uint64_t hash = (uint64_t)(uintptr_t)key;
    TsValue keyTV = nanbox_to_tagged(key);
    int64_t bucket = __ts_map_find_bucket(rawCtx, hash, (uint8_t)keyTV.type, keyTV.i_val);
    if (bucket < 0) {
        return ts_value_make_undefined();
    }
    uint8_t result_type;
    int64_t result_val;
    __ts_map_get_value_at(rawCtx, bucket, &result_type, &result_val);
    TsValue result;
    result.type = (ValueType)result_type;
    result.i_val = result_val;
    return nanbox_from_tagged(result);
}

// Fast path for Map.get() — bypasses nanbox decode, magic checks, prototype chain,
// and numeric-to-string coercion. Used by the compiler for typed Map access.
TsValue* ts_map_get_fast(void* map, TsValue* key) {
    TsValue keyTV = nanbox_to_tagged(key);
    TsHashTable* ht = (TsHashTable*)((TsMap*)map)->impl;
    TsValue result = ht->Get(keyTV);
    if (result.type == ValueType::UNDEFINED) {
        return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    }
    return nanbox_from_tagged(result);
}

// Fast path for Map.set() — bypasses nanbox decode into __ts_map_set_at's
// magic check, setter check, and NaN-box unboxing of the map parameter.
extern "C" bool ts_can_be_held_weakly(TsValue* key);  // defined below

TsValue* ts_map_set_fast(void* map, TsValue* key, TsValue* value) {
    // A WeakMap receiver (WMAP magic at offset 16) requires a weakly-holdable key
    // (Object or non-registered Symbol), else TypeError. Checked here so BOTH the
    // compiler-direct lowering and the branded wrapper are covered.
    if (map && (uintptr_t)map >= 0x1000 &&
        *(uint32_t*)((char*)map + 16) == 0x574D4150 /* WEAKMAP_MAGIC "WMAP" */ &&
        !ts_can_be_held_weakly(key)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Invalid value used as weak map key"));
        return ts_value_make_undefined();
    }
    TsValue keyTV = nanbox_to_tagged(key);
    TsValue valTV = nanbox_to_tagged(value);
    ((TsMap*)map)->Set(keyTV, valTV);
    return ts_value_make_object(map);   // Map/WeakMap.prototype.set returns the receiver
}

// Fast path for Map.has()
TsValue* ts_map_has_fast(void* map, TsValue* key) {
    TsValue keyTV = nanbox_to_tagged(key);
    TsHashTable* ht = (TsHashTable*)((TsMap*)map)->impl;
    return ts_value_make_bool(ht->Has(keyTV));
}

TsValue* ts_map_has_wrapper(void* context, TsValue* key) {
    void* rawCtx = requireMapData(context, "has");
    if (!rawCtx) return ts_value_make_bool(false);
    uint32_t obj_magic = ((TsObject*)rawCtx)->magic;
    if (obj_magic == TsSet::MAGIC) {
        return ts_value_make_bool(ts_set_has(rawCtx, key));
    }
    return ts_value_make_bool(ts_map_has(rawCtx, key));
}

TsValue* ts_map_delete_wrapper(void* context, TsValue* key) {
    void* rawCtx = requireMapData(context, "delete");
    if (!rawCtx) return ts_value_make_bool(false);
    uint32_t obj_magic = ((TsObject*)rawCtx)->magic;
    if (obj_magic == TsSet::MAGIC) {
        return ts_value_make_bool(ts_set_delete(rawCtx, key));
    }
    return ts_value_make_bool(ts_map_delete(rawCtx, key));
}

TsValue* ts_map_clear_wrapper(void* context) {
    void* rawCtx = requireMapData(context, "clear");
    if (!rawCtx) return ts_value_make_undefined();
    uint32_t obj_magic = ((TsObject*)rawCtx)->magic;
    if (obj_magic == TsSet::MAGIC) {
        ts_set_clear(rawCtx);
        return ts_value_make_undefined();
    }
    ts_map_clear(rawCtx);
    return ts_value_make_undefined();
}

TsValue* ts_map_size_wrapper(void* context) {
    void* rawCtx = requireMapData(context, "size");
    if (!rawCtx) return ts_value_make_int(0);
    uint32_t obj_magic = ((TsObject*)rawCtx)->magic;
    if (obj_magic == TsSet::MAGIC) {
        return ts_value_make_int(ts_set_size(rawCtx));
    }
    return ts_value_make_int(ts_map_size(rawCtx));
}

// Map.prototype.getOrInsert ( key, value ) — TC39 upsert proposal.
//   1-2. RequireInternalSlot([[MapData]]).
//   3.   key = CanonicalizeKeyedCollectionKey(key).
//   4.   if an entry with SameValue key exists, return its value.
//   5-6. else append {key, value} and return value.
// Built on the existing has/get/set wrappers so key canonicalization and
// SameValueZero semantics are identical to the rest of Map.prototype.
TsValue* ts_map_getOrInsert_wrapper(void* context, TsValue* key, TsValue* value) {
    void* rawCtx = requireMapData(context, "getOrInsert");
    if (!rawCtx) return ts_value_make_undefined();
    if (ts_value_get_bool(ts_map_has_wrapper(rawCtx, key))) {
        return ts_map_get_wrapper(rawCtx, key);
    }
    ts_map_set_wrapper(rawCtx, key, value);
    return value;
}

// Map.prototype.getOrInsertComputed ( key, callbackfn ) — TC39 upsert proposal.
//   3.   if IsCallable(callbackfn) is false, throw TypeError (BEFORE the search,
//        so a non-callable throws even when the key is already present).
//   4-5. if key present, return existing value (callback NOT invoked).
//   6.   else value = Call(callbackfn, undefined, <key>); set; return value.
// set (not append) handles the case where the callback inserted the key itself.
TsValue* ts_map_getOrInsertComputed_wrapper(void* context, TsValue* key, TsValue* callbackfn) {
    void* rawCtx = requireMapData(context, "getOrInsertComputed");
    if (!rawCtx) return ts_value_make_undefined();
    if (!ts_is_callable_map((void*)callbackfn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.getOrInsertComputed callback is not callable"));
        return ts_value_make_undefined();
    }
    if (ts_value_get_bool(ts_map_has_wrapper(rawCtx, key))) {
        return ts_map_get_wrapper(rawCtx, key);
    }
    TsValue* args[1] = { key };
    TsValue* value = ts_function_call((TsValue*)callbackfn, 1, args);
    if (!value) value = ts_value_make_undefined();
    ts_map_set_wrapper(rawCtx, key, value);
    return value;
}

// ============================================================
// Brand-checked prototype-method entry points
// ============================================================
// Map.prototype.* and WeakMap.prototype.* are distinct collections that share
// the TsMap-backed ops, but a method from one must throw TypeError when invoked
// with a receiver of the OTHER (ECMA-262 brand check; e.g.
// `Map.prototype.get.call(new WeakMap())` or `... .call(new Set())`).
//
// The plain ts_map_*_wrapper functions above stay permissive (CollBrand::MapLike)
// because the compiler's TYPED Map/Set dispatch and the TsMap-vtable property
// accessor (also used by WeakMap instances) route through them. These branded
// entry points are what the TsGlobals.cpp prototype-method dynamic-dispatch
// lambdas call, so a wrong-collection receiver is rejected.
// They validate the requested brand, then delegate to the SAME underlying ops
// (ts_map_*) — for Map/WeakMap the receiver is never a Set, so no Set branch.

// NOTE: after brand validation, rawCtx is a verified raw TsMap/TsWeakMap pointer.
// We must NOT re-enter the permissive ts_map_*_wrapper functions: those re-run
// requireMapData(MapLike) which requires Map|Set magic and would WRONGLY reject a
// WeakMap (WEAKMAP_MAGIC). Instead delegate to the brandless raw ops, which call
// ((TsMap*)rawCtx)->Get/Set/Has/Delete/Clear/Size directly (WeakMap IS-A TsMap).

extern "C" TsValue* ts_map_get_wrapper_branded(void* context, TsValue* key, int brand) {
    void* rawCtx = requireMapData(context, "get", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    return ts_map_get_fast(rawCtx, key);
}

// ECMA-262 7.2.9 CanBeHeldWeakly: a WeakMap/WeakSet key must be an Object or a
// (non-registered) Symbol. Primitives (number/string/boolean/null/undefined) are
// rejected with a TypeError. Symbols are heap-backed (SYMB magic) so they pass the
// "pointer that isn't a string primitive" test; the rare registered-symbol case
// is not distinguished here.
extern "C" bool ts_can_be_held_weakly(TsValue* key) {
    uint64_t nb = nanbox_from_tsvalue_ptr(key);
    if (!nanbox_is_ptr(nb)) return false;            // number/bool/null/undefined
    void* raw = nanbox_to_ptr(nb);
    if (!raw || (uintptr_t)raw < 0x1000) return false;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 /*STRG*/ || m0 == 0x434F4E53 /*CONS*/) return false;  // string primitive
    return true;  // objects + symbols are weakly holdable
}

extern "C" TsValue* ts_map_set_wrapper_branded(void* context, TsValue* key, TsValue* value, int brand) {
    void* rawCtx = requireMapData(context, "set", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    // WeakMap CanBeHeldWeakly key validation lives in ts_map_set_fast (covers all paths).
    ts_map_set_fast(rawCtx, key, value);
    return ts_value_make_undefined();
}

extern "C" TsValue* ts_map_has_wrapper_branded(void* context, TsValue* key, int brand) {
    void* rawCtx = requireMapData(context, "has", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_map_has(rawCtx, key));
}

extern "C" TsValue* ts_map_delete_wrapper_branded(void* context, TsValue* key, int brand) {
    void* rawCtx = requireMapData(context, "delete", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_map_delete(rawCtx, key));
}

extern "C" TsValue* ts_map_clear_wrapper_branded(void* context, int brand) {
    void* rawCtx = requireMapData(context, "clear", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    ts_map_clear(rawCtx);
    return ts_value_make_undefined();
}

extern "C" TsValue* ts_map_size_wrapper_branded(void* context, int brand) {
    void* rawCtx = requireMapData(context, "size", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_int(0);
    return ts_value_make_int(ts_map_size(rawCtx));
}

extern "C" TsValue* ts_map_getOrInsert_wrapper_branded(void* context, TsValue* key, TsValue* value, int brand) {
    void* rawCtx = requireMapData(context, "getOrInsert", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    if (ts_map_has(rawCtx, key)) {
        return ts_map_get_fast(rawCtx, key);
    }
    ts_map_set_fast(rawCtx, key, value);
    return value;
}

extern "C" TsValue* ts_map_getOrInsertComputed_wrapper_branded(void* context, TsValue* key, TsValue* callbackfn, int brand) {
    void* rawCtx = requireMapData(context, "getOrInsertComputed", (CollBrand)brand);
    if (!rawCtx) return ts_value_make_undefined();
    if (!ts_is_callable_map((void*)callbackfn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Map.prototype.getOrInsertComputed callback is not callable"));
        return ts_value_make_undefined();
    }
    if (ts_map_has(rawCtx, key)) {
        return ts_map_get_fast(rawCtx, key);
    }
    TsValue* args[1] = { key };
    TsValue* value = ts_function_call((TsValue*)callbackfn, 1, args);
    if (!value) value = ts_value_make_undefined();
    ts_map_set_fast(rawCtx, key, value);
    return value;
}

// Brand integer codes shared with TsGlobals.cpp (must match CollBrand order).
//   0=Map, 1=Set, 2=WeakMap, 3=WeakSet
// (MapLike is internal-only and never passed across the TU boundary.)

// ============================================================
// Iterator Protocol - Map/Set/Array .keys()/.values()/.entries()
// ============================================================
// An iterator is a TsMap with:
//   _items: TsArray* of items
//   _index: int (current position)
//   next: native function returning {value, done}

struct IteratorState {
    TsArray* items;
    int64_t index;
};

static TsValue* ts_iterator_next(void* context, int argc, TsValue** argv) {
    IteratorState* state = (IteratorState*)context;
    if (!state || !state->items) {
        // Return {value: undefined, done: true}
        TsMap* result = TsMap::Create();
        TsValue keyDone; keyDone.type = ValueType::STRING_PTR;
        keyDone.ptr_val = TsString::Create("done");
        TsValue valTrue; valTrue.type = ValueType::BOOLEAN;
        valTrue.i_val = 1;
        result->Set(keyDone, valTrue);
        TsValue keyValue; keyValue.type = ValueType::STRING_PTR;
        keyValue.ptr_val = TsString::Create("value");
        TsValue valUndef; valUndef.type = ValueType::UNDEFINED;
        valUndef.i_val = 0;
        result->Set(keyValue, valUndef);
        return ts_value_make_object(result);
    }

    int64_t len = state->items->Length();
    TsMap* result = TsMap::Create();

    TsValue keyDone; keyDone.type = ValueType::STRING_PTR;
    keyDone.ptr_val = TsString::Create("done");
    TsValue keyValue; keyValue.type = ValueType::STRING_PTR;
    keyValue.ptr_val = TsString::Create("value");

    if (state->index >= len) {
        // Done
        TsValue valTrue; valTrue.type = ValueType::BOOLEAN;
        valTrue.i_val = 1;
        result->Set(keyDone, valTrue);
        TsValue valUndef; valUndef.type = ValueType::UNDEFINED;
        valUndef.i_val = 0;
        result->Set(keyValue, valUndef);
    } else {
        // Return current item, advance index
        TsValue valFalse; valFalse.type = ValueType::BOOLEAN;
        valFalse.i_val = 0;
        result->Set(keyDone, valFalse);

        int64_t raw = state->items->Get(state->index);
        TsValue valItem;
        valItem.type = ValueType::OBJECT_PTR;
        valItem.i_val = raw;
        result->Set(keyValue, valItem);

        state->index++;
    }

    return ts_value_make_object(result);
}

// ============================================================================
// ArrayIteratorPrototype — per ES spec, all array iterators share this object
// as their [[Prototype]]. Tests check `Object.getPrototypeOf(iter) ===
// Object.getPrototypeOf([][Symbol.iterator]())` for prototype identity.
//
// Iterators carry their state (items array + index) as hidden own properties
// under the keys "__iter_items" and "__iter_index". `.next()` on the
// prototype reads this-iterator's own props via ts_get_call_this() and
// advances the index in place.
// ============================================================================

extern "C" void* ts_get_call_this();

static TsValue* ts_array_iterator_proto_next(void* ctx, int argc, TsValue** argv);
static TsValue* ts_array_iterator_proto_iter_self(void* ctx, int argc, TsValue** argv);

// Local copy of TsGlobals.cpp's addMethod (it is static there).
static void ts_map_addMethod_local(TsMap* map, const char* name, void* nativeFn, int arity) {
    TsValue key; key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::GetInterned(name);
    TsValue* fn = ts_value_make_native_function(nativeFn, nullptr);
    TsFunction* func = (TsFunction*)fn;
    func->name = TsString::Create(name);
    func->arity = arity;
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
    TsValue val; val.type = ValueType::FUNCTION_PTR; val.ptr_val = fn;
    map->SetWithAttrs(key, val,
        TsHashTable::ATTR_WRITABLE | TsHashTable::ATTR_CONFIGURABLE);
}

// ---- Iterator Helpers (ECMA-262 27.1.4) — EAGER consumers ----------------
// Installed on %IteratorPrototype% so every iterator inherits them. The generic
// protocol functions live in namespace ts (TsPromise.h); a file-local static
// ts_iterator_next(void*,int,argv) shadows the unqualified name, so qualify with
// ts:: (mirrors TsArray.cpp's spread/destructure callers).

// `this` must be an Object (the iterator); else TypeError. Returns it boxed.
static TsValue* iterhelper_require_this() {
    void* ctx = ts_get_call_this();
    uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)ctx);
    if (!nanbox_is_ptr(nb)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Iterator.prototype method called on a non-object"));
        return nullptr;
    }
    return (TsValue*)ctx;
}
// IfAbruptCloseIterator is approximated by direct return (array-backed iterators
// have no observable .return; custom-iterator close is a later refinement).
static bool iterhelper_require_callable(TsValue* fn) {
    if (!fn || !ts_is_callable((void*)fn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Iterator helper callback is not a function"));
        return false;
    }
    return true;
}
// Generic iterator step via the object protocol (avoids the file-local
// ts_iterator_next shadow): call iter.next(), read result.done / result.value.
static TsValue* ih_iter_next(TsValue* iter) {
    void* raw = ts_value_get_object(iter); if (!raw) raw = iter;
    TsValue* nextFn = ts_object_get_property(raw, "next");
    if (!nextFn || !ts_is_callable((void*)nextFn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError", "iterator.next is not a function"));
        return nullptr;
    }
    return ts_function_call_with_this(nextFn, iter, 0, nullptr);
}
static bool ih_res_done(TsValue* res) {
    if (!res) return true;
    void* raw = ts_value_get_object(res); if (!raw) raw = res;
    TsValue* d = ts_object_get_property(raw, "done");
    return d && ts_value_to_bool(d);
}
static TsValue* ih_res_value(TsValue* res) {
    if (!res) return ts_value_make_undefined();
    void* raw = ts_value_get_object(res); if (!raw) raw = res;
    return ts_object_get_property(raw, "value");
}
static TsValue* iterhelper_call(TsValue* fn, TsValue* v, int64_t i) {
    TsValue* idx = ts_value_make_double((double)i);
    TsValue* a[2] = { v, idx };
    return ts_function_call_with_this(fn, ts_value_make_undefined(), 2, a);
}

static TsValue* iter_toArray_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsArray* out = (TsArray*)ts_array_create();
    TsValue* u = ts_value_make_undefined();
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        out->Push((int64_t)nanbox_from_tsvalue_ptr(ih_res_value(res)));
    }
    return ts_value_make_object(out);
}
static TsValue* iter_forEach_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!iterhelper_require_callable(fn)) return ts_value_make_undefined();
    TsValue* u = ts_value_make_undefined(); int64_t i = 0;
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        iterhelper_call(fn, ih_res_value(res), i++);
    }
    return ts_value_make_undefined();
}
static TsValue* iter_some_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!iterhelper_require_callable(fn)) return ts_value_make_undefined();
    TsValue* u = ts_value_make_undefined(); int64_t i = 0;
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        if (ts_value_to_bool(iterhelper_call(fn, ih_res_value(res), i++)))
            return ts_value_make_bool(true);
    }
    return ts_value_make_bool(false);
}
static TsValue* iter_every_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!iterhelper_require_callable(fn)) return ts_value_make_undefined();
    TsValue* u = ts_value_make_undefined(); int64_t i = 0;
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        if (!ts_value_to_bool(iterhelper_call(fn, ih_res_value(res), i++)))
            return ts_value_make_bool(false);
    }
    return ts_value_make_bool(true);
}
static TsValue* iter_find_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!iterhelper_require_callable(fn)) return ts_value_make_undefined();
    TsValue* u = ts_value_make_undefined(); int64_t i = 0;
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        TsValue* v = ih_res_value(res);
        if (ts_value_to_bool(iterhelper_call(fn, v, i++))) return v;
    }
    return ts_value_make_undefined();
}
static TsValue* iter_reduce_native(void* ctx, int argc, TsValue** argv) {
    TsValue* it = iterhelper_require_this(); if (!it) return ts_value_make_undefined();
    TsValue* fn = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
    if (!iterhelper_require_callable(fn)) return ts_value_make_undefined();
    TsValue* u = ts_value_make_undefined();
    TsValue* acc; int64_t i = 0;
    bool haveInit = (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]));
    if (haveInit) { acc = argv[1]; }
    else {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reduce of empty iterator with no initial value"));
            return ts_value_make_undefined();
        }
        acc = ih_res_value(res); i = 1;
    }
    while (true) {
        TsValue* res = ih_iter_next(it);
        if (!res || ih_res_done(res)) break;
        TsValue* v = ih_res_value(res);
        TsValue* a[3] = { acc, v, ts_value_make_double((double)i++) };
        acc = ts_function_call_with_this(fn, ts_value_make_undefined(), 3, a);
    }
    return acc;
}

// ECMA-262 27.1.2 %IteratorPrototype% — the root every built-in iterator
// prototype inherits from. It owns `[Symbol.iterator]() { return this }` and the
// (proposal) iterator-helper methods; the per-kind prototypes (%ArrayIteratorPrototype%
// etc.) must NOT have their own @@iterator (tests assert hasOwnProperty is false
// and that the chain reaches %IteratorPrototype%). GC-rooted/immortal-tenured.
static TsMap* g_iterator_prototype = nullptr;
TsMap* getIteratorPrototype() {
    if (!g_iterator_prototype) {
        ts_gc_push_tenure();
        TsMap* proto = TsMap::Create();
        ts_map_addMethod_local(proto, "[Symbol.iterator]",
                               (void*)ts_array_iterator_proto_iter_self, 0);
        ts_map_addMethod_local(proto, "toArray", (void*)iter_toArray_native, 0);
        ts_map_addMethod_local(proto, "forEach", (void*)iter_forEach_native, 1);
        ts_map_addMethod_local(proto, "some",    (void*)iter_some_native, 1);
        ts_map_addMethod_local(proto, "every",   (void*)iter_every_native, 1);
        ts_map_addMethod_local(proto, "find",    (void*)iter_find_native, 1);
        ts_map_addMethod_local(proto, "reduce",  (void*)iter_reduce_native, 1);
        g_iterator_prototype = proto;
        ts_gc_pop_tenure();
        ts_gc_register_root((void**)&g_iterator_prototype);
    }
    return g_iterator_prototype;
}

// Boxed accessor for TsGlobals.cpp (Iterator.prototype === %IteratorPrototype%).
extern "C" void* getIteratorPrototypeBoxed() {
    return (void*)ts_value_make_object(getIteratorPrototype());
}

// All iterator prototypes share next() — the only per-kind difference is
// @@toStringTag and identity. [Symbol.iterator] is inherited from
// %IteratorPrototype% (set as the prototype here), not installed per-kind.
static TsMap* buildIteratorPrototype(const char* tagStr) {
    TsMap* proto = TsMap::Create();
    ts_map_addMethod_local(proto, "next", (void*)ts_array_iterator_proto_next, 0);
    proto->SetPrototype(getIteratorPrototype());
    TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
    tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
    TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
    tagVal.ptr_val = TsString::Create(tagStr);
    proto->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
    return proto;
}

// These three iterator-prototype singletons live in plain C++ statics and are
// reached by iterator objects via their prototype field. A static TsMap* is
// invisible to the GC's object scan, so a nursery-allocated prototype here was
// a rooting gap: with no live iterator referencing it at GC time, the proto map
// was moved/collected and the stale static then handed a dangling pointer to
// SetPrototype (observed: iter->prototype = &interned "next" string + 0x10 ->
// crash/hang in the prototype-chain walk). Fix = immortal-tenure the singleton
// (old-gen, never moves) AND register the static as a GC root (full GC keeps it
// alive even when no iterator currently references it) — the same belt-and-
// suspenders the eager builtins use. See gc-unscanned-cpp-containers memory.
static TsMap* g_array_iterator_prototype = nullptr;
TsMap* getArrayIteratorPrototype() {
    if (!g_array_iterator_prototype) {
        ts_gc_push_tenure();
        g_array_iterator_prototype = buildIteratorPrototype("Array Iterator");
        ts_gc_pop_tenure();
        ts_gc_register_root((void**)&g_array_iterator_prototype);
    }
    return g_array_iterator_prototype;
}

static TsMap* g_map_iterator_prototype = nullptr;
TsMap* getMapIteratorPrototype() {
    if (!g_map_iterator_prototype) {
        ts_gc_push_tenure();
        g_map_iterator_prototype = buildIteratorPrototype("Map Iterator");
        ts_gc_pop_tenure();
        ts_gc_register_root((void**)&g_map_iterator_prototype);
    }
    return g_map_iterator_prototype;
}

static TsMap* g_set_iterator_prototype = nullptr;
TsMap* getSetIteratorPrototype() {
    if (!g_set_iterator_prototype) {
        ts_gc_push_tenure();
        g_set_iterator_prototype = buildIteratorPrototype("Set Iterator");
        ts_gc_pop_tenure();
        ts_gc_register_root((void**)&g_set_iterator_prototype);
    }
    return g_set_iterator_prototype;
}

static TsMap* g_string_iterator_prototype = nullptr;
TsMap* getStringIteratorPrototype() {
    if (!g_string_iterator_prototype) {
        ts_gc_push_tenure();
        g_string_iterator_prototype = buildIteratorPrototype("String Iterator");
        ts_gc_pop_tenure();
        ts_gc_register_root((void**)&g_string_iterator_prototype);
    }
    return g_string_iterator_prototype;
}

// ECMA-262 22.1.5 CreateStringIterator — iterate `str` by CODE POINT (surrogate
// pairs count as one step). We decode up-front into a TsArray of one-code-point
// strings and reuse the shared array-iterator next() (same approach as the
// compiler's string for-of path), backed by %StringIteratorPrototype%.
extern "C" void* ts_create_string_iterator(void* strPtr) {
    // Caller passes a TsString* (ToString result). CodePointAt/Length handle the
    // representation directly — mirrors the compiler's string for-of decode.
    TsString* s = (TsString*)strPtr;
    TsArray* arr = (TsArray*)ts_array_create();
    if (s) {
        int64_t len = s->Length();
        int64_t i = 0;
        while (i < len) {
            int64_t cp = s->CodePointAt(i);
            int64_t cps[1] = { cp };
            TsString* part = TsString::FromCodePoint(cps, 1);
            TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = part;
            arr->Push((int64_t)(uintptr_t)nanbox_from_tagged(v));
            i += (cp > 0xFFFF) ? 2 : 1;
        }
    }
    return (void*)ts_create_iterator_with_proto(arr, getStringIteratorPrototype());
}

// Called via prototype dispatch with `this` bound to the iterator TsMap.
static TsValue* ts_array_iterator_proto_next(void* ctx, int argc, TsValue** argv) {
    // `ctx` is typically null for prototype-dispatched methods; use
    // ts_get_call_this() to retrieve `this` (the iterator).
    if (!ctx) ctx = ts_get_call_this();
    if (!ctx) {
        // No receiver — return {done: true, value: undefined}.
        TsMap* r = TsMap::Create();
        TsValue dk; dk.type = ValueType::STRING_PTR; dk.ptr_val = TsString::Create("done");
        TsValue dv; dv.type = ValueType::BOOLEAN; dv.i_val = 1;
        r->Set(dk, dv);
        TsValue vk; vk.type = ValueType::STRING_PTR; vk.ptr_val = TsString::Create("value");
        TsValue vv; vv.type = ValueType::UNDEFINED; vv.i_val = 0;
        r->Set(vk, vv);
        return ts_value_make_object(r);
    }

    void* rawCtx = ts_value_get_object((TsValue*)ctx);
    if (!rawCtx) rawCtx = ctx;

    // Brand check (%ArrayIteratorPrototype%/%MapIteratorPrototype%/%SetIteratorPrototype%
    // .next): the receiver must be the corresponding iterator, which carries the
    // __iter_items internal slot. A non-iterator (plain object, array, primitive,
    // foreign object) must throw a TypeError instead of yielding {done:true}.
    // Reject anything that isn't a safe heap pointer before dereferencing.
    auto throw_iter_brand = []() {
        extern void* ts_error_create_typed(const char* type, const char* message);
        extern void ts_throw(TsValue* err);
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Iterator.prototype.next called on incompatible receiver"));
    };
    {
        uintptr_t p = (uintptr_t)rawCtx;
        if (p < 4096 || p > 0x00007FFFFFFFFFFFULL) { throw_iter_brand(); return ts_value_make_undefined(); }
        // The iterator is a TsMap (it carries __iter_items). A heap pointer that
        // isn't a TsMap — e.g. a string receiver `next.call('')`, where
        // ts_value_get_object returns the TsString — passed the range guard above
        // and then CRASHED when Get() dereferenced it as a TsMap. Require the TsMap
        // magic at offset 16 before the deref.
        if (*(uint32_t*)((char*)rawCtx + 16) != TsMap::MAGIC) { throw_iter_brand(); return ts_value_make_undefined(); }
    }
    TsMap* iter = (TsMap*)rawCtx;

    // Read state: items TsArray, index, kind (0=keys, 1=values, 2=entries).
    TsValue itemsKey; itemsKey.type = ValueType::STRING_PTR;
    itemsKey.ptr_val = TsString::GetInterned("__iter_items");
    TsValue itemsVal = iter->Get(itemsKey);
    if (itemsVal.type == ValueType::UNDEFINED) { throw_iter_brand(); return ts_value_make_undefined(); }
    TsArray* items = (itemsVal.type == ValueType::OBJECT_PTR || itemsVal.type == ValueType::ARRAY_PTR)
                        ? (TsArray*)itemsVal.ptr_val : nullptr;

    TsValue indexKey; indexKey.type = ValueType::STRING_PTR;
    indexKey.ptr_val = TsString::GetInterned("__iter_index");
    TsValue indexVal = iter->Get(indexKey);
    int64_t index = (indexVal.type == ValueType::NUMBER_INT) ? indexVal.i_val : 0;

    // Build {done, value} result.
    TsMap* result = TsMap::Create();
    TsValue doneKey; doneKey.type = ValueType::STRING_PTR; doneKey.ptr_val = TsString::Create("done");
    TsValue valueKey; valueKey.type = ValueType::STRING_PTR; valueKey.ptr_val = TsString::Create("value");

    int64_t len = items ? items->Length() : 0;
    if (!items || index >= len) {
        TsValue dv; dv.type = ValueType::BOOLEAN; dv.i_val = 1;
        result->Set(doneKey, dv);
        TsValue vv; vv.type = ValueType::UNDEFINED; vv.i_val = 0;
        result->Set(valueKey, vv);
    } else {
        TsValue dv; dv.type = ValueType::BOOLEAN; dv.i_val = 0;
        result->Set(doneKey, dv);

        int64_t raw = items->Get(index);
        TsValue vv;
        vv.type = ValueType::OBJECT_PTR;
        vv.i_val = raw;
        result->Set(valueKey, vv);

        // Advance index.
        TsValue newIndex; newIndex.type = ValueType::NUMBER_INT; newIndex.i_val = index + 1;
        iter->Set(indexKey, newIndex);
    }

    return ts_value_make_object(result);
}

// Iterators are themselves iterable: `[Symbol.iterator]()` returns self.
static TsValue* ts_array_iterator_proto_iter_self(void* ctx, int argc, TsValue** argv) {
    if (!ctx) ctx = ts_get_call_this();
    if (!ctx) return ts_value_make_undefined();
    return (TsValue*)ctx;
}

static TsValue* ts_create_iterator_with_proto(TsArray* items, TsMap* proto) {
    // Create iterator object as a TsMap with state as hidden own properties
    // and prototype chain set to the caller-chosen iterator prototype.
    TsMap* iter = TsMap::Create();
    iter->SetPrototype(proto);

    TsValue itemsKey; itemsKey.type = ValueType::STRING_PTR;
    itemsKey.ptr_val = TsString::GetInterned("__iter_items");
    TsValue itemsVal; itemsVal.type = ValueType::OBJECT_PTR;
    itemsVal.ptr_val = items;
    iter->Set(itemsKey, itemsVal);

    TsValue indexKey; indexKey.type = ValueType::STRING_PTR;
    indexKey.ptr_val = TsString::GetInterned("__iter_index");
    TsValue indexVal; indexVal.type = ValueType::NUMBER_INT; indexVal.i_val = 0;
    iter->Set(indexKey, indexVal);

    return ts_value_make_object(iter);
}

static TsValue* ts_create_iterator(TsArray* items) {
    return ts_create_iterator_with_proto(items, getArrayIteratorPrototype());
}

// Extern C wrapper for creating iterators (used by TsObject.cpp for Array)
void* ts_create_array_iterator(void* items) {
    return (void*)ts_create_iterator((TsArray*)items);
}

void* ts_create_map_iterator(void* items) {
    return (void*)ts_create_iterator_with_proto((TsArray*)items, getMapIteratorPrototype());
}

void* ts_create_set_iterator(void* items) {
    return (void*)ts_create_iterator_with_proto((TsArray*)items, getSetIteratorPrototype());
}

// Map method wrappers that return iterators
static TsValue* ts_map_keys_iter_wrapper(void* context, int argc, TsValue** argv) {
    // Brand-check the receiver: a primitive (Map.prototype.keys.call(5)) was cast
    // straight to TsMap* -> GetKeys() deref -> crash; a non-Map object yielded a
    // bogus iterator. requireMapData throws a TypeError on either.
    if (!requireMapData(context, "keys", CollBrand::Map)) return ts_value_make_undefined();
    TsArray* keys = (TsArray*)ts_map_keys(context);
    return ts_create_iterator_with_proto(keys, getMapIteratorPrototype());
}

static TsValue* ts_map_values_iter_wrapper(void* context, int argc, TsValue** argv) {
    if (!requireMapData(context, "values", CollBrand::Map)) return ts_value_make_undefined();
    TsArray* values = (TsArray*)ts_map_values(context);
    return ts_create_iterator_with_proto(values, getMapIteratorPrototype());
}

static TsValue* ts_map_entries_iter_wrapper(void* context, int argc, TsValue** argv) {
    if (!requireMapData(context, "entries", CollBrand::Map)) return ts_value_make_undefined();
    TsArray* entries = (TsArray*)ts_map_entries(context);
    return ts_create_iterator_with_proto(entries, getMapIteratorPrototype());
}

// Map forEach wrapper
static TsValue* ts_map_forEach_iter_wrapper(void* context, int argc, TsValue** argv) {
    if (argc < 1 || !argv[0]) return ts_value_make_undefined();
    void* thisArg = (argc >= 2) ? argv[1] : nullptr;
    ts_map_forEach(context, argv[0], thisArg);
    return ts_value_make_undefined();
}

// Helper: create a TsFunction with name, arity, and properties TsMap
static TsValue* makeMapMethod(void* funcPtr, void* ctx, const char* methodName, int arity) {
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

TsValue* ts_map_get_property(void* obj, void* propName) {
    TsMap* map = (TsMap*)obj;
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();

    // Fallback: look in the map (for object-like behavior)
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = prop;

    if (map->Has(key)) {
        TsValue val = map->Get(key);
        return nanbox_from_tagged(val);
    }

    if (strcmp(name, "get") == 0) {
        return makeMapMethod((void*)ts_map_get_wrapper, obj, "get", 1);
    } else if (strcmp(name, "set") == 0) {
        return makeMapMethod((void*)ts_map_set_wrapper, obj, "set", 2);
    } else if (strcmp(name, "has") == 0) {
        return makeMapMethod((void*)ts_map_has_wrapper, obj, "has", 1);
    } else if (strcmp(name, "delete") == 0) {
        return makeMapMethod((void*)ts_map_delete_wrapper, obj, "delete", 1);
    } else if (strcmp(name, "getOrInsert") == 0) {
        return makeMapMethod((void*)ts_map_getOrInsert_wrapper, obj, "getOrInsert", 2);
    } else if (strcmp(name, "getOrInsertComputed") == 0) {
        return makeMapMethod((void*)ts_map_getOrInsertComputed_wrapper, obj, "getOrInsertComputed", 2);
    } else if (strcmp(name, "clear") == 0) {
        return makeMapMethod((void*)ts_map_clear_wrapper, obj, "clear", 0);
    } else if (strcmp(name, "size") == 0) {
        return ts_value_make_int(ts_map_size(obj));
    } else if (strcmp(name, "keys") == 0) {
        return makeMapMethod((void*)ts_map_keys_iter_wrapper, obj, "keys", 0);
    } else if (strcmp(name, "values") == 0) {
        return makeMapMethod((void*)ts_map_values_iter_wrapper, obj, "values", 0);
    } else if (strcmp(name, "entries") == 0) {
        return makeMapMethod((void*)ts_map_entries_iter_wrapper, obj, "entries", 0);
    } else if (strcmp(name, "forEach") == 0) {
        return makeMapMethod((void*)ts_map_forEach_iter_wrapper, obj, "forEach", 1);
    }

    return ts_value_make_undefined();
}

// ============================================================
// Inline IR Helpers - Scalar-based API to avoid struct passing
// ============================================================

static TsValue __ts_value_from_scalars(uint8_t type, int64_t value) {
    TsValue v;
    v.type = (ValueType)type;
    v.i_val = value;
    return v;
}

// Find bucket index for given key, or -1 if not found
// Walks the prototype chain to find inherited properties
int64_t __ts_map_find_bucket(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val) {
    if (!map) {
        return -1;
    }

    if (!ts_is_unchecked<TsMap>(map)) {
        return -1;
    }

    TsMap* tsmap = (TsMap*)map;
    TsValue key = __ts_value_from_scalars(key_type, key_val);

    // For JavaScript object property semantics, numeric keys should be coerced to strings.
    TsValue stringKey;
    bool hasStringKey = false;
    if (key.type == ValueType::NUMBER_INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)key.i_val);
        stringKey.type = ValueType::STRING_PTR;
        stringKey.ptr_val = TsString::Create(buf);
        hasStringKey = true;
    } else if (key.type == ValueType::NUMBER_DBL) {
        char buf[64];
        double d = key.d_val;
        if (d == (int64_t)d && d >= -9007199254740991.0 && d <= 9007199254740991.0) {
            snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)d);
        } else {
            snprintf(buf, sizeof(buf), "%g", d);
        }
        stringKey.type = ValueType::STRING_PTR;
        stringKey.ptr_val = TsString::Create(buf);
        hasStringKey = true;
    }

    // Walk the prototype chain looking for the key
    TsMap* currentMap = tsmap;
    while (currentMap != nullptr) {
        TsHashTable* ht = (TsHashTable*)currentMap->impl;

        // Try original key first
        size_t idx = ht->FindIndex(key);

        // If not found and we have a string-coerced key, try that
        if (idx == TsHashTable::NOT_FOUND && hasStringKey) {
            idx = ht->FindIndex(stringKey);
        }

        if (idx != TsHashTable::NOT_FOUND) {
            // Calculate how far down the prototype chain we found it
            int64_t protoDepth = 0;
            TsMap* check = tsmap;
            while (check != currentMap) {
                protoDepth++;
                check = check->GetPrototype();
            }

            // Pack protoDepth and bucketIdx: upper 16 bits = protoDepth, lower 48 bits = bucket
            int64_t result = (protoDepth << 48) | ((int64_t)idx & 0xFFFFFFFFFFFFLL);
            return result;
        }
        currentMap = currentMap->GetPrototype();
    }

    return -1;
}

// Get value at bucket index via out-parameters
void __ts_map_get_value_at(void* map, int64_t bucket_idx, uint8_t* out_type, int64_t* out_value) {
    if (!map || bucket_idx < 0) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }

    if (!ts_is_unchecked<TsMap>(map)) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }

    // Decode prototype depth and actual bucket index
    int64_t protoDepth = (bucket_idx >> 48) & 0xFFFF;
    int64_t actualBucketIdx = bucket_idx & 0xFFFFFFFFFFFFLL;

    // Walk the prototype chain to find the right map
    TsMap* tsmap = (TsMap*)map;
    for (int64_t i = 0; i < protoDepth && tsmap; i++) {
        tsmap = tsmap->GetPrototype();
    }

    if (!tsmap) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }

    TsHashTable* ht = (TsHashTable*)tsmap->impl;
    TsValue entryKey, entryVal;
    if (!ht->GetEntryAt((size_t)actualBucketIdx, &entryKey, &entryVal)) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }

    *out_type = (uint8_t)entryVal.type;
    if (entryVal.type == ValueType::NUMBER_DBL) {
        std::memcpy(out_value, &entryVal.d_val, sizeof(double));
    } else {
        *out_value = entryVal.i_val;
    }
}

// Set value at key (insert or update)
void __ts_map_set_at(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val,
                     uint8_t val_type, int64_t val_val) {
    if (!map) return;

    // Check if this is a NaN-boxed TsValue* instead of raw TsMap*
    TsValue decoded = nanbox_to_tagged((TsValue*)map);
    if ((decoded.type == ValueType::OBJECT_PTR || decoded.type == ValueType::ARRAY_PTR) && decoded.ptr_val) {
        map = decoded.ptr_val;
    }

    if (!ts_is_unchecked<TsMap>(map)) {
        return;
    }

    // Targeted trace: watch tracked module.exports writes
    if (g_debug_lodash_module_map && map == g_debug_lodash_module_map && key_type == (uint8_t)ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)key_val;
        const char* keyUtf8 = keyStr ? keyStr->ToUtf8() : nullptr;
        if (keyUtf8 && std::strcmp(keyUtf8, "exports") == 0) {
            std::printf("[__ts_map_set_at] module.exports write: val_type=%d val_val=%p\n",
                        (int)val_type, (void*)val_val);
            std::printf("[__ts_map_set_at]   map=%p key_hash=%llx\n", map, (unsigned long long)key_hash);
        }
    }

    TsMap* tsmap = (TsMap*)map;
    TsValue key = __ts_value_from_scalars(key_type, key_val);
    TsValue val = __ts_value_from_scalars(val_type, val_val);

    // Check for setter (__setter_<propertyName>) if key is a string
    // Skip for explicit Maps (new Map()) — they don't have setters
    if (!tsmap->IsExplicitMap() && key.type == ValueType::STRING_PTR && key.ptr_val) {
        TsString* keyStr = (TsString*)key.ptr_val;
        const char* keyUtf8 = keyStr->ToUtf8();
        if (keyUtf8) {
            std::string setterKeyName = std::string("__setter_") + keyUtf8;
            TsValue setterKey;
            setterKey.type = ValueType::STRING_PTR;
            setterKey.ptr_val = TsString::GetInterned(setterKeyName.c_str());
            TsValue setterVal = tsmap->Get(setterKey);
            if (setterVal.type != ValueType::UNDEFINED) {
                TsValue* boxedObj = ts_value_make_object(map);
                TsValue* boxedVal = nanbox_from_tagged(val);
                TsValue* setterFn = nanbox_from_tagged(setterVal);
                TsValue* args[] = { boxedVal };
                ts_function_call_with_this(setterFn, boxedObj, 1, args);
                return;
            }
        }
    }

    tsmap->Set(key, val);
}

}

// ============================================================================
// TsWeakMap Implementation
// ============================================================================

TsWeakMap* TsWeakMap::Create() {
    void* mem = ts_alloc(sizeof(TsWeakMap));
    TsWeakMap* map = new(mem) TsWeakMap();

    if (!TsMap_VTable[1]) {
        TsMap_VTable[1] = (void*)ts_map_get_property;
    }
    map->vtable = TsMap_VTable;

    return map;
}

TsWeakMap::TsWeakMap() : TsMap() {
    TsObject::magic = MAGIC;
}
