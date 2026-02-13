#include "TsFlatObject.h"
#include "TsGC.h"
#include "TsNanBox.h"
#include "TsObject.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"

#include <cstring>

// Global shape table
ShapeDescriptor* g_shape_table[MAX_SHAPES] = {};
uint32_t g_shape_count = 0;

extern "C" void ts_shape_register(uint32_t shapeId, ShapeDescriptor* desc) {
    if (shapeId < MAX_SHAPES) {
        g_shape_table[shapeId] = desc;
        if (shapeId >= g_shape_count) g_shape_count = shapeId + 1;
    }
}

extern "C" void* ts_flat_object_create(uint32_t shapeId) {
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return nullptr;

    uint32_t totalSize = 8 + desc->numSlots * 8 + 8;  // header + slots + overflow ptr
    void* mem = ts_gc_alloc(totalSize);

    // Write header
    *(uint32_t*)mem = FLAT_MAGIC;
    *((uint32_t*)mem + 1) = shapeId;

    // Initialize all slots to NaN-boxed undefined
    uint64_t* slots = (uint64_t*)((char*)mem + 8);
    for (uint32_t i = 0; i < desc->numSlots; i++) {
        slots[i] = NANBOX_UNDEFINED;
    }

    // Initialize overflow map to nullptr
    uint64_t* overflowSlot = (uint64_t*)((char*)mem + 8 + desc->numSlots * 8);
    *overflowSlot = 0;

    return mem;
}

