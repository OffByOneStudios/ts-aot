#pragma once

#include "TsObject.h"
#include "TsString.h"
#include "TsArray.h"

// ============================================================================
// util module implementation
// ============================================================================

// util.format - printf-like string formatting
TsString* ts_util_format_impl(TsString* format, TsArray* args);

// util.inspect - returns string representation of an object  
TsString* ts_util_inspect_impl(void* obj, int depth, bool colors);

// util.isDeepStrictEqual - deep equality check
bool ts_util_is_deep_strict_equal_impl(void* val1, void* val2);

// ============================================================================
// util.types - Type checking utilities
// ============================================================================
namespace TsUtilTypes {
    bool isPromise(void* value);
    bool isTypedArray(void* value);
    bool isArrayBuffer(void* value);
    bool isArrayBufferView(void* value);
    bool isAsyncFunction(void* value);
    bool isDate(void* value);
    bool isMap(void* value);
    bool isSet(void* value);
    bool isRegExp(void* value);
    bool isNativeError(void* value);
    bool isGeneratorFunction(void* value);
    bool isGeneratorObject(void* value);

    // Specific TypedArray type checks
    bool isInt8Array(void* value);
    bool isInt16Array(void* value);
    bool isInt32Array(void* value);
    bool isUint8Array(void* value);
    bool isUint8ClampedArray(void* value);
    bool isUint16Array(void* value);
    bool isUint32Array(void* value);
    bool isFloat32Array(void* value);
    bool isFloat64Array(void* value);
    bool isBigInt64Array(void* value);
    bool isBigUint64Array(void* value);
    bool isDataView(void* value);

    // Additional type checks
    bool isProxy(void* value);
    bool isWeakMap(void* value);
    bool isWeakSet(void* value);
    bool isAnyArrayBuffer(void* value);

    // Boxed primitive checks
    bool isBooleanObject(void* value);
    bool isNumberObject(void* value);
    bool isStringObject(void* value);
    bool isSymbolObject(void* value);
    bool isBoxedPrimitive(void* value);
}

extern "C" {
    // util module functions
    void* ts_util_format(void* format, void* args);
    void* ts_util_inspect(void* obj, void* options);
    bool ts_util_is_deep_strict_equal(void* val1, void* val2);
    
    // util.promisify - returns a wrapped function
    // Note: This is a complex wrapper - for now we provide a stub
    void* ts_util_promisify(void* fn);
    
    // util.inherits - legacy prototype inheritance
    // Note: In AOT compiled code, this is mostly a no-op
    void ts_util_inherits(void* constructor, void* superConstructor);
    
    // util.deprecate - wraps function with deprecation warning
    void* ts_util_deprecate(void* fn, void* msg);
    
    // util.callbackify - convert async function to callback style
    void* ts_util_callbackify(void* fn);

    // util.stripVTControlCharacters - removes ANSI escape codes
    void* ts_util_strip_vt_control_characters(void* str);

    // util.toUSVString - converts to valid Unicode scalar value string
    void* ts_util_to_usv_string(void* str);

    // util.getSystemErrorName - returns the string name for a numeric error code
    void* ts_util_get_system_error_name(int64_t errnum);

    // util.getSystemErrorMap - returns a Map of all system error codes
    void* ts_util_get_system_error_map();

    // util.styleText - apply ANSI styles to text
    void* ts_util_style_text(void* format, void* text);

    // util.debuglog - returns a logging function for the given section
    void* ts_util_debuglog(void* section);

    // util.formatWithOptions - like util.format() but with inspect options
    void* ts_util_format_with_options(void* options, void* format, void* args);

    // util.types functions
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
    bool ts_util_types_is_generator_function(void* value);
    bool ts_util_types_is_generator_object(void* value);

    // Specific TypedArray type checks
    bool ts_util_types_is_int8_array(void* value);
    bool ts_util_types_is_int16_array(void* value);
    bool ts_util_types_is_int32_array(void* value);
    bool ts_util_types_is_uint8_array(void* value);
    bool ts_util_types_is_uint8_clamped_array(void* value);
    bool ts_util_types_is_uint16_array(void* value);
    bool ts_util_types_is_uint32_array(void* value);
    bool ts_util_types_is_float32_array(void* value);
    bool ts_util_types_is_float64_array(void* value);
    bool ts_util_types_is_big_int64_array(void* value);
    bool ts_util_types_is_big_uint64_array(void* value);
    bool ts_util_types_is_data_view(void* value);

    // Additional type checks
    bool ts_util_types_is_proxy(void* value);
    bool ts_util_types_is_weak_map(void* value);
    bool ts_util_types_is_weak_set(void* value);
    bool ts_util_types_is_any_array_buffer(void* value);

    // Boxed primitive checks
    bool ts_util_types_is_boolean_object(void* value);
    bool ts_util_types_is_number_object(void* value);
    bool ts_util_types_is_string_object(void* value);
    bool ts_util_types_is_symbol_object(void* value);
    bool ts_util_types_is_boxed_primitive(void* value);
}
