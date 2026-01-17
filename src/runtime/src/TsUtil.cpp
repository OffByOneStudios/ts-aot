#include "../include/TsUtil.h"
#include "../include/TsRuntime.h"
#include "../include/TsJSON.h"
#include "../include/TsBuffer.h"
#include "../include/TsMap.h"
#include "../include/TsSet.h"
#include "../include/TsDate.h"
#include "../include/TsRegExp.h"
#include "../include/TsArray.h"
#include "../include/TsPromise.h"
#include "../include/GC.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cmath>

// ============================================================================
// util.format implementation - Printf-like string formatting
// ============================================================================

// Helper to convert a value to string for format placeholders
static TsString* formatValueToString(void* val) {
    if (!val) return TsString::Create("undefined");

    // The value from array is stored as int64_t (pointer cast to int)
    // It's a boxed TsValue* - get its string representation
    TsValue* tv = (TsValue*)val;

    // Check if valid TsValue type
    uint8_t typeField = *(uint8_t*)tv;
    if (typeField <= 10) {
        // Valid TsValue - convert to string based on type
        switch (tv->type) {
            case ValueType::STRING_PTR:
                return (TsString*)tv->ptr_val;
            case ValueType::NUMBER_INT:
                return TsString::FromInt(tv->i_val);
            case ValueType::NUMBER_DBL:
                return TsString::FromDouble(tv->d_val);
            case ValueType::BOOLEAN:
                return TsString::FromBool(tv->b_val);
            case ValueType::UNDEFINED:
                return TsString::Create("undefined");
            case ValueType::OBJECT_PTR:
            case ValueType::ARRAY_PTR: {
                // For objects/arrays, use JSON stringify
                TsString* json = (TsString*)ts_json_stringify(tv->ptr_val, nullptr, nullptr);
                return json ? json : TsString::Create("[object Object]");
            }
            default:
                return TsString::Create("[object]");
        }
    }

    // Check if raw TsString
    uint32_t magic = *(uint32_t*)val;
    if (magic == TsString::MAGIC) {
        return (TsString*)val;
    }

    return TsString::Create("[object]");
}

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
                        TsString* str = formatValueToString(val);
                        if (str) result << str->ToUtf8();
                    }
                    i += 2;
                    break;
                    
                case 'd': // Number (integer)
                case 'i':
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            TsValue* tv = (TsValue*)val;
                            uint8_t typeField = *(uint8_t*)tv;
                            if (typeField <= 10) {
                                // Valid boxed value
                                if (tv->type == ValueType::NUMBER_INT) {
                                    result << tv->i_val;
                                } else if (tv->type == ValueType::NUMBER_DBL) {
                                    result << (int64_t)tv->d_val;
                                } else {
                                    result << 0;
                                }
                            } else {
                                result << 0;
                            }
                        }
                    }
                    i += 2;
                    break;

                case 'f': // Float
                    if (args && argIndex < (size_t)args->Length()) {
                        void* val = (void*)args->Get(argIndex++);
                        if (val) {
                            TsValue* tv = (TsValue*)val;
                            uint8_t typeField = *(uint8_t*)tv;
                            if (typeField <= 10) {
                                if (tv->type == ValueType::NUMBER_DBL) {
                                    result << tv->d_val;
                                } else if (tv->type == ValueType::NUMBER_INT) {
                                    result << (double)tv->i_val;
                                } else {
                                    result << 0.0;
                                }
                            } else {
                                result << 0.0;
                            }
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
                            TsValue* tv = (TsValue*)val;
                            uint8_t typeField = *(uint8_t*)tv;
                            void* objToStringify = val;
                            if (typeField <= 10 && (tv->type == ValueType::OBJECT_PTR || tv->type == ValueType::ARRAY_PTR)) {
                                objToStringify = tv->ptr_val;
                            }
                            TsString* str = (TsString*)ts_json_stringify(objToStringify, nullptr, nullptr);
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
        TsString* str = formatValueToString(val);
        if (str) result << str->ToUtf8();
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

// Helper to check if pointer is a known raw object type by checking magic at offset 16
static bool isRawObject(void* p) {
    if (!p || (uintptr_t)p < 4096) return false;
    // Check magic at offset 16 (TsObject-derived classes)
    uint32_t magic16 = *(uint32_t*)((char*)p + 16);
    return magic16 == TsMap::MAGIC ||
           magic16 == TsSet::MAGIC ||
           magic16 == TsBuffer::MAGIC ||
           magic16 == TsTypedArray::MAGIC;
}

// Helper to check if pointer is a TsArray (magic at offset 8)
static bool isRawArray(void* p) {
    if (!p || (uintptr_t)p < 4096) return false;
    uint32_t magic = *(uint32_t*)p;
    return magic == TsArray::MAGIC;
}

// Helper to check if pointer is a TsString
static bool isRawString(void* p) {
    if (!p || (uintptr_t)p < 4096) return false;
    uint32_t magic = *(uint32_t*)p;
    return magic == TsString::MAGIC;
}

bool ts_util_is_deep_strict_equal_impl(void* val1, void* val2) {
    // Same reference
    if (val1 == val2) return true;

    // Both null/undefined
    if (!val1 && !val2) return true;
    if (!val1 || !val2) return false;

    // Check if raw strings (compare directly)
    if (isRawString(val1) && isRawString(val2)) {
        return ((TsString*)val1)->Equals((TsString*)val2);
    }

    // Check if raw objects/arrays (compare via JSON)
    bool isRaw1 = isRawObject(val1) || isRawArray(val1);
    bool isRaw2 = isRawObject(val2) || isRawArray(val2);

    if (isRaw1 && isRaw2) {
        TsString* json1 = (TsString*)ts_json_stringify(val1, nullptr, nullptr);
        TsString* json2 = (TsString*)ts_json_stringify(val2, nullptr, nullptr);

        if (!json1 && !json2) return true;
        if (!json1 || !json2) return false;

        return json1->Equals(json2);
    }

    // If one is raw and one is not, they're different types
    if (isRaw1 || isRaw2) {
        return false;
    }

    // Try to interpret as boxed TsValue
    TsValue* tv1 = (TsValue*)val1;
    TsValue* tv2 = (TsValue*)val2;

    // Sanity check that type values are in range
    if ((uint8_t)tv1->type > (uint8_t)ValueType::FUNCTION_PTR ||
        (uint8_t)tv2->type > (uint8_t)ValueType::FUNCTION_PTR) {
        // Fall back to JSON comparison
        TsString* json1 = (TsString*)ts_json_stringify(val1, nullptr, nullptr);
        TsString* json2 = (TsString*)ts_json_stringify(val2, nullptr, nullptr);
        if (!json1 && !json2) return true;
        if (!json1 || !json2) return false;
        return json1->Equals(json2);
    }

    // Check if both are undefined type
    if (tv1->type == ValueType::UNDEFINED && tv2->type == ValueType::UNDEFINED) {
        return true;
    }

    // Different types = not equal
    if (tv1->type != tv2->type) {
        return false;
    }

    // Compare based on type
    switch (tv1->type) {
        case ValueType::NUMBER_INT:
            return tv1->i_val == tv2->i_val;

        case ValueType::NUMBER_DBL:
            // Handle NaN specially (NaN === NaN is true in deepStrictEqual)
            if (std::isnan(tv1->d_val) && std::isnan(tv2->d_val)) return true;
            return tv1->d_val == tv2->d_val;

        case ValueType::BOOLEAN:
            return tv1->b_val == tv2->b_val;

        case ValueType::STRING_PTR: {
            TsString* s1 = (TsString*)tv1->ptr_val;
            TsString* s2 = (TsString*)tv2->ptr_val;
            if (!s1 && !s2) return true;
            if (!s1 || !s2) return false;
            return s1->Equals(s2);
        }

        case ValueType::OBJECT_PTR:
        case ValueType::ARRAY_PTR: {
            // For objects and arrays, compare as JSON for deep equality
            TsString* json1 = (TsString*)ts_json_stringify(tv1->ptr_val, nullptr, nullptr);
            TsString* json2 = (TsString*)ts_json_stringify(tv2->ptr_val, nullptr, nullptr);

            if (!json1 && !json2) return true;
            if (!json1 || !json2) return false;

            return json1->Equals(json2);
        }

        default:
            // For other types (Promise, BigInt, Symbol, Function), compare by reference
            return tv1->ptr_val == tv2->ptr_val;
    }
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

    // TsPromise inherits from TsObject, check magic at standard offset
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == ts::TsPromise::MAGIC;
}

bool isTypedArray(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // Check for Buffer and TypedArray types (both inherit from TsObject)
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsBuffer::MAGIC || obj->magic == TsTypedArray::MAGIC;
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
    if (*magicPtr != TsMap::MAGIC) return false;

    // Must be an explicit Map (new Map()), not a plain object
    TsMap* map = (TsMap*)rawPtr;
    return map->IsExplicitMap();
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

bool isGeneratorFunction(void* value) {
    // Can't detect in AOT - all functions are compiled
    return false;
}

bool isGeneratorObject(void* value) {
    // Can't detect without proper RTTI
    return false;
}

// Helper to get TypedArray if value is one
static TsTypedArray* getTypedArray(void* value) {
    if (!value) return nullptr;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    if (obj->magic == TsTypedArray::MAGIC) {
        return (TsTypedArray*)rawPtr;
    }
    return nullptr;
}

bool isInt8Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Int8;
}

bool isInt16Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Int16;
}

bool isInt32Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Int32;
}

