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

// Define TsHashTable static members (declared in TsHashTable.h)
TsValueHash TsHashTable::hasher_;
TsValueEqual TsHashTable::equal_;

void* TsMap_VTable[2] = { nullptr, nullptr };
extern "C" TsValue* ts_map_get_property(void* obj, void* propName);

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

void TsMap::Set(TsValue key, TsValue value) {
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

TsValue TsMap::Get(TsValue key) {
    return ((TsHashTable*)impl)->Get(key);
}

bool TsMap::Has(TsValue key) {
    return ((TsHashTable*)impl)->Has(key);
}

bool TsMap::Delete(TsValue key) {
    if (frozen || sealed) return false;
    return ((TsHashTable*)impl)->Delete(key);
}

void TsMap::Clear() {
    if (frozen || sealed) return;
    ((TsHashTable*)impl)->Clear();
}

int64_t TsMap::Size() {
    return static_cast<int64_t>(((TsHashTable*)impl)->Size());
}

void* TsMap::GetKeys() {
    auto* ht = (TsHashTable*)impl;
    TsArray* keys = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        keys->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
    });
    return keys;
}

void* TsMap::GetEnumerableKeys() {
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
    return ((TsHashTable*)impl)->GetAttrs(key);
}

void TsMap::SetPropertyAttrs(TsValue key, uint8_t attrs) {
    ((TsHashTable*)impl)->SetAttrs(key, attrs);
}

void* TsMap::GetValues() {
    auto* ht = (TsHashTable*)impl;
    TsArray* values = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        values->Push((int64_t)(uintptr_t)nanbox_from_tagged(val));
    });
    return values;
}

void* TsMap::GetEntries() {
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

    auto* ht = (TsHashTable*)impl;
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        TsValue* v = nanbox_from_tagged(val);
        TsValue* k = nanbox_from_tagged(key);
        TsValue* m = ts_value_make_object(this);
        ts_call_3(cbVal, v, k, m);
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

void* ts_map_keys(void* map) {
    if (!map) return nullptr;
    return ((TsMap*)map)->GetKeys();
}

void* ts_map_enumerable_keys(void* map) {
    if (!map) return nullptr;
    return ((TsMap*)map)->GetEnumerableKeys();
}

void* ts_map_values(void* map) {
    if (!map) return nullptr;
    return ((TsMap*)map)->GetValues();
}

void* ts_map_entries(void* map) {
    if (!map) return nullptr;
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

void ts_map_forEach(void* map, void* callback, void* thisArg) {
    if (!map) return;
    ((TsMap*)map)->ForEach(callback, thisArg);
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

static void* requireMapData(void* context, const char* methodName) {
    if (!context) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Method Map.prototype.get called on incompatible receiver"));
        return nullptr;
    }
    void* rawCtx = context;
    uint64_t nb = (uint64_t)(uintptr_t)context;
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
    uint32_t m16 = *(uint32_t*)((char*)rawCtx + 16);
    uint32_t m20 = *(uint32_t*)((char*)rawCtx + 20);
    uint32_t m24 = *(uint32_t*)((char*)rawCtx + 24);
    bool hasMapMagic = (m16 == TsMap::MAGIC || m20 == TsMap::MAGIC || m24 == TsMap::MAGIC);
    bool hasSetMagic = (m16 == TsSet::MAGIC || m20 == TsSet::MAGIC || m24 == TsSet::MAGIC);
    bool hasWeakMapMagic = (m16 == WEAKMAP_MAGIC || m20 == WEAKMAP_MAGIC || m24 == WEAKMAP_MAGIC);
    bool hasWeakSetMagic = (m16 == WEAKSET_MAGIC || m20 == WEAKSET_MAGIC || m24 == WEAKSET_MAGIC);
    bool isValid = false;
    if (hasWeakMapMagic || hasWeakSetMagic || hasSetMagic) {
        // WeakMap/WeakSet/Set are explicit instances — accept directly.
        isValid = true;
    } else if (hasMapMagic) {
        // Distinguish explicit Map from plain object literal.
        isValid = ((TsMap*)rawCtx)->IsExplicitMap();
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
TsValue* ts_map_set_fast(void* map, TsValue* key, TsValue* value) {
    TsValue keyTV = nanbox_to_tagged(key);
    TsValue valTV = nanbox_to_tagged(value);
    ((TsMap*)map)->Set(keyTV, valTV);
    return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
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

// All iterator prototypes share next()/[Symbol.iterator]() bodies — the only
// per-kind difference is @@toStringTag and identity. Build the singleton
// lazily; callers pick which prototype via ts_create_iterator_with_proto.
static TsMap* buildIteratorPrototype(const char* tagStr) {
    TsMap* proto = TsMap::Create();
    ts_map_addMethod_local(proto, "next", (void*)ts_array_iterator_proto_next, 0);
    ts_map_addMethod_local(proto, "[Symbol.iterator]", (void*)ts_array_iterator_proto_iter_self, 0);
    TsValue tagKey; tagKey.type = ValueType::STRING_PTR;
    tagKey.ptr_val = TsString::GetInterned("[Symbol.toStringTag]");
    TsValue tagVal; tagVal.type = ValueType::STRING_PTR;
    tagVal.ptr_val = TsString::Create(tagStr);
    proto->SetWithAttrs(tagKey, tagVal, TsHashTable::ATTR_CONFIGURABLE);
    return proto;
}

static TsMap* g_array_iterator_prototype = nullptr;
TsMap* getArrayIteratorPrototype() {
    if (!g_array_iterator_prototype) g_array_iterator_prototype = buildIteratorPrototype("Array Iterator");
    return g_array_iterator_prototype;
}

static TsMap* g_map_iterator_prototype = nullptr;
TsMap* getMapIteratorPrototype() {
    if (!g_map_iterator_prototype) g_map_iterator_prototype = buildIteratorPrototype("Map Iterator");
    return g_map_iterator_prototype;
}

static TsMap* g_set_iterator_prototype = nullptr;
TsMap* getSetIteratorPrototype() {
    if (!g_set_iterator_prototype) g_set_iterator_prototype = buildIteratorPrototype("Set Iterator");
    return g_set_iterator_prototype;
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
    TsMap* iter = (TsMap*)rawCtx;

    // Read state: items TsArray, index, kind (0=keys, 1=values, 2=entries).
    TsValue itemsKey; itemsKey.type = ValueType::STRING_PTR;
    itemsKey.ptr_val = TsString::GetInterned("__iter_items");
    TsValue itemsVal = iter->Get(itemsKey);
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
    TsArray* keys = (TsArray*)ts_map_keys(context);
    return ts_create_iterator_with_proto(keys, getMapIteratorPrototype());
}

static TsValue* ts_map_values_iter_wrapper(void* context, int argc, TsValue** argv) {
    TsArray* values = (TsArray*)ts_map_values(context);
    return ts_create_iterator_with_proto(values, getMapIteratorPrototype());
}

static TsValue* ts_map_entries_iter_wrapper(void* context, int argc, TsValue** argv) {
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

    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
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

    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
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

    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
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
