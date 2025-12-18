#include "TsMap.h"
#include "TsArray.h"
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

using MapType = std::unordered_map<TsString*, int64_t, TsStringHash, TsStringEqual, TsAllocator<std::pair<TsString* const, int64_t>>>;

TsMap* TsMap::Create() {
    void* mem = ts_alloc(sizeof(TsMap));
    return new(mem) TsMap();
}

TsMap::TsMap() {
    void* mem = ts_alloc(sizeof(MapType));
    impl = new(mem) MapType();
}

void TsMap::Set(TsString* key, int64_t value) {
    ((MapType*)impl)->insert_or_assign(key, value);
}

int64_t TsMap::Get(TsString* key) {
    auto* map = (MapType*)impl;
    auto it = map->find(key);
    if (it != map->end()) {
        return it->second;
    }
    return 0;
}

bool TsMap::Has(TsString* key) {
    MapType* map = static_cast<MapType*>(impl);
    return map->find(key) != map->end();
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

extern "C" {
    void* ts_map_create() {
        return TsMap::Create();
    }
    void ts_map_set(void* map, void* key, int64_t value) {
        ((TsMap*)map)->Set((TsString*)key, value);
    }
    int64_t ts_map_get(void* map, void* key) {
        return ((TsMap*)map)->Get((TsString*)key);
    }
    bool ts_map_has(void* map, void* key) {
        return static_cast<TsMap*>(map)->Has(static_cast<TsString*>(key));
    }

    int64_t ts_map_size(void* map) {
        return static_cast<TsMap*>(map)->Size();
    }

    void* ts_map_get_keys(void* map) {
        return static_cast<TsMap*>(map)->GetKeys();
    }
}
