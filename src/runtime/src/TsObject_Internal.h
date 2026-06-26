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
namespace fs = std::filesystem;  // matches the alias used in TsObject.cpp
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

//---- Cross-TU prototypes for the TsObject_Builtins.cpp split ----------------
extern "C" {
TsValue* ts_array_at_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_copyWithin_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_entries_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_fill_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findLastIndex_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findLast_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_flatMap_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_flat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_includes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_indexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_keys_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_pop_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reduceRight_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reverse_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_shift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_slice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toReversed_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_values_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_with_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_boolean_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_boolean_valueOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_UTC_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_now_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_parse_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setTime_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toDateString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toTimeString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toISOString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toJSON_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_toUTCString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_valueOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_number_toExponential_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_number_toFixed_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_number_toPrecision_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_number_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_number_valueOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_exec_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_symbol_matchAll_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_symbol_match_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_symbol_replace_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_symbol_search_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_symbol_split_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_test_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_regexp_tostring_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_anchor_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_at_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_big_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_blink_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_bold_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_charAt_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_charCodeAt_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_codePointAt_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_concat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_endsWith_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_fixed_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_fontcolor_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_fontsize_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_includes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_indexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_italics_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_lastIndexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_link_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_matchAll_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_match_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_normalize_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_padEnd_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_padStart_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_repeat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_replaceAll_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_replace_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_search_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_slice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_small_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_split_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_startsWith_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_strike_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_sub_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_substr_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_substring_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_sup_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_toLowerCase_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_toUpperCase_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_trimEnd_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_trimStart_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_string_trim_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_at_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_copyWithin_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_fill_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_includes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_indexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_join_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_reverse_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_set_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_slice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_subarray_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_toLocaleString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_typed_array_toString_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_ta_get_boxed(TsTypedArray* ta, size_t index);
TsValue* temporal_proto_get(void* obj, void* protoRaw, const char* keyStr);
void* ts_get_call_this();
void* ts_get_global_Boolean();
void* ts_get_global_Number();
int64_t ts_string_search_regexp(void* str, void* regexp);
int64_t ts_to_index_integer(TsValue* v);
double ts_to_number(TsValue* v);
}

//---- Cross-TU prototypes: date (macro-gen) + remaining builtins -------------
extern "C" {
TsValue* ts_date_getDate_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getFullYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getHours_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getMilliseconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getMinutes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getMonth_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getSeconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getTime_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCDate_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCFullYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCHours_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCMilliseconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCMinutes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCMonth_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_getUTCSeconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setDate_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setFullYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setHours_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setMilliseconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setMinutes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setMonth_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setSeconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCDate_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCFullYear_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCHours_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCMilliseconds_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCMinutes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCMonth_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_date_setUTCSeconds_native(void* ctx, int argc, TsValue** argv);
bool ts_value_is_callable(TsValue* val);
}

//---- Cross-TU C++-linkage prototypes (module path helpers) ------------------
// Defined in TsObject.cpp; called from TsObject_Module.cpp (ts_require). C++
// linkage (std::string / fs::path), so NOT inside an extern "C" block.
std::string finalize_module_path(const fs::path& base);
std::string resolve_node_module(const std::string& spec, const std::string& referrerPath);

//---- Cross-TU prototypes for the TsObject_ObjectStatics.cpp split -----------
// C++-linkage (defined before the big extern "C" block, or external globals):
TsMap* getNativeProps(void* obj);
TsMap* getOrCreateNativeProps(void* obj);
extern "C" bool g_array_default_iterator_deleted;  // defined in TsArray.cpp
inline bool nanbox_is_string_ptr(uint64_t nb) {
    if (!nanbox_is_ptr(nb)) return false;
    void* ptr = nanbox_to_ptr(nb);
    return ts_is_any_string(ptr);
}
// C-linkage (inside the big extern "C" block in their defining TU):
extern "C" {
bool parse_canonical_array_index(const char* s, int64_t* out);
TsValue* ts_func_props_view(TsMap* props, bool entries);
TsString* ts_symbol_storage_key(TsSymbol* sym);
bool array_index_attrs_get(TsArray* a, size_t idx, uint8_t* outAttrs);
void array_index_attrs_clear(TsArray* a, size_t idx);
void ts_array_prototype_bump_version();
bool ts_array_is_prototype_map(void* maybeMap);
}

//---- Cross-TU prototypes for the TsObject_Call.cpp split --------------------
extern "C" {
TsFunction* ts_extract_function(TsValue* boxedFunc);
TsProxy* ts_extract_proxy(TsValue* boxedFunc);
void* ts_value_get_element(void* param, int64_t index);
TsValue* ts_new_from_constructor_impl(TsValue* constructorFn, int argc, TsValue** argv);
}

extern "C" {
TsClosure* ts_extract_closure(TsValue* boxedFunc);
TsValue* ts_array_constructor_native(void* ctx, int argc, TsValue** argv);
}

extern "C" {
TsClosure* ts_funcptr_as_closure(void* funcPtr);
}
