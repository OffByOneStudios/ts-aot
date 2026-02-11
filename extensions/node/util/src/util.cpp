#include "TsUtil.h"
#include "TsRuntime.h"
#include "TsJSON.h"
#include "TsBuffer.h"
#include "TsMap.h"
#include "TsWeakMap.h"
#include "TsSet.h"
#include "TsWeakSet.h"
#include "TsDate.h"
#include "TsRegExp.h"
#include "TsArray.h"
#include "TsPromise.h"
#include "TsProxy.h"
#include "TsBoxedPrimitives.h"
#include "TsSymbol.h"
#include "GC.h"
#include "TsGC.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <unordered_set>
#include <cctype>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

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

// Forward declaration for recursive calls
static TsString* inspectValue(void* val, int depth, int currentDepth, bool colors);

static TsString* inspectArray(TsArray* arr, int depth, int currentDepth, bool colors) {
    if (!arr) return TsString::Create("[]");

    size_t len = (size_t)arr->Length();
    if (len == 0) return TsString::Create("[]");

    if (currentDepth >= depth) {
        return TsString::Create("[Array]");
    }

    std::ostringstream result;
    result << "[ ";

    for (size_t i = 0; i < len; i++) {
        if (i > 0) result << ", ";
        // Use GetElementBoxed to get a properly typed TsValue*
        TsValue* elem = arr->GetElementBoxed(i);
        TsString* elemStr = inspectValue(elem, depth, currentDepth + 1, colors);
        result << (elemStr ? elemStr->ToUtf8() : "undefined");
    }

    result << " ]";
    return TsString::Create(result.str().c_str());
}

static TsString* inspectMap(TsMap* map, int depth, int currentDepth, bool colors) {
    if (!map) return TsString::Create("{}");

    if (currentDepth >= depth) {
        return TsString::Create("[Object]");
    }

    // Check if it's an explicit Map (new Map()) vs plain object
    if (map->IsExplicitMap()) {
        std::ostringstream result;
        result << "Map(";

        // Get keys array using GetKeys()
        TsArray* keys = (TsArray*)map->GetKeys();
        if (keys) {
            size_t len = (size_t)keys->Length();
            result << len << ") { ";
            for (size_t i = 0; i < len; i++) {
                if (i > 0) result << ", ";
                // Use GetElementBoxed for properly typed access
                TsValue* keyElem = keys->GetElementBoxed(i);
                TsString* keyStr = inspectValue(keyElem, depth, currentDepth + 1, colors);
                // Get the value from the map
                TsValue keyVal;
                keyVal.type = keyElem ? keyElem->type : ValueType::UNDEFINED;
                keyVal.ptr_val = keyElem ? keyElem->ptr_val : nullptr;
                TsValue val = map->Get(keyVal);
                // Box the value for inspection
                TsValue* valBoxed = (TsValue*)ts_alloc(sizeof(TsValue));
                *valBoxed = val;
                TsString* valStr = inspectValue(valBoxed, depth, currentDepth + 1, colors);
                result << (keyStr ? keyStr->ToUtf8() : "undefined") << " => " << (valStr ? valStr->ToUtf8() : "undefined");
            }
            result << " }";
        } else {
            result << "0) {}";
        }
        return TsString::Create(result.str().c_str());
    }

    // Plain object - use JSON-like representation
    TsString* json = (TsString*)ts_json_stringify(map, nullptr, nullptr);
    if (json) return json;

    return TsString::Create("[Object]");
}

static TsString* inspectValue(void* val, int depth, int currentDepth, bool colors) {
    if (!val) return TsString::Create("undefined");

    // Check if it's a boxed TsValue
    TsValue* tv = (TsValue*)val;
    uint8_t typeField = *(uint8_t*)tv;

    if (typeField <= (uint8_t)ValueType::FUNCTION_PTR) {
        switch (tv->type) {
            case ValueType::UNDEFINED:
                return TsString::Create("undefined");
            case ValueType::BOOLEAN:
                return TsString::Create(tv->b_val ? "true" : "false");
            case ValueType::NUMBER_INT: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)tv->i_val);
                return TsString::Create(buf);
            }
            case ValueType::NUMBER_DBL: {
                char buf[32];
                if (std::isnan(tv->d_val)) return TsString::Create("NaN");
                if (std::isinf(tv->d_val)) return TsString::Create(tv->d_val > 0 ? "Infinity" : "-Infinity");
                snprintf(buf, sizeof(buf), "%.17g", tv->d_val);
                return TsString::Create(buf);
            }
            case ValueType::STRING_PTR: {
                TsString* s = (TsString*)tv->ptr_val;
                if (!s) return TsString::Create("''");
                std::string result = "'";
                result += s->ToUtf8();
                result += "'";
                return TsString::Create(result.c_str());
            }
            case ValueType::ARRAY_PTR: {
                TsArray* arr = (TsArray*)tv->ptr_val;
                return inspectArray(arr, depth, currentDepth, colors);
            }
            case ValueType::OBJECT_PTR: {
                void* ptr = tv->ptr_val;
                // Check various object types by magic
                if (ptr) {
                    // First check if this is actually a pointer to another TsValue (double-boxed)
                    uint8_t innerType = *(uint8_t*)ptr;
                    if (innerType <= (uint8_t)ValueType::FUNCTION_PTR) {
                        // This looks like a TsValue, recurse into it
                        return inspectValue(ptr, depth, currentDepth, colors);
                    }

                    // Check magic at offset 0 for non-TsObject classes (TsDate, TsRegExp, TsArray, TsString, TsBuffer)
                    uint32_t magic0 = *(uint32_t*)ptr;
                    if (magic0 == TsDate::MAGIC) {
                        TsDate* d = (TsDate*)ptr;
                        return TsString::Create(d->ToISOString()->ToUtf8());
                    }
                    if (magic0 == TsRegExp::MAGIC) {
                        TsRegExp* re = (TsRegExp*)ptr;
                        TsString* source = re->GetSource();
                        TsString* flags = re->GetFlags();
                        std::string result = "/";
                        result += (source ? source->ToUtf8() : "");
                        result += "/";
                        result += (flags ? flags->ToUtf8() : "");
                        return TsString::Create(result.c_str());
                    }
                    if (magic0 == TsArray::MAGIC) {
                        return inspectArray((TsArray*)ptr, depth, currentDepth, colors);
                    }
                    if (magic0 == TsString::MAGIC) {
                        TsString* s = (TsString*)ptr;
                        std::string result = "'";
                        result += s->ToUtf8();
                        result += "'";
                        return TsString::Create(result.c_str());
                    }
                    if (magic0 == TsBuffer::MAGIC) {
                        TsBuffer* buf = (TsBuffer*)ptr;
                        std::ostringstream result;
                        result << "<Buffer";
                        size_t len = buf->GetLength();
                        if (len > 0) {
                            for (size_t i = 0; i < std::min(len, (size_t)50); i++) {
                                char hex[4];
                                snprintf(hex, sizeof(hex), " %02x", buf->GetData()[i]);
                                result << hex;
                            }
                            if (len > 50) result << " ... " << (len - 50) << " more bytes";
                        }
                        result << ">";
                        return TsString::Create(result.str().c_str());
                    }
                    // Check for TsObject-derived types: magic is at offset 16
                    // (offset 0-7 = vtable, offset 8-15 = explicit vtable ptr, offset 16 = magic)
                    uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
                    if (magic16 == TsSet::MAGIC) {
                        TsSet* s = (TsSet*)ptr;
                        std::ostringstream result;
                        result << "Set(" << s->Size() << ") { ";
                        TsArray* values = (TsArray*)s->GetValues();
                        if (values) {
                            size_t len = (size_t)values->Length();
                            for (size_t i = 0; i < len; i++) {
                                if (i > 0) result << ", ";
                                // Use GetElementBoxed for properly typed access
                                TsValue* v = values->GetElementBoxed(i);
                                TsString* vs = inspectValue(v, depth, currentDepth + 1, colors);
                                result << (vs ? vs->ToUtf8() : "undefined");
                            }
                        }
                        result << " }";
                        return TsString::Create(result.str().c_str());
                    }
                    if (magic16 == TsMap::MAGIC) {
                        return inspectMap((TsMap*)ptr, depth, currentDepth, colors);
                    }
                }
                return TsString::Create("[Object]");
            }
            case ValueType::FUNCTION_PTR:
                return TsString::Create("[Function]");
            case ValueType::PROMISE_PTR:
                return TsString::Create("Promise { <pending> }");
            case ValueType::BIGINT_PTR:
                return TsString::Create("[BigInt]");
            case ValueType::SYMBOL_PTR:
                return TsString::Create("Symbol()");
            default:
                break;
        }
    }

    // Raw pointer - check object types
    uint32_t magic = *(uint32_t*)val;

    // TsString
    if (magic == TsString::MAGIC) {
        TsString* s = (TsString*)val;
        std::string result = "'";
        result += s->ToUtf8();
        result += "'";
        return TsString::Create(result.c_str());
    }

    // TsArray
    if (magic == TsArray::MAGIC) {
        return inspectArray((TsArray*)val, depth, currentDepth, colors);
    }

    // TsDate (magic at offset 0)
    if (magic == TsDate::MAGIC) {
        TsDate* d = (TsDate*)val;
        return TsString::Create(d->ToISOString()->ToUtf8());
    }

    // TsRegExp (magic at offset 0)
    if (magic == TsRegExp::MAGIC) {
        TsRegExp* re = (TsRegExp*)val;
        TsString* source = re->GetSource();
        TsString* flags = re->GetFlags();
        std::string result = "/";
        result += (source ? source->ToUtf8() : "");
        result += "/";
        result += (flags ? flags->ToUtf8() : "");
        return TsString::Create(result.c_str());
    }

    // Check magic at offset 16 for TsObject-derived classes
    uint32_t* magic16 = (uint32_t*)((char*)val + 16);
    if (*magic16 == TsMap::MAGIC) {
        return inspectMap((TsMap*)val, depth, currentDepth, colors);
    }

    // Use JSON stringify as fallback
    TsString* json = (TsString*)ts_json_stringify(val, nullptr, nullptr);
    if (json) return json;

    return TsString::Create("[object Object]");
}

