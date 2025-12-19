#pragma once

#include <cstdint>
#include "TsString.h"

class TsMap {
public:
    static constexpr uint32_t MAGIC = 0x4D415053; // "MAPS"
    static TsMap* Create();

    void Set(TsString* key, int64_t value);
    int64_t Get(TsString* key);
    bool Has(TsString* key);
    bool Delete(TsString* key);
    void Clear();
    int64_t Size();
    void* GetKeys();

private:
    TsMap();
    uint32_t magic = MAGIC;
    void* impl; // Pointer to std::unordered_map
};

extern "C" {
    void* ts_map_create();
    void ts_map_set(void* map, void* key, int64_t value);
    int64_t ts_map_get(void* map, void* key);
    bool ts_map_has(void* map, void* key);
    bool ts_map_delete(void* map, void* key);
    void ts_map_clear(void* map);
    int64_t ts_map_size(void* map);
}
