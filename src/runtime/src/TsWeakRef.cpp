#include "TsWeakRef.h"
#include "GC.h"
#include "TsRuntime.h"
#include <cstring>
#include <new>

// ============================================================================
// TsWeakRef Implementation
// ============================================================================
// With Boehm GC, true weak references are not possible.
// WeakRef holds a strong reference. deref() always returns the target
// unless the WeakRef was never properly initialized.

TsWeakRef::TsWeakRef() : target(nullptr) {
    TsObject::magic = MAGIC;
}

TsWeakRef* TsWeakRef::Create(void* target) {
    void* mem = ts_alloc(sizeof(TsWeakRef));
    TsWeakRef* ref = new (mem) TsWeakRef();
    ref->target = target;
    return ref;
}

// ============================================================================
// TsFinalizationRegistry Implementation
// ============================================================================
// Best-effort implementation. Since Boehm GC may not reliably call finalizers,
// the cleanup callback may not always fire. This allows code that uses
// FinalizationRegistry to compile and run, with callbacks firing on a
// best-effort basis.

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
    // Best-effort: register with Boehm GC finalizer
    // For now, this is a no-op since Boehm GC finalizers are unreliable
    // and calling back into JS from a GC finalizer is unsafe.
    // The API exists so code that uses FinalizationRegistry compiles.
    (void)registry;
    (void)target;
    (void)heldValue;
    (void)unregisterToken;
}

bool ts_finalization_registry_unregister(void* registry, void* unregisterToken) {
    // Best-effort: always returns false (nothing to unregister in no-op impl)
    (void)registry;
    (void)unregisterToken;
    return false;
}

} // extern "C"