TsString* ts_util_inspect_impl(void* obj, int depth, bool colors) {
    if (!obj) return TsString::Create("undefined");
    return inspectValue(obj, depth, 0, colors);
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
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsBuffer::MAGIC;
}

bool isArrayBufferView(void* value) {
    // ArrayBufferView includes TypedArrays and DataView
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsTypedArray::MAGIC || obj->magic == TsDataView::MAGIC;
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
    if (!value) return false;
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    TsObject* obj = (TsObject*)rawPtr;
    // Check for TsGenerator (sync generator objects) and TsAsyncGenerator
    return obj->magic == ts::TsGenerator::MAGIC || obj->magic == ts::TsAsyncGenerator::MAGIC;
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

bool isBigInt64Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::BigInt64;
}

bool isBigUint64Array(void* value) {
    TsTypedArray* ta = getTypedArray(value);
    return ta && ta->GetType() == TypedArrayType::BigUint64;
}

bool isDataView(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsDataView inherits from TsObject, check magic
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsDataView::MAGIC;
}

bool isProxy(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    // TsProxy has its own proxyMagic member
    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawPtr);
    return proxy != nullptr;
}

bool isWeakMap(void* value) {
    if (!value) return false;
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsWeakMap::MAGIC;
}

bool isWeakSet(void* value) {
    if (!value) return false;
    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;
    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsWeakSet::MAGIC;
}

bool isAnyArrayBuffer(void* value) {
    // isAnyArrayBuffer returns true for both ArrayBuffer and SharedArrayBuffer
    // We only have ArrayBuffer (implemented as Buffer), no SharedArrayBuffer
    return isArrayBuffer(value);
}

bool isBooleanObject(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsBooleanObject::MAGIC;
}

bool isNumberObject(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsNumberObject::MAGIC;
}

bool isStringObject(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsStringObject::MAGIC;
}

bool isSymbolObject(void* value) {
    if (!value) return false;

    void* rawPtr = ts_value_get_object((TsValue*)value);
    if (!rawPtr) rawPtr = value;

    TsObject* obj = (TsObject*)rawPtr;
    return obj->magic == TsSymbolObject::MAGIC;
}

bool isBoxedPrimitive(void* value) {
    // Returns true for any boxed primitive (Boolean, Number, String, Symbol, BigInt objects)
    // We currently support Boolean, Number, String, and Symbol objects
    return isBooleanObject(value) || isNumberObject(value) || isStringObject(value) || isSymbolObject(value);
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
    // In Node.js, util.inherits sets up prototype chain:
    // constructor.prototype = Object.create(superConstructor.prototype)
    // constructor.prototype.constructor = constructor
    // constructor.super_ = superConstructor
    //
    // In our AOT environment, we can set the super_ property for compatibility
    // and try to set up prototype if the constructor is a function object

    if (!constructor || !superConstructor) return;

    // Unbox if needed
    TsValue* ctorVal = (TsValue*)constructor;
    TsValue* superVal = (TsValue*)superConstructor;

    // If constructor is a function, set the super_ property
    if (ctorVal->type == ValueType::FUNCTION_PTR) {
        TsFunction* ctorFunc = (TsFunction*)ctorVal->ptr_val;
        if (!ctorFunc) return;

        // Ensure properties map exists
        if (!ctorFunc->properties) {
            ctorFunc->properties = TsMap::Create();
        }

        // Set super_ = superConstructor
        TsString* superKey = TsString::Create("super_");
        TsValue keyVal;
        keyVal.type = ValueType::STRING_PTR;
        keyVal.ptr_val = superKey;
        ctorFunc->properties->Set(keyVal, *superVal);
    }

    // Note: Full prototype chain linking would require Object.create and
    // prototype manipulation which we don't fully support for AOT-compiled code.
    // The key purpose is to make ctor.super_ accessible for compatibility.
}

