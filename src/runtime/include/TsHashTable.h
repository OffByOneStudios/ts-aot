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
                const char* str = ((TsString*)v.ptr_val)->ToUtf8();
                if (!str) return 0;
                size_t h = 5381;
                int c;
                while ((c = *str++))
                    h = ((h << 5) + h) + c;
                return h;
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
                if (!lhs.ptr_val || !rhs.ptr_val) return lhs.ptr_val == rhs.ptr_val;
                const char* a = ((TsString*)lhs.ptr_val)->ToUtf8();
                const char* b = ((TsString*)rhs.ptr_val)->ToUtf8();
                if (!a || !b) return a == b;
                return std::strcmp(a, b) == 0;
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

    static TsHashTable* Create(size_t initial_capacity = 8) {
        TsHashTable* ht = (TsHashTable*)ts_gc_alloc_old_gen(sizeof(TsHashTable));
        ht->size_ = 0;
        // Round up to power of 2
        size_t cap = 8;
        while (cap < initial_capacity) cap <<= 1;
        ht->capacity_ = cap;
        ht->grow_thresh_ = cap - cap / 8;  // 87.5%
        size_t alloc_size = cap + cap * sizeof(Entry);
        void* buf = ts_gc_alloc_old_gen(alloc_size);
        std::memset(buf, 0, alloc_size);
        ht->ctrl_ = (uint8_t*)buf;
        ht->entries_ = (Entry*)((uint8_t*)buf + cap);
        return ht;
    }

    void Set(const TsValue& key, const TsValue& value) {
        // Check for existing key first to avoid duplicate entries
        size_t existing = find_slot(key);
        if (existing != NOT_FOUND) {
            store_entry(existing, key, value);
            return;
        }

        // New key — grow if needed, then insert
        if (size_ >= grow_thresh_) {
            rehash(capacity_ * 2);
        }

        insert_entry(key, value);
        size_++;
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
        entries_[idx].key = TsValue();
        entries_[idx].value = TsValue();
        size_--;
        return true;
    }

    void Clear() {
        std::memset(ctrl_, 0, capacity_);
        std::memset(entries_, 0, capacity_ * sizeof(Entry));
        size_ = 0;
    }

    size_t Size() const { return size_; }

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

    size_t probe_distance(size_t slot, const TsValue& key) const {
        size_t hash = hasher_(key);
        size_t ideal = h1_from_hash(hash);
        return (slot + capacity_ - ideal) & (capacity_ - 1);
    }

    size_t find_slot(const TsValue& key) const {
        size_t hash = hasher_(key);
        uint8_t h2 = h2_from_hash(hash);
        size_t idx = h1_from_hash(hash);
        size_t dist = 0;

        for (;;) {
            uint8_t c = ctrl_[idx];

            if (c == CTRL_EMPTY) return NOT_FOUND;

            if (c == h2 && equal_(entries_[idx].key, key)) {
                return idx;
            }

            // Robin Hood: if current occupant's probe distance is less than ours,
            // the key we're looking for can't be further ahead
            if (c != CTRL_DELETED) {
                size_t occ_dist = probe_distance(idx, entries_[idx].key);
                if (occ_dist < dist) return NOT_FOUND;
            }

            idx = (idx + 1) & (capacity_ - 1);
            dist++;
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
        Entry* old_entries = entries_;
        size_t old_capacity = capacity_;

        capacity_ = new_capacity;
        grow_thresh_ = new_capacity - new_capacity / 8;

        size_t alloc_size = new_capacity + new_capacity * sizeof(Entry);
        void* buf = ts_gc_alloc_old_gen(alloc_size);
        std::memset(buf, 0, alloc_size);
        ctrl_ = (uint8_t*)buf;
        entries_ = (Entry*)((uint8_t*)buf + new_capacity);

        // Re-insert all live entries
        size_t count = 0;
        for (size_t i = 0; i < old_capacity; i++) {
            uint8_t c = old_ctrl[i];
            if (c != CTRL_EMPTY && c != CTRL_DELETED) {
                insert_entry(old_entries[i].key, old_entries[i].value);
                count++;
            }
        }
        size_ = count;
        // Old buffer is GC-managed (old-gen), will be collected when unreachable
    }

    // Insert an entry known to not exist in the table.
    // Caller must ensure there is room (size_ < grow_thresh_).
    void insert_entry(const TsValue& key, const TsValue& value) {
        size_t hash = hasher_(key);
        uint8_t h2 = h2_from_hash(hash);
        size_t idx = h1_from_hash(hash);
        size_t dist = 0;

        TsValue ins_key = key;
        TsValue ins_val = value;
        uint8_t ins_h2 = h2;

        for (;;) {
            uint8_t c = ctrl_[idx];

            if (c == CTRL_EMPTY || c == CTRL_DELETED) {
                ctrl_[idx] = ins_h2;
                store_entry(idx, ins_key, ins_val);
                return;
            }

            size_t occ_dist = probe_distance(idx, entries_[idx].key);
            if (occ_dist < dist) {
                std::swap(ins_h2, ctrl_[idx]);
                TsValue tmp_key = entries_[idx].key;
                TsValue tmp_val = entries_[idx].value;
                store_entry(idx, ins_key, ins_val);
                ins_key = tmp_key;
                ins_val = tmp_val;
                dist = occ_dist;
            }

            idx = (idx + 1) & (capacity_ - 1);
            dist++;
        }
    }
};

// Static members — defined in TsMap.cpp
// (Both TsMap and TsSet include this header, only one .cpp defines these)
