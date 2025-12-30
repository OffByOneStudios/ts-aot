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
    bool isUint8Array(void* value);
    bool isGeneratorFunction(void* value);
    bool isGeneratorObject(void* value);
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
    bool ts_util_types_is_uint8_array(void* value);
    bool ts_util_types_is_generator_function(void* value);
    bool ts_util_types_is_generator_object(void* value);
}