// Context for deprecated function wrapper
struct DeprecateContext {
    TsValue* originalFunction;
    TsString* message;
    bool warned;
};

// Set to track which warnings have been shown (by message)
static std::unordered_set<std::string>* deprecateWarningsShown = nullptr;

// The wrapper function that emits warning on first call
static TsValue* deprecated_wrapper(void* ctx, int argc, TsValue** argv) {
    DeprecateContext* depCtx = (DeprecateContext*)ctx;
    if (!depCtx) return nullptr;

    // Emit warning only once (per message)
    if (!depCtx->warned && depCtx->message) {
        const char* msgCStr = depCtx->message->ToUtf8();

        // Initialize warnings set if needed
        if (!deprecateWarningsShown) {
            deprecateWarningsShown = new std::unordered_set<std::string>();
        }

        // Only show each unique message once
        if (deprecateWarningsShown->count(msgCStr) == 0) {
            deprecateWarningsShown->insert(msgCStr);
            fprintf(stderr, "(node) DeprecationWarning: %s\n", msgCStr);
        }
        depCtx->warned = true;
    }

    // Call the original function using ts_function_call which handles all function types
    if (!depCtx->originalFunction) return nullptr;
    return ts_function_call(depCtx->originalFunction, argc, argv);
}

void* ts_util_deprecate(void* fn, void* msg) {
    // Keep the original function as TsValue*
    TsValue* originalFunc = (TsValue*)fn;

    // Unbox the message
    TsString* msgStr = nullptr;
    if (msg) {
        void* rawMsg = ts_value_get_string((TsValue*)msg);
        if (!rawMsg) rawMsg = msg;
        msgStr = (TsString*)rawMsg;
    }

    // Create context
    DeprecateContext* ctx = (DeprecateContext*)ts_alloc(sizeof(DeprecateContext));
    ctx->originalFunction = originalFunc;
    ctx->message = msgStr;
    ctx->warned = false;

    // Create and return the wrapper function
    return ts_value_make_native_function((void*)deprecated_wrapper, ctx);
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

bool ts_util_types_is_big_int64_array(void* value) {
    return TsUtilTypes::isBigInt64Array(value);
}

bool ts_util_types_is_big_uint64_array(void* value) {
    return TsUtilTypes::isBigUint64Array(value);
}

bool ts_util_types_is_data_view(void* value) {
    return TsUtilTypes::isDataView(value);
}

bool ts_util_types_is_proxy(void* value) {
    return TsUtilTypes::isProxy(value);
}

bool ts_util_types_is_weak_map(void* value) {
    return TsUtilTypes::isWeakMap(value);
}

bool ts_util_types_is_weak_set(void* value) {
    return TsUtilTypes::isWeakSet(value);
}

bool ts_util_types_is_any_array_buffer(void* value) {
    return TsUtilTypes::isAnyArrayBuffer(value);
}

bool ts_util_types_is_boolean_object(void* value) {
    return TsUtilTypes::isBooleanObject(value);
}

bool ts_util_types_is_number_object(void* value) {
    return TsUtilTypes::isNumberObject(value);
}

bool ts_util_types_is_string_object(void* value) {
    return TsUtilTypes::isStringObject(value);
}

bool ts_util_types_is_symbol_object(void* value) {
    return TsUtilTypes::isSymbolObject(value);
}

bool ts_util_types_is_boxed_primitive(void* value) {
    return TsUtilTypes::isBoxedPrimitive(value);
}

// ============================================================================
// util.getSystemErrorName(errno) - Returns the string name for a numeric error code
// ============================================================================
void* ts_util_get_system_error_name(int64_t errnum) {
    // Map common errno values to their names
    const char* name = nullptr;
    switch (errnum) {
        case 1: name = "EPERM"; break;
        case 2: name = "ENOENT"; break;
        case 3: name = "ESRCH"; break;
        case 4: name = "EINTR"; break;
        case 5: name = "EIO"; break;
        case 6: name = "ENXIO"; break;
        case 7: name = "E2BIG"; break;
        case 8: name = "ENOEXEC"; break;
        case 9: name = "EBADF"; break;
        case 10: name = "ECHILD"; break;
        case 11: name = "EAGAIN"; break;  // Also EWOULDBLOCK
        case 12: name = "ENOMEM"; break;
        case 13: name = "EACCES"; break;
        case 14: name = "EFAULT"; break;
        case 16: name = "EBUSY"; break;
        case 17: name = "EEXIST"; break;
        case 18: name = "EXDEV"; break;
        case 19: name = "ENODEV"; break;
        case 20: name = "ENOTDIR"; break;
        case 21: name = "EISDIR"; break;
        case 22: name = "EINVAL"; break;
        case 23: name = "ENFILE"; break;
        case 24: name = "EMFILE"; break;
        case 25: name = "ENOTTY"; break;
        case 27: name = "EFBIG"; break;
        case 28: name = "ENOSPC"; break;
        case 29: name = "ESPIPE"; break;
        case 30: name = "EROFS"; break;
        case 31: name = "EMLINK"; break;
        case 32: name = "EPIPE"; break;
        case 33: name = "EDOM"; break;
        case 34: name = "ERANGE"; break;
        case 36: name = "EDEADLK"; break;
        case 38: name = "ENAMETOOLONG"; break;
        case 39: name = "ENOLCK"; break;
        case 40: name = "ENOSYS"; break;
        case 41: name = "ENOTEMPTY"; break;
        // Network errors (platform-specific, common values)
        case 100: name = "EPROTO"; break;
        case 101: name = "ENETDOWN"; break;
        case 102: name = "ENETUNREACH"; break;
        case 104: name = "ECONNRESET"; break;
        case 105: name = "ENOBUFS"; break;
        case 106: name = "EISCONN"; break;
        case 107: name = "ENOTCONN"; break;
        case 110: name = "ETIMEDOUT"; break;
        case 111: name = "ECONNREFUSED"; break;
        case 113: name = "EHOSTUNREACH"; break;
        case 114: name = "EALREADY"; break;
        case 115: name = "EINPROGRESS"; break;
        default: name = nullptr; break;
    }

    if (name) {
        return TsString::Create(name);
    }

    // Return empty string for unknown errors
    return TsString::Create("");
}

// ============================================================================
// util.getSystemErrorMap() - Returns a Map of all system error codes
// ============================================================================
void* ts_util_get_system_error_map() {
    TsMap* map = TsMap::Create();

    // Add common errno mappings
    auto addError = [&](int code, const char* name, const char* message) {
        TsArray* arr = TsArray::Create(2);
        arr->Set(0, (int64_t)TsString::Create(name));
        arr->Set(1, (int64_t)TsString::Create(message));
        map->Set((int64_t)code, (int64_t)arr);
    };

    addError(1, "EPERM", "operation not permitted");
    addError(2, "ENOENT", "no such file or directory");
    addError(3, "ESRCH", "no such process");
    addError(4, "EINTR", "interrupted system call");
    addError(5, "EIO", "i/o error");
    addError(6, "ENXIO", "no such device or address");
    addError(7, "E2BIG", "argument list too long");
    addError(8, "ENOEXEC", "exec format error");
    addError(9, "EBADF", "bad file descriptor");
    addError(10, "ECHILD", "no child processes");
    addError(11, "EAGAIN", "resource temporarily unavailable");
    addError(12, "ENOMEM", "cannot allocate memory");
    addError(13, "EACCES", "permission denied");
    addError(14, "EFAULT", "bad address");
    addError(16, "EBUSY", "resource busy or locked");
    addError(17, "EEXIST", "file already exists");
    addError(18, "EXDEV", "cross-device link not permitted");
    addError(19, "ENODEV", "no such device");
    addError(20, "ENOTDIR", "not a directory");
    addError(21, "EISDIR", "is a directory");
    addError(22, "EINVAL", "invalid argument");
    addError(23, "ENFILE", "file table overflow");
    addError(24, "EMFILE", "too many open files");
    addError(25, "ENOTTY", "inappropriate ioctl for device");
    addError(27, "EFBIG", "file too large");
    addError(28, "ENOSPC", "no space left on device");
    addError(29, "ESPIPE", "illegal seek");
    addError(30, "EROFS", "read-only file system");
    addError(31, "EMLINK", "too many links");
    addError(32, "EPIPE", "broken pipe");
    addError(33, "EDOM", "numerical argument out of domain");
    addError(34, "ERANGE", "result too large");
    addError(36, "EDEADLK", "resource deadlock would occur");
    addError(38, "ENAMETOOLONG", "file name too long");
    addError(39, "ENOLCK", "no locks available");
    addError(40, "ENOSYS", "function not implemented");
    addError(41, "ENOTEMPTY", "directory not empty");
    addError(100, "EPROTO", "protocol error");
    addError(101, "ENETDOWN", "network is down");
    addError(102, "ENETUNREACH", "network is unreachable");
    addError(104, "ECONNRESET", "connection reset by peer");
    addError(105, "ENOBUFS", "no buffer space available");
    addError(106, "EISCONN", "socket is already connected");
    addError(107, "ENOTCONN", "socket is not connected");
    addError(110, "ETIMEDOUT", "connection timed out");
    addError(111, "ECONNREFUSED", "connection refused");
    addError(113, "EHOSTUNREACH", "host is unreachable");
    addError(114, "EALREADY", "operation already in progress");
    addError(115, "EINPROGRESS", "operation now in progress");

    return map;
}

// ============================================================================
// util.styleText(format, text) - Apply ANSI styles to text
// ============================================================================
void* ts_util_style_text(void* formatArg, void* textArg) {
    // Unbox if needed
    void* rawFormat = ts_value_get_string((TsValue*)formatArg);
    if (!rawFormat) rawFormat = formatArg;
    TsString* format = (TsString*)rawFormat;

    void* rawText = ts_value_get_string((TsValue*)textArg);
    if (!rawText) rawText = textArg;
    TsString* text = (TsString*)rawText;

    if (!format || !text) return TsString::Create("");

    const char* formatStr = format->ToUtf8();
    const char* textStr = text->ToUtf8();

    if (!formatStr || !textStr) return TsString::Create("");

    // ANSI escape codes
    const char* startCode = "";
    const char* endCode = "\x1b[0m";  // Reset

    // Text styles
    if (strcmp(formatStr, "bold") == 0) startCode = "\x1b[1m";
    else if (strcmp(formatStr, "italic") == 0) startCode = "\x1b[3m";
    else if (strcmp(formatStr, "underline") == 0) startCode = "\x1b[4m";
    else if (strcmp(formatStr, "strikethrough") == 0) startCode = "\x1b[9m";
    else if (strcmp(formatStr, "dim") == 0) startCode = "\x1b[2m";
    else if (strcmp(formatStr, "inverse") == 0) startCode = "\x1b[7m";
    else if (strcmp(formatStr, "hidden") == 0) startCode = "\x1b[8m";
    // Foreground colors
    else if (strcmp(formatStr, "black") == 0) startCode = "\x1b[30m";
    else if (strcmp(formatStr, "red") == 0) startCode = "\x1b[31m";
    else if (strcmp(formatStr, "green") == 0) startCode = "\x1b[32m";
    else if (strcmp(formatStr, "yellow") == 0) startCode = "\x1b[33m";
    else if (strcmp(formatStr, "blue") == 0) startCode = "\x1b[34m";
    else if (strcmp(formatStr, "magenta") == 0) startCode = "\x1b[35m";
    else if (strcmp(formatStr, "cyan") == 0) startCode = "\x1b[36m";
    else if (strcmp(formatStr, "white") == 0) startCode = "\x1b[37m";
    else if (strcmp(formatStr, "gray") == 0 || strcmp(formatStr, "grey") == 0) startCode = "\x1b[90m";
    // Bright foreground colors
    else if (strcmp(formatStr, "redBright") == 0) startCode = "\x1b[91m";
    else if (strcmp(formatStr, "greenBright") == 0) startCode = "\x1b[92m";
    else if (strcmp(formatStr, "yellowBright") == 0) startCode = "\x1b[93m";
    else if (strcmp(formatStr, "blueBright") == 0) startCode = "\x1b[94m";
    else if (strcmp(formatStr, "magentaBright") == 0) startCode = "\x1b[95m";
    else if (strcmp(formatStr, "cyanBright") == 0) startCode = "\x1b[96m";
    else if (strcmp(formatStr, "whiteBright") == 0) startCode = "\x1b[97m";
    // Background colors
    else if (strcmp(formatStr, "bgBlack") == 0) startCode = "\x1b[40m";
    else if (strcmp(formatStr, "bgRed") == 0) startCode = "\x1b[41m";
    else if (strcmp(formatStr, "bgGreen") == 0) startCode = "\x1b[42m";
    else if (strcmp(formatStr, "bgYellow") == 0) startCode = "\x1b[43m";
    else if (strcmp(formatStr, "bgBlue") == 0) startCode = "\x1b[44m";
    else if (strcmp(formatStr, "bgMagenta") == 0) startCode = "\x1b[45m";
    else if (strcmp(formatStr, "bgCyan") == 0) startCode = "\x1b[46m";
    else if (strcmp(formatStr, "bgWhite") == 0) startCode = "\x1b[47m";
    // If unknown format, just return the text unchanged
    else {
        return text;
    }

    // Construct styled string
    std::string result = startCode;
    result += textStr;
    result += endCode;

    return TsString::Create(result.c_str());
}

// ============================================================================
// util.debuglog(section) - Returns a logging function for the given section
// Checks NODE_DEBUG environment variable to see if this section is enabled
// NODE_DEBUG can be a comma-separated list of section names (case-insensitive)
// ============================================================================

// Global cache of enabled sections (parsed once from NODE_DEBUG)
static std::unordered_set<std::string>* debuglogEnabledSections = nullptr;
static bool debuglogInitialized = false;

static void initDebuglogSections() {
    if (debuglogInitialized) return;
    debuglogInitialized = true;

    debuglogEnabledSections = new std::unordered_set<std::string>();

    const char* nodeDebug = getenv("NODE_DEBUG");
    if (!nodeDebug) return;

    // Parse comma-separated list of section names
    std::string sections(nodeDebug);
    size_t pos = 0;
    while (pos < sections.length()) {
        // Skip leading whitespace and commas
        while (pos < sections.length() && (sections[pos] == ',' || sections[pos] == ' ')) {
            pos++;
        }

        // Find end of section name
        size_t start = pos;
        while (pos < sections.length() && sections[pos] != ',' && sections[pos] != ' ') {
            pos++;
        }

        if (pos > start) {
            std::string section = sections.substr(start, pos - start);
            // Convert to uppercase for case-insensitive comparison
            for (char& c : section) {
                c = toupper((unsigned char)c);
            }
            debuglogEnabledSections->insert(section);
        }
    }
}

static bool isDebuglogSectionEnabled(const char* section) {
    initDebuglogSections();
    if (!debuglogEnabledSections || debuglogEnabledSections->empty()) return false;

    std::string sectionUpper(section);
    for (char& c : sectionUpper) {
        c = toupper((unsigned char)c);
    }

    // Check for wildcard
    if (debuglogEnabledSections->count("*") > 0) return true;

    return debuglogEnabledSections->count(sectionUpper) > 0;
}

// Context for debuglog wrapper function
struct DebuglogContext {
    TsString* section;
    bool enabled;
};

// The actual debuglog function that gets called
static TsValue* debuglog_function(void* ctx, int argc, TsValue** argv) {
    DebuglogContext* dlCtx = (DebuglogContext*)ctx;
    if (!dlCtx || !dlCtx->enabled) return nullptr;

    // Build the message: "SECTION PID: <formatted message>"
    std::ostringstream prefix;
    prefix << dlCtx->section->ToUtf8() << " " << getpid() << ": ";

    // Format the arguments (similar to console.log)
    std::ostringstream msg;
    msg << prefix.str();

    for (int i = 0; i < argc; i++) {
        if (i > 0) msg << " ";
        TsValue* arg = argv[i];
        if (!arg) {
            msg << "undefined";
            continue;
        }

        switch (arg->type) {
            case ValueType::STRING_PTR:
                if (arg->ptr_val) {
                    msg << ((TsString*)arg->ptr_val)->ToUtf8();
                }
                break;
            case ValueType::NUMBER_INT:
                msg << arg->i_val;
                break;
            case ValueType::NUMBER_DBL:
                msg << arg->d_val;
                break;
            case ValueType::BOOLEAN:
                msg << (arg->b_val ? "true" : "false");
                break;
            case ValueType::UNDEFINED:
                msg << "undefined";
                break;
            default:
                msg << "[object]";
                break;
        }
    }

    // Output to stderr (like Node.js debuglog)
    fprintf(stderr, "%s\n", msg.str().c_str());

    return nullptr;
}

void* ts_util_debuglog(void* section) {
    // Unbox the section string
    void* rawSection = ts_value_get_string((TsValue*)section);
    if (!rawSection) rawSection = section;
    TsString* sectionStr = (TsString*)rawSection;

    if (!sectionStr) {
        // Return a no-op function
        return ts_value_make_native_function(nullptr, nullptr);
    }

    const char* sectionCStr = sectionStr->ToUtf8();
    bool enabled = isDebuglogSectionEnabled(sectionCStr);

    // Create the context
    DebuglogContext* ctx = (DebuglogContext*)ts_alloc(sizeof(DebuglogContext));
    ctx->section = TsString::Create(sectionCStr);
    // Convert section to uppercase for display
    std::string upper(sectionCStr);
    for (char& c : upper) {
        c = toupper((unsigned char)c);
    }
    ctx->section = TsString::Create(upper.c_str());
    ctx->enabled = enabled;

    // Create and return the debuglog function
    return ts_value_make_native_function((void*)debuglog_function, ctx);
}

// ============================================================================
// util.formatWithOptions(inspectOptions, format, ...args)
// Like util.format() but with inspect options
// ============================================================================
void* ts_util_format_with_options(void* options, void* format, void* args) {
    // For now, just delegate to regular format (ignoring options)
    // A full implementation would use the options for object inspection
    return ts_util_format(format, args);
}

// ============================================================================
// util.parseArgs(config?)
// Parses command-line arguments
// Returns: { values: object, positionals: string[] }
// ============================================================================
void* ts_util_parse_args(void* configPtr) {
    // Unbox the config if provided
    TsMap* config = nullptr;
    if (configPtr) {
        void* rawPtr = ts_value_get_object((TsValue*)configPtr);
        if (!rawPtr) rawPtr = configPtr;
        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16 == TsMap::MAGIC) {
            config = (TsMap*)rawPtr;
        }
    }

    // Create the result object: { values: {}, positionals: [] }
    TsMap* result = TsMap::Create();
    TsMap* values = TsMap::Create();
    TsArray* positionals = TsArray::Create();

    // Get options definition from config
    TsMap* optionsConfig = nullptr;
    bool strict = true;
    bool allowPositionals = false;
    TsArray* argsArray = nullptr;

    if (config) {
        // config.options
        TsValue optionsKey;
        optionsKey.type = ValueType::STRING_PTR;
        optionsKey.ptr_val = TsString::Create("options");
        TsValue optionsVal = config->Get(optionsKey);
        if (optionsVal.type == ValueType::OBJECT_PTR && optionsVal.ptr_val) {
            uint32_t magic = *(uint32_t*)((char*)optionsVal.ptr_val + 16);
            if (magic == TsMap::MAGIC) {
                optionsConfig = (TsMap*)optionsVal.ptr_val;
            }
        }

        // config.strict
        TsValue strictKey;
        strictKey.type = ValueType::STRING_PTR;
        strictKey.ptr_val = TsString::Create("strict");
        TsValue strictVal = config->Get(strictKey);
        if (strictVal.type == ValueType::BOOLEAN) {
            strict = strictVal.b_val;
        }

        // config.allowPositionals
        TsValue allowPosKey;
        allowPosKey.type = ValueType::STRING_PTR;
        allowPosKey.ptr_val = TsString::Create("allowPositionals");
        TsValue allowPosVal = config->Get(allowPosKey);
        if (allowPosVal.type == ValueType::BOOLEAN) {
            allowPositionals = allowPosVal.b_val;
        }

        // config.args (if provided, use instead of process.argv)
        TsValue argsKey;
        argsKey.type = ValueType::STRING_PTR;
        argsKey.ptr_val = TsString::Create("args");
        TsValue argsVal = config->Get(argsKey);
        if (argsVal.type == ValueType::ARRAY_PTR && argsVal.ptr_val) {
            argsArray = (TsArray*)argsVal.ptr_val;
        } else if (argsVal.type == ValueType::OBJECT_PTR && argsVal.ptr_val) {
            // Maybe it's boxed as an object? Check if it's actually an array
            uint32_t magic0 = *(uint32_t*)argsVal.ptr_val;
            if (magic0 == TsArray::MAGIC) {
                argsArray = (TsArray*)argsVal.ptr_val;
            }
        }
    }

    // Build a map of short aliases to long names
    std::unordered_map<std::string, std::string> shortAliases;
    std::unordered_map<std::string, std::string> optionTypes;
    std::unordered_map<std::string, bool> optionMultiple;

    // Helper to extract string from possibly double-boxed value
    auto extractString = [](TsValue* val) -> TsString* {
        if (!val || !val->ptr_val) return nullptr;
        if (val->type == ValueType::STRING_PTR) {
            return (TsString*)val->ptr_val;
        } else if (val->type == ValueType::OBJECT_PTR) {
            uint32_t magic = *(uint32_t*)val->ptr_val;
            if (magic == TsString::MAGIC) {
                return (TsString*)val->ptr_val;
            }
            // Check for double-boxed
            TsValue* inner = (TsValue*)val->ptr_val;
            if ((uint8_t)inner->type <= (uint8_t)ValueType::FUNCTION_PTR) {
                if (inner->type == ValueType::STRING_PTR && inner->ptr_val) {
                    return (TsString*)inner->ptr_val;
                }
            }
        }
        return nullptr;
    };

    if (optionsConfig) {
        TsArray* keys = (TsArray*)optionsConfig->GetKeys();
        if (keys) {
            for (int64_t i = 0; i < keys->Length(); i++) {
                TsValue* keyVal = keys->GetElementBoxed(i);
                TsString* keyStr = extractString(keyVal);
                if (!keyStr) continue;
                const char* longName = keyStr->ToUtf8();

                // Create a proper key for lookup
                TsValue lookupKey;
                lookupKey.type = ValueType::STRING_PTR;
                lookupKey.ptr_val = keyStr;
                TsValue optValue = optionsConfig->Get(lookupKey);
                if (optValue.type == ValueType::OBJECT_PTR && optValue.ptr_val) {
                    TsMap* optObj = nullptr;
                    uint32_t magic16 = *(uint32_t*)((char*)optValue.ptr_val + 16);
                    uint32_t magic0 = *(uint32_t*)optValue.ptr_val;
                    if (magic16 == TsMap::MAGIC) {
                        optObj = (TsMap*)optValue.ptr_val;
                    } else if (magic0 == TsMap::MAGIC) {
                        optObj = (TsMap*)optValue.ptr_val;
                    }
                    if (optObj) {
                        // Get type
                        TsValue typeKey;
                        typeKey.type = ValueType::STRING_PTR;
                        typeKey.ptr_val = TsString::Create("type");
                        TsValue typeVal = optObj->Get(typeKey);
                        TsString* typeStr = extractString(&typeVal);
                        if (typeStr) {
                            optionTypes[longName] = typeStr->ToUtf8();
                        }

                        // Get short alias
                        TsValue shortKey;
                        shortKey.type = ValueType::STRING_PTR;
                        shortKey.ptr_val = TsString::Create("short");
                        TsValue shortVal = optObj->Get(shortKey);
                        TsString* shortStr = extractString(&shortVal);
                        if (shortStr) {
                            shortAliases[shortStr->ToUtf8()] = longName;
                        }

                        // Get multiple flag
                        TsValue multiKey;
                        multiKey.type = ValueType::STRING_PTR;
                        multiKey.ptr_val = TsString::Create("multiple");
                        TsValue multiVal = optObj->Get(multiKey);
                        if (multiVal.type == ValueType::BOOLEAN) {
                            optionMultiple[longName] = multiVal.b_val;
                        }
                    }
                }
            }
        }
    }

    // Parse arguments
    auto args = std::vector<std::string>();

    // Get args from argsArray or process.argv
    if (argsArray) {
        for (int64_t i = 0; i < argsArray->Length(); i++) {
            TsValue* argVal = argsArray->GetElementBoxed(i);
            if (!argVal) continue;

            // Handle double-boxed values: argVal might be OBJECT_PTR pointing to another TsValue
            TsString* str = nullptr;
            if (argVal->type == ValueType::STRING_PTR && argVal->ptr_val) {
                str = (TsString*)argVal->ptr_val;
            } else if (argVal->type == ValueType::OBJECT_PTR && argVal->ptr_val) {
                // Check if ptr_val is a TsString
                uint32_t magic = *(uint32_t*)argVal->ptr_val;
                if (magic == TsString::MAGIC) {
                    str = (TsString*)argVal->ptr_val;
                } else {
                    // It might be another TsValue (double-boxed)
                    TsValue* inner = (TsValue*)argVal->ptr_val;
                    if ((uint8_t)inner->type <= (uint8_t)ValueType::FUNCTION_PTR) {
                        // It's a TsValue, use its value
                        if (inner->type == ValueType::STRING_PTR && inner->ptr_val) {
                            str = (TsString*)inner->ptr_val;
                        }
                    }
                }
            }
            if (str) {
                args.push_back(str->ToUtf8());
            }
        }
    } else {
        // Use process.argv, skip first 2 (executable, script)
        TsArray* processArgv = (TsArray*)ts_get_process_argv();
        if (processArgv) {
            int64_t len = processArgv->Length();
            for (int64_t i = 2; i < len; i++) {
                TsValue* argVal = processArgv->GetElementBoxed(i);
                if (argVal && argVal->type == ValueType::STRING_PTR && argVal->ptr_val) {
                    args.push_back(((TsString*)argVal->ptr_val)->ToUtf8());
                }
            }
        }
    }

    // Parse the arguments
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& arg = args[i];

        if (arg.length() >= 2 && arg[0] == '-') {
            std::string optName;
            std::string optValue;
            bool hasValue = false;

            if (arg[1] == '-') {
                // Long option: --foo or --foo=bar
                size_t eqPos = arg.find('=', 2);
                if (eqPos != std::string::npos) {
                    optName = arg.substr(2, eqPos - 2);
                    optValue = arg.substr(eqPos + 1);
                    hasValue = true;
                } else {
                    optName = arg.substr(2);
                }
            } else {
                // Short option: -f or -f bar
                std::string shortOpt = arg.substr(1, 1);
                auto it = shortAliases.find(shortOpt);
                if (it != shortAliases.end()) {
                    optName = it->second;
                } else {
                    optName = shortOpt;
                }

                // Check for value in same arg: -fvalue
                if (arg.length() > 2) {
                    optValue = arg.substr(2);
                    hasValue = true;
                }
            }

            // Determine if this option takes a value
            bool isString = false;
            auto typeIt = optionTypes.find(optName);
            if (typeIt != optionTypes.end()) {
                isString = (typeIt->second == "string");
            }

            // If string type and no value yet, get from next arg
            if (isString && !hasValue && i + 1 < args.size()) {
                optValue = args[++i];
                hasValue = true;
            }

            // Set the value
            TsValue nameKey;
            nameKey.type = ValueType::STRING_PTR;
            nameKey.ptr_val = TsString::Create(optName.c_str());

            TsValue val;
            if (isString) {
                val.type = ValueType::STRING_PTR;
                val.ptr_val = TsString::Create(hasValue ? optValue.c_str() : "");
            } else {
                val.type = ValueType::BOOLEAN;
                val.b_val = true;
            }

            values->Set(nameKey, val);
        } else {
            // Positional argument
            if (allowPositionals || !strict) {
                TsValue* posVal = (TsValue*)ts_alloc(sizeof(TsValue));
                posVal->type = ValueType::STRING_PTR;
                posVal->ptr_val = TsString::Create(arg.c_str());
                positionals->Push((int64_t)posVal);
            }
        }
    }

    // Set result.values and result.positionals
    TsValue valuesKey;
    valuesKey.type = ValueType::STRING_PTR;
    valuesKey.ptr_val = TsString::Create("values");
    TsValue valuesVal;
    valuesVal.type = ValueType::OBJECT_PTR;
    valuesVal.ptr_val = values;
    result->Set(valuesKey, valuesVal);

    TsValue posKey;
    posKey.type = ValueType::STRING_PTR;
    posKey.ptr_val = TsString::Create("positionals");
    TsValue posVal;
    posVal.type = ValueType::ARRAY_PTR;
    posVal.ptr_val = positionals;
    result->Set(posKey, posVal);

    return ts_value_make_object(result);
}

