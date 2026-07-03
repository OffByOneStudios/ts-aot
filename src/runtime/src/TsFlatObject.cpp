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
    // GC-001 Phase 3a: tenure object literals to old-gen so they never move.
    // The minor GC roots the stack conservatively-only, so a flat object held
    // solely in a callee-saved register / unspilled slot across a minor GC is
    // promoted (moved) without its holder being forwarded -> stale pointer ->
    // blanked fields. Old-gen objects never move, sidestepping the defect
    // (same mechanism that fixed closures/cells in BUG 4). See [[GC-001]].
    void* mem = ts_gc_alloc_old_gen(totalSize);

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

extern "C" TsValue* ts_object_proto_dynamic_lookup(const char* key);
extern "C" TsValue* ts_object_proto_dynamic_lookup_recv(const char* key, TsValue* recv);

extern "C" void* ts_flat_object_get_property(void* obj, const char* key) {
    if (!obj || !key) return (void*)(uintptr_t)NANBOX_UNDEFINED;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return (void*)(uintptr_t)NANBOX_UNDEFINED;

    // Check inline slots. DELETED sentinel means the slot was removed via
    // `delete` — treat as absent so vtable/overflow lookup can proceed
    // (and ultimately undefined). This matches the spec where deleting a
    // configurable own property removes it from the object entirely.
    int slot = flat_find_slot(desc, key);
    if (slot >= 0) {
        uint64_t val = *(uint64_t*)((char*)obj + 16 + slot * 8);
        if (val != NANBOX_DELETED) {
            return (void*)(uintptr_t)val;
        }
        // Slot tombstoned — fall through to overflow/vtable. Note: if a
        // shape includes both a slot named X and a method named X
        // (unusual but possible if the user re-adds), the method may now
        // resolve. That matches the spec since the slot is fully gone.
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
            // Convert TaggedValue back to a NaN-boxed TsValue*. Use the shared
            // tagged->nanbox converter so EVERY type round-trips — the old
            // per-type ladder (int/double/bool/ptr) silently dropped a stored
            // `null` (its ptr_val is 0, so the `else if (result.ptr_val)` arm
            // failed) and returned undefined. So `var o={a:1}; o.c=null; o.c`
            // read undefined instead of null (broke lodash _.assign of nullish
            // source values, which then compared unequal under _.isEqual).
            return (void*)nanbox_from_tagged(result);
        }
    }

    // Check vtable methods (class instances have methods in their vtable).
    // The vtable is keyed by the original class method name. For accessor
    // declarations the compiler stores the entry with a `__getter_` /
    // `__setter_` prefix, so we need to probe both keys.
    //
    // For plain method-name hits, we want spec-correct identity: the value
    // returned must be the SAME function object that lives on
    // `<Class>.prototype`. That guarantees `c.m === C.prototype.m` per
    // ECMA-262 §10.1.5 OrdinaryGet via the prototype chain. To do this we
    // need the prototype map; we obtain it from
    // `desc->constructorSlot` (set by HIRToLLVM shape registration to the
    // address of __closure_cache_<ClassName>_constructor), then read its
    // `prototype` property to get the prototype TsMap, then read `key` on
    // that map. The previous code synthesized a fresh BoundMethodCtx every
    // call, which broke identity and `verifyProperty(C.prototype, ...)`
    // tests that compare `c.m` to `C.prototype.m`.
    //
    // Accessor (__getter_<key>) hits still call directly through the
    // vtable since accessors return computed values, not function objects
    // whose identity matters.
    // Accessor (__getter_<key>) handler runs first — accessors return a
    // computed value, not a function whose identity matters.
    if (desc->numMethods > 0 && desc->methodNames) {
        void** vtable = *(void***)((char*)obj + 8); // vtable pointer at offset 8
        if (vtable) {
            std::string getterKey = std::string("__getter_") + key;
            for (uint32_t i = 0; i < desc->numMethods; i++) {
                const char* mname = desc->methodNames[i];
                if (!mname) continue;
                if (strcmp(mname, getterKey.c_str()) == 0) {
                    void* methodPtr = vtable[i + 1];
                    if (methodPtr) {
                        using GetterFn = void* (*)(void*);
                        return ((GetterFn)methodPtr)(obj);
                    }
                }
            }
        }
    }

    // Prototype-chain lookup via ShapeDescriptor::constructorSlot (set by
    // HIRToLLVM to the address of __closure_cache_<ClassName>_constructor).
    // This handles BOTH:
    //   - Class methods (`c.m` returns the same function as
    //     `C.prototype.m`) — ECMA-262 §10.1.5 OrdinaryGet
    //   - Non-method properties on the prototype chain like `constructor`,
    //     methods inherited from a base class via Derived.prototype's
    //     [[Prototype]] link (set in emitDeferredStaticInits).
    // ts_object_get_property walks TsMap prototype chains automatically, so
    // reading `key` from the prototype map cascades to Base.prototype, etc.
    if (desc->constructorSlot) {
        TsValue* ctorVal = *(TsValue**)desc->constructorSlot;
        if (ctorVal) {
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            TsValue* protoVal = ts_object_get_property((void*)ctorVal, "prototype");
            if (protoVal) {
                uint64_t protoNb = nanbox_from_tsvalue_ptr(protoVal);
                if (nanbox_is_ptr(protoNb)) {
                    void* protoObj = nanbox_to_ptr(protoNb);
                    if (protoObj) {
                        TsValue* method = ts_object_get_property(protoObj, key);
                        uint64_t methodNb = nanbox_from_tsvalue_ptr(method);
                        if (methodNb != NANBOX_UNDEFINED) {
                            return (void*)method;
                        }
                    }
                }
            }
        }
    }

    // Fallback for compatibility: synthesize BoundMethodCtx when the
    // prototype path isn't available. This preserves behavior for shapes
    // without a constructorSlot back-pointer (object literals, edge cases).
    if (desc->numMethods > 0 && desc->methodNames) {
        void** vtable = *(void***)((char*)obj + 8);
        if (vtable) {
            for (uint32_t i = 0; i < desc->numMethods; i++) {
                const char* mname = desc->methodNames[i];
                if (!mname) continue;
                if (strcmp(mname, key) == 0) {
                    void* methodPtr = vtable[i + 1];
                    if (methodPtr) {
                        void* mem = ts_gc_alloc(sizeof(BoundMethodCtx));
                        BoundMethodCtx* ctx = new (mem) BoundMethodCtx{obj, methodPtr};
                        return ts_value_make_native_function(
                            (void*)flat_bound_method_trampoline, ctx);
                    }
                }
            }
        }
    }

    // #66: dynamic Object.prototype inheritance. Gate: the overflow map may
    // own the key as a data entry holding undefined or as a set-only
    // accessor (__setter_<key>) — either SHADOWS the inherited property
    // (getter entries returned above already).
    {
        bool ownsKey = false;
        void* ovf = *(void**)((char*)obj + 16 + desc->numSlots * 8);
        if (ovf) {
            TsMap* m = (TsMap*)ovf;
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(key);
            if (m->Has(k)) ownsKey = true;
            if (!ownsKey) {
                std::string sk = std::string("__setter_") + key;
                k.ptr_val = TsString::Create(sk.c_str());
                ownsKey = m->Has(k);
            }
        }
        if (!ownsKey) {
            // obj is a NaN-boxed pointer already (see the getter call above)
            if (TsValue* pv = ts_object_proto_dynamic_lookup_recv(key, (TsValue*)obj))
                return (void*)pv;
        }
    }
    return (void*)(uintptr_t)NANBOX_UNDEFINED;
}

