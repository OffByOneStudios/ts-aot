#pragma once
#include <cstddef>

class TsString;

extern "C" {

// --- Memory Management ---
void* ts_alloc(size_t size);
void ts_gc_init();

// --- Event Loop ---
void ts_loop_init();
void ts_loop_run();

// --- Console ---
void ts_console_log(TsString* str);

// --- Entry Point ---
int ts_main(int argc, char** argv, void (*user_main)());

}