// ============================================================================
// util.inspect object and properties
// ============================================================================

// Global symbol for util.inspect.custom (created lazily)
static TsSymbol* inspectCustomSymbol = nullptr;

// Global default options object (created lazily)
static TsMap* inspectDefaultOptions = nullptr;

// Get the util.inspect.custom Symbol
// This is used by objects to define custom inspect behavior
void* ts_util_inspect_custom_symbol() {
    if (!inspectCustomSymbol) {
        // Create a well-known symbol with description "nodejs.util.inspect.custom"
        inspectCustomSymbol = TsSymbol::For(TsString::Create("nodejs.util.inspect.custom"));
        ts_gc_register_root((void**)&inspectCustomSymbol);
    }
    // Return as a boxed symbol value so typeof works correctly
    return ts_value_make_symbol(inspectCustomSymbol);
}

// Get util.inspect.defaultOptions object
// Returns a mutable object with default inspect settings
void* ts_util_inspect_default_options() {
    if (!inspectDefaultOptions) {
        inspectDefaultOptions = TsMap::Create();
        ts_gc_register_root((void**)&inspectDefaultOptions);

        // Set default values matching Node.js defaults
        TsValue key, val;

        // showHidden: false
        key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::Create("showHidden");
        val.type = ValueType::BOOLEAN;
        val.b_val = false;
        inspectDefaultOptions->Set(key, val);

        // depth: 2
        key.ptr_val = TsString::Create("depth");
        val.type = ValueType::NUMBER_INT;
        val.i_val = 2;
        inspectDefaultOptions->Set(key, val);

        // colors: false
        key.ptr_val = TsString::Create("colors");
        val.type = ValueType::BOOLEAN;
        val.b_val = false;
        inspectDefaultOptions->Set(key, val);

        // customInspect: true
        key.ptr_val = TsString::Create("customInspect");
        val.type = ValueType::BOOLEAN;
        val.b_val = true;
        inspectDefaultOptions->Set(key, val);

        // showProxy: false
        key.ptr_val = TsString::Create("showProxy");
        val.type = ValueType::BOOLEAN;
        val.b_val = false;
        inspectDefaultOptions->Set(key, val);

        // maxArrayLength: 100
        key.ptr_val = TsString::Create("maxArrayLength");
        val.type = ValueType::NUMBER_INT;
        val.i_val = 100;
        inspectDefaultOptions->Set(key, val);

        // maxStringLength: 10000
        key.ptr_val = TsString::Create("maxStringLength");
        val.type = ValueType::NUMBER_INT;
        val.i_val = 10000;
        inspectDefaultOptions->Set(key, val);

        // breakLength: 80
        key.ptr_val = TsString::Create("breakLength");
        val.type = ValueType::NUMBER_INT;
        val.i_val = 80;
        inspectDefaultOptions->Set(key, val);

        // compact: 3
        key.ptr_val = TsString::Create("compact");
        val.type = ValueType::NUMBER_INT;
        val.i_val = 3;
        inspectDefaultOptions->Set(key, val);

        // sorted: false
        key.ptr_val = TsString::Create("sorted");
        val.type = ValueType::BOOLEAN;
        val.b_val = false;
        inspectDefaultOptions->Set(key, val);

        // getters: false
        key.ptr_val = TsString::Create("getters");
        val.type = ValueType::BOOLEAN;
        val.b_val = false;
        inspectDefaultOptions->Set(key, val);
    }

    return ts_value_make_object(inspectDefaultOptions);
}

