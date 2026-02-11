#pragma once

#include <cstddef>

extern "C" {

// Initialize precise root pushing. Hooks into Boehm's push_other_roots.
// Must be called AFTER GC_INIT() and ts_stackmap_init().
// No-op when no stack maps are present.
void ts_gc_roots_init();

// Diagnostic counters from the last GC collection
size_t ts_gc_roots_last_frames_walked();
size_t ts_gc_roots_last_safepoints_found();
size_t ts_gc_roots_last_roots_pushed();
size_t ts_gc_roots_total_collections();

}
