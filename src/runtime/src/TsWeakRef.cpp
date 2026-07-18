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

extern "C" bool ts_can_be_held_weakly(TsValue* key);  // defined in TsMap.cpp
extern "C" void* ts_error_create_typed(const char* type, const char* message);
extern "C" void* ts_get_call_this();  // defined in TsObject.cpp

extern "C" {

void* ts_weakref_create(void* target) {
    // ECMA-262 26.1.1.1: target must satisfy CanBeHeldWeakly (Object or
    // non-registered Symbol), else TypeError — a primitive target was accepted.
    if (!ts_can_be_held_weakly((TsValue*)target)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "WeakRef: target must be an object or an unregistered symbol"));
        return nullptr;  // unreachable
    }
    // Unbox if needed
    void* rawTarget = ts_nanbox_safe_unbox(target);
    return TsWeakRef::Create(rawTarget);
}

void* ts_weakref_deref(void* weakref) {
    if (!weakref) return ts_value_make_undefined();
    TsWeakRef* ref = (TsWeakRef*)weakref;
    if (!ref->target) return ts_value_make_undefined();
    return ts_value_make_object(ref->target);
}

void* ts_finalization_registry_create(void* cleanupCallback) {
    // ECMA-262 26.2.1.1: the cleanup callback must be callable, else TypeError.
    if (!ts_is_callable(cleanupCallback)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "FinalizationRegistry: cleanup callback must be a function"));
        return nullptr;  // unreachable
    }
    return TsFinalizationRegistry::Create(cleanupCallback);
}

void ts_finalization_registry_register(void* registry, void* target, void* heldValue, void* unregisterToken) {
    if (!registry || !target) return;
    TsFinalizationRegistry* reg = (TsFinalizationRegistry*)registry;

    // Unbox target if needed
    void* rawTarget = ts_nanbox_safe_unbox(target);

    // Register with the GC: when rawTarget is collected, schedule
    // reg->cleanupCallback(heldValue) via process.nextTick
    ts_gc_register_finalizer(rawTarget, reg->cleanupCallback, heldValue, unregisterToken);
}

bool ts_finalization_registry_unregister(void* registry, void* unregisterToken) {
    if (!registry || !unregisterToken) return false;
    return ts_gc_unregister_finalizer(unregisterToken);
}

// ---- Prototype-method natives (ES 26.1.3.2 deref, 26.2.3.1 register,
// 26.2.3.2 unregister). Receiver-dispatched via ts_get_call_this so they also
// work when pulled off the prototype and .call()ed. Native ABI:
// TsValue*(void* ctx, int argc, TsValue** argv). ts_make_named_native_function
// builds them as NON-constructors (built-ins/*/not-a-constructor.js).
static void* weakref_recv(uint32_t magic16, const char* msg) {
    void* self = ts_get_call_this();
    void* raw = self ? ts_nanbox_safe_unbox(self) : nullptr;
    if (raw && (uintptr_t)raw >= 4096 &&
        (uintptr_t)raw < 0x0000800000000000ULL &&
        *(uint32_t*)((char*)raw + 16) == magic16) {
        return raw;
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
    return nullptr;  // unreachable
}

TsValue* ts_weakref_deref_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx; (void)argc; (void)argv;
    void* ref = weakref_recv(TsWeakRef::MAGIC,
        "WeakRef.prototype.deref called on incompatible receiver");
    return (TsValue*)ts_weakref_deref(ref);
}

TsValue* ts_fr_register_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    void* reg = weakref_recv(TsFinalizationRegistry::MAGIC,
        "FinalizationRegistry.prototype.register called on incompatible receiver");
    void* target = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
    void* held   = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
    void* token  = (argc >= 3 && argv) ? (void*)argv[2] : nullptr;
    ts_finalization_registry_register(reg, target, held, token);
    return (TsValue*)ts_value_make_undefined();
}

TsValue* ts_fr_unregister_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    void* reg = weakref_recv(TsFinalizationRegistry::MAGIC,
        "FinalizationRegistry.prototype.unregister called on incompatible receiver");
    void* token = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
    return (TsValue*)ts_value_make_bool(ts_finalization_registry_unregister(reg, token));
}

} // extern "C"