// Get the util.inspect object (callable with properties)
// This is returned when accessing util.inspect as a property
void* ts_util_get_inspect() {
    // Create a callable object that has both the inspect function
    // and the custom/defaultOptions properties
    // For now, we return a map with properties; calling it would need special handling
    TsMap* inspectObj = TsMap::Create();

    TsValue key, val;

    // custom: Symbol
    key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::Create("custom");
    val.type = ValueType::SYMBOL_PTR;
    val.ptr_val = ts_util_inspect_custom_symbol();
    inspectObj->Set(key, val);

    // defaultOptions: object
    key.ptr_val = TsString::Create("defaultOptions");
    val.type = ValueType::OBJECT_PTR;
    val.ptr_val = inspectDefaultOptions ? inspectDefaultOptions : (TsMap*)ts_util_inspect_default_options();
    inspectObj->Set(key, val);

    return ts_value_make_object(inspectObj);
}

// ============================================================================
// util.parseEnv(content) - Parse dotenv file content to object
// Parses key=value pairs, handles comments (#), quoted values, multi-line
// ============================================================================
void* ts_util_parse_env(void* contentArg) {
    // Unbox if needed
    void* rawContent = ts_value_get_string((TsValue*)contentArg);
    if (!rawContent) rawContent = contentArg;
    TsString* content = (TsString*)rawContent;

    TsMap* result = TsMap::Create();

    if (!content) {
        return ts_value_make_object(result);
    }

    const char* src = content->ToUtf8();
    if (!src) {
        return ts_value_make_object(result);
    }

    size_t len = strlen(src);
    size_t i = 0;

    while (i < len) {
        // Skip leading whitespace
        while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;

        // Skip empty lines
        if (i < len && (src[i] == '\n' || src[i] == '\r')) {
            i++;
            continue;
        }

        // Skip comment lines (starting with #)
        if (i < len && src[i] == '#') {
            while (i < len && src[i] != '\n') i++;
            if (i < len) i++; // Skip the newline
            continue;
        }

        // Check for 'export ' prefix (optional)
        if (i + 7 <= len && strncmp(src + i, "export ", 7) == 0) {
            i += 7;
            // Skip whitespace after export
            while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;
        }

        // Parse the key (identifier: letters, digits, underscores)
        size_t keyStart = i;
        while (i < len && (isalnum((unsigned char)src[i]) || src[i] == '_')) i++;

        if (i == keyStart) {
            // No key found, skip to next line
            while (i < len && src[i] != '\n') i++;
            if (i < len) i++;
            continue;
        }

        std::string key(src + keyStart, i - keyStart);

        // Skip whitespace before =
        while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;

        // Expect =
        if (i >= len || src[i] != '=') {
            // No =, skip to next line
            while (i < len && src[i] != '\n') i++;
            if (i < len) i++;
            continue;
        }
        i++; // Skip =

        // Skip whitespace after =
        while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;

        // Parse the value
        std::string value;

        if (i < len && (src[i] == '"' || src[i] == '\'')) {
            // Quoted value
            char quote = src[i];
            i++; // Skip opening quote

            while (i < len && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < len) {
                    // Handle escape sequences
                    i++;
                    switch (src[i]) {
                        case 'n': value += '\n'; break;
                        case 'r': value += '\r'; break;
                        case 't': value += '\t'; break;
                        case '\\': value += '\\'; break;
                        case '"': value += '"'; break;
                        case '\'': value += '\''; break;
                        default: value += src[i]; break;
                    }
                } else if (src[i] == '\n' && quote == '"') {
                    // Multi-line in double quotes
                    value += '\n';
                } else {
                    value += src[i];
                }
                i++;
            }

            if (i < len && src[i] == quote) {
                i++; // Skip closing quote
            }
        } else {
            // Unquoted value - read until end of line or comment
            while (i < len && src[i] != '\n' && src[i] != '\r' && src[i] != '#') {
                value += src[i];
                i++;
            }

            // Trim trailing whitespace from unquoted values
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.pop_back();
            }
        }

        // Skip to end of line (past any inline comments)
        while (i < len && src[i] != '\n') i++;
        if (i < len) i++; // Skip newline

        // Add to result map
        TsValue keyVal;
        keyVal.type = ValueType::STRING_PTR;
        keyVal.ptr_val = TsString::Create(key.c_str());

        TsValue valueVal;
        valueVal.type = ValueType::STRING_PTR;
        valueVal.ptr_val = TsString::Create(value.c_str());

        result->Set(keyVal, valueVal);
    }

    return ts_value_make_object(result);
}

