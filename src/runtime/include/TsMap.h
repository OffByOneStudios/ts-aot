#pragma once

#include <cstdint>
#include "TsString.h"

class TsMap {
public:
    static TsMap* Create();

    void Set(TsString* key, int64_t value);
    int64_t Get(TsString* key);
    bool Has(TsString* key);

private:
    TsMap();
    void* impl; // Pointer to std::unordered_map
};

extern "C" {
    void* ts_map_create();
    void ts_map_set(void* map, void* key, int64_t value);
    int64_t ts_map_get(void* map, void* key);
    int64_t ts_map_has(void* map, void* key);
}
