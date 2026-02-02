#ifndef TS_V8_EXT_H
#define TS_V8_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// v8 module stub functions
// The v8 module is V8-specific and mostly incompatible with AOT compilation.
// These stubs return placeholder data for compatibility.

// v8.getHeapStatistics() - returns stub heap statistics
void* ts_v8_get_heap_statistics();

// v8.getHeapSpaceStatistics() - returns empty array (no V8 heap spaces)
void* ts_v8_get_heap_space_statistics();

// v8.getHeapCodeStatistics() - returns stub code statistics
void* ts_v8_get_heap_code_statistics();

// v8.setFlagsFromString(flags) - no-op (no V8 flags in AOT)
void ts_v8_set_flags_from_string(void* flags);

// v8.writeHeapSnapshot(filename) - returns empty string (not supported)
void* ts_v8_write_heap_snapshot(void* filename);

// v8.serialize(value) - not supported, returns null
void* ts_v8_serialize(void* value);

// v8.deserialize(buffer) - not supported, returns undefined
void* ts_v8_deserialize(void* buffer);

// v8.cachedDataVersionTag() - returns a constant (no V8 cache in AOT)
int64_t ts_v8_cached_data_version_tag();

#ifdef __cplusplus
}
#endif

#endif // TS_V8_EXT_H
