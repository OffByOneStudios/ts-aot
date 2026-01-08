#define FMT_UNICODE 0
#define SPDLOG_COMPILED_LIB 1
#include "TsObject.h"
#include "TsArray.h"
#include "TsBigInt.h"
#include "TsMap.h"
#include "TsJSON.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsEventEmitter.h"
#include "TsHttp.h"
#include "TsRegExp.h"
#include "GC.h"
#include "TsRuntime.h"
#include "MemoryTracker.h"
#include <new>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

extern "C" {
    // Prototypes for functions in Primitives.cpp
    double ts_value_get_double(TsValue* v);
    int64_t ts_value_get_int(TsValue* v);
    bool ts_value_to_bool(TsValue* v);
    
    // Forward declaration for TsArray.cpp
    TsValue* ts_array_get_as_value(void* arr, int64_t index);
}

static std::unordered_map<std::string, TsValue*> g_module_cache;

// Debug hook: captures the TsMap* backing lodash's synthetic module object.
// Used by TsMap.cpp to trace writes to module.exports.
extern "C" void* g_debug_lodash_module_map = nullptr;

// CRITICAL FIX: Property access return value pool
// Allocating a new TsValue* for every property read causes massive memory leak
// Use thread-local pool of 8 slots to avoid allocation while supporting nested access
thread_local static TsValue g_property_return_pool[8];
thread_local static int g_property_return_index = 0;

static inline TsValue* ts_property_return_value(const TsValue& val) {
    TsValue* slot = &g_property_return_pool[g_property_return_index];
    g_property_return_index = (g_property_return_index + 1) % 8;
    *slot = val;
    return slot;
}

// Internal helpers (no C linkage needed).
static std::string finalize_module_path(const fs::path& base) {
    fs::path candidate = base;
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
        return fs::absolute(candidate).string();
    }

    std::string withJs = candidate.string() + ".js";
    if (fs::exists(withJs)) {
        return fs::absolute(withJs).string();
    }

    std::string withTs = candidate.string() + ".ts";
    if (fs::exists(withTs)) {
        return fs::absolute(withTs).string();
    }

    if (fs::exists(candidate) && fs::is_directory(candidate)) {
        fs::path idxJs = candidate / "index.js";
        if (fs::exists(idxJs)) {
            return fs::absolute(idxJs).string();
        }
        fs::path idxTs = candidate / "index.ts";
        if (fs::exists(idxTs)) {
            return fs::absolute(idxTs).string();
        }
    }

    return "";
}

// Extremely small package.json parser to extract "main".
static std::string read_package_main(const fs::path& packageJsonPath) {
    std::ifstream in(packageJsonPath);
    if (!in) return "";
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string key = "\"main\"";
    size_t pos = content.find(key);
    if (pos == std::string::npos) return "";
    pos = content.find(':', pos + key.size());
    if (pos == std::string::npos) return "";
    pos = content.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = content.find('"', pos + 1);
    if (end == std::string::npos || end <= pos + 1) return "";
    return content.substr(pos + 1, end - pos - 1);
}

static std::string resolve_node_module(const std::string& spec, const std::string& referrerPath) {
    fs::path referrer = fs::absolute(referrerPath);
    fs::path dir = referrer.has_filename() ? referrer.parent_path() : referrer;

    while (true) {
        fs::path candidateBase = dir / "node_modules" / spec;
        if (fs::exists(candidateBase)) {
            if (fs::is_directory(candidateBase)) {
                fs::path packageJson = candidateBase / "package.json";
                if (fs::exists(packageJson)) {
                    std::string mainEntry = read_package_main(packageJson);
                    if (!mainEntry.empty()) {
                        std::string resolved = finalize_module_path(candidateBase / mainEntry);
                        if (!resolved.empty()) return resolved;
                    }
                }

                // Fallbacks if package.json is missing or lacks "main".
                std::string resolved = finalize_module_path(candidateBase / "index");
                if (!resolved.empty()) return resolved;
                resolved = finalize_module_path(candidateBase);
                if (!resolved.empty()) return resolved;
            } else {
                std::string resolved = finalize_module_path(candidateBase);
                if (!resolved.empty()) return resolved;
            }
        }

        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }

    return "";
}

// Currently empty, as TsObject.h only defines structs/enums for now.

extern "C" {

TsValue* ts_value_make_undefined() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::UNDEFINED;
    v->ptr_val = nullptr;
    return v;
}

TsValue* ts_value_make_null() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    v->type = ValueType::OBJECT_PTR; // Use OBJECT_PTR with nullptr for null
    v->ptr_val = nullptr;
    return v;
}

