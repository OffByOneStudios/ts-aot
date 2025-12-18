#pragma once
#include <cstddef>
#include <cstdint>
#include "TsObject.h"

class TsString;

extern "C" {

// --- Memory Management ---
void* ts_alloc(size_t size);
void ts_gc_init();

// --- Event Loop ---
void ts_loop_init();
void ts_loop_run();
void ts_queue_microtask(void (*callback)(void*), void* data);
void ts_run_microtasks();

// --- Console ---
void ts_console_log(TsString* str);
void ts_console_log_int(int64_t val);
void ts_console_log_double(double val);
void ts_console_log_bool(bool val);

// --- String ---
void* ts_string_create(const char* str);
void* ts_string_concat(void* a, void* b);
void* ts_string_from_int(int64_t val);
void* ts_string_from_double(double val);
void* ts_string_from_bool(bool val);

// --- Exceptions ---
void* ts_push_exception_handler();
void ts_pop_exception_handler();
void ts_throw(void* exception);
void* ts_get_exception();

// --- Entry Point ---
int ts_main(int argc, char** argv, void (*user_main)());

// --- Value Creation ---
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_int(int64_t i);
TsValue* ts_value_make_double(double d);
TsValue* ts_value_make_bool(bool b);
TsValue* ts_value_make_string(void* s);
TsValue* ts_value_make_object(void* o);
TsValue* ts_value_make_function(void* f, void* ctx);
void* ts_function_get_ptr(TsValue* val);

}
