#include "TsWeakRef.h"
#include "GC.h"
#include "TsGC.h"
#include "TsRuntime.h"
#include <cstring>
#include <new>

// ============================================================================
// TsWeakRef Implementation
// ============================================================================
// With custom GC, WeakRef has true weak semantics.
// The GC clears ref->target to nullptr when the target is collected.

TsWeakRef::TsWeakRef() : target(nullptr) {
    TsObject::magic = MAGIC;
}

TsWeakRef* TsWeakRef::Create(void* target) {
    void* mem = ts_alloc(sizeof(TsWeakRef));
    TsWeakRef* ref = new (mem) TsWeakRef();
    ref->target = target;
    // Register the target field as a weak reference with the GC.
    // During collection, if target is unmarked, GC will set ref->target = nullptr.
    ts_gc_register_weak_ref(&ref->target);
    return ref;
}

// ============================================================================
// TsFinalizationRegistry Implementation
// ============================================================================
// With custom GC, finalizers are invoked when targets are collected.
// Cleanup callbacks are scheduled via process.nextTick (not called during GC).

TsFinalizationRegistry::TsFinalizationRegistry() : cleanupCallback(nullptr) {
    TsObject::magic = MAGIC;
}

TsFinalizationRegistry* TsFinalizationRegistry::Create(void* cleanupCallback) {
    void* mem = ts_alloc(sizeof(TsFinalizationRegistry));
    TsFinalizationRegistry* reg = new (mem) TsFinalizationRegistry();
    reg->cleanupCallback = cleanupCallback;
    return reg;
}

// ============================================================================
// C API Functions
// ============================================================================

extern "C" {

void* ts_weakref_create(void* target) {
    // Unbox if needed
    void* rawTarget = ts_value_get_object((TsValue*)target);
    if (!rawTarget) rawTarget = target;
    return TsWeakRef::Create(rawTarget);
}

void* ts_weakref_deref(void* weakref) {
    if (!weakref) return ts_value_make_undefined();
    TsWeakRef* ref = (TsWeakRef*)weakref;
    if (!ref->target) return ts_value_make_undefined();
    return ts_value_make_object(ref->target);
}

void* ts_finalization_registry_create(void* cleanupCallback) {
    return TsFinalizationRegistry::Create(cleanupCallback);
}

void ts_finalization_registry_register(void* registry, void* target, void* heldValue, void* unregisterToken) {
    if (!registry || !target) return;
    TsFinalizationRegistry* reg = (TsFinalizationRegistry*)registry;

    // Unbox target if needed
    void* rawTarget = ts_value_get_object((TsValue*)target);
    if (!rawTarget) rawTarget = target;

    // Register with the GC: when rawTarget is collected, schedule
    // reg->cleanupCallback(heldValue) via process.nextTick
    ts_gc_register_finalizer(rawTarget, reg->cleanupCallback, heldValue, unregisterToken);
}

bool ts_finalization_registry_unregister(void* registry, void* unregisterToken) {
    if (!registry || !unregisterToken) return false;
    return ts_gc_unregister_finalizer(unregisterToken);
}

} // extern "C"