// ============================================================================
// Additional util.types functions for 100% coverage
// ============================================================================

// util.types.isCryptoKey - checks if value is a CryptoKey
// Note: We don't have a native CryptoKey implementation, always returns false
bool ts_util_types_is_crypto_key(void* value) {
    // CryptoKey is part of the Web Crypto API, not implemented in our runtime
    return false;
}

// util.types.isExternal - checks if value is an External value (V8-specific)
// Note: This is a V8-specific concept, not applicable in AOT compilation
bool ts_util_types_is_external(void* value) {
    // External values are V8-specific, not applicable in AOT
    return false;
}

// util.types.isKeyObject - checks if value is a KeyObject (crypto keys)
// Note: We don't have a native KeyObject implementation, always returns false
bool ts_util_types_is_key_object(void* value) {
    // KeyObject is part of Node.js crypto module for key storage
    // Not yet implemented in our runtime
    return false;
}

// util.types.isMapIterator - checks if value is a Map Iterator
// Note: Our Map.entries/keys/values return arrays, not true iterators
bool ts_util_types_is_map_iterator(void* value) {
    // We return arrays from Map iteration methods, not true iterators
    // A true Map iterator would have a specific type/magic
    return false;
}

// util.types.isModuleNamespaceObject - checks if value is a module namespace
// Note: AOT compilation doesn't preserve module namespace objects
bool ts_util_types_is_module_namespace_object(void* value) {
    // Module namespace objects are not preserved in AOT compilation
    return false;
}