bool isUint8Array(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    // Buffer is effectively Uint8Array
    if (obj->magic == TsBuffer::MAGIC) return true;

    // TypedArray with Uint8 type
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Uint8;
}

bool isUint8ClampedArray(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Uint8Clamped;
}

bool isUint16Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Uint16;
}

bool isUint32Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Uint32;
}

bool isFloat32Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Float32;
}

bool isFloat64Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::Float64;
}

bool isDataView(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsDataView inherits from TsObject, check magic
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsDataView::MAGIC;
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

// ============================================================================
// util.promisify implementation
// ============================================================================

// Context for the promisify callback (err, value) → resolve/reject
struct PromisifyCallbackContext {
    ts::TsPromise* promise;
};

// Callback function that receives (err, value) and resolves/rejects the promise
static TsValue* promisify_callback(void* ctx, int argc, TsValue** argv) {
    PromisifyCallbackContext* context = (PromisifyCallbackContext*)ctx;
    if (!context || !context->promise) return nullptr;

    // Node.js callback convention: callback(err, value)
    // If err is truthy (non-null, non-undefined), reject with err
    // Otherwise, resolve with value
    TsValue* err = (argc > 0) ? argv[0] : nullptr;
    TsValue* value = (argc > 1) ? argv[1] : nullptr;

    bool hasError = false;
    if (err) {
        if (err->type == ValueType::UNDEFINED) {
            hasError = false;
        } else if (err->type == ValueType::OBJECT_PTR ||
                   err->type == ValueType::STRING_PTR ||
                   err->type == ValueType::NUMBER_INT ||
                   err->type == ValueType::NUMBER_DBL ||
                   err->type == ValueType::BOOLEAN) {
            // For boolean, check if it's true
            if (err->type == ValueType::BOOLEAN) {
                hasError = err->b_val;
            } else if (err->type == ValueType::NUMBER_INT) {
                hasError = (err->i_val != 0);
            } else if (err->type == ValueType::NUMBER_DBL) {
                hasError = (err->d_val != 0.0);
            } else {
                // Object or string - check for null/empty
                hasError = (err->ptr_val != nullptr);
            }
        }
    }

    if (hasError) {
        ts::ts_promise_reject_internal(context->promise, err);
    } else {
        ts::ts_promise_resolve_internal(context->promise, value);
    }

    return nullptr;
}

// Context for the promisified wrapper function
struct PromisifyWrapperContext {
    TsValue* originalFunction;
};

// The wrapper function that gets called when the promisified function is invoked
static TsValue* promisified_wrapper(void* ctx, int argc, TsValue** argv) {
    PromisifyWrapperContext* wrapperCtx = (PromisifyWrapperContext*)ctx;
    if (!wrapperCtx || !wrapperCtx->originalFunction) return nullptr;

    // Create a new promise
    ts::TsPromise* promise = ts::ts_promise_create();

    // Create the callback context
    PromisifyCallbackContext* cbCtx = (PromisifyCallbackContext*)ts_alloc(sizeof(PromisifyCallbackContext));
    cbCtx->promise = promise;

    // Create the callback function
    TsValue* callback = ts_value_make_native_function((void*)promisify_callback, cbCtx);

    // Build new argument array: original args + callback
    int newArgc = argc + 1;
    TsValue** newArgv = (TsValue**)ts_alloc(sizeof(TsValue*) * newArgc);
    for (int i = 0; i < argc; i++) {
        newArgv[i] = argv[i];
    }
    newArgv[argc] = callback;

    // Call the original function with the new arguments
    ts_function_call(wrapperCtx->originalFunction, newArgc, newArgv);

    // Return the promise as a boxed value
    TsValue* result = (TsValue*)ts_alloc(sizeof(TsValue));
    result->type = ValueType::PROMISE_PTR;
    result->ptr_val = promise;
    return result;
}

void* ts_util_promisify(void* fn) {
    if (!fn) return nullptr;

    // Create the wrapper context with the original function
    PromisifyWrapperContext* ctx = (PromisifyWrapperContext*)ts_alloc(sizeof(PromisifyWrapperContext));
    ctx->originalFunction = (TsValue*)fn;

    // Create and return the wrapper function
    return ts_value_make_native_function((void*)promisified_wrapper, ctx);
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

// ============================================================================
// util.callbackify implementation
// ============================================================================

// Context for the callbackified wrapper function
struct CallbackifyWrapperContext {
    TsValue* originalFunction;
};

// Callback to handle promise resolution/rejection
struct CallbackifyPromiseContext {
    TsValue* callback;
    bool resolved;
};

static TsValue* callbackify_then_handler(void* ctx, int argc, TsValue** argv) {
    CallbackifyPromiseContext* promiseCtx = (CallbackifyPromiseContext*)ctx;
    if (!promiseCtx || !promiseCtx->callback) return nullptr;

    // Call callback(null, value)
    TsValue* nullVal = ts_value_make_null();
    TsValue* value = (argc > 0) ? argv[0] : ts_value_make_undefined();

    TsValue* args[2] = { nullVal, value };
    ts_function_call(promiseCtx->callback, 2, args);

    return nullptr;
}

static TsValue* callbackify_catch_handler(void* ctx, int argc, TsValue** argv) {
    CallbackifyPromiseContext* promiseCtx = (CallbackifyPromiseContext*)ctx;
    if (!promiseCtx || !promiseCtx->callback) return nullptr;

    // Call callback(err)
    TsValue* err = (argc > 0) ? argv[0] : ts_value_make_undefined();

    // If error is falsy, create an Error with a default message
    bool isFalsy = false;
    if (!err || err->type == ValueType::UNDEFINED) {
        isFalsy = true;
    } else if (err->type == ValueType::BOOLEAN && !err->b_val) {
        isFalsy = true;
    } else if (err->type == ValueType::NUMBER_INT && err->i_val == 0) {
        isFalsy = true;
    }

    if (isFalsy) {
        TsString* errMsg = TsString::Create("The callback was invoked with a falsy reason");
        err = (TsValue*)ts_error_create(errMsg);
    }

    TsValue* args[1] = { err };
    ts_function_call(promiseCtx->callback, 1, args);

    return nullptr;
}

// The wrapper function that gets called when the callbackified function is invoked
static TsValue* callbackified_wrapper(void* ctx, int argc, TsValue** argv) {
    CallbackifyWrapperContext* wrapperCtx = (CallbackifyWrapperContext*)ctx;
    if (!wrapperCtx || !wrapperCtx->originalFunction) return nullptr;

    // The last argument should be the callback
    if (argc < 1) return nullptr;

    TsValue* callback = argv[argc - 1];

    // Call the original function without the callback
    int origArgc = argc - 1;
    TsValue** origArgv = (origArgc > 0) ? argv : nullptr;

    // Call the original async function
    TsValue* promiseVal = ts_function_call(wrapperCtx->originalFunction, origArgc, origArgv);

    // If result is a promise, attach then/catch handlers
    if (promiseVal && promiseVal->type == ValueType::PROMISE_PTR) {
        ts::TsPromise* promise = (ts::TsPromise*)promiseVal->ptr_val;

        // Create context for handlers
        CallbackifyPromiseContext* promiseCtx = (CallbackifyPromiseContext*)ts_alloc(sizeof(CallbackifyPromiseContext));
        promiseCtx->callback = callback;
        promiseCtx->resolved = false;

        // Create then handler using placement new to properly set magic
        TsValue thenHandler;
        thenHandler.type = ValueType::FUNCTION_PTR;
        void* thenMem = ts_alloc(sizeof(TsFunction));
        TsFunction* thenFunc = new (thenMem) TsFunction((void*)callbackify_then_handler, promiseCtx, FunctionType::NATIVE);
        thenHandler.ptr_val = thenFunc;

        // Create catch handler using placement new to properly set magic
        TsValue catchHandler;
        catchHandler.type = ValueType::FUNCTION_PTR;
        void* catchMem = ts_alloc(sizeof(TsFunction));
        TsFunction* catchFunc = new (catchMem) TsFunction((void*)callbackify_catch_handler, promiseCtx, FunctionType::NATIVE);
        catchHandler.ptr_val = catchFunc;

        promise->then(thenHandler, catchHandler);
    } else {
        // Not a promise - call callback with the result
        TsValue* nullVal = ts_value_make_null();
        TsValue* args[2] = { nullVal, promiseVal };
        ts_function_call(callback, 2, args);
    }

    return ts_value_make_undefined();
}

void* ts_util_callbackify(void* fn) {
    // Unbox if needed
    TsValue* fnVal = (TsValue*)fn;
    void* rawFn = ts_value_get_object(fnVal);
    if (!rawFn) rawFn = fn;

    // Create wrapper context
    CallbackifyWrapperContext* ctx = (CallbackifyWrapperContext*)ts_alloc(sizeof(CallbackifyWrapperContext));
    ctx->originalFunction = (TsValue*)rawFn;

    // If rawFn is a TsFunction, store the whole TsValue for proper calling
    if (fnVal && (fnVal->type == ValueType::FUNCTION_PTR || fnVal->type == ValueType::OBJECT_PTR)) {
        ctx->originalFunction = fnVal;
    }

    // Create and return the wrapper function
    return ts_value_make_native_function((void*)callbackified_wrapper, ctx);
}

// ============================================================================
// util.stripVTControlCharacters - removes ANSI escape codes from string
// ANSI escape sequences typically start with ESC (0x1B) followed by [
// Format: ESC [ <parameters> <letter>
// Also handles simple ESC sequences like ESC(B
// ============================================================================
void* ts_util_strip_vt_control_characters(void* str) {
    // Unbox if needed
    void* rawStr = ts_value_get_string((TsValue*)str);
    if (!rawStr) rawStr = str;
    TsString* input = (TsString*)rawStr;

    if (!input) return TsString::Create("");

    const char* src = input->ToUtf8();
    if (!src) return TsString::Create("");

    size_t len = strlen(src);
    std::string result;
    result.reserve(len);

    for (size_t i = 0; i < len; i++) {
        // Check for ESC character (0x1B or 27)
        if ((unsigned char)src[i] == 0x1B) {
            // Check for CSI (Control Sequence Introducer) - ESC [
            if (i + 1 < len && src[i + 1] == '[') {
                // Skip ESC [
                i += 2;
                // Skip parameter bytes (0x30-0x3F) and intermediate bytes (0x20-0x2F)
                while (i < len) {
                    char c = src[i];
                    // Parameter bytes: 0-9 ; : < = > ?
                    // Intermediate bytes: space ! " # $ % & ' ( ) * + , - . /
                    if ((c >= 0x20 && c <= 0x3F)) {
                        i++;
                    } else {
                        break;
                    }
                }
                // Skip final byte (0x40-0x7E)
                if (i < len && src[i] >= 0x40 && src[i] <= 0x7E) {
                    // i is incremented by the outer loop
                }
                continue;
            }
            // Check for OSC (Operating System Command) - ESC ]
            else if (i + 1 < len && src[i + 1] == ']') {
                // Skip until BEL (0x07) or ST (ESC \)
                i += 2;
                while (i < len) {
                    if (src[i] == 0x07) break;  // BEL
                    if (src[i] == 0x1B && i + 1 < len && src[i + 1] == '\\') {
                        i++;
                        break;
                    }
                    i++;
                }
                continue;
            }
            // Check for simple two-byte escape sequences (ESC followed by a char)
            // Examples: ESC(B, ESC)0, etc.
            else if (i + 1 < len) {
                char next = src[i + 1];
                // Skip character set designation and other simple sequences
                if ((next >= 0x40 && next <= 0x5F) || // C1 control codes
                    (next >= 0x60 && next <= 0x7E) || // Single character functions
                    next == '(' || next == ')' || next == '*' || next == '+' ||
                    next == '-' || next == '.' || next == '/') {
                    i++; // Skip the next character too
                    // If it's a character set, skip the designator
                    if ((next == '(' || next == ')' || next == '*' || next == '+') &&
                        i + 1 < len) {
                        i++;
                    }
                }
                continue;
            }
            // Single ESC at end - skip it
            continue;
        }
        // Check for other control characters (C0 and C1)
        // C0: 0x00-0x1F except common ones like \t, \n, \r
        // C1: 0x80-0x9F (in UTF-8 these are part of multibyte sequences, so skip this check)
        else if ((unsigned char)src[i] < 0x20 &&
                 src[i] != '\t' && src[i] != '\n' && src[i] != '\r') {
            continue;  // Skip control character
        }
        // Regular character - keep it
        result += src[i];
    }

    return TsString::Create(result.c_str());
}

// ============================================================================
// util.toUSVString - converts to valid Unicode Scalar Value string
// Replaces lone surrogates (U+D800-U+DFFF) with U+FFFD
// In UTF-8, lone surrogates are technically invalid but may appear in
// some malformed strings. We detect and replace them.
// ============================================================================
void* ts_util_to_usv_string(void* str) {
    // Unbox if needed
    void* rawStr = ts_value_get_string((TsValue*)str);
    if (!rawStr) rawStr = str;
    TsString* input = (TsString*)rawStr;

    if (!input) return TsString::Create("");

    const char* src = input->ToUtf8();
    if (!src) return TsString::Create("");

    size_t len = strlen(src);
    std::string result;
    result.reserve(len);

    for (size_t i = 0; i < len; ) {
        unsigned char c = (unsigned char)src[i];

        // ASCII (single byte)
        if (c < 0x80) {
            result += src[i];
            i++;
        }
        // 2-byte sequence
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < len && (src[i + 1] & 0xC0) == 0x80) {
                result += src[i];
                result += src[i + 1];
            } else {
                // Invalid sequence - replace with U+FFFD (EF BF BD in UTF-8)
                result += "\xEF\xBF\xBD";
            }
            i += 2;
        }
        // 3-byte sequence (where surrogates would be)
        else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < len && (src[i + 1] & 0xC0) == 0x80 && (src[i + 2] & 0xC0) == 0x80) {
                // Decode to check for surrogates
                uint32_t codepoint = ((c & 0x0F) << 12) |
                                   ((src[i + 1] & 0x3F) << 6) |
                                   (src[i + 2] & 0x3F);
                // Check if it's a surrogate (U+D800-U+DFFF)
                if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                    // Replace with U+FFFD
                    result += "\xEF\xBF\xBD";
                } else {
                    result += src[i];
                    result += src[i + 1];
                    result += src[i + 2];
                }
            } else {
                // Invalid sequence
                result += "\xEF\xBF\xBD";
            }
            i += 3;
        }
        // 4-byte sequence
        else if ((c & 0xF8) == 0xF0) {
            if (i + 3 < len &&
                (src[i + 1] & 0xC0) == 0x80 &&
                (src[i + 2] & 0xC0) == 0x80 &&
                (src[i + 3] & 0xC0) == 0x80) {
                result += src[i];
                result += src[i + 1];
                result += src[i + 2];
                result += src[i + 3];
            } else {
                // Invalid sequence
                result += "\xEF\xBF\xBD";
            }
            i += 4;
        }
        // Invalid leading byte
        else {
            result += "\xEF\xBF\xBD";
            i++;
        }
    }

    return TsString::Create(result.c_str());
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

// Specific TypedArray type checks
bool ts_util_types_is_int8_array(void* value) {
    return TsUtilTypes::isInt8Array(value);
}

bool ts_util_types_is_int16_array(void* value) {
    return TsUtilTypes::isInt16Array(value);
}

bool ts_util_types_is_int32_array(void* value) {
    return TsUtilTypes::isInt32Array(value);
}

bool ts_util_types_is_uint8_clamped_array(void* value) {
    return TsUtilTypes::isUint8ClampedArray(value);
}

bool ts_util_types_is_uint16_array(void* value) {
    return TsUtilTypes::isUint16Array(value);
}

bool ts_util_types_is_uint32_array(void* value) {
    return TsUtilTypes::isUint32Array(value);
}

bool ts_util_types_is_float32_array(void* value) {
    return TsUtilTypes::isFloat32Array(value);
}

bool ts_util_types_is_float64_array(void* value) {
    return TsUtilTypes::isFloat64Array(value);
}

bool ts_util_types_is_data_view(void* value) {
    return TsUtilTypes::isDataView(value);
}

} // extern "C"
