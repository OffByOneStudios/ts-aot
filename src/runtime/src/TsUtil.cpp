#include "../include/TsUtil.h"
#include "../include/TsRuntime.h"
#include "../include/TsJSON.h"
#include "../include/TsBuffer.h"
#include "../include/TsMap.h"
#include "../include/TsSet.h"
#include "../include/TsDate.h"
#include "../include/TsRegExp.h"
#include "../include/GC.h"
#include <sstream>
#include <cstdio>
#include <cstring>

// ============================================================================
// util.format implementation - Printf-like string formatting
// ============================================================================
TsString* ts_util_format_impl(TsString* format, TsArray* args) {
    if (!format) return TsString::Create("");
    
    const char* formatStr = format->ToUtf8();
    if (!formatStr) return TsString::Create("");
    
    std::ostringstream result;
    size_t argIndex = 0;
    size_t len = strlen(formatStr);
    
    size_t i = 0;
    while (i < len) {
        if (formatStr[i] == '%' && i + 1 < len) {
            char spec = formatStr[i + 1];
            switch (spec) {
                case 's': // String
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            TsString* str = (TsString*)ts_value_get_string((TsValue*)val);
                            if (str) result << str->ToUtf8();
                        }
                    }
                    i += 2;
                    break;
                    
                case 'd': // Number (integer)
                case 'i':
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            int64_t num = ts_value_get_int((TsValue*)val);
                            result << num;
                        }
                    }
                    i += 2;
                    break;
                    
                case 'f': // Float
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            double num = ts_value_get_double((TsValue*)val);
                            result << num;
                        }
                    }
                    i += 2;
                    break;
                    
                case 'o': // Object
                case 'O':
                case 'j': // JSON
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            TsString* str = (TsString*)ts_json_stringify(val, nullptr, nullptr);
                            if (str) result << str->ToUtf8();
                        }
                    }
                    i += 2;
                    break;
                    
                case '%': // Escaped percent
                    result << '%';
                    i += 2;
                    break;
                    
                default:
                    result << formatStr[i];
                    i++;
                    break;
            }
        } else {
            result << formatStr[i];
            i++;
        }
    }
    
    // Append any remaining arguments separated by spaces
    while (args && argIndex < (size_t)args->Length()) {
        result << " ";
        void* val = (void*)args->Get(argIndex++);
        if (val) {
            TsString* str = (TsString*)ts_json_stringify(val, nullptr, nullptr);
            if (str) result << str->ToUtf8();
        }
    }
    
    return TsString::Create(result.str().c_str());
}

// ============================================================================
// util.inspect implementation - Returns string representation of an object
// ============================================================================
TsString* ts_util_inspect_impl(void* obj, int depth, bool colors) {
    if (!obj) return TsString::Create("undefined");
    
    // Use JSON stringify for basic representation
    TsString* str = (TsString*)ts_json_stringify(obj, nullptr, nullptr);
    if (str) return str;
    
    return TsString::Create("[object Object]");
}

// ============================================================================
// util.isDeepStrictEqual implementation
// ============================================================================
bool ts_util_is_deep_strict_equal_impl(void* val1, void* val2) {
    // Same reference
    if (val1 == val2) return true;
    
    // Both null/undefined
    if (!val1 && !val2) return true;
    if (!val1 || !val2) return false;
    
    // Compare as JSON for deep equality (simple implementation)
    TsString* json1 = (TsString*)ts_json_stringify(val1, nullptr, nullptr);
    TsString* json2 = (TsString*)ts_json_stringify(val2, nullptr, nullptr);
    
    if (!json1 && !json2) return true;
    if (!json1 || !json2) return false;
    
    return json1->Equals(json2);
}

