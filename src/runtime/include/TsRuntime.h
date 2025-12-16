#pragma once

extern "C" {

// --- Memory Management ---
void* ts_alloc(size_t size);
void ts_gc_init();

// --- Event Loop ---
void ts_loop_init();
void ts_loop_run();

}