extern "C" void ts_flat_object_set_property_ex(void* obj, const char* key,
                                               void* value, int strict,
                                               int* violated);

extern "C" void ts_flat_object_set_property(void* obj, const char* key, void* value) {
    int dummy = 0;
    ts_flat_object_set_property_ex(obj, key, value, 0, &dummy);
}

// ECMA-262 9.1.9 [[Set]]: a blocked write (frozen receiver, or a sealed
// receiver gaining a NEW property) silently no-ops in sloppy mode; in strict
// mode *violated is raised and the CALLER (ts_object_set_dynamic — a clean
// frame per the longjmp-stdstring rule) throws TypeError.
extern "C" void ts_flat_object_set_property_ex(void* obj, const char* key,
                                               void* value, int strict,
                                               int* violated) {
    if (!obj || !key) return;

    if (flat_object_is_frozen(obj)) {
        if (strict) *violated = 1;
        return;
    }

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return;

    // Check inline slots first
    int slot = flat_find_slot(desc, key);
    if (slot >= 0) {
        uint64_t* slotPtr = (uint64_t*)((char*)obj + 16 + slot * 8);
        // ES property-insertion order: a DELETED slot's name that is
        // re-assigned becomes the NEWEST property — re-adding in place would
        // resurrect its original enumeration position (Object.keys
        // return-order). Route the re-add to the overflow map, which is
        // insertion-ordered and already consulted by get/has/keys.
        if (*slotPtr == NANBOX_DELETED) {
            slot = -1;   // fall through to the overflow store below
        } else {
            *slotPtr = (uint64_t)(uintptr_t)value;
            ts_gc_write_barrier(slotPtr, value);
            return;
        }
    }

    // Sealed/non-extensible objects can update existing properties (seal
    // leaves [[Writable]] intact) but can't ADD new ones. Existing overflow
    // properties are handled below (the overflow store honors seal via
    // sealedRecv); only bail here when there is no overflow map at all.
    bool sealedRecv = flat_object_is_sealed(obj);

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

    // Computed / dynamically-installed setters live on the CLASS PROTOTYPE map
    // (`set [k](v)`), NOT in the instance vtable, so the vtable scan above misses
    // them and `inst[key] = v` silently stored a data property instead of running
    // the setter. Walk to the prototype the same way the get path does
    // (desc->constructorSlot -> ctor -> "prototype") and dispatch `__setter_<key>`
    // before falling through to the overflow store. Pass the RAW flat-object
    // pointer (TsValue*)obj as `this` (already a valid NaN-boxed pointer) and the
    // already-NaN-boxed `value` as the sole arg (matches the working getter/MAPS
    // walk convention).
    if (desc->constructorSlot) {
        TsValue* ctorVal = *(TsValue**)desc->constructorSlot;
        if (ctorVal) {
            extern TsValue* ts_object_get_property(void* o, const char* k);
            extern TsValue* ts_function_call_with_this(TsValue*, TsValue*, int, TsValue**);
            TsValue* protoVal = ts_object_get_property((void*)ctorVal, "prototype");
            if (protoVal) {
                uint64_t protoNb = nanbox_from_tsvalue_ptr(protoVal);
                if (nanbox_is_ptr(protoNb)) {
                    void* protoObj = nanbox_to_ptr(protoNb);
                    if (protoObj) {
                        std::string setterKey = std::string("__setter_") + key;
                        TsValue* setterVal = ts_object_get_property(protoObj, setterKey.c_str());
                        if (setterVal && nanbox_from_tsvalue_ptr(setterVal) != NANBOX_UNDEFINED) {
                            TsValue* argv[] = { (TsValue*)value };
                            ts_function_call_with_this(setterVal, (TsValue*)obj, 1, argv);
                            return;
                        }
                    }
                }
            }
        }
    }

    // Property not in shape - use overflow map
    void** overflowPtr = (void**)((char*)obj + 16 + desc->numSlots * 8);
    TsMap* overflow = (TsMap*)*overflowPtr;
    if (!overflow) {
        // A sealed object cannot gain its first overflow property.
        if (sealedRecv) {
            if (strict) *violated = 1;
            return;
        }
        overflow = TsMap::Create();
        *overflowPtr = overflow;
        ts_gc_write_barrier(overflowPtr, overflow);
    }

    // Convert NaN-boxed value to TaggedValue for TsMap storage
    TsString* keyStr = TsString::Create(key);
    {
        TsValue kv(keyStr);
        bool exists = overflow->Has(kv);
        // Sealed: existing properties stay writable, NEW ones are rejected.
        if (sealedRecv && !exists) {
            if (strict) *violated = 1;
            return;
        }
        // OrdinarySet: honor writable:false on an existing overflow property
        // (defineProperty on a flat object stores its descriptor here).
        if (exists) {
            constexpr uint8_t ATTR_WRITABLE = 0x02;
            uint8_t a = overflow->GetPropertyAttrs(kv);
            if (!(a & ATTR_WRITABLE)) {
                if (strict) *violated = 1;
                return;  // silent fail (non-strict)
            }
        }
    }
    TsValue tv = nanbox_to_tagged((TsValue*)value);
    overflow->Set(TsValue(keyStr), tv);
}

