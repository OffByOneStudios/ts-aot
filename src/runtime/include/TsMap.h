#pragma once

#include <cstdint>
#include <vector>
#include "TsString.h"
#include "TsObject.h"

class TsMap : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D415053; // "MAPS"
    static TsMap* Create();

    void Set(TsString* key, TsValue value);
    TsValue Get(TsString* key);
    bool Has(TsString* key);
    bool Delete(TsString* key);
    void Clear();
    int64_t Size();
    void* GetKeys();
    void* GetValues();
    void* GetEntries();
    void ForEach(void* callback, void* thisArg = nullptr);
    TsMap* CopyExcluding(std::vector<TsString*>& excluded);

protected:
    TsMap();
private:
    uint32_t magic = MAGIC;
    void* impl; // Pointer to std::unordered_map
};

extern "C" {
    void* ts_map_create();
    void ts_map_set(void* map, void* key, TsValue* value);
    TsValue* ts_map_get(void* map, void* key);
    bool ts_map_has(void* map, void* key);
    bool ts_map_delete(void* map, void* key);
    void ts_map_clear(void* map);
    int64_t ts_map_size(void* map);
    void* ts_map_keys(void* map);
    void* ts_map_values(void* map);
    void* ts_map_entries(void* map);
    void ts_map_forEach(void* map, void* callback, void* thisArg);
}
