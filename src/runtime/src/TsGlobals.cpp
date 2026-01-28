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

// Generic global lookup by name
void* ts_get_global(void* namePtr) {
    // For unknown globals, return null
    return nullptr;
}

} // extern "C"
