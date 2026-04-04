#pragma once

#include <cstdint>
#include <vector>
#include "TsString.h"
#include "TsObject.h"

// Forward declaration for write barrier
extern "C" void ts_gc_write_barrier(void* slot_addr, void* stored_value);

class TsMap : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x4D415053; // "MAPS"
    static TsMap* Create();

    void Set(TsValue key, TsValue value);
    void SetWithAttrs(TsValue key, TsValue value, uint8_t attrs);
    TsValue Get(TsValue key);
    bool Has(TsValue key);
    bool Delete(TsValue key);
    void Clear();
    int64_t Size();
    void* GetKeys();
    void* GetEnumerableKeys();
    void* GetValues();
    void* GetEntries();
    uint8_t GetPropertyAttrs(TsValue key);
    void SetPropertyAttrs(TsValue key, uint8_t attrs);
    void ForEach(void* callback, void* thisArg = nullptr);
    TsMap* CopyExcluding(std::vector<TsString*>& excluded);
    
    void* impl; // Pointer to TsHashTable - public for inline IR helpers

    // Prototype chain support
    TsMap* GetPrototype() const { return prototype; }
    void SetPrototype(TsMap* proto) {
        prototype = proto;
        if (proto) ts_gc_write_barrier((void*)&this->prototype, proto);
    }
    bool WouldCreateCycle(TsMap* proto) const;

    // Object state flags
    void Freeze() { frozen = true; }
    void Seal() { sealed = true; }
    void PreventExtensions() { extensible = false; }
    bool IsFrozen() const { return frozen; }
    bool IsSealed() const { return sealed; }
    bool IsExtensible() const { return extensible; }

    // Flag to distinguish explicit Map (new Map()) from plain objects
    void SetExplicitMap(bool value) { isExplicitMap = value; }
    bool IsExplicitMap() const { return isExplicitMap; }

    // Init-in-place for stack allocation (escape analysis)
    static void InitInPlace(void* mem);

protected:
    TsMap();
private:
    uint32_t magic = MAGIC;
    TsMap* prototype = nullptr;
    bool frozen = false;
    bool sealed = false;
    bool extensible = true;
    bool isExplicitMap = false;  // true for new Map(), false for plain objects
};

extern "C" {
    void* ts_map_create();
    void* ts_map_create_explicit();  // Creates a Map object (new Map())
    void ts_map_init_inplace(void* mem);  // Placement-new a TsMap at caller-provided memory

    // Helper for CommonJS module initialization - sets a property by C string key
    void ts_map_set_cstr(void* map, const char* key, void* value);

    // Helper for setting string values with proper STRING_PTR type
    void ts_map_set_cstr_string(void* map, const char* key, void* stringValue);
    
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

    // Iterator protocol: create an iterator from an array of items
    // Returns a TsMap-based iterator with .next() method returning {value, done}
    void* ts_create_array_iterator(void* items);
}
