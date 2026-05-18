#pragma once

#include "TsObject.h"

// TsWeakRef holds a reference to a target object.
// Note: ts-aot's GC does not currently support weak references, so this is
// effectively a strong reference distinguished only by magic.
// deref() returns the target or undefined if explicitly cleared.
class TsWeakRef : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x57524546; // "WREF"
    static TsWeakRef* Create(void* target);

    void* target;  // The referenced object (or nullptr if cleared)

private:
    TsWeakRef();
};

// TsFinalizationRegistry allows registering cleanup callbacks for objects.
// Note: ts-aot's GC does not currently support finalizers, so the registry
// stores callbacks but never invokes them (no-op best-effort).
class TsFinalizationRegistry : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x46494E52; // "FINR"
    static TsFinalizationRegistry* Create(void* cleanupCallback);

    void* cleanupCallback;  // The callback function to call when targets are collected

private:
    TsFinalizationRegistry();
};

extern "C" {
    // WeakRef
    void* ts_weakref_create(void* target);
    void* ts_weakref_deref(void* weakref);

    // FinalizationRegistry
    void* ts_finalization_registry_create(void* cleanupCallback);
    void ts_finalization_registry_register(void* registry, void* target, void* heldValue, void* unregisterToken);
    bool ts_finalization_registry_unregister(void* registry, void* unregisterToken);
}
