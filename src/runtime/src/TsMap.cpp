#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "GC.h"
#include <unordered_map>
#include <string>
#include <cstring>
#include <new>
#include <functional>
#include <iostream>
#include <cmath>

// Allocator
template <class T>
struct TsAllocator {
    typedef T value_type;
    TsAllocator() = default;
    template <class U> constexpr TsAllocator(const TsAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        if (n > std::size_t(-1) / sizeof(T)) throw std::bad_alloc();
        if (auto p = static_cast<T*>(ts_alloc(n * sizeof(T)))) return p;
        throw std::bad_alloc();
    }
    void deallocate(T* p, std::size_t) noexcept { }
};
template <class T, class U>
bool operator==(const TsAllocator<T>&, const TsAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const TsAllocator<T>&, const TsAllocator<U>&) { return false; }

// Hash and Equal for TsValue
struct TsValueHash {
    std::size_t operator()(const TsValue& v) const {
        switch (v.type) {
            case ValueType::UNDEFINED: return 0;
            case ValueType::NUMBER_INT: return std::hash<int64_t>{}(v.i_val);
            case ValueType::NUMBER_DBL: return std::hash<double>{}(v.d_val);
            case ValueType::BOOLEAN: return std::hash<bool>{}(v.b_val);
            case ValueType::STRING_PTR: {
                if (!v.ptr_val) return 0;
                TsString* s = (TsString*)v.ptr_val;
                const char* str = s->ToUtf8();
                if (!str) return 0;
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
            }
            case ValueType::BIGINT_PTR: {
                if (!v.ptr_val) return 0;
                TsBigInt* bi = (TsBigInt*)v.ptr_val;
                const char* str = bi->ToString();
                if (!str) return 0;
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
            }
            default: return std::hash<void*>{}(v.ptr_val);
        }
    }
};

struct TsValueEqual {
    bool operator()(const TsValue& lhs, const TsValue& rhs) const {
        if (lhs.type != rhs.type) return false;
        switch (lhs.type) {
            case ValueType::UNDEFINED: return true;
            case ValueType::NUMBER_INT: return lhs.i_val == rhs.i_val;
            case ValueType::NUMBER_DBL: {
                if (std::isnan(lhs.d_val) && std::isnan(rhs.d_val)) return true; // SameValueZero
                return lhs.d_val == rhs.d_val;
            }
            case ValueType::BOOLEAN: return lhs.b_val == rhs.b_val;
            case ValueType::STRING_PTR: {
                if (!lhs.ptr_val || !rhs.ptr_val) return lhs.ptr_val == rhs.ptr_val;
                const char* a = ((TsString*)lhs.ptr_val)->ToUtf8();
                const char* b = ((TsString*)rhs.ptr_val)->ToUtf8();
                if (!a || !b) return a == b;
                return std::strcmp(a, b) == 0;
            }
            case ValueType::BIGINT_PTR: {
                if (!lhs.ptr_val || !rhs.ptr_val) return lhs.ptr_val == rhs.ptr_val;
                const char* a = ((TsBigInt*)lhs.ptr_val)->ToString();
                const char* b = ((TsBigInt*)rhs.ptr_val)->ToString();
                if (!a || !b) return a == b;
                return std::strcmp(a, b) == 0;
            }
            default: return lhs.ptr_val == rhs.ptr_val;
        }
    }
};

using MapType = std::unordered_map<TsValue, TsValue, TsValueHash, TsValueEqual, TsAllocator<std::pair<const TsValue, TsValue>>>;

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

TsMap::TsMap() {
    TsObject::magic = MAGIC;  // Set base class magic for type detection
    void* mem = ts_alloc(sizeof(MapType));
    impl = new(mem) MapType();
}

void TsMap::Set(TsValue key, TsValue value) {
    ((MapType*)impl)->insert_or_assign(key, value);
}

TsValue TsMap::Get(TsValue key) {
    auto* map = (MapType*)impl;
    auto it = map->find(key);
    if (it != map->end()) {
        return it->second;
    }
    return TsValue();
}

bool TsMap::Has(TsValue key) {
    MapType* map = static_cast<MapType*>(impl);
    return map->find(key) != map->end();
}

