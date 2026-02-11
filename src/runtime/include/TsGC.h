#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

// Allocate GC-tracked memory. Returns zeroed memory. Thread-safe.
// May trigger collection if allocation threshold exceeded.
void* ts_gc_alloc(size_t size);

// Pointer validation: returns base of containing GC object, or nullptr.
// Replaces GC_base(). Used for interior pointer detection and pointer validation.
void* ts_gc_base(void* ptr);

// Realloc for tommath (BigInt): allocate new, copy, abandon old.
void* ts_gc_realloc(void* ptr, size_t old_size, size_t new_size);

// Global root registration: location is a pointer-to-pointer that the GC
// will dereference during mark phase to find live objects.
void ts_gc_register_root(void** location);
void ts_gc_unregister_root(void** location);

// Custom root scanner: callback invoked during mark phase.
// Use ts_gc_mark() inside the callback to mark objects as live.
typedef void (*ts_gc_scan_callback)(void* context);
void ts_gc_register_scanner(ts_gc_scan_callback cb, void* context);

// Mark an object as live during collection. Called from scanner callbacks.
void ts_gc_mark_object(void* ptr);

// Weak reference support
void ts_gc_register_weak_ref(void** location);
void ts_gc_unregister_weak_ref(void** location);

// Finalization support
void ts_gc_register_finalizer(void* target, void* callback,
                              void* held_value, void* unregister_token);
bool ts_gc_unregister_finalizer(void* unregister_token);

// Force a full collection cycle
void ts_gc_force_collect();

// Diagnostics
size_t ts_gc_heap_size();       // Total allocated bytes
size_t ts_gc_live_size();       // Bytes surviving last collection
size_t ts_gc_collection_count();

}
