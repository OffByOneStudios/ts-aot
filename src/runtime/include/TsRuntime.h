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
void ts_console_log_value(TsValue* val);

// --- String ---
void* ts_string_create(const char* str);
void* ts_string_concat(void* a, void* b);
void* ts_string_from_int(int64_t val);
void* ts_string_from_double(double val);
void* ts_string_from_bool(bool val);
void* ts_string_from_value(TsValue* val);
int64_t ts_string_length(void* str);

// --- Array ---
int64_t ts_array_length(void* arr);

// --- Value Length ---
int64_t ts_value_length(TsValue* val);

// --- Exceptions ---
void* ts_push_exception_handler();
void ts_pop_exception_handler();
void ts_throw(void* exception);
void* ts_get_exception();

// --- Entry Point ---
int ts_main(int argc, char** argv, TsValue* (*user_main)(void*));
void* ts_get_process_argv();
void* ts_get_process_env();
void ts_process_exit(int64_t code);
void* ts_process_cwd();

// --- Value Creation ---
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_int(int64_t i);
TsValue* ts_value_make_double(double d);
TsValue* ts_value_make_bool(bool b);
TsValue* ts_value_make_string(void* s);
TsValue* ts_value_make_object(void* o);
void* ts_value_get_object(TsValue* v);
TsValue* ts_value_make_promise(void* p);
TsValue* ts_value_make_function(void* funcPtr, void* context);
void* ts_function_get_ptr(TsValue* val);

// --- Promises ---
TsValue* ts_promise_all(TsValue* iterable);
TsValue* ts_promise_race(TsValue* iterable);
TsValue* ts_promise_allSettled(TsValue* iterable);
TsValue* ts_promise_any(TsValue* iterable);
TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected);
TsValue* ts_call_0(TsValue* func);

}