bool TsMap::Delete(TsValue key) {
    MapType* map = static_cast<MapType*>(impl);
    return map->erase(key) > 0;
}

void TsMap::Clear() {
    MapType* map = static_cast<MapType*>(impl);
    map->clear();
}

int64_t TsMap::Size() {
    MapType* map = static_cast<MapType*>(impl);
    return static_cast<int64_t>(map->size());
}

void* TsMap::GetKeys() {
    MapType* map = static_cast<MapType*>(impl);
    TsArray* keys = TsArray::Create(map->size());
    for (auto const& [key, val] : *map) {
        TsValue* k = (TsValue*)ts_alloc(sizeof(TsValue));
        *k = key;
        keys->Push((int64_t)k);
    }
    return keys;
}

void* TsMap::GetValues() {
    MapType* map = static_cast<MapType*>(impl);
    TsArray* values = TsArray::Create(map->size());
    for (auto const& [key, val] : *map) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        *v = val;
        values->Push((int64_t)v);
    }
    return values;
}

void* TsMap::GetEntries() {
    MapType* map = static_cast<MapType*>(impl);
    TsArray* entries = TsArray::Create(map->size());
    for (auto const& [key, val] : *map) {
        TsArray* entry = TsArray::Create(2);
        
        TsValue* k = (TsValue*)ts_alloc(sizeof(TsValue));
        *k = key;
        entry->Push((int64_t)k);
        
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        *v = val;
        entry->Push((int64_t)v);
        
        entries->Push((int64_t)entry);
    }
    return entries;
}

void TsMap::ForEach(void* callback, void* thisArg) {
    TsValue* cbVal = (TsValue*)callback;
    if (!cbVal || cbVal->type != ValueType::FUNCTION_PTR) return;

    MapType* map = (MapType*)impl;
    for (auto const& [key, val] : *map) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        *v = val;
        TsValue* k = (TsValue*)ts_alloc(sizeof(TsValue));
        *k = key;
        TsValue* m = ts_value_make_object(this);
        ts_call_3(cbVal, v, k, m);
    }
}

TsMap* TsMap::CopyExcluding(std::vector<TsString*>& excluded) {
    TsMap* dest = TsMap::Create();
    MapType* map = (MapType*)impl;
    
    for (auto const& [key, val] : *map) {
        if (key.type != ValueType::STRING_PTR) {
            dest->Set(key, val);
            continue;
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
    }
    return dest;
}

extern "C" {

// Debug hook from TsObject.cpp: TsMap* backing lodash's module object.
extern void* g_debug_lodash_module_map;

void* ts_map_create() {
    return TsMap::Create();
}

// Value-based API variants - avoid heap allocation by passing/returning TsValue by value
// These are more efficient for hot paths where the caller can use stack-allocated TsValue
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
    return ((TsMap*)map)->Has(*key);
}

bool ts_map_delete(void* map, TsValue* key) {
    if (!map || !key) return false;
    return ((TsMap*)map)->Delete(*key);
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
        TsValue* val = (TsValue*)excluded->Get(i);
        if (val->type == ValueType::STRING_PTR) {
            excluded_vec.push_back((TsString*)val->ptr_val);
        }
    }
    
    return map->CopyExcluding(excluded_vec);
}

TsValue* ts_map_set_wrapper(void* context, TsValue* key, TsValue* value) {
    // Use scalar helper directly
    uint64_t hash = (uint64_t)key->i_val; // Use value as hash
    __ts_map_set_at(context, hash, (uint8_t)key->type, key->i_val, (uint8_t)value->type, value->i_val);
    return ts_value_make_undefined();
}