// util.types.isSetIterator - checks if value is a Set Iterator
// Note: Our Set.entries/keys/values return arrays, not true iterators
bool ts_util_types_is_set_iterator(void* value) {
    // We return arrays from Set iteration methods, not true iterators
    return false;
}

// util.types.isSharedArrayBuffer - checks if value is a SharedArrayBuffer
// Note: SharedArrayBuffer requires shared memory support, not implemented
bool ts_util_types_is_shared_array_buffer(void* value) {
    // SharedArrayBuffer is not implemented (requires threading support)
    return false;
}


// ============================================================================
// util.transferableAbortController and util.transferableAbortSignal
// These are used for transferring AbortController/Signal across workers
// Since we don't have worker_threads, these return the input unchanged
// ============================================================================

void* ts_util_transferable_abort_controller() {
    // Without worker_threads support, we create a regular AbortController
    // For now, return a simple object that mimics AbortController interface
    TsMap* controller = TsMap::Create();

    // Create a signal object
    TsMap* signal = TsMap::Create();
    TsValue abortedKey, abortedVal;
    abortedKey.type = ValueType::STRING_PTR;
    abortedKey.ptr_val = TsString::Create("aborted");
    abortedVal.type = ValueType::BOOLEAN;
    abortedVal.b_val = false;
    signal->Set(abortedKey, abortedVal);

    TsValue reasonKey, reasonVal;
    reasonKey.type = ValueType::STRING_PTR;
    reasonKey.ptr_val = TsString::Create("reason");
    reasonVal.type = ValueType::UNDEFINED;
    signal->Set(reasonKey, reasonVal);

    // Set signal on controller
    TsValue signalKey, signalVal;
    signalKey.type = ValueType::STRING_PTR;
    signalKey.ptr_val = TsString::Create("signal");
    signalVal.type = ValueType::OBJECT_PTR;
    signalVal.ptr_val = signal;
    controller->Set(signalKey, signalVal);

    return ts_value_make_object(controller);
}

void* ts_util_transferable_abort_signal(void* signal) {
    // Without worker_threads, just return the signal as-is
    // The signal is already "transferable" in a single-threaded context
    if (!signal) return nullptr;
    return signal;
}

} // extern "C"

// Namespace implementations (outside extern "C" for C++ linkage)
namespace TsUtilTypes {
    bool isCryptoKey(void* value) { return false; }
    bool isExternal(void* value) { return false; }
    bool isKeyObject(void* value) { return false; }
    bool isMapIterator(void* value) { return false; }
    bool isModuleNamespaceObject(void* value) { return false; }
    bool isSetIterator(void* value) { return false; }
    bool isSharedArrayBuffer(void* value) { return false; }
}
