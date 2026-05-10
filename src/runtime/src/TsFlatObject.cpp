#include "TsFlatObject.h"
#include "TsGC.h"
#include "TsNanBox.h"
#include "TsObject.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsClosure.h"

#include <cstring>

// Bound method context for vtable method dispatch via native function wrappers.
// When a flat object's vtable method is looked up dynamically (e.g., comp.test(v)
// where comp is any-typed), we create a native function that binds 'this' to the
// object. The trampoline receives (ctx, argc, argv) and calls methodPtr(this, args...).
struct BoundMethodCtx {
    void* obj;       // 'this' pointer (the flat object)
    void* methodPtr; // compiled method function pointer
};

// Exposed (no `static`) so ts_call_with_this_N in TsObject.cpp can detect a
// bound-method TsFunction and skip the `func->context = thisArg` override
// — that override clobbers the BoundMethodCtx* pointer and the trampoline
// then casts a raw thisArg as BoundMethodCtx, reading garbage methodPtr
// → indirect call to data → 0xc0000005 access violation. ~684 of the
// class CRASH cluster trip this when a method is fetched via a getter
// (e.g. `get method() { return this.#m; }; new C().method([1,2,3])`).
extern "C" TsValue* flat_bound_method_trampoline(void* ctx, int argc, TsValue** argv) {
    BoundMethodCtx* bm = (BoundMethodCtx*)ctx;
    void* thisObj = bm->obj;
    void* methodPtr = bm->methodPtr;
    TsValue* u = (TsValue*)(uintptr_t)NANBOX_UNDEFINED;

    // Call methodPtr(this, arg0, arg1, ...) with padding to 4 args
    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
    switch (argc) {
        case 0:  return ((Fn)methodPtr)(thisObj, u, u, u, u);
        case 1:  return ((Fn)methodPtr)(thisObj, argv[0], u, u, u);
        case 2:  return ((Fn)methodPtr)(thisObj, argv[0], argv[1], u, u);
        case 3:  return ((Fn)methodPtr)(thisObj, argv[0], argv[1], argv[2], u);
        default: return ((Fn)methodPtr)(thisObj, argv[0], argv[1], argv[2], argv[3]);
    }
}

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

    uint32_t totalSize = 16 + desc->numSlots * 8 + 8;  // header(8) + vtable(8) + slots + overflow ptr
    void* mem = ts_gc_alloc(totalSize);

    // Write header
    *(uint32_t*)mem = FLAT_MAGIC;
    *((uint32_t*)mem + 1) = shapeId;

    // Initialize vtable pointer to nullptr (object literals have no vtable)
    *(void**)((char*)mem + 8) = nullptr;

    // Initialize all slots to NaN-boxed undefined
    uint64_t* slots = (uint64_t*)((char*)mem + 16);
    for (uint32_t i = 0; i < desc->numSlots; i++) {
        slots[i] = NANBOX_UNDEFINED;
    }

    // Initialize overflow map to nullptr
    uint64_t* overflowSlot = (uint64_t*)((char*)mem + 16 + desc->numSlots * 8);
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
        uint64_t val = *(uint64_t*)((char*)obj + 16 + slot * 8);
        return (void*)(uintptr_t)val;
    }

    // Check overflow map
    void* overflow = *(void**)((char*)obj + 16 + desc->numSlots * 8);
    if (overflow) {
        TsMap* map = (TsMap*)overflow;
        // First, check for an accessor descriptor stored as __getter_<key>.
        // Object.defineProperty(o, key, {get}) stores here when o is flat.
        {
            std::string getterKey = std::string("__getter_") + key;
            TsString* gkStr = TsString::Create(getterKey.c_str());
            TsValue getterVal = map->Get(TsValue(gkStr));
            if (getterVal.type != ValueType::UNDEFINED) {
                extern TsValue* ts_function_call_with_this(TsValue*, TsValue*, int, TsValue**);
                extern TsValue* nanbox_from_tagged_impl(TsValue);
                TsValue* getterFunc = (TsValue*)(uintptr_t)((getterVal.type == ValueType::OBJECT_PTR ||
                    getterVal.type == ValueType::FUNCTION_PTR) ?
                    (uint64_t)(uintptr_t)getterVal.ptr_val : 0);
                if (!getterFunc) getterFunc = (TsValue*)getterVal.ptr_val;
                // obj is a NaN-boxed pointer already (caller passes raw flat-obj ptr)
                TsValue* result = ts_function_call_with_this(getterFunc, (TsValue*)obj, 0, nullptr);
                return (void*)result;
            }
        }
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

    // Check vtable methods (class instances have methods in their vtable).
    // The vtable is keyed by the original class method name. For accessor
    // declarations the compiler stores the entry with a `__getter_` /
    // `__setter_` prefix, so we need to probe both keys: a plain method
    // name match returns a bound method; a `__getter_<key>` match calls
    // the getter and returns its result.
    if (desc->numMethods > 0 && desc->methodNames) {
        void** vtable = *(void***)((char*)obj + 8); // vtable pointer at offset 8
        if (vtable) {
            // Build the getter-prefixed key once for the loop.
            std::string getterKey = std::string("__getter_") + key;
            for (uint32_t i = 0; i < desc->numMethods; i++) {
                const char* mname = desc->methodNames[i];
                if (!mname) continue;
                if (strcmp(mname, key) == 0) {
                    void* methodPtr = vtable[i + 1]; // +1 to skip parent vtable pointer
                    if (methodPtr) {
                        // Create a bound method: native function that calls methodPtr(this, args...)
                        void* mem = ts_gc_alloc(sizeof(BoundMethodCtx));
                        BoundMethodCtx* ctx = new (mem) BoundMethodCtx{obj, methodPtr};
                        return ts_value_make_native_function(
                            (void*)flat_bound_method_trampoline, ctx);
                    }
                }
                if (strcmp(mname, getterKey.c_str()) == 0) {
                    void* methodPtr = vtable[i + 1];
                    if (methodPtr) {
                        // Call the getter directly with `obj` as `this` and
                        // no further arguments. The lowered getter has the
                        // signature: TsValue* getter(TsValue* this).
                        using GetterFn = void* (*)(void*);
                        return ((GetterFn)methodPtr)(obj);
                    }
                }
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
        uint64_t* slotPtr = (uint64_t*)((char*)obj + 16 + slot * 8);
        *slotPtr = (uint64_t)(uintptr_t)value;
        ts_gc_write_barrier(slotPtr, value);
        return;
    }

    // Check the vtable for a `__setter_<key>` entry. Class accessor
    // declarations register their setters under this prefixed key in the
    // class's vtable; per spec, an assignment to such a property must
    // dispatch to the setter rather than installing a same-named data
    // property in the overflow map.
    if (desc->numMethods > 0 && desc->methodNames) {
        void** vtable = *(void***)((char*)obj + 8);
        if (vtable) {
            std::string setterKey = std::string("__setter_") + key;
            for (uint32_t i = 0; i < desc->numMethods; i++) {
                const char* mname = desc->methodNames[i];
                if (mname && strcmp(mname, setterKey.c_str()) == 0) {
                    void* methodPtr = vtable[i + 1];
                    if (methodPtr) {
                        using SetterFn = void (*)(void*, void*);
                        ((SetterFn)methodPtr)(obj, value);
                        return;
                    }
                }
            }
        }
    }

    // Property not in shape - use overflow map
    void** overflowPtr = (void**)((char*)obj + 16 + desc->numSlots * 8);
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
    void* overflow = *(void**)((char*)obj + 16 + desc->numSlots * 8);
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
    void* overflow = *(void**)((char*)obj + 16 + desc->numSlots * 8);
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
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
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
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
        // Include all shape slots, even when the slot value is
        // undefined. Per ECMA-262, every declared class field is an
        // own property — `class { x; y = 1; }` must report both `x`
        // and `y` for Object.getOwnPropertyDescriptor /
        // Object.keys / etc. Slots are pre-allocated by shape, so
        // NANBOX_UNDEFINED at this point reflects the field's value
        // (set explicitly by the constructor field-init pass).
        TsValue tv = nanbox_to_tagged((TsValue*)(uintptr_t)val);
        map->Set(TsValue(TsString::Create(desc->propNames[i])), tv);
    }

    // Copy overflow entries too
    void* overflow = *(void**)((char*)obj + 16 + desc->numSlots * 8);
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
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
        pair->Push((int64_t)val);
        entries->Push((int64_t)(uintptr_t)pair);
    }

    return entries;
}
