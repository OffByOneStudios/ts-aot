#pragma once
//==============================================================================
// TsObject_Internal.h — PRIVATE shared header for the split TsObject_*.cpp
// files. This is NOT the public interface (that is TsObject.h).
//
// Carries (a) the common include set so every split translation unit gets the
// runtime types by including only this header, and (b) the file-scope SHARED
// MUTABLE STATE that lived as `static` in the monolithic TsObject.cpp, declared
// `extern` here with its SINGLE definition staying in TsObject.cpp. Without the
// extern split, each split .cpp would get its own private `static` copy and
// silently break shared state (the call `this`-slot, the require() cache, ...).
//
// ⛔ DO NOT move the GC scanner-registration structs (ModuleCacheScanner /
// NativePropsScanner) here. They are static-init instances whose constructors
// call ts_gc_register_scanner / ts_gc_register_minor_fixup, and MUST be
// instantiated in exactly ONE translation unit (TsObject.cpp) so the scanner +
// minor-fixup register exactly once. Putting them in this header would
// re-register them per includer → duplicate scanners racing over the shared
// state → GC corruption invisible to TS_GC_VERIFY=2, reproducing only under
// TS_GC_NURSERY=0. See runtime-safety.md "GC Rooting of C++ Containers".
//==============================================================================

// spdlog config must precede any spdlog include pulled in below.
#ifndef FMT_UNICODE
#define FMT_UNICODE 0
#endif
#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB 1
#endif

//---- Common runtime type headers (mirrors TsObject.cpp preamble) -------------
#include "TsObject.h"
#include "TsArray.h"
#include "TsError.h"
#include "TsBigInt.h"
#include "TsMap.h"
#include "TsWeakMap.h"
#include "TsSet.h"
#include "TsWeakSet.h"
#include "TsJSON.h"
#include "TsString.h"
#include "TsConsString.h"
#include "TsBuffer.h"  // TsTypedArray and TsDataView
#include "TsEventEmitter.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsRegExp.h"
#include "TsBoundFunction.h"
#include "TsClosure.h"
#include "TsHashTable.h"
#include "TsProxy.h"
#include "TsTextEncoding.h"
#include "GC.h"
// Captured in TsProxy::Create — the TsProxy vtable pointer, for cheap proxy
// detection (single pointer compare) on the property-read hot path.
extern "C" void* g_ts_proxy_vtable;
#include "TsGC.h"  // ts_gc_base()
#include "TsFlatObject.h"
#include "TsNanBox.h"
#include "TsDate.h"
#include "TsSymbol.h"
#include "TsRuntime.h"
#include "MemoryTracker.h"

//---- Common standard / third-party headers -----------------------------------
#include <new>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <string>
#include <csetjmp>
#include <excpt.h>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <spdlog/spdlog.h>
#include <unicode/regex.h>
#include <unicode/unistr.h>

//---- Runtime-private virtual-inheritance class dispatch -----------------------
// (e.g. HTTP), registered by TsHttp.cpp at startup. Moved here from TsObject.cpp
// so the g_vtable_dispatch extern decl below has a complete element type. Not
// part of any public header.
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

//---- Cross-TU function prototypes (exposed as TsObject.cpp is split) ----------
// These were file-internal forward decls / implicit definitions in the monolith;
// once functions live in different split .cpp files they need a shared prototype.
extern "C" {
// Defined in TsObject.cpp; called from TsObject_Value.cpp (box_any debug note).
void ts_offcanon_note(const char* where, void* p);
// Defined in TsObject_Value.cpp; called from TsObject.cpp (property/closure paths).
TsValue* ts_value_make_function_object(void* fnObj);
}