extern "C" bool ts_flat_object_has_property(void* obj, const char* key) {
    if (!obj || !key) return false;

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return false;

    // Check inline slots — but skip tombstoned (deleted) slots so that
    // hasOwnProperty(obj, key) returns false after `delete obj[key]`.
    int slotIdx = flat_find_slot(desc, key);
    if (slotIdx >= 0) {
        uint64_t val = *(uint64_t*)((char*)obj + 16 + slotIdx * 8);
        if (val != NANBOX_DELETED) return true;
        // Fall through to overflow lookup — a later Set could have stored
        // the key there even though the inline slot is dead.
    }

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

// Defined in TsObject.cpp: true iff a property-key string is the internal
// storage key for a user Symbol ("\x01@@sym\x01<index>").
extern "C" int ts_is_user_symbol_storage_key(const char* k);
extern "C" void* ts_value_get_string(TsValue* v);

// symbolsOnly=false -> the string-keyed own properties (Object.keys / for-in),
// excluding user-symbol storage keys. symbolsOnly=true -> only user-symbol
// storage keys (for Object.getOwnPropertySymbols).
static void* flat_object_keys_impl(void* obj, bool symbolsOnly) {
    if (!obj) return TsArray::Create(0);

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return TsArray::Create(0);

    TsArray* keys = TsArray::Create(desc->numSlots + 4);

    for (uint32_t i = 0; i < desc->numSlots; i++) {
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
        if (val == NANBOX_DELETED) continue;  // tombstoned by delete
        bool isSym = ts_is_user_symbol_storage_key(desc->propNames[i]) != 0;
        // Any other '\x01'-prefixed slot is an internal storage key
        // (private fields "\x01#x") — never an enumerable property key.
        if (!isSym && desc->propNames[i] && desc->propNames[i][0] == '\x01') continue;
        if (isSym != symbolsOnly) continue;
        TsString* name = TsString::Create(desc->propNames[i]);
        keys->Push((int64_t)(uintptr_t)name);
    }

    // Add overflow keys
    void* overflow = *(void**)((char*)obj + 16 + desc->numSlots * 8);
    if (overflow) {
        TsArray* overflowKeys = (TsArray*)ts_map_keys(overflow);
        if (overflowKeys) {
            for (int64_t i = 0; i < overflowKeys->Length(); i++) {
                int64_t boxed = overflowKeys->Get(i);
                void* sp = ts_value_get_string((TsValue*)(intptr_t)boxed);
                const char* kc = sp ? ((TsString*)sp)->ToUtf8() : nullptr;
                bool isSym = kc && ts_is_user_symbol_storage_key(kc) != 0;
                if (!isSym && kc && kc[0] == '\x01') continue;  // internal storage key
                if (isSym != symbolsOnly) continue;
                keys->Push(boxed);
            }
        }
    }

    return keys;
}

extern "C" void* ts_flat_object_keys(void* obj) {
    return flat_object_keys_impl(obj, false);
}

// Own user-Symbol storage keys (as strings) of a flat object.
extern "C" void* ts_flat_object_symbol_keys(void* obj) {
    return flat_object_keys_impl(obj, true);
}

extern "C" void* ts_flat_object_values(void* obj) {
    if (!obj) return TsArray::Create(0);

    uint32_t shapeId = flat_object_shape_id(obj);
    ShapeDescriptor* desc = ts_shape_lookup(shapeId);
    if (!desc) return TsArray::Create(0);

    TsArray* values = TsArray::Create(desc->numSlots + 4);

    for (uint32_t i = 0; i < desc->numSlots; i++) {
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
        if (val == NANBOX_DELETED) continue;  // tombstoned by delete
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
        // Skip DELETED — those slots were removed via `delete` and
        // must not surface in the demoted TsMap.
        if (val == NANBOX_DELETED) continue;
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
        uint64_t val = *(uint64_t*)((char*)obj + 16 + i * 8);
        if (val == NANBOX_DELETED) continue;  // tombstoned by delete
        TsArray* pair = TsArray::Create(2);
        TsString* name = TsString::Create(desc->propNames[i]);
        pair->Push((int64_t)(uintptr_t)name);
        pair->Push((int64_t)val);
        entries->Push((int64_t)(uintptr_t)pair);
    }

    return entries;
}
