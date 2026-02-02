// V8 extension for ts-aot
// Stub implementation of Node.js v8 module.
// The v8 module is V8-specific and mostly incompatible with AOT compilation.
// These stubs return placeholder data for compatibility.

#include "TsV8Ext.h"
#include "TsString.h"
#include "TsObject.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include "TsArray.h"
#include "GC.h"
#include <cstdio>
#include <cstdint>

extern "C" {

// v8.getHeapStatistics() - returns stub heap statistics
// Since we use Boehm GC, V8 heap stats are not meaningful
void* ts_v8_get_heap_statistics() {
    TsMap* stats = TsMap::Create();

    // Return placeholder values
    stats->Set(TsString::Create("total_heap_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("total_heap_size_executable"), ts_value_make_int(0));
    stats->Set(TsString::Create("total_physical_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("total_available_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("used_heap_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("heap_size_limit"), ts_value_make_int(0));
    stats->Set(TsString::Create("malloced_memory"), ts_value_make_int(0));
    stats->Set(TsString::Create("peak_malloced_memory"), ts_value_make_int(0));
    stats->Set(TsString::Create("does_zap_garbage"), ts_value_make_int(0));
    stats->Set(TsString::Create("number_of_native_contexts"), ts_value_make_int(0));
    stats->Set(TsString::Create("number_of_detached_contexts"), ts_value_make_int(0));
    stats->Set(TsString::Create("external_memory"), ts_value_make_int(0));

    return ts_value_make_object(stats);
}

// v8.getHeapSpaceStatistics() - returns empty array (no V8 heap spaces)
void* ts_v8_get_heap_space_statistics() {
    TsArray* spaces = TsArray::Create();
    return ts_value_make_object(spaces);
}

// v8.getHeapCodeStatistics() - returns stub code statistics
void* ts_v8_get_heap_code_statistics() {
    TsMap* stats = TsMap::Create();

    stats->Set(TsString::Create("code_and_metadata_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("bytecode_and_metadata_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("external_script_source_size"), ts_value_make_int(0));
    stats->Set(TsString::Create("cpu_profiler_metadata_size"), ts_value_make_int(0));

    return ts_value_make_object(stats);
}

// v8.setFlagsFromString(flags) - no-op (no V8 flags in AOT)
void ts_v8_set_flags_from_string(void* flags) {
    // No-op - V8 flags are not applicable in AOT-compiled code
    (void)flags;
}

// v8.writeHeapSnapshot(filename) - returns empty string (not supported)
void* ts_v8_write_heap_snapshot(void* filename) {
    // Heap snapshots are V8-specific and not supported
    (void)filename;
    fprintf(stderr, "Warning: v8.writeHeapSnapshot() is not supported in AOT-compiled code.\n");
    return ts_value_make_string(TsString::Create(""));
}

// v8.serialize(value) - not supported, returns null
void* ts_v8_serialize(void* value) {
    fprintf(stderr, "Warning: v8.serialize() is not fully supported in AOT-compiled code.\n");
    (void)value;
    return ts_value_make_null();
}

// v8.deserialize(buffer) - not supported, returns undefined
void* ts_v8_deserialize(void* buffer) {
    fprintf(stderr, "Warning: v8.deserialize() is not fully supported in AOT-compiled code.\n");
    (void)buffer;
    return ts_value_make_undefined();
}

// v8.cachedDataVersionTag() - returns a constant (no V8 cache in AOT)
int64_t ts_v8_cached_data_version_tag() {
    // Return a constant version tag since there's no V8 cached data
    return 0;
}

} // extern "C"
