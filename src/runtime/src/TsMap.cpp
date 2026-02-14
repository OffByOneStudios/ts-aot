#include "TsMap.h"
#include "TsHashTable.h"
#include "TsWeakMap.h"
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
    if (frozen) return;

    if (sealed || !extensible) {
        auto* ht = (TsHashTable*)impl;
        if (!ht->Has(key)) return;  // Don't add new properties
    }

    ((TsHashTable*)impl)->Set(key, value);
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

void* ts_map_values(void* map) {
    if (!map) return nullptr;
    return ((TsMap*)map)->GetValues();
}

void* ts_map_entries(void* map) {
    if (!map) return nullptr;
    return ((TsMap*)map)->GetEntries();
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

TsValue* ts_map_set_wrapper(void* context, TsValue* key, TsValue* value) {
    TsValue keyDecoded = nanbox_to_tagged(key);
    TsValue valDecoded = nanbox_to_tagged(value);
    uint64_t hash = (uint64_t)keyDecoded.i_val;
    __ts_map_set_at(context, hash, (uint8_t)keyDecoded.type, keyDecoded.i_val, (uint8_t)valDecoded.type, valDecoded.i_val);
    return ts_value_make_undefined();
}

TsValue* ts_map_get_wrapper(void* context, TsValue* key) {
    uint64_t hash = (uint64_t)(uintptr_t)key;
    TsValue keyTV = nanbox_to_tagged(key);
    int64_t bucket = __ts_map_find_bucket(context, hash, (uint8_t)keyTV.type, keyTV.i_val);
    if (bucket < 0) {
        return ts_value_make_undefined();
    }
    uint8_t result_type;
    int64_t result_val;
    __ts_map_get_value_at(context, bucket, &result_type, &result_val);
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
    return ts_value_make_bool(ts_map_has(context, key));
}

TsValue* ts_map_delete_wrapper(void* context, TsValue* key) {
    return ts_value_make_bool(ts_map_delete(context, key));
}

TsValue* ts_map_clear_wrapper(void* context) {
    ts_map_clear(context);
    return ts_value_make_undefined();
}

TsValue* ts_map_size_wrapper(void* context) {
    return ts_value_make_int(ts_map_size(context));
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
        return ts_value_make_function((void*)ts_map_get_wrapper, obj);
    } else if (strcmp(name, "set") == 0) {
        return ts_value_make_function((void*)ts_map_set_wrapper, obj);
    } else if (strcmp(name, "has") == 0) {
        return ts_value_make_function((void*)ts_map_has_wrapper, obj);
    } else if (strcmp(name, "delete") == 0) {
        return ts_value_make_function((void*)ts_map_delete_wrapper, obj);
    } else if (strcmp(name, "clear") == 0) {
        return ts_value_make_function((void*)ts_map_clear_wrapper, obj);
    } else if (strcmp(name, "size") == 0) {
        return ts_value_make_int(ts_map_size(obj));
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
