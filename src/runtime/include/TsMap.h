#pragma once

#include <cstdint>
#include <vector>
#include "TsString.h"
#include "TsObject.h"
#include "TsTyped.h"

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

    // BUG 7: some receivers arrive off-by-8 — a pointer to the `vtable` member
    // (offset 8) instead of the C++ object base. A correct base has the C++
    // vftable at offset 0; an off-by-8 pointer has the runtime TsMap_VTable
    // there. self() detects that unique sentinel and returns the corrected
    // base so member access reads the right fields. (The upstream source of
    // the +8 is scale/GC-dependent and tracked separately.)
    TsMap* self();

    // Prototype chain support
    TsMap* GetPrototype() const { return prototype; }
    void SetPrototype(TsMap* proto) {
        // A map's [[Prototype]] is a TsMap* or null — never a tagged primitive.
        // Reject a non-heap value (e.g. 0x07 = NaN-boxed `true`): storing it
        // would later crash the prototype-chain walk (ts_object_get_property)
        // when it deref's `currentMap->Get(...)` on the bogus pointer. This is
        // the correct no-op for an invalid proto, and prevents a known
        // nondeterministic-GC-induced crash from propagating a clobbered value.
        if (proto && (uintptr_t)proto < 0x10000) {
            return;
        }
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

    // ES 10.4.6 module namespace exotic object: [[Set]] always returns
    // false (strict write -> TypeError), [[Delete]] of an own export is
    // false, non-extensible, null [[Prototype]]. Set by
    // ts_module_mark_namespace at the end of an ESM module's init.
    void SetModuleNamespace(bool value) { isModuleNamespace = value; }
    bool IsModuleNamespace() const { return isModuleNamespace; }
    // PRE-brand (self-import splice, CONF-P3): exotic READ behavior
    // ([[GetPrototypeOf]] null, isExtensible false, @@toStringTag) is live,
    // but [[Set]]/extension REJECTION waits for the init-end full mark so
    // the module's own export population (exports.default = ...) works.
    void SetModuleNamespacePre(bool value) { isModuleNamespacePre = value; }
    bool IsModuleNamespacePre() const { return isModuleNamespacePre; }
    bool IsModuleNamespaceAny() const {
        return isModuleNamespace || isModuleNamespacePre;
    }

    // true for `Object.create(null)` — a genuinely prototype-less object.
    // Distinguished from a plain `{}` (whose `prototype` is also nullptr but
    // logically inherits Object.prototype, which we don't materialize). Used
    // to suppress the inherited-Object.prototype-member fallback in the `in`
    // operator and dynamic `.constructor`/toString/valueOf, and to make
    // Object.getPrototypeOf return null.
    void SetNullPrototype(bool value) { nullPrototype = value; }
    bool HasNullPrototype() const { return nullPrototype; }

    // Init-in-place for stack allocation (escape analysis)
    static void InitInPlace(void* mem);

protected:
    TsMap();
private:
    // NOTE: no shadow `magic` field here — the canonical type tag is the
    // inherited TsObject::magic at offset 16 (set in the ctor). A redundant
    // shadow used to live here to support an off-by-N magic scan; that scan was
    // collapsed to offset-16-only (Step B2), so the shadow is gone.
    TsMap* prototype = nullptr;
    bool frozen = false;
    bool sealed = false;
    bool extensible = true;
    bool isExplicitMap = false;
    bool isModuleNamespace = false;
    bool isModuleNamespacePre = false;  // pre-brand: read-exotic only
    bool nullPrototype = false;  // true for Object.create(null)
};

// Enrol TsMap for validated, offset-derived type-tag dispatch (ts_is<T>/ts_cast<T>).
// Centralized here so any file including TsMap.h can use it (no per-.cpp re-decl).
TS_DECLARE_TAG(TsMap);

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