TsValue* ts_map_get_wrapper(void* context, TsValue* key) {
    // Use scalar helpers directly
    uint64_t hash = (uint64_t)key->i_val; // Use value as hash for now
    int64_t bucket = __ts_map_find_bucket(context, hash, (uint8_t)key->type, key->i_val);
    if (bucket < 0) {
        return ts_value_make_undefined();
    }
    TsValue* result = (TsValue*)ts_alloc(sizeof(TsValue));
    uint8_t result_type;
    int64_t result_val;
    __ts_map_get_value_at(context, bucket, &result_type, &result_val);
    result->type = (ValueType)result_type;
    result->i_val = result_val;
    return result;
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
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        *res = val;
        return res;
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
// These functions take TsValue fields separately (type as i8, value as i64)
// to avoid Windows x64 ABI issues with 16-byte struct passing
// ============================================================

// Helper: Build TsValue from scalar fields
static TsValue __ts_value_from_scalars(uint8_t type, int64_t value) {
    TsValue v;
    v.type = (ValueType)type;
    // Union interpretation: int64 can hold pointer, int, or double bits
    v.i_val = value;
    return v;
}

// Find bucket index for given key, or -1 if not found
// Returns: bucket index (>= 0) if found, -1 if not found
int64_t __ts_map_find_bucket(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val) {
    if (!map) return -1;
    
    // Verify this is actually a TsMap before using it
    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
        // Not a TsMap - return not found
        return -1;
    }
    
    TsMap* tsmap = (TsMap*)map;
    MapType* impl = (MapType*)tsmap->impl;
    
    TsValue key = __ts_value_from_scalars(key_type, key_val);
    
    auto it = impl->find(key);
    if (it == impl->end()) {
        return -1;
    }
    
    // Return bucket index (distance from begin)
    return std::distance(impl->begin(), it);
}

// Get value at bucket index via out-parameters
// Avoids returning TsValue struct (Windows x64 ABI issue)
void __ts_map_get_value_at(void* map, int64_t bucket_idx, uint8_t* out_type, int64_t* out_value) {
    if (!map || bucket_idx < 0) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }
    
    // Verify this is actually a TsMap before using it
    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }
    
    TsMap* tsmap = (TsMap*)map;
    MapType* impl = (MapType*)tsmap->impl;
    
    if (bucket_idx >= (int64_t)impl->size()) {
        *out_type = (uint8_t)ValueType::UNDEFINED;
        *out_value = 0;
        return;
    }
    
    auto it = impl->begin();
    std::advance(it, bucket_idx);
    
    *out_type = (uint8_t)it->second.type;
    *out_value = it->second.i_val;
}

// Set value at key (insert or update)
void __ts_map_set_at(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val,
                     uint8_t val_type, int64_t val_val) {
    if (!map) return;
    
    // Check if this is a boxed TsValue* instead of raw TsMap*
    // TsValue has type enum (0-10) at offset 0
    uint8_t firstByte = *(uint8_t*)map;
    if (firstByte <= 10) {  // Could be a TsValue*
        TsValue* maybeVal = (TsValue*)map;
        if ((maybeVal->type == ValueType::OBJECT_PTR || maybeVal->type == ValueType::ARRAY_PTR) && maybeVal->ptr_val) {
            map = maybeVal->ptr_val;  // Unwrap to get the raw object
        }
    }
    
    // Verify this is actually a TsMap before using it
    uint32_t magic = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(map) + 16);
    if (magic != TsMap::MAGIC) {
        // Not a TsMap - silently fail to avoid crash
        return;
    }

    // Targeted trace: watch tracked module.exports writes
    if (g_debug_lodash_module_map && map == g_debug_lodash_module_map && key_type == (uint8_t)ValueType::STRING_PTR) {
        TsString* keyStr = (TsString*)key_val;
        const char* keyUtf8 = keyStr ? keyStr->ToUtf8() : nullptr;
        if (keyUtf8 && std::strcmp(keyUtf8, "exports") == 0) {
            std::printf("[__ts_map_set_at] module.exports write: val_type=%d val_val=%p\n",
                        (int)val_type, (void*)val_val);
            // TEMP DEBUG: Log stack trace context
            std::printf("[__ts_map_set_at]   map=%p key_hash=%llx\n", map, (unsigned long long)key_hash);
        }
    }
    
    TsMap* tsmap = (TsMap*)map;
    TsValue key = __ts_value_from_scalars(key_type, key_val);
    TsValue val = __ts_value_from_scalars(val_type, val_val);
    
    tsmap->Set(key, val);
}

}

