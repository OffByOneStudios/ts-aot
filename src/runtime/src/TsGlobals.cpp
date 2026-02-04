// TsGlobals.cpp - Global object getters for HIR pipeline
//
// These functions return pointers to global objects that are used
// by the LoadGlobal HIR opcode. For some globals (like console),
// the actual method handling is done in the HIR->LLVM lowering.

#include "GC.h"
#include "TsObject.h"

extern "C" {

// Console global - returns a sentinel; actual handling is in HIR->LLVM lowering
void* ts_get_global_console() {
    // Return a sentinel value - console methods are handled specially
    static const char sentinel[] = "console";
    return (void*)sentinel;
}

// Math global - returns the Math object (if implemented)
void* ts_get_global_Math() {
    static const char sentinel[] = "Math";
    return (void*)sentinel;
}

// JSON global - returns the JSON object (if implemented)
void* ts_get_global_JSON() {
    static const char sentinel[] = "JSON";
    return (void*)sentinel;
}

// Object constructor/global
void* ts_get_global_Object() {
    static const char sentinel[] = "Object";
    return (void*)sentinel;
}

// Array constructor/global
void* ts_get_global_Array() {
    static const char sentinel[] = "Array";
    return (void*)sentinel;
}

// String constructor/global
void* ts_get_global_String() {
    static const char sentinel[] = "String";
    return (void*)sentinel;
}

// Number constructor/global
void* ts_get_global_Number() {
    static const char sentinel[] = "Number";
    return (void*)sentinel;
}

// Boolean constructor/global
void* ts_get_global_Boolean() {
    static const char sentinel[] = "Boolean";
    return (void*)sentinel;
}

// Date constructor/global
void* ts_get_global_Date() {
    static const char sentinel[] = "Date";
    return (void*)sentinel;
}

// RegExp constructor/global
void* ts_get_global_RegExp() {
    static const char sentinel[] = "RegExp";
    return (void*)sentinel;
}

// Promise constructor/global
void* ts_get_global_Promise() {
    static const char sentinel[] = "Promise";
    return (void*)sentinel;
}

// Error constructor/global
void* ts_get_global_Error() {
    static const char sentinel[] = "Error";
    return (void*)sentinel;
}

// Buffer constructor/global
void* ts_get_global_Buffer() {
    static const char sentinel[] = "Buffer";
    return (void*)sentinel;
}

// process global
void* ts_get_global_process() {
    static const char sentinel[] = "process";
    return (void*)sentinel;
}

// globalThis / global
void* ts_get_global_globalThis() {
    static const char sentinel[] = "globalThis";
    return (void*)sentinel;
}

// ========================================
// Node.js module globals
// These return sentinel values - actual method handling is in HIR->LLVM lowering
// ========================================

void* ts_get_global_path() {
    static const char sentinel[] = "path";
    return (void*)sentinel;
}

void* ts_get_global_fs() {
    static const char sentinel[] = "fs";
    return (void*)sentinel;
}

void* ts_get_global_os() {
    static const char sentinel[] = "os";
    return (void*)sentinel;
}

void* ts_get_global_url() {
    static const char sentinel[] = "url";
    return (void*)sentinel;
}

void* ts_get_global_util() {
    static const char sentinel[] = "util";
    return (void*)sentinel;
}

void* ts_get_global_crypto() {
    static const char sentinel[] = "crypto";
    return (void*)sentinel;
}

void* ts_get_global_http() {
    static const char sentinel[] = "http";
    return (void*)sentinel;
}

void* ts_get_global_https() {
    static const char sentinel[] = "https";
    return (void*)sentinel;
}

void* ts_get_global_net() {
    static const char sentinel[] = "net";
    return (void*)sentinel;
}

void* ts_get_global_dgram() {
    static const char sentinel[] = "dgram";
    return (void*)sentinel;
}

void* ts_get_global_dns() {
    static const char sentinel[] = "dns";
    return (void*)sentinel;
}

void* ts_get_global_tls() {
    static const char sentinel[] = "tls";
    return (void*)sentinel;
}

void* ts_get_global_zlib() {
    static const char sentinel[] = "zlib";
    return (void*)sentinel;
}

void* ts_get_global_stream() {
    static const char sentinel[] = "stream";
    return (void*)sentinel;
}

void* ts_get_global_events() {
    static const char sentinel[] = "events";
    return (void*)sentinel;
}

void* ts_get_global_querystring() {
    static const char sentinel[] = "querystring";
    return (void*)sentinel;
}

void* ts_get_global_assert() {
    static const char sentinel[] = "assert";
    return (void*)sentinel;
}

void* ts_get_global_child_process() {
    static const char sentinel[] = "child_process";
    return (void*)sentinel;
}

void* ts_get_global_cluster() {
    static const char sentinel[] = "cluster";
    return (void*)sentinel;
}

void* ts_get_global_timers() {
    static const char sentinel[] = "timers";
    return (void*)sentinel;
}

void* ts_get_global_readline() {
    static const char sentinel[] = "readline";
    return (void*)sentinel;
}

void* ts_get_global_perf_hooks() {
    static const char sentinel[] = "perf_hooks";
    return (void*)sentinel;
}

void* ts_get_global_async_hooks() {
    static const char sentinel[] = "async_hooks";
    return (void*)sentinel;
}

void* ts_get_global_tty() {
    static const char sentinel[] = "tty";
    return (void*)sentinel;
}

void* ts_get_global_string_decoder() {
    static const char sentinel[] = "string_decoder";
    return (void*)sentinel;
}

void* ts_get_global_buffer() {
    static const char sentinel[] = "buffer";
    return (void*)sentinel;
}

void* ts_get_global_http2() {
    static const char sentinel[] = "http2";
    return (void*)sentinel;
}

void* ts_get_global_inspector() {
    static const char sentinel[] = "inspector";
    return (void*)sentinel;
}

void* ts_get_global_module() {
    static const char sentinel[] = "module";
    return (void*)sentinel;
}

void* ts_get_global_vm() {
    static const char sentinel[] = "vm";
    return (void*)sentinel;
}

void* ts_get_global_v8() {
    static const char sentinel[] = "v8";
    return (void*)sentinel;
}

// Generic global lookup by name
void* ts_get_global(void* namePtr) {
    // For unknown globals, return null
    return nullptr;
}

} // extern "C"
