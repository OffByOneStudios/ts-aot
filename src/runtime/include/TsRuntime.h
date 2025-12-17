#pragma once
#include <cstddef>
#include <cstdint>

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
void ts_console_log_int(int64_t val);
void ts_console_log_double(double val);
void ts_console_log_bool(bool val);

// --- String ---
TsString* ts_string_create(const char* str);

// --- Entry Point ---
int ts_main(int argc, char** argv, void (*user_main)());

}
