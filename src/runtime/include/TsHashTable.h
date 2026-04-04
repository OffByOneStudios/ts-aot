#pragma once

// TsHashTable: Open-addressing hash table with integrated GC write barriers.
// Replaces std::unordered_map/set for TsMap/TsSet to eliminate the need for
// full old-gen scans during minor GC.
//
// Design:
// - Robin Hood linear probing with power-of-2 capacity
// - Control byte array (1 byte/slot): EMPTY=0, DELETED=0x7F, else H2 hash
// - Single contiguous allocation (ctrl + entries) in old-gen
// - Every pointer store goes through ts_gc_write_barrier()

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <cmath>
#include "TsObject.h"
#include "TsBigInt.h"

// Forward declarations - defined in TsGC.h but we avoid including it
extern "C" {
    void* ts_gc_alloc_old_gen(size_t size);
    void ts_gc_write_barrier(void* slot_addr, void* stored_value);
}

// ============================================================================
// Hash and Equality for TsValue
// ============================================================================

struct TsValueHash {
    size_t operator()(const TsValue& v) const {
        switch (v.type) {
            case ValueType::UNDEFINED: return 0;
            case ValueType::NUMBER_INT: return std::hash<int64_t>{}(v.i_val);
            case ValueType::NUMBER_DBL: return std::hash<double>{}(v.d_val);
            case ValueType::BOOLEAN: return std::hash<bool>{}(v.b_val);
            case ValueType::STRING_PTR: {
                if (!v.ptr_val) return 0;
                return ((TsString*)v.ptr_val)->Hash();
            }
            case ValueType::BIGINT_PTR: {
                if (!v.ptr_val) return 0;
                const char* str = ((TsBigInt*)v.ptr_val)->ToString();
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
                if (std::isnan(lhs.d_val) && std::isnan(rhs.d_val)) return true;
                return lhs.d_val == rhs.d_val;
            }
            case ValueType::BOOLEAN: return lhs.b_val == rhs.b_val;
            case ValueType::STRING_PTR: {
                if (lhs.ptr_val == rhs.ptr_val) return true;  // Pointer equality (interned strings)
                if (!lhs.ptr_val || !rhs.ptr_val) return false;
                TsString* a = (TsString*)lhs.ptr_val;
                TsString* b = (TsString*)rhs.ptr_val;
                return a->Equals(b);  // Length check + memcmp/ICU comparison
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

// ============================================================================
// TsHashTable
// ============================================================================

class TsHashTable {
public:
    struct Entry {
        TsValue key;
        TsValue value;
    };

    static constexpr uint8_t CTRL_EMPTY   = 0x00;
    static constexpr uint8_t CTRL_DELETED  = 0x7F;
    // Live entries: H2 in range [0x01, 0x7E]

    // Property attribute flags (1 byte per slot, parallel to ctrl_)
    static constexpr uint8_t ATTR_ENUMERABLE   = 0x01;
    static constexpr uint8_t ATTR_WRITABLE     = 0x02;
    static constexpr uint8_t ATTR_CONFIGURABLE = 0x04;
    static constexpr uint8_t ATTR_DEFAULT      = 0x07; // all true (JS assignment default)

    static TsHashTable* Create(size_t initial_capacity = 8) {
        TsHashTable* ht = (TsHashTable*)ts_gc_alloc_old_gen(sizeof(TsHashTable));
        ht->size_ = 0;
        // Round up to power of 2
        size_t cap = 8;
        while (cap < initial_capacity) cap <<= 1;
        ht->capacity_ = cap;
        ht->grow_thresh_ = cap - cap / 8;  // 87.5%
        // Layout: ctrl_[cap] + attrs_[cap] + entries_[cap * sizeof(Entry)]
        size_t alloc_size = cap + cap + cap * sizeof(Entry);
        void* buf = ts_gc_alloc_old_gen(alloc_size);
        std::memset(buf, 0, alloc_size);
        ht->ctrl_ = (uint8_t*)buf;
        ht->attrs_ = (uint8_t*)buf + cap;
        ht->entries_ = (Entry*)((uint8_t*)buf + cap + cap);
        return ht;
    }

    void Set(const TsValue& key, const TsValue& value) {
        // Check for existing key first — preserve attrs on update
        size_t existing = find_slot(key);
        if (existing != NOT_FOUND) {
            store_entry(existing, key, value);
            // attrs_[existing] preserved — Set() doesn't change attributes
            return;
        }

        // New key — default attributes (enumerable+writable+configurable)
        if (size_ >= grow_thresh_) {
            size_t new_cap = capacity_ < 256 ? capacity_ * 4 : capacity_ * 2;
            rehash(new_cap);
        }

        insert_entry(key, value, ATTR_DEFAULT);
        size_++;
    }

    void SetWithAttrs(const TsValue& key, const TsValue& value, uint8_t attrs) {
        size_t existing = find_slot(key);
        if (existing != NOT_FOUND) {
            store_entry(existing, key, value);
            attrs_[existing] = attrs;
            return;
        }

        if (size_ >= grow_thresh_) {
            size_t new_cap = capacity_ < 256 ? capacity_ * 4 : capacity_ * 2;
            rehash(new_cap);
        }

        insert_entry(key, value, attrs);
        size_++;
    }

    uint8_t GetAttrs(const TsValue& key) const {
        size_t idx = find_slot(key);
        if (idx == NOT_FOUND) return 0;
        return attrs_[idx];
    }

    void SetAttrs(const TsValue& key, uint8_t attrs) {
        size_t idx = find_slot(key);
        if (idx != NOT_FOUND) {
            attrs_[idx] = attrs;
        }
    }

    TsValue Get(const TsValue& key) const {
        size_t idx = find_slot(key);
        if (idx == NOT_FOUND) return TsValue();
        return entries_[idx].value;
    }

    bool Has(const TsValue& key) const {
        return find_slot(key) != NOT_FOUND;
    }

    bool Delete(const TsValue& key) {
        size_t idx = find_slot(key);
        if (idx == NOT_FOUND) return false;
        ctrl_[idx] = CTRL_DELETED;
        attrs_[idx] = 0;
        entries_[idx].key = TsValue();
        entries_[idx].value = TsValue();
        size_--;
        return true;
    }

    void Clear() {
        std::memset(ctrl_, 0, capacity_);
        std::memset(attrs_, 0, capacity_);
        std::memset(entries_, 0, capacity_ * sizeof(Entry));
        size_ = 0;
    }

    size_t Size() const { return size_; }

    void Resize(size_t new_capacity) {
        if (new_capacity <= capacity_) return;
        size_t cap = capacity_;
        while (cap < new_capacity) cap <<= 1;
        rehash(cap);
    }

    // Iterate all live entries. Callback receives (const TsValue& key, const TsValue& value).
    template<typename F>
    void ForEach(F&& fn) const {
        for (size_t i = 0; i < capacity_; i++) {
            uint8_t c = ctrl_[i];
            if (c != CTRL_EMPTY && c != CTRL_DELETED) {
                fn(entries_[i].key, entries_[i].value);
            }
        }
    }

    // Iterate only enumerable live entries.
    template<typename F>
    void ForEachEnumerable(F&& fn) const {
        for (size_t i = 0; i < capacity_; i++) {
            uint8_t c = ctrl_[i];
            if (c != CTRL_EMPTY && c != CTRL_DELETED && (attrs_[i] & ATTR_ENUMERABLE)) {
                fn(entries_[i].key, entries_[i].value);
            }
        }
    }

    // Find a key and return its flat index, or NOT_FOUND.
    // For __ts_map_find_bucket compatibility.
    size_t FindIndex(const TsValue& key) const {
        return find_slot(key);
    }

    // Direct access to entry by flat index.
    const Entry& GetEntry(size_t idx) const { return entries_[idx]; }

    // Get entry at flat index with bounds check
    bool GetEntryAt(size_t idx, TsValue* out_key, TsValue* out_value) const {
        if (idx >= capacity_) return false;
        uint8_t c = ctrl_[idx];
        if (c == CTRL_EMPTY || c == CTRL_DELETED) return false;
        *out_key = entries_[idx].key;
        *out_value = entries_[idx].value;
        return true;
    }

    static constexpr size_t NOT_FOUND = (size_t)-1;

private:
    uint8_t* ctrl_;
    uint8_t* attrs_;   // Property attributes, parallel to ctrl_ (1 byte per slot)
    Entry* entries_;
    size_t size_;
    size_t capacity_;
    size_t grow_thresh_;

    static TsValueHash hasher_;
    static TsValueEqual equal_;

    static uint8_t h2_from_hash(size_t hash) {
        // Use lower 7 bits, biased by +1 so result is in [1, 128] — never 0 or 0x7F
        uint8_t h2 = (uint8_t)(hash & 0x7F);
        if (h2 == 0) h2 = 1;
        if (h2 == CTRL_DELETED) h2 = 1;  // Avoid collision with tombstone
        return h2;
    }

    size_t h1_from_hash(size_t hash) const {
        return (hash >> 7) & (capacity_ - 1);
    }

    size_t find_slot(const TsValue& key) const {
        size_t hash = hasher_(key);
        uint8_t h2 = h2_from_hash(hash);
        size_t idx = h1_from_hash(hash);

        for (;;) {
            uint8_t c = ctrl_[idx];

            if (c == CTRL_EMPTY) return NOT_FOUND;

            if (c == h2 && equal_(entries_[idx].key, key)) {
                return idx;
            }

            // Linear probe — skip deleted and non-matching slots
            idx = (idx + 1) & (capacity_ - 1);
        }
    }

    void store_entry(size_t slot, const TsValue& key, const TsValue& value) {
        entries_[slot].key = key;
        entries_[slot].value = value;
        // Write barriers for pointer-type values stored into old-gen
        if (key.ptr_val) {
            ts_gc_write_barrier(&entries_[slot].key.ptr_val, key.ptr_val);
        }
        if (value.ptr_val) {
            ts_gc_write_barrier(&entries_[slot].value.ptr_val, value.ptr_val);
        }
    }

    void rehash(size_t new_capacity) {
        uint8_t* old_ctrl = ctrl_;
        uint8_t* old_attrs = attrs_;
        Entry* old_entries = entries_;
        size_t old_capacity = capacity_;

        capacity_ = new_capacity;
        grow_thresh_ = new_capacity - new_capacity / 8;

        size_t alloc_size = new_capacity + new_capacity + new_capacity * sizeof(Entry);
        void* buf = ts_gc_alloc_old_gen(alloc_size);
        std::memset(buf, 0, alloc_size);
        ctrl_ = (uint8_t*)buf;
        attrs_ = (uint8_t*)buf + new_capacity;
        entries_ = (Entry*)((uint8_t*)buf + new_capacity + new_capacity);

        // Re-insert all live entries with preserved attributes
        size_t count = 0;
        for (size_t i = 0; i < old_capacity; i++) {
            uint8_t c = old_ctrl[i];
            if (c != CTRL_EMPTY && c != CTRL_DELETED) {
                insert_entry(old_entries[i].key, old_entries[i].value, old_attrs[i]);
                count++;
            }
        }
        size_ = count;
    }

    // Insert an entry known to not exist in the table.
    // Caller must ensure there is room (size_ < grow_thresh_).
    void insert_entry(const TsValue& key, const TsValue& value, uint8_t attrs = ATTR_DEFAULT) {
        size_t hash = hasher_(key);
        uint8_t h2 = h2_from_hash(hash);
        size_t idx = h1_from_hash(hash);

        for (;;) {
            uint8_t c = ctrl_[idx];

            if (c == CTRL_EMPTY || c == CTRL_DELETED) {
                ctrl_[idx] = h2;
                attrs_[idx] = attrs;
                store_entry(idx, key, value);
                return;
            }

            idx = (idx + 1) & (capacity_ - 1);
        }
    }
};

// Static members — defined in TsMap.cpp
// (Both TsMap and TsSet include this header, only one .cpp defines these)
