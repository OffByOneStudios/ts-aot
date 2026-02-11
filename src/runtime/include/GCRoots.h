#pragma once

#include <cstddef>

extern "C" {

// Initialize root pushing infrastructure.
// Must be called AFTER ts_stackmap_init().
// No-op when no stack maps are present (falls back to conservative scanning).
void ts_gc_roots_init();

// Push precise stack roots from LLVM statepoints into TsGC mark worklist.
// Called from TsGC mark phase. Falls back to no-op if no statepoints.
void ts_gc_push_precise_stack_roots();

// Diagnostic counters from the last GC collection
size_t ts_gc_roots_last_frames_walked();
size_t ts_gc_roots_last_safepoints_found();
size_t ts_gc_roots_last_roots_pushed();
size_t ts_gc_roots_total_collections();

}
