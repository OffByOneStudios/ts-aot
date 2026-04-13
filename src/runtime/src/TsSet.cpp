#include "TsSet.h"
#include "TsHashTable.h"
#include "TsWeakSet.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "GC.h"
#include "TsGC.h"
#include <string>
#include <cstring>
#include <new>
#include <functional>
#include <iostream>
#include <cmath>

void* TsSet_VTable[2] = { nullptr, nullptr };
extern "C" TsValue* ts_set_get_property(void* obj, void* propName);

TsSet* TsSet::Create() {
    void* mem = ts_alloc(sizeof(TsSet));
    TsSet* set = new(mem) TsSet();

    if (!TsSet_VTable[1]) {
        TsSet_VTable[1] = (void*)ts_set_get_property;
    }
    set->vtable = TsSet_VTable;

    return set;
}

TsSet::TsSet() {
    TsObject::magic = MAGIC;
    impl = TsHashTable::Create();
}

void TsSet::Add(TsValue value) {
    // For Set, store value as key with UNDEFINED as the map value
    TsValue undef;
    undef.type = ValueType::UNDEFINED;
    undef.i_val = 0;
    ((TsHashTable*)impl)->Set(value, undef);
}

bool TsSet::Has(TsValue value) {
    return ((TsHashTable*)impl)->Has(value);
}

bool TsSet::Delete(TsValue value) {
    return ((TsHashTable*)impl)->Delete(value);
}

void TsSet::Clear() {
    ((TsHashTable*)impl)->Clear();
}

int64_t TsSet::Size() {
    return static_cast<int64_t>(((TsHashTable*)impl)->Size());
}

void* TsSet::GetValues() {
    auto* ht = (TsHashTable*)impl;
    TsArray* values = TsArray::Create(ht->Size());
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        values->Push((int64_t)(uintptr_t)nanbox_from_tagged(key));
    });
    return values;
}

void TsSet::ForEach(void* callback, void* thisArg) {
    if (!callback) return;
    TsValue* cbVal = (TsValue*)callback;

    auto* ht = (TsHashTable*)impl;
    ht->ForEach([&](const TsValue& key, const TsValue& val) {
        TsValue* v1 = nanbox_from_tagged(key);
        TsValue* v2 = nanbox_from_tagged(key);
        TsValue* s = ts_value_make_object(this);
        ts_call_3(cbVal, v1, v2, s);
    });
}

extern "C" {

void* ts_set_create() {
    return TsSet::Create();
}

void ts_set_add(void* set, TsValue* value) {
    if (!set) return;
    TsValue v = nanbox_to_tagged(value);
    ((TsSet*)set)->Add(v);
}

bool ts_set_has(void* set, TsValue* value) {
    if (!set) return false;
    TsValue v = nanbox_to_tagged(value);
    return ((TsSet*)set)->Has(v);
}

bool ts_set_delete(void* set, TsValue* value) {
    if (!set) return false;
    TsValue v = nanbox_to_tagged(value);
    return ((TsSet*)set)->Delete(v);
}

void ts_set_clear(void* set) {
    if (!set) return;
    ((TsSet*)set)->Clear();
}

int64_t ts_set_size(void* set) {
    if (!set) return 0;
    return ((TsSet*)set)->Size();
}

void* ts_set_values(void* set) {
    if (!set) return nullptr;
    return ((TsSet*)set)->GetValues();
}

void ts_set_forEach(void* set, void* callback, void* thisArg) {
    if (!set) return;
    ((TsSet*)set)->ForEach(callback, thisArg);
}

// Validate that ctx points to a TsSet (has [[SetData]] internal slot).
// Per ES spec, Set methods must throw TypeError if `this` is not a Set.
extern "C" void ts_throw(TsValue* err);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

static void* requireSet(void* context, const char* methodName) {
    if (!context) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    void* rawCtx = context;
    uint64_t nb = (uint64_t)(uintptr_t)context;
    if (nanbox_is_ptr(nb) && nb > NANBOX_UNDEFINED) {
        rawCtx = nanbox_to_ptr(nb);
    }
    if (!rawCtx) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    // Check for TsSet magic at multiple offsets (TsSet extends TsObject → magic at 16, 20, or 24)
    uint32_t m16 = *(uint32_t*)((char*)rawCtx + 16);
    uint32_t m20 = *(uint32_t*)((char*)rawCtx + 20);
    uint32_t m24 = *(uint32_t*)((char*)rawCtx + 24);
    if (m16 != TsSet::MAGIC && m20 != TsSet::MAGIC && m24 != TsSet::MAGIC) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Set method called on incompatible receiver"));
        return nullptr;
    }
    return rawCtx;
}

TsValue* ts_set_add_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "add");
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_add(rawCtx, value);
    return ts_value_make_object(rawCtx);
}

TsValue* ts_set_has_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "has");
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_has(rawCtx, value));
}

TsValue* ts_set_delete_wrapper(void* context, TsValue* value) {
    void* rawCtx = requireSet(context, "delete");
    if (!rawCtx) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_set_delete(rawCtx, value));
}

TsValue* ts_set_clear_wrapper(void* context) {
    void* rawCtx = requireSet(context, "clear");
    if (!rawCtx) return ts_value_make_undefined();
    ts_set_clear(rawCtx);
    return ts_value_make_undefined();
}

TsValue* ts_set_size_wrapper(void* context) {
    void* rawCtx = requireSet(context, "size");
    if (!rawCtx) return ts_value_make_int(0);
    return ts_value_make_int(ts_set_size(rawCtx));
}

TsValue* ts_set_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();

    if (strcmp(name, "add") == 0) {
        return ts_value_make_function((void*)ts_set_add_wrapper, obj);
    } else if (strcmp(name, "has") == 0) {
        return ts_value_make_function((void*)ts_set_has_wrapper, obj);
    } else if (strcmp(name, "delete") == 0) {
        return ts_value_make_function((void*)ts_set_delete_wrapper, obj);
    } else if (strcmp(name, "clear") == 0) {
        return ts_value_make_function((void*)ts_set_clear_wrapper, obj);
    } else if (strcmp(name, "size") == 0) {
        return ts_value_make_int(ts_set_size(obj));
    }

    return ts_value_make_undefined();
}

}

// ============================================================================
// TsWeakSet Implementation
// ============================================================================

TsWeakSet* TsWeakSet::Create() {
    void* mem = ts_alloc(sizeof(TsWeakSet));
    TsWeakSet* set = new(mem) TsWeakSet();

    if (!TsSet_VTable[1]) {
        TsSet_VTable[1] = (void*)ts_set_get_property;
    }
    set->vtable = TsSet_VTable;

    return set;
}

TsWeakSet::TsWeakSet() : TsSet() {
    TsObject::magic = MAGIC;
}
