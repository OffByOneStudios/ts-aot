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

// Nursery support
bool ts_gc_is_nursery(void* ptr);     // Check if pointer is in nursery
void ts_gc_nursery_info(void** out_base, size_t* out_size);
void ts_gc_minor_collect();            // Trigger minor (nursery) collection
void ts_gc_write_barrier(void* slot_addr, void* stored_value);  // Card-marking barrier

// Allocate directly in old-gen, bypassing nursery.
// Use for STL container allocators whose internal pointer updates bypass write barriers.
void* ts_gc_alloc_old_gen(size_t size);

// Minor GC fixup callback: called during minor GC to fix up nursery pointers
// in external data structures (caches, registries) not covered by card table.
// Use ts_gc_minor_lookup_forward() inside the callback to resolve nursery pointers.
typedef void (*ts_gc_minor_fixup_callback)(void* context);
void ts_gc_register_minor_fixup(ts_gc_minor_fixup_callback cb, void* context);

// Resolve a nursery pointer to its new old-gen address during minor GC.
// Only valid inside a minor fixup callback. Returns the same pointer if pinned or not nursery.
void* ts_gc_minor_lookup_forward(void* ptr);

// Exported globals for compiler inline write barriers
extern uint64_t ts_gc_nursery_base;
extern uint64_t ts_gc_nursery_end;
extern uint8_t* ts_gc_card_table_ptr;
extern uint64_t ts_gc_card_table_base_addr;

}