TsValue* ts_value_make_int(int64_t i) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::NUMBER_INT;
        v->i_val = i;
        return v;
    }

    TsValue* ts_value_make_double(double d) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::NUMBER_DBL;
        v->d_val = d;
        return v;
    }

    TsValue* ts_value_make_bool(bool b) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::BOOLEAN;
        v->b_val = b;
        return v;
    }

    TsValue* ts_value_make_string(void* s) {
        // printf("ts_value_make_string(%p)\n", s);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::STRING_PTR;
        v->ptr_val = s;
        return v;
    }

    TsValue* ts_value_make_object(void* o) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::OBJECT_PTR;
        v->ptr_val = o;
        return v;
    }

    TsValue* ts_value_make_function_object(void* fnObj) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::FUNCTION_PTR;
        v->ptr_val = fnObj;
        return v;
    }

    TsValue* ts_value_make_promise(void* promise) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        val->type = ValueType::PROMISE_PTR;
        val->ptr_val = promise;
        return val;
    }

    TsValue* ts_value_make_array(void* arr) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        val->type = ValueType::ARRAY_PTR;
        val->ptr_val = arr;
        return val;
    }

    bool ts_value_is_undefined(TsValue* v) {
        if (!v) {
            return true;
        }
        return v->type == ValueType::UNDEFINED;
    }

    // Box any pointer by detecting its runtime type
    // This is used when the compile-time type is 'any' but we need proper boxing
    TsValue* ts_value_box_any(void* ptr) {
        if (!ptr) {
            return ts_value_make_undefined();
        }

        // Guard against obviously-invalid pointers (common when 'any' accidentally contains
        // a non-pointer bit-pattern). Avoid dereferencing very low addresses.
        uintptr_t raw = (uintptr_t)ptr;
        if (raw < 0x10000) {
            return ts_value_make_undefined();
        }

#ifdef _MSC_VER
        __try {
#endif

        // Check magic numbers to detect type
        uint32_t magic = *(uint32_t*)ptr;
        uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        uint32_t magic24 = *(uint32_t*)((char*)ptr + 24);
        
        if (magic == 0x41525259 || magic8 == 0x41525259 || magic16 == 0x41525259) { // TsArray::MAGIC "ARRY"
            return ts_value_make_array(ptr);
        }
        if (magic == 0x53545247) { // TsString::MAGIC "STRG"
            return ts_value_make_string((TsString*)ptr);
        }
        if (magic == 0x4D415053 || magic8 == 0x4D415053 || magic16 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC "MAPS"  
            return ts_value_make_object(ptr);
        }
        if (magic == 0x53455453 || magic8 == 0x53455453 || magic16 == 0x53455453 || magic24 == 0x53455453) { // TsSet::MAGIC "SETS"
            return ts_value_make_object(ptr);
        }
        if (magic == 0x46554E43 || magic8 == 0x46554E43 || magic16 == 0x46554E43 || magic24 == 0x46554E43) { // TsFunction::MAGIC "FUNC"
            return ts_value_make_function_object(ptr);
        }
        
        if (magic == 0x42554646 || magic8 == 0x42554646 || magic16 == 0x42554646) { // TsBuffer::MAGIC "BUFF"
            return ts_value_make_object(ptr);
        }
        
        // Check if it's already a TsValue* (types 0-10)
        // Do this AFTER TsObject magic checks to avoid misclassifying TsFunction*/TsMap*
        // (their vtable pointers can have a low byte <= 10).
        uint8_t firstByte = *(uint8_t*)ptr;
        if (firstByte <= 10) {
            return (TsValue*)ptr;
        }

        // Default: treat as generic object
        return ts_value_make_object(ptr);

#ifdef _MSC_VER
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return ts_value_make_undefined();
        }
#endif
    }

    TsValue* ts_value_make_function(void* funcPtr, void* context) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::FUNCTION_PTR;
        v->ptr_val = func;
        return v;
    }

    TsValue* ts_value_make_native_function(void* funcPtr, void* context) {
        void* mem = ts_alloc(sizeof(TsFunction));
        TsFunction* func = new (mem) TsFunction(funcPtr, context, FunctionType::NATIVE);
        // Explicitly set magic using member access instead of offset calculation
        func->magic = TsFunction::MAGIC;
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        v->type = ValueType::FUNCTION_PTR;
        v->ptr_val = func;
        return v;
    }

    void* ts_function_get_ptr(TsValue* val) {
        if (!val) return nullptr;
        // Accept both FUNCTION_PTR and OBJECT_PTR for backwards compatibility
        if (val->type != ValueType::FUNCTION_PTR && val->type != ValueType::OBJECT_PTR) return nullptr;
        TsFunction* func = (TsFunction*)val->ptr_val;
        return func->funcPtr;
    }

    void* ts_value_get_function(TsValue* val) {
        if (!val) return nullptr;
        // Accept both FUNCTION_PTR and OBJECT_PTR for backwards compatibility
        if (val->type != ValueType::FUNCTION_PTR && val->type != ValueType::OBJECT_PTR) return nullptr;
        TsFunction* func = (TsFunction*)val->ptr_val;
        return func->funcPtr;
    }

    void* ts_value_get_context(TsValue* val) {
        if (!val) return nullptr;
        // Accept both FUNCTION_PTR and OBJECT_PTR for backwards compatibility
        if (val->type != ValueType::FUNCTION_PTR && val->type != ValueType::OBJECT_PTR) return nullptr;
        TsFunction* func = (TsFunction*)val->ptr_val;
        return func->context;
    }

    bool ts_value_get_bool(TsValue* v) {
        if (!v) return false;
        if (v->type == ValueType::BOOLEAN) return v->b_val;
        return ts_value_to_bool(v);
    }

    void* ts_value_get_string(TsValue* v) {
        if (!v) return nullptr;

        // Check type field first - if valid TsValue type, use it directly
        uint8_t typeField = *(uint8_t*)v;
        if (typeField <= 10) {
            // It's a proper TsValue
            if (v->type == ValueType::STRING_PTR) return v->ptr_val;
            // Not a string - try to convert
            return ts_string_from_value(v);
        }

        // Type > 10 means this might be a raw TsString* pointer
        // Check for TsString magic (no exception handler needed - if it crashes, it's a bug)
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247) { // TsString::MAGIC
            return v;
        }

        // Unknown pointer type - cannot extract string
        return nullptr;
    }

    void* ts_value_get_object(TsValue* v) {
        if (!v) {
            return nullptr;
        }

        // FIRST: Check if this is a TsValue by examining the type field
        // TsValue has ValueType enum at offset 0, which is <= 10
        uint8_t typeField = *(uint8_t*)v;
        if (typeField <= 10) {
            // It's a proper boxed TsValue - extract ptr_val
            if (v->type == ValueType::OBJECT_PTR || 
                v->type == ValueType::ARRAY_PTR || 
                v->type == ValueType::PROMISE_PTR ||
                v->type == ValueType::FUNCTION_PTR) {
                return v->ptr_val;
            }
            return nullptr;
        }

        // Not a boxed TsValue - check for TsObject-derived classes with vtable
        // Layout: [vtable ptr (8)] [explicit vtable (8)] [TsObject::magic (4)] ...
        // So TsObject::magic is at offset 16
        uint32_t magic16 = *(uint32_t*)((char*)v + 16);
        if (magic16 == 0x4D415053) { // TsMap::MAGIC
            return v;
        }
        if (magic16 == 0x53455453) { // TsSet::MAGIC
            return v;
        }
        if (magic16 == 0x41525259) { // TsArray::MAGIC
            return v;
        }
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC
            return v;
        }
        if (magic16 == 0x42554646) { // TsBuffer::MAGIC
            return v;
        }

        // Type > 10 means this might be a raw pointer without vtable
        // Check magic at offset 0 for structs without virtual methods
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x41525259) { // TsArray::MAGIC
            return v;
        }
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return v;
        }
        if (magic == 0x53455453) { // TsSet::MAGIC
            return v;
        }
        if (magic == 0x53545247) { // TsString::MAGIC - not an object
            return nullptr;
        }

        // Check offset 8 for objects with different layout
        uint32_t magic8 = *(uint32_t*)((char*)v + 8);
        if (magic8 == 0x4D415053 || magic8 == 0x42554646 || 
            magic8 == 0x53455453 || magic8 == 0x46554E43) {
            return v;
        }

        // Unknown - assume it's an object pointer
        return v;
    }

    // Strict equality comparison for boxed values (implements === semantics)
    bool ts_value_strict_eq_bool(TsValue* lhs, TsValue* rhs) {
        // Treat nullptr as undefined
        TsValue undef;
        undef.type = ValueType::UNDEFINED;
        undef.ptr_val = nullptr;

        if (!lhs) lhs = &undef;
        if (!rhs) rhs = &undef;
        
        // Get the actual TsValue structs (handle raw pointers)
        TsValue lhsVal = {};
        TsValue rhsVal = {};
        
        // Check if lhs is a raw pointer or a boxed TsValue
        uint8_t lhsType = *(uint8_t*)lhs;
        if (lhsType <= 10) {
            lhsVal = *lhs;
        } else {
            // It's a raw pointer - box it to determine type
            uint32_t magic = *(uint32_t*)lhs;
            if (magic == 0x41525259) { // TsArray::MAGIC
                lhsVal.type = ValueType::ARRAY_PTR;
                lhsVal.ptr_val = lhs;
            } else if (magic == 0x53545247) { // TsString::MAGIC
                lhsVal.type = ValueType::STRING_PTR;
                lhsVal.ptr_val = lhs;
            } else {
                lhsVal.type = ValueType::OBJECT_PTR;
                lhsVal.ptr_val = lhs;
            }
        }
        
        // Check if rhs is a raw pointer or a boxed TsValue
        uint8_t rhsType = *(uint8_t*)rhs;
        if (rhsType <= 10) {
            rhsVal = *rhs;
        } else {
            // It's a raw pointer - box it to determine type
            uint32_t magic = *(uint32_t*)rhs;
            if (magic == 0x41525259) { // TsArray::MAGIC
                rhsVal.type = ValueType::ARRAY_PTR;
                rhsVal.ptr_val = rhs;
            } else if (magic == 0x53545247) { // TsString::MAGIC
                rhsVal.type = ValueType::STRING_PTR;
                rhsVal.ptr_val = rhs;
            } else {
                rhsVal.type = ValueType::OBJECT_PTR;
                rhsVal.ptr_val = rhs;
            }
        }
        
        // Types must match for strict equality, EXCEPT for:
        // 1. FUNCTION_PTR vs OBJECT_PTR which can both represent the same underlying function
        // 2. NUMBER_INT vs NUMBER_DBL which JavaScript treats as the same type for strict equality
        bool bothFunctionLike = (lhsVal.type == ValueType::FUNCTION_PTR || lhsVal.type == ValueType::OBJECT_PTR) &&
                                (rhsVal.type == ValueType::FUNCTION_PTR || rhsVal.type == ValueType::OBJECT_PTR);
        bool bothNumeric = (lhsVal.type == ValueType::NUMBER_INT || lhsVal.type == ValueType::NUMBER_DBL) &&
                           (rhsVal.type == ValueType::NUMBER_INT || rhsVal.type == ValueType::NUMBER_DBL);
        if (lhsVal.type != rhsVal.type && !bothFunctionLike && !bothNumeric) return false;
        
        // For mixed numeric types, compare as doubles
        if (bothNumeric && lhsVal.type != rhsVal.type) {
            double lhsD = (lhsVal.type == ValueType::NUMBER_DBL) ? lhsVal.d_val : (double)lhsVal.i_val;
            double rhsD = (rhsVal.type == ValueType::NUMBER_DBL) ? rhsVal.d_val : (double)rhsVal.i_val;
            if (std::isnan(lhsD) && std::isnan(rhsD)) return false; // NaN !== NaN
            return lhsD == rhsD;
        }
        
        // For function-like types with mismatched wrapper types, compare underlying pointers
        if (lhsVal.type != rhsVal.type && bothFunctionLike) {
            // Check if both point to the same underlying object/function
            void* lhsPtr = lhsVal.ptr_val;
            void* rhsPtr = rhsVal.ptr_val;
            
            // Handle double-boxing: OBJECT_PTR might point to a TsValue with FUNCTION_PTR type
            if (lhsVal.type == ValueType::OBJECT_PTR && lhsPtr) {
                // Check if ptr_val is itself a TsValue (check type field <= 10)
                uint8_t innerType = *(uint8_t*)lhsPtr;
                if (innerType <= 10) {
                    TsValue* innerVal = (TsValue*)lhsPtr;
                    lhsPtr = innerVal->ptr_val;
                }
            }
            if (rhsVal.type == ValueType::OBJECT_PTR && rhsPtr) {
                uint8_t innerType = *(uint8_t*)rhsPtr;
                if (innerType <= 10) {
                    TsValue* innerVal = (TsValue*)rhsPtr;
                    rhsPtr = innerVal->ptr_val;
                }
            }
            
            // Now check if both are TsFunctions and compare funcPtr
            if (lhsPtr && rhsPtr) {
                uint32_t lhsMagic = *(uint32_t*)lhsPtr;
                uint32_t rhsMagic = *(uint32_t*)rhsPtr;
                if (lhsMagic == 0x46554E43 && rhsMagic == 0x46554E43) {
                    TsFunction* lhsFunc = (TsFunction*)lhsPtr;
                    TsFunction* rhsFunc = (TsFunction*)rhsPtr;
                    return lhsFunc->funcPtr == rhsFunc->funcPtr;
                }
            }
            
            // Fall through to pointer comparison
            return lhsPtr == rhsPtr;
        }
        
        switch (lhsVal.type) {
            case ValueType::UNDEFINED: return true;
            case ValueType::NUMBER_INT: return lhsVal.i_val == rhsVal.i_val;
            case ValueType::NUMBER_DBL: {
                if (std::isnan(lhsVal.d_val) && std::isnan(rhsVal.d_val)) return false; // NaN !== NaN
                return lhsVal.d_val == rhsVal.d_val;
            }
            case ValueType::BOOLEAN: return lhsVal.b_val == rhsVal.b_val;
            case ValueType::STRING_PTR: {
                TsString* s1 = (TsString*)lhsVal.ptr_val;
                TsString* s2 = (TsString*)rhsVal.ptr_val;
                if (!s1 && !s2) return true;
                if (!s1 || !s2) return false;
                return std::strcmp(s1->ToUtf8(), s2->ToUtf8()) == 0;
            }
            case ValueType::BIGINT_PTR: {
                TsBigInt* b1 = (TsBigInt*)lhsVal.ptr_val;
                TsBigInt* b2 = (TsBigInt*)rhsVal.ptr_val;
                if (!b1 && !b2) return true;
                if (!b1 || !b2) return false;
                return std::strcmp(b1->ToString(), b2->ToString()) == 0;
            }
            // For objects/arrays/etc, strict equality compares identity (same pointer)
            default:
                return lhsVal.ptr_val == rhsVal.ptr_val;
        }
    }

    TsValue* ts_value_strict_eq(TsValue* lhs, TsValue* rhs) {
        return ts_value_make_bool(ts_value_strict_eq_bool(lhs, rhs));
    }

    // Note: ts_value_get_int and ts_value_get_double are defined in Primitives.cpp

    int64_t ts_value_length(TsValue* val) {
        if (!val) return 0;

        void* rawPtr = val;
        
        // Check if it's a boxed TsValue
        uint8_t type = *(uint8_t*)val;
        if (type <= 10) {
            if (type == (uint8_t)ValueType::STRING_PTR) return ((TsString*)val->ptr_val)->Length();
            if (type == (uint8_t)ValueType::ARRAY_PTR) return ((TsArray*)val->ptr_val)->Length();
            if (type == (uint8_t)ValueType::OBJECT_PTR) {
                rawPtr = val->ptr_val;
                if (!rawPtr) return 0;
            } else {
                return 0;
            }
        }

        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x41525259) return ((TsArray*)rawPtr)->Length();
        if (magic0 == 0x53545247) return ((TsString*)rawPtr)->Length();

        uint32_t magic8 = *(uint32_t*)((char*)rawPtr + 8);
        if (magic8 == 0x42554646) {
            return ((TsBuffer*)rawPtr)->GetLength();
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16 == 0x42554646) {
            return ((TsBuffer*)rawPtr)->GetLength();
        }

        return 0;
    }

    void* ts_value_get_element(void* param, int64_t index) {
        if (!param) return ts_value_make_undefined();

        TsValue* val = (TsValue*)param;
        void* rawPtr = param;
        
        // Unbox if it's a TsValue*
        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::ARRAY_PTR || val->type == ValueType::OBJECT_PTR) {
                rawPtr = val->ptr_val;
            } else if (val->type == ValueType::STRING_PTR) {
                TsString* s = (TsString*)val->ptr_val;
                return s->Substring(index, index + 1);
            } else if (val->type == ValueType::NUMBER_INT || val->type == ValueType::NUMBER_DBL || val->type == ValueType::BOOLEAN || val->type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
        }

        if (!rawPtr) return ts_value_make_undefined();

        // Check magics
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x41525259) { // TsArray::MAGIC
            // Use ts_array_get_as_value to properly handle specialized arrays
            // This boxes integers/doubles from specialized arrays into TsValue*
            return ts_array_get_as_value(rawPtr, index);
        }

        uint32_t magic8 = *(uint32_t*)((char*)rawPtr + 8);
        if (magic8 == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)rawPtr;
            if (index < 0 || (size_t)index >= buf->GetLength()) {
                return ts_value_make_undefined();
            }
            return ts_value_make_int(buf->GetData()[index]);
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16 == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)rawPtr;
            if (index < 0 || (size_t)index >= buf->GetLength()) {
                return ts_value_make_undefined();
            }
            return ts_value_make_int(buf->GetData()[index]);
        }

        return ts_value_make_undefined();
    }

    // Forward declarations for prototype methods
    static TsValue* ts_function_toString_native(void* ctx, int argc, TsValue** argv);
    static TsValue* ts_object_hasOwnProperty_native(void* ctx, int argc, TsValue** argv);
    static TsValue* ts_object_toString_native(void* ctx, int argc, TsValue** argv);
    static TsValue* ts_object_valueOf_native(void* ctx, int argc, TsValue** argv);

    static TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv);
    static TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv);

    TsValue* ts_object_get_property(void* obj, const char* keyStr) {
        if (!obj) {
            return ts_value_make_undefined();
        }

        if (!keyStr) {
            return ts_value_make_undefined();
        }
        
        // Try to detect TsValue* at the start
        // TsValue has type enum (0-10) at offset 0, then padding, then value at offset 8
        // Real object pointers have vtable at offset 0 which is a large address
        // Check if first byte is a valid ValueType and if the "ptr_val" (at offset 8) looks reasonable
        uint8_t firstByte = *(uint8_t*)obj;
        if (firstByte <= 10) {  // Could be a valid ValueType
            TsValue* maybeVal = (TsValue*)obj;
            // Additional check: for UNDEFINED, ptr_val should be null/zero
            if (maybeVal->type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
            // For object types, unwrap if ptr_val is valid (non-zero)
            if ((maybeVal->type == ValueType::OBJECT_PTR || maybeVal->type == ValueType::ARRAY_PTR || 
                 maybeVal->type == ValueType::PROMISE_PTR) && maybeVal->ptr_val) {
                obj = maybeVal->ptr_val;
            }
        }
        
        // IMPORTANT: Check magic FIRST before any dynamic_cast!
        // Many runtime types (TsRegExp, TsMap, TsArray) don't inherit from TsObject,
        // so dynamic_cast on them would cause undefined behavior/crashes.
        uint32_t magic0 = *(uint32_t*)obj;
        uint32_t magic8 = *(uint32_t*)((char*)obj + 8);
        uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)obj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)obj + 24);

        // Check for TsRegExp (magic at offset 0) - handle BEFORE dynamic_cast!
        if (magic0 == 0x52454758) { // TsRegExp::MAGIC ("REGX")
            TsRegExp* re = (TsRegExp*)obj;
            if (strcmp(keyStr, "source") == 0) {
                return ts_value_make_string(re->GetSource());
            }
            if (strcmp(keyStr, "flags") == 0) {
                return ts_value_make_string(re->GetFlags());
            }
            if (strcmp(keyStr, "global") == 0) {
                return ts_value_make_bool(re->IsGlobal());
            }
            if (strcmp(keyStr, "ignoreCase") == 0) {
                return ts_value_make_bool(re->IsIgnoreCase());
            }
            if (strcmp(keyStr, "multiline") == 0) {
                return ts_value_make_bool(re->IsMultiline());
            }
            if (strcmp(keyStr, "sticky") == 0) {
                return ts_value_make_bool(re->IsSticky());
            }
            if (strcmp(keyStr, "lastIndex") == 0) {
                return ts_value_make_int(re->GetLastIndex());
            }
            return ts_value_make_undefined();
        }

        // Check for TsMap (magic at offset 16 after vtables) - also try offset 20 and 24
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsMap* map = (TsMap*)obj;
            TsValue k;
            k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(keyStr);
            TsValue val = map->Get(k);
            if (val.type != ValueType::UNDEFINED) {
                return ts_property_return_value(val);
            }
            
            // If not found in the map, check Object.prototype methods
            // This provides prototype chain behavior for plain objects
            if (strcmp(keyStr, "hasOwnProperty") == 0) {
                return ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr);
            }
            if (strcmp(keyStr, "toString") == 0) {
                return ts_value_make_native_function((void*)ts_object_toString_native, nullptr);
            }
            if (strcmp(keyStr, "valueOf") == 0) {
                return ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr);
            }
            
            return ts_value_make_undefined();
        }

        // 2. Fallback to magic-based checks for built-ins
        if (magic0 == 0x41525259 || magic8 == 0x41525259 || magic16 == 0x41525259) { // TsArray::MAGIC ("ARRY")
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(((TsArray*)obj)->Length());
            }
        }
        if (magic0 == 0x53545247 || magic8 == 0x53545247 || magic16 == 0x53545247) { // TsString::MAGIC ("STRG")
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(((TsString*)obj)->Length());
            }
        }
        if (magic0 == 0x42554646 || magic8 == 0x42554646 || magic16 == 0x42554646) { // TsBuffer::MAGIC ("BUFF")
            if (strcmp(keyStr, "length") == 0) {
                TsBuffer* buf = (TsBuffer*)obj;
                return ts_value_make_int(buf->GetLength());
            }
        }
        
        if (magic8 == 0x48454144 || magic16 == 0x48454144) { // TsHeaders::MAGIC ("HEAD")
            struct FakeHeaders { void* vtable; uint32_t magic; TsMap* map; };
            TsMap* map = ((FakeHeaders*)obj)->map;
            TsValue k; k.type = ValueType::STRING_PTR; k.ptr_val = TsString::GetInterned(keyStr);
            TsValue val = map->Get(k);
            return ts_property_return_value(val);
        }
        if (magic8 == 0x45564E54 || magic16 == 0x45564E54) { // TsEventEmitter::MAGIC ("EVNT")
            if (strcmp(keyStr, "on") == 0) {
                return ts_value_make_function((void*)ts_event_emitter_on, obj);
            }
        }

        // Check for TsFunction (magic at offset 16 typically) - functions can have properties like _.chunk
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC ("FUNC")
            TsFunction* func = (TsFunction*)obj;
            if (func->properties) {
                TsValue k;
                k.type = ValueType::STRING_PTR;
                k.ptr_val = TsString::GetInterned(keyStr);
                TsValue val = func->properties->Get(k);
                if (val.type != ValueType::UNDEFINED) {
                    return ts_property_return_value(val);
                }
            }
            
            // Handle .prototype specially - every JS function should have one
            // Create it lazily if it doesn't exist
            if (strcmp(keyStr, "prototype") == 0) {
                if (!func->properties) {
                    func->properties = TsMap::Create();
                }
                TsValue protoKey;
                protoKey.type = ValueType::STRING_PTR;
                protoKey.ptr_val = TsString::GetInterned("prototype");
                
                // Check if we already have it
                TsValue existing = func->properties->Get(protoKey);
                if (existing.type != ValueType::UNDEFINED) {
                    return ts_property_return_value(existing);
                }
                
                // Create a new empty object as the prototype
                TsMap* proto = TsMap::Create();
                TsValue* protoVal = ts_value_make_object(proto);
                func->properties->Set(protoKey, *protoVal);
                return protoVal;
            }
            
            // Handle .length specially - return 0 if not set
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(0);
            }
            
            // Handle .name specially - return empty string if not set
            if (strcmp(keyStr, "name") == 0) {
                return ts_value_make_string(TsString::Create(""));
            }
            
            // Handle Function.prototype methods directly on the function
            if (strcmp(keyStr, "toString") == 0) {
                return ts_value_make_native_function((void*)ts_function_toString_native, (void*)func);
            }

            // Function.prototype.call / apply
            // Needed for patterns like: (function(){ ... }.call(this));
            if (strcmp(keyStr, "call") == 0) {
                // Create a boxed reference to the target function.
                TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                target->type = ValueType::OBJECT_PTR; // ts_extract_function accepts OBJECT_PTR
                target->ptr_val = func;
                return ts_value_make_native_function((void*)ts_function_call_native, (void*)target);
            }
            if (strcmp(keyStr, "apply") == 0) {
                TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                target->type = ValueType::OBJECT_PTR;
                target->ptr_val = func;
                return ts_value_make_native_function((void*)ts_function_apply_native, (void*)target);
            }

            // Function.prototype.bind: not implemented yet; return undefined to avoid crashes.
            if (strcmp(keyStr, "bind") == 0) {
                return ts_value_make_undefined();
            }
            
            return ts_value_make_undefined();
        }

        // Only try dynamic_cast for TsIncomingMessage AFTER all magic checks.
        // TsIncomingMessage inherits from TsObject, so this is safe.
        // Do NOT do dynamic_cast on arbitrary pointers - they may not be polymorphic types!
        // Check if this could be a TsIncomingMessage by looking for typical vtable pattern
        // TsIncomingMessage has a vtable, and doesn't have magic at common offsets
        if (magic0 != 0x52454758 && magic0 != 0x41525259 && magic0 != 0x53545247 &&
            magic0 != 0x42554646 && magic0 != 0x4D415053) {
            // Might be a polymorphic TsObject - try dynamic_cast carefully
            try {
                TsObject* tsObj = (TsObject*)obj;
                TsIncomingMessage* incomingMsg = dynamic_cast<TsIncomingMessage*>(tsObj);
                if (incomingMsg) {
                    if (strcmp(keyStr, "statusCode") == 0) {
                        return ts_value_make_int(incomingMsg->statusCode);
                    }
                    if (strcmp(keyStr, "method") == 0) {
                        return ts_value_make_string(incomingMsg->method);
                    }
                    if (strcmp(keyStr, "url") == 0) {
                        return ts_value_make_string(incomingMsg->url);
                    }
                    if (strcmp(keyStr, "headers") == 0) {
                        return ts_value_make_object(incomingMsg->headers);
                    }
                }
            } catch (...) {
                // dynamic_cast failed - not a polymorphic type
            }
        }

        return ts_value_make_undefined();
    }

    // Native wrappers for Function.prototype.call/apply
    // ctx: TsValue* that boxes the target function (OBJECT_PTR or FUNCTION_PTR)
    static TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv) {
        TsValue* target = (TsValue*)ctx;
        TsValue* thisArg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue** args = (argc > 1 && argv) ? (argv + 1) : nullptr;
        int callArgc = argc > 1 ? (argc - 1) : 0;
        return ts_function_call_with_this(target, thisArg, callArgc, args);
    }

    static TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv) {
        TsValue* target = (TsValue*)ctx;
        TsValue* thisArg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue* argsArray = (argc >= 2 && argv) ? argv[1] : ts_value_make_undefined();
        return ts_function_apply(target, thisArg, argsArray);
    }

    TsValue* ts_value_get_property(TsValue* val, void* propName) {
        if (!val || !propName) {
            return ts_value_make_undefined();
        }
        
        const char* keyCStr = nullptr;
        // Check if propName is a raw TsString* or a TsValue*
        uint32_t magic = *(uint32_t*)propName;
        if (magic == 0x53545247) { // TsString::MAGIC
            keyCStr = ((TsString*)propName)->ToUtf8();
        } else {
            TsString* key = (TsString*)ts_value_get_string((TsValue*)propName);
            if (key) {
                keyCStr = key->ToUtf8();
            }
        }

        if (!keyCStr) {
            return ts_value_make_undefined();
        }

        if (val->type == ValueType::OBJECT_PTR || val->type == ValueType::ARRAY_PTR || val->type == ValueType::PROMISE_PTR) {
            return ts_object_get_property(val->ptr_val, keyCStr);
        }

        if (val->type == ValueType::STRING_PTR) {
            TsString* s = (TsString*)val->ptr_val;
            if (strcmp(keyCStr, "length") == 0) {
                return ts_value_make_int(s->Length());
            }
        }

        // Fallback for raw pointers (though they should be boxed)
        uint32_t magic0 = *(uint32_t*)val;
        uint32_t magic8 = *(uint32_t*)((char*)val + 8);
        if (magic0 == 0x41525259 || magic8 == 0x41525259 || // TsArray
            magic0 == 0x53545247 || magic8 == 0x53545247 || // TsString
            magic0 == 0x4D415053 || magic8 == 0x4D415053 || // TsMap
            magic0 == 0x45564E54 || magic8 == 0x45564E54) { // TsEventEmitter
            return ts_object_get_property(val, keyCStr);
        }

        return ts_value_make_undefined();
    }

    // Helper to safely extract TsFunction from boxed value
    static TsFunction* ts_extract_function(TsValue* boxedFunc) {
        if (!boxedFunc) {
            return nullptr;
        }
        // Accept both FUNCTION_PTR and OBJECT_PTR for backward compatibility
        if (boxedFunc->type != ValueType::FUNCTION_PTR && boxedFunc->type != ValueType::OBJECT_PTR) {
            return nullptr;
        }
        void* ptr = boxedFunc->ptr_val;
        if (!ptr) {
            return nullptr;
        }
        // Check if this is actually a TsFunction (has FUNC magic)
        TsFunction* func = (TsFunction*)ptr;
        if (func->magic != TsFunction::MAGIC) {
            return nullptr;
        }
        return func;
    }

    TsValue* ts_call_0(TsValue* boxedFunc) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            auto result = ((TsFunctionPtr)func->funcPtr)(func->context, 0, nullptr);
            return result;
        } else {
            typedef TsValue* (*Fn0)(void*);
            auto result = ((Fn0)func->funcPtr)(func->context);
            return result;
        }
    }

    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[1] = { arg1 };
            TsValue* result = ((TsFunctionPtr)func->funcPtr)(func->context, 1, argv);
            return result;
        } else {
            typedef TsValue* (*Fn1)(void*, TsValue*);
            TsValue* result = ((Fn1)func->funcPtr)(func->context, arg1);
            return result;
        }
    }

    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[2] = { arg1, arg2 };
            TsValue* result = ((TsFunctionPtr)func->funcPtr)(func->context, 2, argv);
            return result;
        } else {
            typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
            TsValue* result = ((Fn2)func->funcPtr)(func->context, arg1, arg2);
            return result;
        }
    }

    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[3] = { arg1, arg2, arg3 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 3, argv);
        } else {
            typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
            return ((Fn3)func->funcPtr)(func->context, arg1, arg2, arg3);
        }
    }

    TsValue* ts_call_4(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[4] = { arg1, arg2, arg3, arg4 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 4, argv);
        } else {
            typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn4)func->funcPtr)(func->context, arg1, arg2, arg3, arg4);
        }
    }

    TsValue* ts_call_5(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[5] = { arg1, arg2, arg3, arg4, arg5 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 5, argv);
        } else {
            typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn5)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5);
        }
    }

    TsValue* ts_call_6(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 6, argv);
        } else {
            typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn6)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6);
        }
    }

    TsValue* ts_call_7(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 7, argv);
        } else {
            typedef TsValue* (*Fn7)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn7)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
        }
    }

    TsValue* ts_call_8(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[8] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 8, argv);
        } else {
            typedef TsValue* (*Fn8)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn8)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
        }
    }

    TsValue* ts_call_9(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[9] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 9, argv);
        } else {
            typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn9)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
        }
    }

    TsValue* ts_call_10(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9, TsValue* arg10) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[10] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 10, argv);
        } else {
            typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn10)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
        }
    }

    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv) {
        if (argc == 0) return ts_call_0(boxedFunc);
        if (argc == 1) return ts_call_1(boxedFunc, argv[0]);
        if (argc == 2) return ts_call_2(boxedFunc, argv[0], argv[1]);
        if (argc == 3) return ts_call_3(boxedFunc, argv[0], argv[1], argv[2]);
        if (argc == 4) return ts_call_4(boxedFunc, argv[0], argv[1], argv[2], argv[3]);
        if (argc == 5) return ts_call_5(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4]);
        if (argc == 6) return ts_call_6(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
        if (argc == 7) return ts_call_7(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
        if (argc == 8) return ts_call_8(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
        if (argc == 9) return ts_call_9(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
        if (argc == 10) return ts_call_10(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
        // For now, cap at 10 args
        SPDLOG_WARN("ts_function_call called with argc={} > 10; extra args dropped", argc);
        return ts_call_10(boxedFunc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
    }

    TsValue* ts_function_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int argc, TsValue** argv) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }

        // Preserve the captured context and only override when the function has none.
        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_function_call(boxedFunc, argc, argv);

        if (patchedCtx) {
            func->context = savedCtx;
        }
        return result;
    }

    TsValue* ts_function_apply(TsValue* boxedFunc, TsValue* thisArg, TsValue* argsArray) {
        int64_t argc = ts_value_length(argsArray);
        if (argc < 0) argc = 0;

        // Cap apply to 8 args for now to match ts_function_call
        int64_t cappedArgc = argc > 8 ? 8 : argc;
        std::vector<TsValue*> argv(static_cast<size_t>(cappedArgc), ts_value_make_undefined());
        for (int64_t i = 0; i < cappedArgc; ++i) {
            argv[static_cast<size_t>(i)] = (TsValue*)ts_value_get_element(argsArray, i);
        }
        if (argc > 8) {
            SPDLOG_WARN("ts_function_apply truncated args from {} to 8", argc);
        }
        return ts_function_call_with_this(boxedFunc, thisArg, static_cast<int>(cappedArgc), argv.data());
    }

    // Object static methods
    
    // Object.keys(obj) - returns array of string keys
    TsValue* ts_object_keys(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));
        
        // Unbox if needed
        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;
        
        // Check TsMap::magic at offset 16 (after vptr + explicit vtable field)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_keys(rawPtr));
        }
        
        // Not a map - return empty array
        return ts_value_make_array(TsArray::Create(0));
    }
    
    // Object.values(obj) - returns array of values
    TsValue* ts_object_values(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));
        
        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;
        
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_values(rawPtr));
        }
        return ts_value_make_array(TsArray::Create(0));
    }
    
    // Object.entries(obj) - returns array of [key, value] pairs
    TsValue* ts_object_entries(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_entries(rawPtr));
        }

        return ts_value_make_array(TsArray::Create(0));
    }

    // Object.getOwnPropertyNames(obj) - returns array of all own property names
    // In our runtime, this is the same as Object.keys() since we don't have
    // non-enumerable properties
    TsValue* ts_object_getOwnPropertyNames(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_keys(rawPtr));
        }

        return ts_value_make_array(TsArray::Create(0));
    }

    // Object.getPrototypeOf(obj) - returns the prototype of an object
    // In our runtime, we don't have a full prototype chain, so we return null
    // for most objects. This provides basic ES5 conformance.
    TsValue* ts_object_getPrototypeOf(TsValue* obj) {
        if (!obj) return ts_value_make_undefined();

        // For now, return null for all objects since we don't have prototype chains
        // A proper implementation would check the object type and return the
        // appropriate prototype (Object.prototype, Array.prototype, etc.)
        return ts_value_make_undefined();
    }

    // Object.create(proto) - creates new object with specified prototype
    // In our runtime, we don't have prototype chains, so:
    // - Object.create(null) returns an empty object
    // - Object.create(proto) copies properties from proto to new object
    TsValue* ts_object_create(TsValue* proto) {
        // Create a new empty map
        TsMap* newObj = TsMap::Create();

        // If proto is null/undefined, just return empty object
        if (!proto || ts_value_is_undefined(proto)) {
            return ts_value_make_object(newObj);
        }

        // Unbox proto if needed
        void* protoRaw = ts_value_get_object(proto);
        if (!protoRaw) protoRaw = proto;

        // Check if proto is a TsMap
        uint32_t magic = *(uint32_t*)((char*)protoRaw + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            // Copy all properties from proto to new object
            TsMap* protoMap = (TsMap*)protoRaw;
            TsArray* entries = (TsArray*)protoMap->GetEntries();
            int64_t len = entries->Length();
            for (int64_t i = 0; i < len; i++) {
                TsArray* entry = (TsArray*)entries->Get(i);
                TsValue* key = (TsValue*)entry->Get(0);
                TsValue* val = (TsValue*)entry->Get(1);
                newObj->Set(*key, *val);
            }
        }

        return ts_value_make_object(newObj);
    }

    // Object.freeze(obj) - freezes an object, preventing modifications
    TsValue* ts_object_freeze(TsValue* obj) {
        if (!obj) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            map->Freeze();
        }

        return obj;  // Return the same object (frozen)
    }

    // Object.seal(obj) - seals an object, preventing new properties
    TsValue* ts_object_seal(TsValue* obj) {
        if (!obj) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            map->Seal();
        }

        return obj;  // Return the same object (sealed)
    }

    // Object.preventExtensions(obj) - prevents new properties from being added
    TsValue* ts_object_preventExtensions(TsValue* obj) {
        if (!obj) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            map->PreventExtensions();
        }

        return obj;
    }

    // Object.isFrozen(obj) - returns true if object is frozen
    TsValue* ts_object_isFrozen(TsValue* obj) {
        if (!obj) return ts_value_make_bool(true);  // null/undefined considered frozen

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            return ts_value_make_bool(map->IsFrozen());
        }

        return ts_value_make_bool(false);
    }

    // Object.isSealed(obj) - returns true if object is sealed
    TsValue* ts_object_isSealed(TsValue* obj) {
        if (!obj) return ts_value_make_bool(true);  // null/undefined considered sealed

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            return ts_value_make_bool(map->IsSealed() || map->IsFrozen());
        }

        return ts_value_make_bool(false);
    }

    // Object.isExtensible(obj) - returns true if object is extensible
    TsValue* ts_object_isExtensible(TsValue* obj) {
        if (!obj) return ts_value_make_bool(false);  // null/undefined not extensible

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) {  // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;
            return ts_value_make_bool(map->IsExtensible());
        }

        return ts_value_make_bool(true);
    }

    // Object.assign(target, source) - copies properties from source to target
    TsValue* ts_object_assign(TsValue* target, TsValue* source) {
        if (!target) return target;
        if (!source) return target;
        
        void* targetRaw = ts_value_get_object(target);
        if (!targetRaw) targetRaw = target;
        
        void* sourceRaw = ts_value_get_object(source);
        if (!sourceRaw) sourceRaw = source;
        
        // Check both are TsMaps (magic at offset 16 - see TsObject.h layout)
        uint32_t targetMagic = *(uint32_t*)((char*)targetRaw + 16);
        uint32_t sourceMagic = *(uint32_t*)((char*)sourceRaw + 16);
        
        if (targetMagic != 0x4D415053 || sourceMagic != 0x4D415053) {
            return target;
        }
        
        TsMap* targetMap = (TsMap*)targetRaw;
        TsMap* sourceMap = (TsMap*)sourceRaw;
        
        // Get entries from source and copy to target
        TsArray* entries = (TsArray*)sourceMap->GetEntries();
        int64_t len = entries->Length();
        for (int64_t i = 0; i < len; i++) {
            TsArray* entry = (TsArray*)entries->Get(i);
            TsValue* key = (TsValue*)entry->Get(0);
            TsValue* val = (TsValue*)entry->Get(1);
            targetMap->Set(*key, *val);
        }
        
        return target;
    }
    
    // Object.hasOwn(obj, prop) - check if object has own property
    bool ts_object_has_own(TsValue* obj, TsValue* prop) {
        if (!obj || !prop) return false;
        
        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;
        
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            TsValue propVal;
            propVal.type = ValueType::STRING_PTR;
            propVal.ptr_val = prop;
            return ts_map_has_v(rawPtr, propVal);
        }
        
        return false;
    }

    // Object.fromEntries(iterable) - create object from key-value pairs
    TsValue* ts_object_from_entries(TsValue* entries) {
        TsMap* result = TsMap::Create();
        if (!entries) return ts_value_make_object(result);
        
        void* rawPtr = ts_value_get_object(entries);
        if (!rawPtr) rawPtr = entries;
        
        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != 0x41525259) { // TsArray::MAGIC
            return ts_value_make_object(result);
        }
        
        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();
        
        for (int64_t i = 0; i < len; i++) {
            void* entry = (void*)arr->Get(i);
            if (!entry) continue;
            
            // Unbox entry if needed
            void* entryRaw = ts_value_get_object((TsValue*)entry);
            if (!entryRaw) entryRaw = entry;
            
            uint32_t entryMagic = *(uint32_t*)entryRaw;
            if (entryMagic != 0x41525259) continue;
            
            TsArray* pair = (TsArray*)entryRaw;
            if (pair->Length() < 2) continue;
            
            TsValue* key = (TsValue*)pair->Get(0);
            TsValue* val = (TsValue*)pair->Get(1);
            if (key && val) {
                result->Set(*key, *val);
            }
        }
        
        return ts_value_make_object(result);
    }

    TsValue* ts_value_add(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        
        // String concatenation if either is a string
        if (a->type == ValueType::STRING_PTR || b->type == ValueType::STRING_PTR) {
            TsString* s1 = (TsString*)ts_value_get_string(a);
            TsString* s2 = (TsString*)ts_value_get_string(b);
            if (!s1) s1 = TsString::Create("");
            if (!s2) s2 = TsString::Create("");
            return ts_value_make_string(TsString::Concat(s1, s2));
        }
        
        // Number addition
        if (a->type == ValueType::NUMBER_INT && b->type == ValueType::NUMBER_INT) {
            return ts_value_make_int(a->i_val + b->i_val);
        }
        
        double d1 = ts_value_get_double(a);
        double d2 = ts_value_get_double(b);
        return ts_value_make_double(d1 + d2);
    }

    TsValue* ts_value_sub(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        if (a->type == ValueType::NUMBER_INT && b->type == ValueType::NUMBER_INT) {
            return ts_value_make_int(a->i_val - b->i_val);
        }
        return ts_value_make_double(ts_value_get_double(a) - ts_value_get_double(b));
    }

    TsValue* ts_value_mul(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        if (a->type == ValueType::NUMBER_INT && b->type == ValueType::NUMBER_INT) {
            return ts_value_make_int(a->i_val * b->i_val);
        }
        return ts_value_make_double(ts_value_get_double(a) * ts_value_get_double(b));
    }

    TsValue* ts_value_div(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        double d1 = ts_value_get_double(a);
        double d2 = ts_value_get_double(b);
        if (d2 == 0.0) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        return ts_value_make_double(d1 / d2);
    }

    TsValue* ts_value_mod(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        double d1 = ts_value_get_double(a);
        double d2 = ts_value_get_double(b);
        if (d2 == 0.0) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        return ts_value_make_double(std::fmod(d1, d2));
    }

    TsValue* ts_value_eq(TsValue* a, TsValue* b) {
        // Treat nullptr as undefined for loose equality
        if (!a) a = ts_value_make_undefined();
        if (!b) b = ts_value_make_undefined();

        if (a->type == b->type) return ts_value_make_bool(ts_value_strict_eq_bool(a, b));
        
        // null == undefined
        bool a_nullish = (a->type == ValueType::UNDEFINED || (a->type == ValueType::OBJECT_PTR && a->ptr_val == nullptr));
        bool b_nullish = (b->type == ValueType::UNDEFINED || (b->type == ValueType::OBJECT_PTR && b->ptr_val == nullptr));
        if (a_nullish && b_nullish) return ts_value_make_bool(true);
        
        // If one is nullish but not both, they are not equal
        if (a_nullish || b_nullish) return ts_value_make_bool(false);
        
        // Coerce to numbers
        return ts_value_make_bool(ts_value_get_double(a) == ts_value_get_double(b));
    }

    TsValue* ts_value_lt(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        if (a->type == ValueType::STRING_PTR && b->type == ValueType::STRING_PTR) {
            TsString* s1 = (TsString*)a->ptr_val;
            TsString* s2 = (TsString*)b->ptr_val;
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) < 0);
        }
        return ts_value_make_bool(ts_value_get_double(a) < ts_value_get_double(b));
    }

    TsValue* ts_value_gt(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        if (a->type == ValueType::STRING_PTR && b->type == ValueType::STRING_PTR) {
            TsString* s1 = (TsString*)a->ptr_val;
            TsString* s2 = (TsString*)b->ptr_val;
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) > 0);
        }
        return ts_value_make_bool(ts_value_get_double(a) > ts_value_get_double(b));
    }

    TsValue* ts_value_lte(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        if (a->type == ValueType::STRING_PTR && b->type == ValueType::STRING_PTR) {
            TsString* s1 = (TsString*)a->ptr_val;
            TsString* s2 = (TsString*)b->ptr_val;
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) <= 0);
        }
        return ts_value_make_bool(ts_value_get_double(a) <= ts_value_get_double(b));
    }

    TsValue* ts_value_gte(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        if (a->type == ValueType::STRING_PTR && b->type == ValueType::STRING_PTR) {
            TsString* s1 = (TsString*)a->ptr_val;
            TsString* s2 = (TsString*)b->ptr_val;
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) >= 0);
        }
        return ts_value_make_bool(ts_value_get_double(a) >= ts_value_get_double(b));
    }

    TsString* ts_value_typeof(TsValue* v) {
        if (!v) return TsString::Create("undefined");

        // Check type field first - if in valid range (0-10), trust it
        uint8_t typeField = *(uint8_t*)v;
        if (typeField <= 10) {
            // It's a proper TsValue - use the type field directly
            switch (v->type) {
                case ValueType::UNDEFINED: return TsString::Create("undefined");
                case ValueType::NUMBER_INT:
                case ValueType::NUMBER_DBL: return TsString::Create("number");
                case ValueType::BOOLEAN: return TsString::Create("boolean");
                case ValueType::STRING_PTR: return TsString::Create("string");
                case ValueType::FUNCTION_PTR: return TsString::Create("function");
                case ValueType::OBJECT_PTR: {
                    if (!v->ptr_val) return TsString::Create("object");
                    // Check if ptr_val points to a TsFunction (for backwards compatibility)
                    TsFunction* func = dynamic_cast<TsFunction*>((TsObject*)v->ptr_val);
                    if (func) return TsString::Create("function");
                    return TsString::Create("object");
                }
                case ValueType::ARRAY_PTR: return TsString::Create("object");
                case ValueType::BIGINT_PTR: return TsString::Create("bigint");
                case ValueType::SYMBOL_PTR: return TsString::Create("symbol");
                default: return TsString::Create("object");
            }
        }

        // Type > 10 means this is a raw pointer - check magic numbers
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247) return TsString::Create("string"); // TsString::MAGIC
        if (magic == 0x41525259) return TsString::Create("object"); // TsArray::MAGIC
        if (magic == 0x4D415053) return TsString::Create("object"); // TsMap::MAGIC
        if (magic == 0x53455453) return TsString::Create("object"); // TsSet::MAGIC
        if (magic == 0x46554E43) return TsString::Create("function"); // TsFunction::MAGIC

        // Check other offsets for magic (virtual inheritance may shift it)
        uint32_t magic8 = *(uint32_t*)((char*)v + 8);
        if (magic8 == 0x46554E43) return TsString::Create("function");
        
        uint32_t magic16 = *(uint32_t*)((char*)v + 16);
        if (magic16 == 0x46554E43) return TsString::Create("function");

        // Default to object for unknown raw pointers
        return TsString::Create("object");
    }

    TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key) {
        if (!obj || !key) return ts_value_make_undefined();
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return ts_value_make_undefined();
        
        // Check magic at offset 0 first (TsArray has magic at offset 0)
        uint32_t magic0 = *(uint32_t*)rawObj;
        if (magic0 == 0x41525259) { // TsArray::MAGIC = "ARRY"
            // This is an array
            if (key->type == ValueType::NUMBER_INT) {
                return ts_array_get_as_value(rawObj, key->i_val);
            } else if (key->type == ValueType::NUMBER_DBL) {
                return ts_array_get_as_value(rawObj, (int64_t)key->d_val);
            } else if (key->type == ValueType::STRING_PTR) {
                // Handle property access on arrays like "length"
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k && strcmp(k, "length") == 0) {
                        return ts_value_make_int(((TsArray*)rawObj)->Length());
                    }
                }
            }
            return ts_value_make_undefined();
        }
        
        // Check if this is a TsFunction and get its properties map
        uint32_t magic16 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 16);
        if (magic16 == TsFunction::MAGIC) {
            TsFunction* func = (TsFunction*)rawObj;
            if (!func->properties) {
                return ts_value_make_undefined();  // No properties set yet
            }
            // Use the properties map
            TsValue result = func->properties->Get(*key);
            if (result.type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
            TsValue* heapResult = (TsValue*)ts_alloc(sizeof(TsValue));
            *heapResult = result;
            return heapResult;
        }
        
        // Check if this is actually a TsMap before using map operations
        // TsMap::MAGIC is at offset 16 (after vtable ptr + explicit vtable field)
        uint32_t magic20 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 20);
        uint32_t magic24 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 24);
        if (magic16 != 0x4D415053 && magic20 != 0x4D415053 && magic24 != 0x4D415053) { // TsMap::MAGIC = "MAPS"
            // Not a map - return undefined
            return ts_value_make_undefined();
        }
        
        // Use TsMap::Get which handles hashing correctly (by string content, not pointer address)
        TsMap* map = (TsMap*)rawObj;
        TsValue result = map->Get(*key);
        
        if (result.type == ValueType::UNDEFINED) {
            // If not found in the map, check Object.prototype methods
            // This provides prototype chain behavior for plain objects
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        if (strcmp(k, "hasOwnProperty") == 0) {
                            return ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr);
                        }
                        if (strcmp(k, "toString") == 0) {
                            return ts_value_make_native_function((void*)ts_object_toString_native, nullptr);
                        }
                        if (strcmp(k, "valueOf") == 0) {
                            return ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr);
                        }
                    }
                }
            }
            return ts_value_make_undefined();
        }
        
        TsValue* heapResult = (TsValue*)ts_alloc(sizeof(TsValue));
        *heapResult = result;
        return heapResult;
    }

    TsValue* ts_array_get_dynamic(TsValue* arr, TsValue* index) {
        void* rawArr = ts_value_get_object(arr);
        if (!rawArr) return ts_value_make_undefined();
        
        int64_t idx = ts_value_get_int(index);
        return ts_array_get_as_value(rawArr, idx);
    }

    void ts_array_set_dynamic(TsValue* arr, TsValue* index, TsValue* value) {
        void* rawArr = ts_value_get_object(arr);
        if (!rawArr) return;
        
        int64_t idx = ts_value_get_int(index);
        uint32_t magic = *(uint32_t*)rawArr;
        if (magic == 0x41525259) { // TsArray::MAGIC
            ((TsArray*)rawArr)->Set(idx, (int64_t)value);
        }
    }

    // Generic dynamic property/element set - handles both arrays and maps/objects
    void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value) {
        if (!obj || !key || !value) return;
        
        // Delegate to ts_object_set_prop_v which handles all cases
        ts_object_set_prop_v(*obj, *key, *value);
    }

    // ============================================================
    // Value-passing variants (_v) - avoid heap allocation for TsValue
    // These take TsValue by value (16 bytes, fits in 2 registers)
    // ============================================================

    TsValue ts_object_get_prop_v(TsValue obj, TsValue key) {
        // If key is a number, try array access
        if (key.type == ValueType::NUMBER_INT || key.type == ValueType::NUMBER_DBL) {
            int64_t idx = (key.type == ValueType::NUMBER_INT) ? key.i_val : (int64_t)key.d_val;
            void* rawObj = ts_value_get_object(&obj);
            if (rawObj) {
                uint32_t magic = *(uint32_t*)rawObj;
                if (magic == 0x41525259) { // TsArray::MAGIC
                    return ts_array_get_v(rawObj, idx);
                }
            }
        }
        
        // Coerce key to string
        TsString* keyStr = (TsString*)ts_value_get_string(&key);
        if (!keyStr) {
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.i_val = 0;
            return undef;
        }
        
        // Delegate to pointer-based version and dereference
        TsValue* result = ts_value_get_property(&obj, (void*)keyStr);
        if (!result) {
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.i_val = 0;
            return undef;
        }
        return *result;
    }

    TsValue ts_object_set_prop_v(TsValue obj, TsValue key, TsValue value) {
        void* rawObj = ts_value_get_object(&obj);
        if (!rawObj) return value;

        // If key is a number, try array access
        if (key.type == ValueType::NUMBER_INT || key.type == ValueType::NUMBER_DBL) {
            int64_t idx = (key.type == ValueType::NUMBER_INT) ? key.i_val : (int64_t)key.d_val;
            uint32_t magic = *(uint32_t*)rawObj;
            if (magic == 0x41525259) { // TsArray::MAGIC
                ts_array_set_v(rawObj, idx, value);
                return value;
            }
        }

        // Coerce key to string
        TsString* keyStr = (TsString*)ts_value_get_string(&key);
        if (!keyStr) return value;

        // Targeted trace: module.exports writes for the tracked module object (lodash or test_umdsim)
        if (g_debug_lodash_module_map && rawObj == g_debug_lodash_module_map) {
            const char* k = keyStr->ToUtf8();
            if (k && std::strcmp(k, "exports") == 0) {
                std::printf("[ts_object_set_prop_v] module.exports write: value.type=%d value.ptr=%p\n",
                    (int)value.type, value.ptr_val);
            }
        }

        // Check multiple magic offsets for TsMap
        uint32_t magic0 = *(uint32_t*)rawObj;
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawObj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawObj + 24);

        // Check for TsFunction (can have properties like _.chunk)
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC ("FUNC")
            TsFunction* func = (TsFunction*)rawObj;
            if (!func->properties) {
                func->properties = TsMap::Create();
            }
            // Need to box key/value for Map::Set
            TsValue* boxedKey = (TsValue*)ts_alloc(sizeof(TsValue));
            TsValue* boxedVal = (TsValue*)ts_alloc(sizeof(TsValue));
            *boxedKey = key;
            *boxedVal = value;
            func->properties->Set(*boxedKey, *boxedVal);
            return value;
        }

        // Check if it's a map
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC
            ts_map_set_v(rawObj, key, value);
            return value;
        }

        return value;
    }

    bool ts_object_has_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        TsString* keyStr = (TsString*)ts_value_get_string(key);
        if (!keyStr) return false;

        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawObj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawObj + 24);
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC
            TsMap* map = (TsMap*)rawObj;
            return map->Has(*key);
        }
        
        return false;
    }

    bool ts_object_delete_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        TsString* keyStr = (TsString*)ts_value_get_string(key);
        if (!keyStr) return false;

        // Check for TsMap using TsObject::magic at offset 16 (after vptr and vtable)
        uint32_t magic = *(uint32_t*)((char*)rawObj + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC "MAPS"
            TsMap* map = (TsMap*)rawObj;
            return map->Delete(*key);
        }
        
        return false;
    }

    extern "C" void ts_console_log_value_no_newline(TsValue* val);

    TsValue* ts_console_log_native(void* context, int argc, TsValue** argv) {
        for (int i = 0; i < argc; i++) {
            if (i > 0) std::printf(" ");
            ts_console_log_value_no_newline(argv[i]);
        }
        std::printf("\n");
        std::fflush(stdout);
        return ts_value_make_undefined();
    }

    TsValue* ts_object_keys_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_array(TsArray::Create(0));
        return ts_object_keys(argv[0]);
    }

    TsValue* ts_object_values_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_array(TsArray::Create(0));
        return ts_object_values(argv[0]);
    }

    TsValue* ts_object_entries_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_array(TsArray::Create(0));
        return ts_object_entries(argv[0]);
    }

    TsValue* ts_object_getOwnPropertyNames_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_array(TsArray::Create(0));
        return ts_object_getOwnPropertyNames(argv[0]);
    }

    TsValue* ts_object_getPrototypeOf_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();
        return ts_object_getPrototypeOf(argv[0]);
    }

    TsValue* ts_object_create_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_object_create(nullptr);
        return ts_object_create(argv[0]);
    }

    TsValue* ts_object_freeze_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();
        return ts_object_freeze(argv[0]);
    }

    TsValue* ts_object_seal_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();
        return ts_object_seal(argv[0]);
    }

    TsValue* ts_object_preventExtensions_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();
        return ts_object_preventExtensions(argv[0]);
    }

    TsValue* ts_object_isFrozen_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_bool(true);
        return ts_object_isFrozen(argv[0]);
    }

    TsValue* ts_object_isSealed_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_bool(true);
        return ts_object_isSealed(argv[0]);
    }

    TsValue* ts_object_isExtensible_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_bool(false);
        return ts_object_isExtensible(argv[0]);
    }

    TsValue* ts_json_stringify_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();

        void* obj = argv[0];
        void* replacer = (argc >= 2) ? (void*)argv[1] : nullptr;
        void* space = (argc >= 3) ? (void*)argv[2] : nullptr;
        TsString* s = (TsString*)ts_json_stringify(obj, replacer, space);
        if (!s) return ts_value_make_undefined();
        return ts_value_make_string((void*)s);
    }

    TsValue* ts_json_parse_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_undefined();

        TsString* s = (TsString*)ts_value_get_string(argv[0]);
        if (!s) return ts_value_make_undefined();

        void* parsed = ts_json_parse((void*)s);
        if (!parsed) return ts_value_make_undefined();
        return (TsValue*)parsed;
    }

    TsValue* ts_array_isArray_native(void* context, int argc, TsValue** argv) {
        if (argc < 1 || !argv[0]) return ts_value_make_bool(false);
        TsValue* v = argv[0];
        if (v->type == ValueType::ARRAY_PTR) return ts_value_make_bool(true);
        void* raw = ts_value_get_object(v);
        if (!raw) raw = v;
        uint32_t magic = *(uint32_t*)raw;
        return ts_value_make_bool(magic == 0x41525259);
    }

    TsValue* ts_math_random_native(void* context, int argc, TsValue** argv) {
        double r = (double)std::rand() / (double)RAND_MAX;
        return ts_value_make_double(r);
    }

    TsValue* ts_math_floor_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double x = ts_value_get_double(argv[0]);
        return ts_value_make_int((int64_t)std::floor(x));
    }

    TsValue* ts_math_ceil_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double x = ts_value_get_double(argv[0]);
        return ts_value_make_int((int64_t)std::ceil(x));
    }

    TsValue* ts_math_abs_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double x = ts_value_get_double(argv[0]);
        return ts_value_make_double(std::abs(x));
    }

    TsValue* ts_math_max_native(void* context, int argc, TsValue** argv) {
        if (argc == 0) return ts_value_make_double(-std::numeric_limits<double>::infinity());
        double m = ts_value_get_double(argv[0]);
        for (int i = 1; i < argc; i++) {
            double x = ts_value_get_double(argv[i]);
            if (x > m) m = x;
        }
        return ts_value_make_double(m);
    }

    TsValue* ts_math_min_native(void* context, int argc, TsValue** argv) {
        if (argc == 0) return ts_value_make_double(std::numeric_limits<double>::infinity());
        double m = ts_value_get_double(argv[0]);
        for (int i = 1; i < argc; i++) {
            double x = ts_value_get_double(argv[i]);
            if (x < m) m = x;
        }
        return ts_value_make_double(m);
    }

    extern "C" int64_t ts_parseInt(void* value);

    TsValue* ts_parseInt_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_int(0);
        return ts_value_make_int(ts_parseInt(argv[0]));
    }

    TsValue* ts_parseFloat_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        return ts_value_make_double(ts_value_get_double(argv[0]));
    }

    // Global Objects (must match C linkage declared in TsObject.h)
    extern "C" TsValue* Object = nullptr;
    extern "C" TsValue* Array = nullptr;
    extern "C" TsValue* Math = nullptr;
    extern "C" TsValue* JSON = nullptr;
    extern "C" TsValue* console = nullptr;
    extern "C" TsValue* process = nullptr;
    extern "C" TsValue* Buffer = nullptr;
    extern "C" TsValue* global = nullptr;
    extern "C" TsValue* parseInt = nullptr;
    extern "C" TsValue* parseFloat = nullptr;
    
    // Prototype method implementations
    
    // Function.prototype.toString - returns "[native code]" for compiled functions
    static TsValue* ts_function_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(TsString::Create("function() { [native code] }"));
    }
    
    // Object.prototype.hasOwnProperty(key)
    static TsValue* ts_object_hasOwnProperty_native(void* ctx, int argc, TsValue** argv) {
        if (argc < 1 || !argv[0]) {
            return ts_value_make_bool(false);
        }
        
        // 'this' is passed as context for method calls
        if (!ctx) {
            return ts_value_make_bool(false);
        }
        
        // Try to get the object from context (could be boxed TsValue or raw pointer)
        void* obj = ts_value_get_object((TsValue*)ctx);
        if (!obj) obj = ctx;
        
        // Check if it's a TsMap
        TsMap* map = dynamic_cast<TsMap*>((TsObject*)obj);
        if (!map) {
            return ts_value_make_bool(false);
        }
        
        // Get the property key as TsValue
        TsValue* keyVal = argv[0];
        
        // Check if the property exists in the map using TsValue directly
        return ts_value_make_bool(map->Has(*keyVal));
    }
    
    // Object.prototype.toString() - returns "[object Object]"
    static TsValue* ts_object_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(TsString::Create("[object Object]"));
    }
    
    // Object.prototype.valueOf() - returns the object itself
    static TsValue* ts_object_valueOf_native(void* ctx, int argc, TsValue** argv) {
        // Return the context (this) if available, otherwise undefined
        if (ctx) {
            return ts_value_make_object(ctx);
        }
        return ts_value_make_undefined();
    }

    // Object constructor function - converts value to object
    static TsValue* ts_object_constructor_native(void* ctx, int argc, TsValue** argv) {
        if (argc == 0) {
            // Object() with no args returns empty object
            return ts_value_make_object(TsMap::Create());
        }
        TsValue* val = argv[0];
        if (!val) {
            return ts_value_make_object(TsMap::Create());
        }
        // If already an object, return as-is
        if (val->type == ValueType::OBJECT_PTR) {
            return val;
        }
        // Otherwise wrap in object
        return ts_value_make_object(TsMap::Create());
    }

    // Array constructor function
    static TsValue* ts_array_constructor_native(void* ctx, int argc, TsValue** argv) {
        if (argc == 0) {
            return ts_value_make_object(TsArray::Create(0));
        }
        if (argc == 1) {
            // Array(n) creates array of length n
            TsValue* val = argv[0];
            if (val && val->type == ValueType::NUMBER_INT) {
                return ts_value_make_object(TsArray::Create(val->i_val));
            }
        }
        // Array(...items) creates array with items
        TsArray* arr = TsArray::Create(argc);
        for (int i = 0; i < argc; i++) {
            arr->Push((int64_t)argv[i]);
        }
        return ts_value_make_object(arr);
    }

    // isNaN(value) - returns true if value is NaN
    TsValue* ts_isNaN_native(int argc, TsValue** argv, void* context) {
        if (argc < 1 || !argv[0]) {
            return ts_value_make_bool(true); // undefined is NaN
        }
        TsValue* val = argv[0];
        double d = 0.0;
        if (val->type == ValueType::NUMBER_DBL) {
            d = val->d_val;
        } else if (val->type == ValueType::NUMBER_INT) {
            return ts_value_make_bool(false); // integers are never NaN
        } else if (val->type == ValueType::STRING_PTR) {
            // Try to parse string as number
            TsString* str = (TsString*)val->ptr_val;
            if (!str || str->Length() == 0) {
                return ts_value_make_bool(true);
            }
            // For simplicity, assume non-numeric strings are NaN
            return ts_value_make_bool(true);
        } else {
            return ts_value_make_bool(true);
        }
        return ts_value_make_bool(std::isnan(d));
    }

    // isFinite(value) - returns true if value is finite
    TsValue* ts_isFinite_native(int argc, TsValue** argv, void* context) {
        if (argc < 1 || !argv[0]) {
            return ts_value_make_bool(false); // undefined is not finite
        }
        TsValue* val = argv[0];
        double d = 0.0;
        if (val->type == ValueType::NUMBER_DBL) {
            d = val->d_val;
        } else if (val->type == ValueType::NUMBER_INT) {
            return ts_value_make_bool(true); // integers are always finite
        } else {
            return ts_value_make_bool(false);
        }
        return ts_value_make_bool(std::isfinite(d));
    }

    void ts_runtime_init() {
        // Initialize Object global - make it callable
        TsValue* objectConstructor = ts_value_make_native_function((void*)ts_object_constructor_native, nullptr);
        
        // Get the TsFunction so we can add static methods as properties
        TsFunction* objectFunc = (TsFunction*)objectConstructor->ptr_val;
        if (!objectFunc->properties) {
            objectFunc->properties = TsMap::Create();
        }
        
        // Object.keys
        TsValue keysKey; keysKey.type = ValueType::STRING_PTR; keysKey.ptr_val = TsString::Create("keys");
        objectFunc->properties->Set(keysKey, *ts_value_make_native_function((void*)ts_object_keys_native, nullptr));
        
        // Object.values
        TsValue valuesKey; valuesKey.type = ValueType::STRING_PTR; valuesKey.ptr_val = TsString::Create("values");
        objectFunc->properties->Set(valuesKey, *ts_value_make_native_function((void*)ts_object_values_native, nullptr));
        
        // Object.entries
        TsValue entriesKey; entriesKey.type = ValueType::STRING_PTR; entriesKey.ptr_val = TsString::Create("entries");
        objectFunc->properties->Set(entriesKey, *ts_value_make_native_function((void*)ts_object_entries_native, nullptr));

        // Object.getOwnPropertyNames
        TsValue gopnKey; gopnKey.type = ValueType::STRING_PTR; gopnKey.ptr_val = TsString::Create("getOwnPropertyNames");
        objectFunc->properties->Set(gopnKey, *ts_value_make_native_function((void*)ts_object_getOwnPropertyNames_native, nullptr));

        // Object.getPrototypeOf
        TsValue gpoKey; gpoKey.type = ValueType::STRING_PTR; gpoKey.ptr_val = TsString::Create("getPrototypeOf");
        objectFunc->properties->Set(gpoKey, *ts_value_make_native_function((void*)ts_object_getPrototypeOf_native, nullptr));

        // Object.create
        TsValue createKey; createKey.type = ValueType::STRING_PTR; createKey.ptr_val = TsString::Create("create");
        objectFunc->properties->Set(createKey, *ts_value_make_native_function((void*)ts_object_create_native, nullptr));

        // Object.freeze
        TsValue freezeKey; freezeKey.type = ValueType::STRING_PTR; freezeKey.ptr_val = TsString::Create("freeze");
        objectFunc->properties->Set(freezeKey, *ts_value_make_native_function((void*)ts_object_freeze_native, nullptr));

        // Object.seal
        TsValue sealKey; sealKey.type = ValueType::STRING_PTR; sealKey.ptr_val = TsString::Create("seal");
        objectFunc->properties->Set(sealKey, *ts_value_make_native_function((void*)ts_object_seal_native, nullptr));

        // Object.preventExtensions
        TsValue peKey; peKey.type = ValueType::STRING_PTR; peKey.ptr_val = TsString::Create("preventExtensions");
        objectFunc->properties->Set(peKey, *ts_value_make_native_function((void*)ts_object_preventExtensions_native, nullptr));

        // Object.isFrozen
        TsValue isFrozenKey; isFrozenKey.type = ValueType::STRING_PTR; isFrozenKey.ptr_val = TsString::Create("isFrozen");
        objectFunc->properties->Set(isFrozenKey, *ts_value_make_native_function((void*)ts_object_isFrozen_native, nullptr));

        // Object.isSealed
        TsValue isSealedKey; isSealedKey.type = ValueType::STRING_PTR; isSealedKey.ptr_val = TsString::Create("isSealed");
        objectFunc->properties->Set(isSealedKey, *ts_value_make_native_function((void*)ts_object_isSealed_native, nullptr));

        // Object.isExtensible
        TsValue isExtensibleKey; isExtensibleKey.type = ValueType::STRING_PTR; isExtensibleKey.ptr_val = TsString::Create("isExtensible");
        objectFunc->properties->Set(isExtensibleKey, *ts_value_make_native_function((void*)ts_object_isExtensible_native, nullptr));

        Object = objectConstructor;

        // Initialize console
        TsMap* consoleMap = TsMap::Create();
        TsValue logKey; logKey.type = ValueType::STRING_PTR; logKey.ptr_val = TsString::Create("log");
        consoleMap->Set(logKey, *ts_value_make_native_function((void*)ts_console_log_native, nullptr));
        console = ts_value_make_object(consoleMap);

        // Initialize Array - make it callable
        TsValue* arrayConstructor = ts_value_make_native_function((void*)ts_array_constructor_native, nullptr);
        
        // Get the TsFunction so we can add static methods as properties
        TsFunction* arrayFunc = (TsFunction*)arrayConstructor->ptr_val;
        if (!arrayFunc->properties) {
            arrayFunc->properties = TsMap::Create();
        }
        
        // Array.isArray
        TsValue isArrayKey; isArrayKey.type = ValueType::STRING_PTR; isArrayKey.ptr_val = TsString::Create("isArray");
        arrayFunc->properties->Set(isArrayKey, *ts_value_make_native_function((void*)ts_array_isArray_native, nullptr));
        
        Array = arrayConstructor;

        // Initialize Math with minimal functions used by common JS libs
        TsMap* mathMap = TsMap::Create();
        TsValue randomKey; randomKey.type = ValueType::STRING_PTR; randomKey.ptr_val = TsString::Create("random");
        TsValue floorKey; floorKey.type = ValueType::STRING_PTR; floorKey.ptr_val = TsString::Create("floor");
        TsValue ceilKey; ceilKey.type = ValueType::STRING_PTR; ceilKey.ptr_val = TsString::Create("ceil");
        TsValue absKey; absKey.type = ValueType::STRING_PTR; absKey.ptr_val = TsString::Create("abs");
        TsValue maxKey; maxKey.type = ValueType::STRING_PTR; maxKey.ptr_val = TsString::Create("max");
        TsValue minKey; minKey.type = ValueType::STRING_PTR; minKey.ptr_val = TsString::Create("min");
        mathMap->Set(randomKey, *ts_value_make_native_function((void*)ts_math_random_native, nullptr));
        mathMap->Set(floorKey, *ts_value_make_native_function((void*)ts_math_floor_native, nullptr));
        mathMap->Set(ceilKey, *ts_value_make_native_function((void*)ts_math_ceil_native, nullptr));
        mathMap->Set(absKey, *ts_value_make_native_function((void*)ts_math_abs_native, nullptr));
        mathMap->Set(maxKey, *ts_value_make_native_function((void*)ts_math_max_native, nullptr));
        mathMap->Set(minKey, *ts_value_make_native_function((void*)ts_math_min_native, nullptr));
        Math = ts_value_make_object(mathMap);

        // Initialize JSON with parse/stringify
        TsMap* jsonMap = TsMap::Create();
        TsValue parseKey; parseKey.type = ValueType::STRING_PTR; parseKey.ptr_val = TsString::Create("parse");
        TsValue stringifyKey; stringifyKey.type = ValueType::STRING_PTR; stringifyKey.ptr_val = TsString::Create("stringify");
        jsonMap->Set(parseKey, *ts_value_make_native_function((void*)ts_json_parse_native, nullptr));
        jsonMap->Set(stringifyKey, *ts_value_make_native_function((void*)ts_json_stringify_native, nullptr));
        JSON = ts_value_make_object(jsonMap);
        process = ts_value_make_object(TsMap::Create());
        Buffer = ts_value_make_object(TsMap::Create());

        // Global functions
        parseInt = ts_value_make_native_function((void*)ts_parseInt_native, nullptr);
        parseFloat = ts_value_make_native_function((void*)ts_parseFloat_native, nullptr);

        // Node-like global object (minimal) - lodash needs many constructors
        TsMap* globalMap = TsMap::Create();
        
        // Helper to create a key
        auto makeKey = [](const char* name) {
            TsValue k;
            k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(name);
            return k;
        };
        
        // Add all built-in constructors that lodash expects
        if (Object) globalMap->Set(makeKey("Object"), *Object);
        if (Array) globalMap->Set(makeKey("Array"), *Array);
        if (Math) globalMap->Set(makeKey("Math"), *Math);
        if (parseInt) globalMap->Set(makeKey("parseInt"), *parseInt);
        if (parseFloat) globalMap->Set(makeKey("parseFloat"), *parseFloat);
        if (process) globalMap->Set(makeKey("process"), *process);
        if (Buffer) globalMap->Set(makeKey("Buffer"), *Buffer);
        if (JSON) globalMap->Set(makeKey("JSON"), *JSON);
        
        // Create stub constructors for types that lodash checks but we don't fully implement
        // These need .prototype property with proper methods to avoid issues
        auto makeConstructorWithPrototype = [&](const char* name, bool isFunction = false) -> TsValue* {
            // Create a function that acts as a constructor
            TsValue* ctor = ts_value_make_native_function((void*)ts_object_constructor_native, nullptr);
            TsFunction* func = (TsFunction*)ctor->ptr_val;
            if (!func->properties) {
                func->properties = TsMap::Create();
            }
            
            // Create prototype object with methods
            TsMap* protoMap = TsMap::Create();
            
            // Add toString method
            TsValue toStringKey = makeKey("toString");
            if (isFunction) {
                protoMap->Set(toStringKey, *ts_value_make_native_function((void*)ts_function_toString_native, nullptr));
            } else {
                protoMap->Set(toStringKey, *ts_value_make_native_function((void*)ts_object_toString_native, nullptr));
            }
            
            // Add valueOf method
            protoMap->Set(makeKey("valueOf"), *ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr));
            
            // Add hasOwnProperty for Object-like prototypes
            protoMap->Set(makeKey("hasOwnProperty"), *ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr));
            
            // Add .prototype property
            TsValue protoKey;
            protoKey.type = ValueType::STRING_PTR;
            protoKey.ptr_val = TsString::Create("prototype");
            func->properties->Set(protoKey, *ts_value_make_object(protoMap));
            return ctor;
        };
        
        // These constructors are accessed in lodash for .prototype
        TsValue* Function = makeConstructorWithPrototype("Function", true);  // isFunction=true for Function
        TsValue* String = makeConstructorWithPrototype("String");  
        TsValue* Date = makeConstructorWithPrototype("Date");
        TsValue* RegExp = makeConstructorWithPrototype("RegExp");
        TsValue* Error = makeConstructorWithPrototype("Error");
        TsValue* TypeError = makeConstructorWithPrototype("TypeError");
        TsValue* Symbol = makeConstructorWithPrototype("Symbol");
        TsValue* Map = makeConstructorWithPrototype("Map");
        TsValue* Set = makeConstructorWithPrototype("Set");
        TsValue* WeakMap = makeConstructorWithPrototype("WeakMap");
        TsValue* Promise = makeConstructorWithPrototype("Promise");
        
        globalMap->Set(makeKey("Function"), *Function);
        globalMap->Set(makeKey("String"), *String);
        globalMap->Set(makeKey("Date"), *Date);
        globalMap->Set(makeKey("RegExp"), *RegExp);
        globalMap->Set(makeKey("Error"), *Error);
        globalMap->Set(makeKey("TypeError"), *TypeError);
        globalMap->Set(makeKey("Symbol"), *Symbol);
        globalMap->Set(makeKey("Map"), *Map);
        globalMap->Set(makeKey("Set"), *Set);
        globalMap->Set(makeKey("WeakMap"), *WeakMap);
        globalMap->Set(makeKey("Promise"), *Promise);
        
        // Also add prototype to Array and Object with proper methods
        TsValue protoKey = makeKey("prototype");
        
        // Create Object.prototype with methods
        TsMap* objectProtoMap = TsMap::Create();
        objectProtoMap->Set(makeKey("toString"), *ts_value_make_native_function((void*)ts_object_toString_native, nullptr));
        objectProtoMap->Set(makeKey("valueOf"), *ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr));
        objectProtoMap->Set(makeKey("hasOwnProperty"), *ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr));
        objectFunc->properties->Set(protoKey, *ts_value_make_object(objectProtoMap));
        
        // Create Array.prototype with methods
        TsMap* arrayProtoMap = TsMap::Create();
        arrayProtoMap->Set(makeKey("toString"), *ts_value_make_native_function((void*)ts_object_toString_native, nullptr));
        arrayProtoMap->Set(makeKey("valueOf"), *ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr));
        arrayFunc->properties->Set(protoKey, *ts_value_make_object(arrayProtoMap));
        
        // Misc global values
        globalMap->Set(makeKey("undefined"), *ts_value_make_undefined());
        globalMap->Set(makeKey("NaN"), *ts_value_make_double(std::numeric_limits<double>::quiet_NaN()));
        globalMap->Set(makeKey("Infinity"), *ts_value_make_double(std::numeric_limits<double>::infinity()));
        globalMap->Set(makeKey("isNaN"), *ts_value_make_native_function((void*)ts_isNaN_native, nullptr));
        globalMap->Set(makeKey("isFinite"), *ts_value_make_native_function((void*)ts_isFinite_native, nullptr));

        global = ts_value_make_object(globalMap);
        // global.global === global
        globalMap->Set(makeKey("global"), *global);
    }

    void ts_module_register(TsValue* path, TsValue* exports) {
        if (!path || path->type != ValueType::STRING_PTR) return;
        TsString* s = (TsString*)path->ptr_val;
        std::string pathStr = s->ToUtf8();

        g_module_cache[pathStr] = exports;
    }

    TsValue* ts_module_get(const char* path) {
        std::string p = path;
        auto it = g_module_cache.find(p);
        if (it != g_module_cache.end()) {
            return it->second;
        }
        return nullptr;
    }

    TsValue* ts_require(TsValue* specifier, const char* referrerPath) {
        if (!specifier || specifier->type != ValueType::STRING_PTR) {
            return ts_value_make_undefined();
        }
        TsString* s = (TsString*)specifier->ptr_val;
        std::string spec = s->ToUtf8();

        try {
            fs::path resolved;
            std::string absPath;

            if (spec.rfind("./", 0) == 0 || spec.rfind("../", 0) == 0 || spec.rfind("/", 0) == 0) {
                resolved = fs::path(referrerPath).parent_path() / spec;
                absPath = finalize_module_path(resolved);
            } else {
                absPath = resolve_node_module(spec, referrerPath);
            }

            if (absPath.empty()) {
                absPath = finalize_module_path(spec);
            }

            if (absPath.empty()) {
                return ts_value_make_undefined();
            }

            // Handle JSON files: read and parse at runtime
            if (absPath.size() >= 5 && absPath.substr(absPath.size() - 5) == ".json") {
                // Read the JSON file
                std::ifstream file(absPath);
                if (!file.is_open()) {
                    return ts_value_make_undefined();
                }
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();
                
                // Parse the JSON content
                TsString* jsonStr = TsString::Create(content.c_str());
                extern void* ts_json_parse(void* json_str);
                TsValue* parsed = (TsValue*)ts_json_parse(jsonStr);
                if (parsed) {
                    // Cache it for future requires
                    TsValue* pathVal = ts_value_make_string(TsString::Create(absPath.c_str()));
                    ts_module_register(pathVal, parsed);
                    return parsed;
                }
                return ts_value_make_undefined();
            }

            TsValue* moduleObj = ts_module_get(absPath.c_str());
            if (moduleObj) {
                // CommonJS: return module.exports
                if (moduleObj->type == ValueType::OBJECT_PTR) {
                    // Use inline map operations to get "exports" property
                    TsString* exportsStr = TsString::Create("exports");
                    uint64_t hash = (uint64_t)exportsStr;
                    int64_t bucket = __ts_map_find_bucket(moduleObj->ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)exportsStr);
                    if (bucket >= 0) {
                        TsValue result;
                        __ts_map_get_value_at(moduleObj->ptr_val, bucket, reinterpret_cast<uint8_t*>(&result.type), &result.i_val);
                        if (result.type != ValueType::UNDEFINED) {
                            TsValue* exports = (TsValue*)ts_alloc(sizeof(TsValue));
                            *exports = result;
                            return exports;
                        }

                        // CommonJS default: module.exports starts as {}
                        // If it's missing/undefined, initialize it lazily.
                        TsMap* exportsMap = TsMap::Create();
                        TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                        TsValue exportsKey;
                        exportsKey.type = ValueType::STRING_PTR;
                        exportsKey.ptr_val = exportsStr;
                        ((TsMap*)moduleObj->ptr_val)->Set(exportsKey, *exportsBoxed);
                        return exportsBoxed;
                    }

                    // No exports key at all: initialize to {}
                    TsMap* exportsMap = TsMap::Create();
                    TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                    TsValue exportsKey;
                    exportsKey.type = ValueType::STRING_PTR;
                    exportsKey.ptr_val = exportsStr;
                    ((TsMap*)moduleObj->ptr_val)->Set(exportsKey, *exportsBoxed);
                    return exportsBoxed;
                }
                return moduleObj;
            }
        } catch (const std::exception& e) {
            // Swallow errors in requires to keep parity with JS runtime behavior
        }
        
        return ts_value_make_undefined();
    }
    
    // ============================================================
    // Inline IR Helpers - Scalar-based API to avoid struct passing
    // ============================================================
    
    // Get object's internal map pointer (TsMap::impl)
    void* __ts_object_get_map(void* obj) {
        if (!obj) return nullptr;

        // 1) Boxed TsValue* path
        uint8_t typeField = *(uint8_t*)obj;
        if (typeField <= 10) {
            TsValue* val = (TsValue*)obj;
            if (val->type == ValueType::OBJECT_PTR || val->type == ValueType::FUNCTION_PTR) {
                void* raw = val->ptr_val;
                if (!raw) return nullptr;

                uint32_t magic16 = *(uint32_t*)((char*)raw + 16);
                if (magic16 == TsMap::MAGIC) {
                    return raw;
                }
                if (magic16 == TsFunction::MAGIC) {
                    TsFunction* func = (TsFunction*)raw;
                    if (!func->properties) {
                        func->properties = TsMap::Create();
                    }
                    return func->properties;
                }
            }
            return nullptr;
        }

        // 2) Raw pointer path (already unboxed): TsMap* or TsFunction*
        uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
        if (magic16 == TsMap::MAGIC) {
            return obj;
        }
        if (magic16 == TsFunction::MAGIC) {
            TsFunction* func = (TsFunction*)obj;
            if (!func->properties) {
                func->properties = TsMap::Create();
            }
            return func->properties;
        }

        return nullptr;
    }
    
    // Convert value to property key (for number -> string coercion)
    void* __ts_value_to_property_key(uint8_t type, int64_t value) {
        ValueType vt = (ValueType)type;
        
        if (vt == ValueType::STRING_PTR) {
            // Already a string
            return (void*)value;
        }
        
        if (vt == ValueType::NUMBER_INT) {
            // Convert int to string
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)value);
            return TsString::Create(buf);
        }
        
        if (vt == ValueType::NUMBER_DBL) {
            // Convert double to string (value holds the bits)
            double d;
            memcpy(&d, &value, sizeof(double));
            char buf[32];
            snprintf(buf, sizeof(buf), "%.15g", d);
            return TsString::Create(buf);
        }
        
        // For other types, try to use toString or return null
        return nullptr;
    }
}
