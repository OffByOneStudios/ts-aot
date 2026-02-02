// TsUtil.h - Util module for ts-aot
//
// Node.js-compatible util functions.
// This module is built as a separate library (ts_util) and linked
// only when the util module is imported.

#ifndef TS_UTIL_EXT_H
#define TS_UTIL_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Format functions
void* ts_util_format(void* format, void* args);
void* ts_util_format_with_options(void* options, void* format, void* args);

// Inspect functions
void* ts_util_inspect(void* obj, void* options);
void* ts_util_inspect_custom_symbol();
void* ts_util_inspect_default_options();
void* ts_util_get_inspect();

// Comparison
bool ts_util_is_deep_strict_equal(void* val1, void* val2);

// Function utilities
void* ts_util_promisify(void* fn);
void* ts_util_deprecate(void* fn, void* msg);
void* ts_util_callbackify(void* fn);

// String utilities
void* ts_util_strip_vt_control_characters(void* str);
void* ts_util_to_usv_string(void* str);
void* ts_util_style_text(void* formatArg, void* textArg);

// Debug utilities
void* ts_util_debuglog(void* section);

// Error utilities
void* ts_util_get_system_error_name(int64_t errnum);
void* ts_util_get_system_error_map();

// Parsing utilities
void* ts_util_parse_args(void* configPtr);
void* ts_util_parse_env(void* contentArg);

// Type checking functions (util.types.*)
bool ts_util_types_is_promise(void* value);
bool ts_util_types_is_typed_array(void* value);
bool ts_util_types_is_array_buffer(void* value);
bool ts_util_types_is_array_buffer_view(void* value);
bool ts_util_types_is_async_function(void* value);
bool ts_util_types_is_date(void* value);
bool ts_util_types_is_map(void* value);
bool ts_util_types_is_set(void* value);
bool ts_util_types_is_reg_exp(void* value);
bool ts_util_types_is_native_error(void* value);
bool ts_util_types_is_uint8_array(void* value);
bool ts_util_types_is_generator_function(void* value);
bool ts_util_types_is_generator_object(void* value);
bool ts_util_types_is_int8_array(void* value);
bool ts_util_types_is_int16_array(void* value);
bool ts_util_types_is_int32_array(void* value);
bool ts_util_types_is_uint8_clamped_array(void* value);
bool ts_util_types_is_uint16_array(void* value);
bool ts_util_types_is_uint32_array(void* value);
bool ts_util_types_is_float32_array(void* value);
bool ts_util_types_is_float64_array(void* value);
bool ts_util_types_is_big_int64_array(void* value);
bool ts_util_types_is_big_uint64_array(void* value);
bool ts_util_types_is_data_view(void* value);
bool ts_util_types_is_proxy(void* value);
bool ts_util_types_is_weak_map(void* value);
bool ts_util_types_is_weak_set(void* value);
bool ts_util_types_is_any_array_buffer(void* value);
bool ts_util_types_is_boolean_object(void* value);
bool ts_util_types_is_number_object(void* value);
bool ts_util_types_is_string_object(void* value);
bool ts_util_types_is_symbol_object(void* value);
bool ts_util_types_is_boxed_primitive(void* value);
bool ts_util_types_is_crypto_key(void* value);
bool ts_util_types_is_external(void* value);
bool ts_util_types_is_key_object(void* value);
bool ts_util_types_is_map_iterator(void* value);
bool ts_util_types_is_module_namespace_object(void* value);
bool ts_util_types_is_set_iterator(void* value);
bool ts_util_types_is_shared_array_buffer(void* value);

// Transfer utilities
void* ts_util_transferable_abort_controller();
void* ts_util_transferable_abort_signal(void* signal);

#ifdef __cplusplus
}
#endif

#endif // TS_UTIL_EXT_H