// Find slot index for a property name, or -1 if not found
static int flat_find_slot(ShapeDescriptor* desc, const char* key) {
    for (uint32_t i = 0; i < desc->numSlots; i++) {
        if (strcmp(desc->propNames[i], key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

extern "C" void* ts_flat_object_get_property(void* obj, const char* key) {
    if (!obj || !key) return (void*)(uintptr_t)NANBOX_UNDEFINED;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return (void*)(uintptr_t)NANBOX_UNDEFINED;

    // Check inline slots
    int slot = flat_find_slot(desc, key);
    if (slot >= 0) {
        uint64_t val = *(uint64_t*)((char*)obj + 8 + slot * 8);
        return (void*)(uintptr_t)val;
    }

    // Check overflow map
    void* overflow = *(void**)((char*)obj + 8 + desc->numSlots * 8);
    if (overflow) {
        TsMap* map = (TsMap*)overflow;
        TsString* keyStr = TsString::Create(key);
        TsValue result = map->Get(TsValue(keyStr));
        if (result.type != ValueType::UNDEFINED) {
            // Convert TaggedValue back to NaN-boxed TsValue*
            if (result.type == ValueType::NUMBER_INT) {
                return (void*)(uintptr_t)nanbox_int32((int32_t)result.i_val);
            } else if (result.type == ValueType::NUMBER_DBL) {
                return (void*)(uintptr_t)nanbox_double(result.d_val);
            } else if (result.type == ValueType::BOOLEAN) {
                return (void*)(uintptr_t)nanbox_bool(result.i_val != 0);
            } else if (result.ptr_val) {
                return (void*)(uintptr_t)nanbox_ptr(result.ptr_val);
            }
        }
    }

    return (void*)(uintptr_t)NANBOX_UNDEFINED;
}

extern "C" void ts_flat_object_set_property(void* obj, const char* key, void* value) {
    if (!obj || !key) return;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return;

    // Check inline slots first
    int slot = flat_find_slot(desc, key);
    if (slot >= 0) {
        uint64_t* slotPtr = (uint64_t*)((char*)obj + 8 + slot * 8);
        *slotPtr = (uint64_t)(uintptr_t)value;
        ts_gc_write_barrier(slotPtr, value);
        return;
    }

    // Property not in shape - use overflow map
    void** overflowPtr = (void**)((char*)obj + 8 + desc->numSlots * 8);
    TsMap* overflow = (TsMap*)*overflowPtr;
    if (!overflow) {
        overflow = TsMap::Create();
        *overflowPtr = overflow;
        ts_gc_write_barrier(overflowPtr, overflow);
    }

    // Convert NaN-boxed value to TaggedValue for TsMap storage
    TsString* keyStr = TsString::Create(key);
    TsValue tv = nanbox_to_tagged((TsValue*)value);
    overflow->Set(TsValue(keyStr), tv);
}

extern "C" bool ts_flat_object_has_property(void* obj, const char* key) {
    if (!obj || !key) return false;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return false;

    // Check inline slots
    if (flat_find_slot(desc, key) >= 0) return true;

    // Check overflow map
    void* overflow = *(void**)((char*)obj + 8 + desc->numSlots * 8);
    if (overflow) {
        TsMap* map = (TsMap*)overflow;
        TsString* keyStr = TsString::Create(key);
        TsValue keyVal;
        keyVal.type = ValueType::STRING_PTR;
        keyVal.ptr_val = keyStr;
        return map->Has(keyVal);
    }

    return false;
}

extern "C" void* ts_flat_object_keys(void* obj) {
    if (!obj) return TsArray::Create(0);

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return TsArray::Create(0);

    TsArray* keys = TsArray::Create(desc->numSlots + 4);

    for (uint32_t i = 0; i < desc->numSlots; i++) {
        TsString* name = TsString::Create(desc->propNames[i]);
        keys->Push((int64_t)(uintptr_t)name);
    }

    // Add overflow keys
    void* overflow = *(void**)((char*)obj + 8 + desc->numSlots * 8);
    if (overflow) {
        TsArray* overflowKeys = (TsArray*)ts_map_keys(overflow);
        if (overflowKeys) {
            for (int64_t i = 0; i < overflowKeys->Length(); i++) {
                keys->Push(overflowKeys->Get(i));
            }
        }
    }

    return keys;
}

extern "C" void* ts_flat_object_values(void* obj) {
    if (!obj) return TsArray::Create(0);

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return TsArray::Create(0);

    TsArray* values = TsArray::Create(desc->numSlots + 4);

    for (uint32_t i = 0; i < desc->numSlots; i++) {
        uint64_t val = *(uint64_t*)((char*)obj + 8 + i * 8);
        values->Push((int64_t)val);
    }

    return values;
}

extern "C" void* ts_flat_object_to_map(void* obj) {
    if (!obj || !is_flat_object(obj)) return nullptr;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return nullptr;

    TsMap* map = TsMap::Create();
    for (uint32_t i = 0; i < desc->numSlots; i++) {
        uint64_t val = *(uint64_t*)((char*)obj + 8 + i * 8);
        if (val != NANBOX_UNDEFINED) {
            TsValue tv = nanbox_to_tagged((TsValue*)(uintptr_t)val);
            map->Set(TsValue(TsString::Create(desc->propNames[i])), tv);
        }
    }

    // Copy overflow entries too
    void* overflow = *(void**)((char*)obj + 8 + desc->numSlots * 8);
    if (overflow) {
        TsMap* overflowMap = (TsMap*)overflow;
        TsArray* keys = (TsArray*)ts_map_keys(overflowMap);
        if (keys) {
            for (int64_t i = 0; i < keys->Length(); i++) {
                TsString* key = (TsString*)(uintptr_t)keys->Get(i);
                TsValue val = overflowMap->Get(TsValue(key));
                map->Set(TsValue(key), val);
            }
        }
    }

    return map;
}

extern "C" void* ts_flat_object_entries(void* obj) {
    if (!obj) return TsArray::Create(0);

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return TsArray::Create(0);

    TsArray* entries = TsArray::Create(desc->numSlots + 4);

    for (uint32_t i = 0; i < desc->numSlots; i++) {
        TsArray* pair = TsArray::Create(2);
        TsString* name = TsString::Create(desc->propNames[i]);
        pair->Push((int64_t)(uintptr_t)name);
        uint64_t val = *(uint64_t*)((char*)obj + 8 + i * 8);
        pair->Push((int64_t)val);
        entries->Push((int64_t)(uintptr_t)pair);
    }

    return entries;
}
