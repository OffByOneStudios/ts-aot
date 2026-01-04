#pragma once

#include <cstdint>
#include <vector>
#include "TsString.h"
#include "TsObject.h"

class TsMap : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D415053; // "MAPS"
    static TsMap* Create();

    void Set(TsValue key, TsValue value);
    TsValue Get(TsValue key);
    bool Has(TsValue key);
    bool Delete(TsValue key);
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
    
    // DEPRECATED: Use _v variants (pass TsValue by value) for better performance
    [[deprecated("Use ts_map_set_v instead - pass TsValue by value")]]
    void ts_map_set(void* map, TsValue* key, TsValue* value);
    [[deprecated("Use ts_map_get_v instead - pass TsValue by value")]]
    TsValue* ts_map_get(void* map, TsValue* key);
    [[deprecated("Use ts_map_has_v instead - pass TsValue by value")]]
    bool ts_map_has(void* map, TsValue* key);
    [[deprecated("Use ts_map_delete_v instead - pass TsValue by value")]]
    bool ts_map_delete(void* map, TsValue* key);
    
    void ts_map_clear(void* map);
    int64_t ts_map_size(void* map);
    void* ts_map_keys(void* map);
    void* ts_map_values(void* map);
    void* ts_map_entries(void* map);
    void ts_map_forEach(void* map, void* callback, void* thisArg);
    
    // Value-based API variants - avoid heap allocation
    // Pass TsValue by value instead of TsValue* for better performance
    void ts_map_set_v(void* map, TsValue key, TsValue value);
    TsValue ts_map_get_v(void* map, TsValue key);
    bool ts_map_has_v(void* map, TsValue key);
    bool ts_map_delete_v(void* map, TsValue key);
}
