#pragma once

#include <cstdint>
#include <cstring>

// Flat inline-slot objects for known-shape object literals and class instances.
// Instead of using TsMap (hash table), objects with statically known property
// names store values in inline slots at fixed offsets.
//
// Layout:
//   offset 0:   [4 bytes] FLAT_MAGIC (0x464C4154)
//   offset 4:   [4 bytes] shapeId
//   offset 8:   [8 bytes] vtablePtr (nullptr for object literals, class VTable* for instances)
//   offset 16:  [8 bytes] slot 0 (NaN-boxed value, stored as void*)
//   offset 24:  [8 bytes] slot 1
//   ...
//   offset 16+N*8: [8 bytes] overflow TsMap* (nullptr if no dynamic props)

static constexpr uint32_t FLAT_MAGIC = 0x464C4154;  // "FLAT"

struct ShapeDescriptor {
    uint32_t magic;         // 0x464C4154 for validation
    uint32_t numSlots;      // Number of inline property slots
    const char** propNames; // Static array of C string property names (ordered by slot index)
    uint32_t numMethods;    // Number of vtable methods (0 for object literals)
    const char** methodNames; // Method names in vtable order (vtable[i+1] = method i)
};

#define MAX_SHAPES 4096

extern ShapeDescriptor* g_shape_table[MAX_SHAPES];
extern uint32_t g_shape_count;

// Register a shape descriptor (called from compiler-generated module init)
extern "C" void ts_shape_register(uint32_t shapeId, ShapeDescriptor* desc);

// Lookup a shape by ID
inline ShapeDescriptor* ts_shape_lookup(uint32_t shapeId) {
    if (shapeId < MAX_SHAPES) return g_shape_table[shapeId];
    return nullptr;
}

// Check if a pointer is a flat object
inline bool is_flat_object(void* obj) {
    if (!obj) return false;
    return *(uint32_t*)obj == FLAT_MAGIC;
}

// Get shapeId from a flat object
inline uint32_t flat_object_shape_id(void* obj) {
    return *((uint32_t*)obj + 1);
}

// Get vtable pointer from a flat object
inline void** flat_object_vtable_ptr(void* obj) {
    return (void**)((char*)obj + 8);
}

// Get pointer to slot N
inline void** flat_object_slot_ptr(void* obj, uint32_t slotIndex) {
    return (void**)((char*)obj + 16 + slotIndex * 8);
}

// Get overflow TsMap* pointer location
inline void** flat_object_overflow_ptr(void* obj, uint32_t numSlots) {
    return (void**)((char*)obj + 16 + numSlots * 8);
}

// Runtime API
extern "C" {
    // Create a flat object (heap-allocated via GC)
    void* ts_flat_object_create(uint32_t shapeId);

    // Get property by name from flat object (runtime slow path)
    void* ts_flat_object_get_property(void* obj, const char* key);

    // Set property by name on flat object (runtime slow path)
    void ts_flat_object_set_property(void* obj, const char* key, void* value);

    // Check if property exists in flat object
    bool ts_flat_object_has_property(void* obj, const char* key);

    // Get keys of a flat object as TsArray*
    void* ts_flat_object_keys(void* obj);

    // Get values of a flat object as TsArray*
    void* ts_flat_object_values(void* obj);

    // Get entries of a flat object as TsArray* of [key, value] pairs
    void* ts_flat_object_entries(void* obj);

    // Convert a flat object to a TsMap (for interop with code that expects TsMap)
    void* ts_flat_object_to_map(void* obj);
}
