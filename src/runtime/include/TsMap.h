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
    
    void* impl; // Pointer to std::unordered_map - public for inline IR helpers

protected:
    TsMap();
private:
    uint32_t magic = MAGIC;
};

extern "C" {
    void* ts_map_create();
    
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
    
    // Inline IR helpers - scalar-based API to avoid struct passing
    // These take TsValue fields separately to avoid Windows x64 ABI issues
    int64_t __ts_map_find_bucket(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val);
    void __ts_map_get_value_at(void* map, int64_t bucket_idx, uint8_t* out_type, int64_t* out_value);
    void __ts_map_set_at(void* map, uint64_t key_hash, uint8_t key_type, int64_t key_val,
                         uint8_t val_type, int64_t val_val);
}
