#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "GC.h"
#include <unordered_map>
#include <string>
#include <cstring>
#include <new>
#include <functional>

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

// Hash and Equal
struct TsStringHash {
    std::size_t operator()(TsString* const& s) const {
        return std::hash<std::string>{}(s->ToUtf8());
    }
};

struct TsStringEqual {
    bool operator()(TsString* const& lhs, TsString* const& rhs) const {
        return std::strcmp(lhs->ToUtf8(), rhs->ToUtf8()) == 0;
    }
};

using MapType = std::unordered_map<TsString*, TsValue, TsStringHash, TsStringEqual, TsAllocator<std::pair<TsString* const, TsValue>>>;

TsMap* TsMap::Create() {
    void* mem = ts_alloc(sizeof(TsMap));
    return new(mem) TsMap();
}

TsMap::TsMap() {
    void* mem = ts_alloc(sizeof(MapType));
    impl = new(mem) MapType();
}

void TsMap::Set(TsString* key, TsValue value) {
    ((MapType*)impl)->insert_or_assign(key, value);
}

TsValue TsMap::Get(TsString* key) {
    auto* map = (MapType*)impl;
    auto it = map->find(key);
    if (it != map->end()) {
        return it->second;
    }
    return TsValue();
}

bool TsMap::Has(TsString* key) {
    MapType* map = static_cast<MapType*>(impl);
    return map->find(key) != map->end();
}

bool TsMap::Delete(TsString* key) {
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
        keys->Push((int64_t)key);
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
        entry->Push((int64_t)key);
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
        TsValue* k = ts_value_make_string(key);
        TsValue* m = ts_value_make_object(this);
        fp(v, k, m, func->context);
    }
}

extern "C" {
    void* ts_map_create() {
        return TsMap::Create();
    }

    void ts_map_set(void* map, void* key, TsValue* value) {
        ((TsMap*)map)->Set((TsString*)key, *value);
    }
    TsValue* ts_map_get(void* map, void* key) {
        TsValue val = ((TsMap*)map)->Get((TsString*)key);
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        *res = val;
        return res;
    }
    bool ts_map_has(void* map, void* key) {
        return static_cast<TsMap*>(map)->Has(static_cast<TsString*>(key));
    }

    bool ts_map_delete(void* map, void* key) {
        return static_cast<TsMap*>(map)->Delete(static_cast<TsString*>(key));
    }

    void ts_map_clear(void* map) {
        static_cast<TsMap*>(map)->Clear();
    }

    int64_t ts_map_size(void* map) {
        return static_cast<TsMap*>(map)->Size();
    }

    void* ts_map_keys(void* map) {
        return static_cast<TsMap*>(map)->GetKeys();
    }

    void* ts_map_values(void* map) {
        return static_cast<TsMap*>(map)->GetValues();
    }

    void* ts_map_entries(void* map) {
        return static_cast<TsMap*>(map)->GetEntries();
    }

    void ts_map_forEach(void* map, void* callback, void* thisArg) {
        static_cast<TsMap*>(map)->ForEach(callback, thisArg);
    }
}
