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
                TsString* s = (TsString*)v.ptr_val;
                const char* str = s->ToUtf8();
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
            }
            case ValueType::BIGINT_PTR: {
                TsBigInt* bi = (TsBigInt*)v.ptr_val;
                const char* str = bi->ToString();
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
                return std::strcmp(((TsString*)lhs.ptr_val)->ToUtf8(), ((TsString*)rhs.ptr_val)->ToUtf8()) == 0;
            }
            case ValueType::BIGINT_PTR: {
                return std::strcmp(((TsBigInt*)lhs.ptr_val)->ToString(), ((TsBigInt*)rhs.ptr_val)->ToString()) == 0;
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
    if (!cbVal || cbVal->type != ValueType::OBJECT_PTR) return;
    TsFunction* func = (TsFunction*)cbVal->ptr_val;
    auto fp = (TsValue* (*)(TsValue*, TsValue*, TsValue*, void*))func->funcPtr;

    MapType* map = (MapType*)impl;
    for (auto const& [key, val] : *map) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        *v = val;
        TsValue* k = (TsValue*)ts_alloc(sizeof(TsValue));
        *k = key;
        TsValue* m = ts_value_make_object(this);
        fp(v, k, m, func->context);
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

void* ts_map_create() {
    return TsMap::Create();
}

void ts_map_set(void* map, TsValue* key, TsValue* value) {
    if (!map || !key || !value) return;
    ((TsMap*)map)->Set(*key, *value);
}

TsValue* ts_map_get(void* map, TsValue* key) {
    if (!map || !key) return nullptr;
    TsValue res = ((TsMap*)map)->Get(*key);
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    *v = res;
    return v;
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
    ts_map_set(context, key, value);
    return ts_value_make_undefined();
}

TsValue* ts_map_get_wrapper(void* context, TsValue* key) {
    return ts_map_get(context, key);
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
    fprintf(stderr, "DEBUG: ts_map_get_property obj=%p propName=%p\n", obj, propName); fflush(stderr);
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();
    fprintf(stderr, "DEBUG: ts_map_get_property name='%s'\n", name); fflush(stderr);
    
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
    
    // Fallback: look in the map (for object-like behavior)
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = prop;
    
    TsMap* map = (TsMap*)obj;
    if (map->Has(key)) {
        TsValue val = map->Get(key);
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        *res = val;
        return res;
    }
    
    return ts_value_make_undefined();
}

}

