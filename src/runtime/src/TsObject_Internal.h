#pragma once
//==============================================================================
// TsObject_Internal.h — PRIVATE shared header for the (eventual) split
// TsObject_*.cpp files. This is NOT the public interface (that is TsObject.h).
//
// PRE-1 (state centralization): the file-scope SHARED MUTABLE STATE that lived
// as `static` in the monolithic TsObject.cpp is declared `extern` here, with its
// SINGLE definition staying in TsObject.cpp. Once TsObject.cpp is split, every
// split translation unit includes this header and references the SAME storage
// instead of each getting its own private `static` copy (which would silently
// break shared state — the call `this`-slot, the require() cache, etc.).
//
// ⛔ DO NOT move the GC scanner-registration structs (ModuleCacheScanner /
// NativePropsScanner) here. They are static-init instances whose constructors
// call ts_gc_register_scanner / ts_gc_register_minor_fixup. They MUST be
// instantiated in exactly ONE translation unit (TsObject.cpp) so the scanner +
// minor-fixup register exactly once. Putting them in this header would
// re-register them per includer → duplicate scanners racing over the (now
// shared) state → GC corruption that is invisible to TS_GC_VERIFY=2 and only
// reproduces under TS_GC_NURSERY=0. See runtime-safety.md "GC Rooting of C++
// Containers".
//==============================================================================

#include "TsObject.h"   // TsValue
#include "TsMap.h"      // TsMap
#include <unordered_map>
#include <string>
#include <cstdint>

// Virtual-inheritance class dispatch (e.g. HTTP), registered by TsHttp.cpp at
// startup. Moved here from TsObject.cpp so the `g_vtable_dispatch` extern
// declaration below has a complete element type. These types are runtime-private
// (not part of any public header).
typedef TsValue (*VtableDispatchFn)(void* obj, const char* key);
typedef bool (*VtableSetDispatchFn)(void* obj, const char* key, TsValue value);
struct VtableDispatchEntry {
    uint64_t vtable;
    VtableDispatchFn dispatch;
    VtableSetDispatchFn setDispatch;
    bool isEventEmitter;
};

//---- Shared mutable runtime state (SINGLE definition in TsObject.cpp) ---------

// Virtual-dispatch registry (no GC pointers — vtable addresses + fn pointers).
extern VtableDispatchEntry g_vtable_dispatch[8];
extern int g_vtable_dispatch_count;

// require()/module exports cache. GC-rooted in TsObject.cpp (ModuleCacheScanner).
extern std::unordered_map<std::string, TsValue*> g_module_cache;

// Native C++ object -> dynamically-assigned property map side-table. GC-rooted
// in TsObject.cpp (NativePropsScanner, incl. promoted-key reinsertion).
extern std::unordered_map<void*, TsMap*> g_native_object_props;

// Transient call context: the receiver (`this`) and argc of the in-flight call.
// Set-before-call / restored-after; snapshotted across exception unwind in
// Core.cpp via the ts_get_call_this / ts_set_call_this accessors.
extern void* ts_call_this_value;
extern int64_t ts_last_call_argc;