// ============================================================================
// util.types implementations
// ============================================================================
namespace TsUtilTypes {

bool isPromise(void* value) {
    if (!value) return false;
    
    // Unbox if it's a TsValue*
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    
    // Check for TsPromise by looking for magic or state pattern
    // This is simplified - full implementation would need proper RTTI
    return false; // Can't easily detect without RTTI
}

bool isTypedArray(void* value) {
    if (!value) return false;
    
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    
    // Check for Buffer (which is our TypedArray equivalent)
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsBuffer::MAGIC;
}

bool isArrayBuffer(void* value) {
    // In our implementation, Buffer serves as ArrayBuffer
    return isTypedArray(value);
}

bool isArrayBufferView(void* value) {
    return isTypedArray(value);
}

bool isAsyncFunction(void* value) {
    // We can't really detect this at runtime in AOT compiled code
    // Functions are represented as raw function pointers
    return false;
}

bool isDate(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsDate does NOT inherit from TsObject - magic is at offset 0
    uint32_t* magicPtr = (uint32_t*)rawPtr;
    return *magicPtr == TsDate::MAGIC;
}

bool isMap(void* value) {
    if (!value) return false;
    
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    
    // Check magic at offset 16 (TsMap specific layout)
    uint32_t* magicPtr = (uint32_t*)((char*)rawPtr + 16);
    return *magicPtr == TsMap::MAGIC;
}

bool isSet(void* value) {
    if (!value) return false;
    
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsSet::MAGIC;
}

bool isRegExp(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsRegExp does NOT inherit from TsObject - magic is at offset 0
    uint32_t* magicPtr = (uint32_t*)rawPtr;
    return *magicPtr == TsRegExp::MAGIC;
}

bool isNativeError(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsError is a TsMap with "name", "message", and "stack" properties
    // Check magic at offset 16 (TsMap/TsObject layout)
    uint32_t* magicPtr = (uint32_t*)((char*)rawPtr + 16);
    if (*magicPtr != TsMap::MAGIC) return false;

    // Check for "name" property containing error type
    TsMap* map = (TsMap*)rawPtr;
    TsValue nameVal = map->Get(TsString::Create("name"));
    if (nameVal.type != ValueType::STRING_PTR) return false;

    TsString* name = (TsString*)nameVal.ptr_val;
    if (!name) return false;

    const char* nameStr = name->ToUtf8();
    // Check for standard error types
    return nameStr && (
        strcmp(nameStr, "Error") == 0 ||
        strcmp(nameStr, "TypeError") == 0 ||
        strcmp(nameStr, "ReferenceError") == 0 ||
        strcmp(nameStr, "SyntaxError") == 0 ||
        strcmp(nameStr, "RangeError") == 0 ||
        strcmp(nameStr, "URIError") == 0 ||
        strcmp(nameStr, "EvalError") == 0
    );
}

bool isUint8Array(void* value) {
    // Our Buffer is effectively a Uint8Array
    return isTypedArray(value);
}

bool isGeneratorFunction(void* value) {
    // Can't detect in AOT - all functions are compiled
    return false;
}

bool isGeneratorObject(void* value) {
    // Can't detect without proper RTTI
    return false;
}

} // namespace TsUtilTypes

// ============================================================================
// C API implementations
// ============================================================================
extern "C" {

void* ts_util_format(void* format, void* args) {
    TsString* formatStr = nullptr;
    
    // Unbox format if needed
    void* rawFormat = ts_value_get_string((TsValue*)format);
    if (!rawFormat) rawFormat = format;
    formatStr = (TsString*)rawFormat;
    
    // Unbox args if needed
    void* rawArgs = ts_value_get_object((TsValue*)args);
    if (!rawArgs) rawArgs = args;
    TsArray* argsArr = (TsArray*)rawArgs;
    
    return ts_util_format_impl(formatStr, argsArr);
}

void* ts_util_inspect(void* obj, void* options) {
    // For now, ignore options
    return ts_util_inspect_impl(obj, 2, false);
}

bool ts_util_is_deep_strict_equal(void* val1, void* val2) {
    return ts_util_is_deep_strict_equal_impl(val1, val2);
}

void* ts_util_promisify(void* fn) {
    // For now, just return the function as-is
    // Full implementation would require wrapping with Promise logic
    // This is a stub for basic compatibility
    return fn;
}

void ts_util_inherits(void* constructor, void* superConstructor) {
    // In AOT compiled code, inheritance is handled at compile time
    // This is a no-op for compatibility with legacy code
}

void* ts_util_deprecate(void* fn, void* msg) {
    // For now, just return the function as-is
    // Full implementation would wrap and print deprecation warning on first call
    if (msg) {
        TsString* msgStr = (TsString*)ts_value_get_string((TsValue*)msg);
        if (!msgStr) msgStr = (TsString*)msg;
        if (msgStr) {
            fprintf(stderr, "[DEP] Warning: %s\n", msgStr->ToUtf8());
        }
    }
    return fn;
}

void* ts_util_callbackify(void* fn) {
    // For now, just return the function as-is
    // Full implementation would wrap Promise-returning function
    return fn;
}

// util.types C API
bool ts_util_types_is_promise(void* value) {
    return TsUtilTypes::isPromise(value);
}

bool ts_util_types_is_typed_array(void* value) {
    return TsUtilTypes::isTypedArray(value);
}

bool ts_util_types_is_array_buffer(void* value) {
    return TsUtilTypes::isArrayBuffer(value);
}

bool ts_util_types_is_array_buffer_view(void* value) {
    return TsUtilTypes::isArrayBufferView(value);
}

bool ts_util_types_is_async_function(void* value) {
    return TsUtilTypes::isAsyncFunction(value);
}

bool ts_util_types_is_date(void* value) {
    return TsUtilTypes::isDate(value);
}

bool ts_util_types_is_map(void* value) {
    return TsUtilTypes::isMap(value);
}

bool ts_util_types_is_set(void* value) {
    return TsUtilTypes::isSet(value);
}

bool ts_util_types_is_reg_exp(void* value) {
    return TsUtilTypes::isRegExp(value);
}

bool ts_util_types_is_native_error(void* value) {
    return TsUtilTypes::isNativeError(value);
}

bool ts_util_types_is_uint8_array(void* value) {
    return TsUtilTypes::isUint8Array(value);
}

bool ts_util_types_is_generator_function(void* value) {
    return TsUtilTypes::isGeneratorFunction(value);
}

bool ts_util_types_is_generator_object(void* value) {
    return TsUtilTypes::isGeneratorObject(value);
}

} // extern "C"
