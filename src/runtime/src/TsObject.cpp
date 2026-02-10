#define FMT_UNICODE 0
#define SPDLOG_COMPILED_LIB 1
#include "TsObject.h"
#include "TsArray.h"
#include "TsBigInt.h"
#include "TsMap.h"
#include "TsWeakMap.h"
#include "TsSet.h"
#include "TsWeakSet.h"
#include "TsJSON.h"
#include "TsString.h"
#include "TsBuffer.h"  // For TsTypedArray and TsDataView (TsBuffer itself uses virtual dispatch)
#include "TsEventEmitter.h"
#include "TsReadable.h"
#include "TsWritable.h"
#include "TsRegExp.h"
#include "TsBoundFunction.h"
#include "TsClosure.h"
#include "TsProxy.h"
#include "GC.h"
#include <gc/gc.h>  // For GC_base()
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
#ifdef _MSC_VER
#include <excpt.h>
#endif
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

// Global 'this' context for Function.prototype.call/apply support.
// Set before calling a function via .call(thisArg), read by function
// expressions that reference 'this'.
static void* ts_call_this_value = nullptr;

extern "C" {

void ts_set_call_this(void* thisArg) {
    ts_call_this_value = thisArg;
}

void* ts_get_call_this() {
    void* result = ts_call_this_value;
    ts_call_this_value = nullptr;  // Clear after read to avoid leaking
    return result;
}

TsValue* ts_value_make_undefined() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
    v->type = ValueType::UNDEFINED;
    v->ptr_val = nullptr;
    return v;
}

TsValue* ts_value_make_null() {
    TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
    memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
    v->type = ValueType::OBJECT_PTR; // Use OBJECT_PTR with nullptr for null
    v->ptr_val = nullptr;
    return v;
}

TsValue* ts_value_make_int(int64_t i) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::NUMBER_INT;
        v->i_val = i;
        return v;
    }

    TsValue* ts_value_make_double(double d) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::NUMBER_DBL;
        v->d_val = d;
        return v;
    }

    TsValue* ts_value_make_bool(bool b) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::BOOLEAN;
        v->b_val = b;
        return v;
    }

    TsValue* ts_value_make_string(void* s) {
        // Check if s is already a TsValue* to avoid double-boxing.
        // This happens when an any-typed value (TsValue* STRING_PTR from
        // ts_object_get_dynamic) is passed to a string-typed parameter in a
        // cross-module function call. The callee then calls ts_value_make_string
        // on what it thinks is a raw TsString* but is actually a TsValue*.
        if (s) {
            uintptr_t raw = (uintptr_t)s;
            if (raw >= 0x10000) {
                // TsValue has type enum (0-10) as first byte + 7 zero padding bytes.
                // First 8 bytes as uint64_t will be <= 10.
                // TsString has magic 0x53545247 at offset 0, which is much larger.
                uint64_t first8 = *(uint64_t*)s;
                if (first8 <= 10) {
                    // It's already a TsValue* - return as-is
                    return (TsValue*)s;
                }
            }
        }
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::STRING_PTR;
        v->ptr_val = s;
        return v;
    }

    TsValue* ts_value_make_object(void* o) {
        // Check if o is already a TsValue* to avoid double-boxing
        // This happens when union-typed parameters (e.g., string | null) receive
        // already-boxed values like ts_value_make_null() results
        if (o) {
            uintptr_t raw = (uintptr_t)o;
            if (raw >= 0x10000) {  // Guard against invalid pointers
                // Check for TsValue FIRST. A TsValue has type (0-10) in its first
                // uint64_t (type enum + 7 zero padding bytes). No real object has a
                // uint64_t value <= 10 at offset 0: vtable pointers are large addresses,
                // magic numbers like TsArray(0x41525259) and TsString(0x53545247) are > 10.
                // This must be checked BEFORE magic numbers because a TsValue's ptr_val
                // field (at offset 8) can accidentally match magic patterns like TsMap(0x4D415053).
                uint64_t first8 = *(uint64_t*)o;
                if (first8 <= 10) {
                    // Already a TsValue* (types 0-10), return as-is
                    return (TsValue*)o;
                }

                // Check magic numbers to avoid wrapping known types that should use
                // specific boxing functions (ts_value_make_array, ts_value_make_string, etc.)
                uint32_t magic = *(uint32_t*)o;
                uint32_t magic16 = *(uint32_t*)((char*)o + 16);
                bool isKnownNonObject =
                    magic == 0x41525259 || magic16 == 0x41525259 ||   // TsArray (magic at offset 0 or 16)
                    magic == 0x53545247;                               // TsString (magic at offset 0)
                // TsMap, TsSet, TsFunction, TsBuffer are objects - they should be wrapped
                // with OBJECT_PTR, so we don't skip boxing for them
                (void)isKnownNonObject;  // Reserved for future use
            }
        }

        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::OBJECT_PTR;
        v->ptr_val = o;
        return v;
    }

    TsValue* ts_value_make_function_object(void* fnObj) {
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::FUNCTION_PTR;
        v->ptr_val = fnObj;
        return v;
    }

    TsValue* ts_value_make_promise(void* promise) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(val, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        val->type = ValueType::PROMISE_PTR;
        val->ptr_val = promise;
        return val;
    }

    TsValue* ts_value_make_array(void* arr) {
        TsValue* val = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(val, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        val->type = ValueType::ARRAY_PTR;
        val->ptr_val = arr;
        return val;
    }

    TsValue* ts_ensure_boxed(void* v) {
        // Ensure that the value is boxed (TsValue*)
        // This is used for default parameters where inlining may change types
        // ts_value_make_object already handles detecting already-boxed values
        return ts_value_make_object(v);
    }

    bool ts_value_is_undefined(TsValue* v) {
        if (!v) {
            return true;
        }
        return v->type == ValueType::UNDEFINED;
    }

    bool ts_value_is_null(TsValue* v) {
        if (!v) {
            return false;  // null C++ pointer is not the same as JavaScript null
        }
        // Check for explicit null value (OBJECT_PTR with nullptr)
        if (v->type == ValueType::OBJECT_PTR && v->ptr_val == nullptr) {
            return true;
        }
        return false;
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
        // A TsValue has type (0-10) in byte 0 and 7 zero padding bytes,
        // so the first 8 bytes are a small value (0-10).
        // Real objects have a C++ vtable pointer which is a large address.
        uint64_t first8check = *(uint64_t*)ptr;
        if (first8check <= 10) {
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
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, -1);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
        v->type = ValueType::FUNCTION_PTR;
        v->ptr_val = func;
        return v;
    }

    TsValue* ts_value_make_function_with_arity(void* funcPtr, void* context, int arity) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, arity);
        TsValue* v = (TsValue*)ts_alloc(sizeof(TsValue));
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
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
        memset(v, 0, sizeof(TsValue));  // Zero padding bytes for ts_value_get_* detection
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

        // FIRST: Check if this is a boxed TsValue by examining the type field
        // TsValue has ValueType enum at offset 0, which is <= 10
        // HOWEVER: A vtable pointer's low byte could coincidentally be <= 10.
        // TsValue layout: [type(1)][padding(7)][union(8)] - bytes 1-7 should be 0 due to padding.
        // vtable pointer layout: [ptr(8)] - bytes 1-7 are part of the address (typically non-zero on 64-bit).
        // So we check that bytes 1-3 are zero to distinguish TsValue from vtable pointer.
        uint8_t typeField = *(uint8_t*)v;
        uint8_t byte1 = *((uint8_t*)v + 1);
        uint8_t byte2 = *((uint8_t*)v + 2);
        uint8_t byte3 = *((uint8_t*)v + 3);

        // A TsValue has type <= 10 AND bytes 1-3 are zero (padding)
        // A vtable pointer has first byte that may be anything, but bytes 1-3 are part of address (non-zero on 64-bit)
        if (typeField <= 10 && byte1 == 0 && byte2 == 0 && byte3 == 0) {
            // It's a proper boxed TsValue - extract ptr_val
            if (v->type == ValueType::OBJECT_PTR ||
                v->type == ValueType::ARRAY_PTR ||
                v->type == ValueType::PROMISE_PTR ||
                v->type == ValueType::FUNCTION_PTR ||
                v->type == ValueType::BIGINT_PTR ||
                v->type == ValueType::SYMBOL_PTR ||
                v->type == ValueType::STRING_PTR) {  // Also extract STRING_PTR for string values
                return v->ptr_val;
            }
            // It's a TsValue but not an object type (e.g., int, bool, undefined)
            return nullptr;
        }

        // Not a TsValue - check for raw object pointers by magic numbers
        // Check magic at offset 0 for structs without virtual methods (TsArray layout: [magic(4)] ...)
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x41525259 || // TsArray::MAGIC
            magic == 0x4D415053 || // TsMap::MAGIC
            magic == 0x53455453) { // TsSet::MAGIC
            return v;
        }
        if (magic == 0x53545247) { // TsString::MAGIC - return the string pointer
            return v;  // TsString* is already "unboxed", return as-is
        }

        // Check offset 8 for objects with different layout
        uint32_t magic8 = *(uint32_t*)((char*)v + 8);
        if (magic8 == 0x4D415053 || magic8 == 0x42554646 ||
            magic8 == 0x53455453 || magic8 == 0x46554E43 ||
            magic8 == 0x534F434B) { // TsSocket::MAGIC "SOCK"
            return v;
        }

        // Check for TsObject-derived classes with vtable
        // Layout: [C++ vtable ptr (8)] [explicit vtable (8)] [TsObject::magic (4)] ...
        // So TsObject::magic is at offset 16
        uint32_t magic16 = *(uint32_t*)((char*)v + 16);
        if (magic16 == 0x4D415053 || // TsMap::MAGIC
            magic16 == 0x53455453 || // TsSet::MAGIC
            magic16 == 0x41525259 || // TsArray::MAGIC
            magic16 == 0x46554E43 || // TsFunction::MAGIC
            magic16 == 0x42554646 || // TsBuffer::MAGIC
            magic16 == 0x534F434B || // TsSocket::MAGIC "SOCK"
            magic16 == 0x55445053 || // TsUDPSocket::MAGIC "UDPS"
            magic16 == 0x45564E54) { // TsEventEmitter::MAGIC "EVNT"
            return v;
        }

        // Unknown - assume it's an object pointer (fallback)
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

        uint32_t magic8b = *(uint32_t*)((char*)rawPtr + 8);
        if (magic8b == 0x42554646) { // TsBuffer::MAGIC
            TsBuffer* buf = (TsBuffer*)rawPtr;
            if (index < 0 || (size_t)index >= buf->GetLength()) {
                return ts_value_make_undefined();
            }
            return ts_value_make_int(buf->GetData()[index]);
        }

        uint32_t magic16b = *(uint32_t*)((char*)rawPtr + 16);
        if (magic16b == 0x42554646) { // TsBuffer::MAGIC
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

    // Native wrappers for string methods (ctx = TsString*)
    static TsValue* ts_string_startsWith_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* prefix = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!prefix) prefix = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_bool(ts_string_startsWith(str, prefix));
    }
    static TsValue* ts_string_endsWith_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* suffix = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!suffix) suffix = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // Implement endsWith inline since the extern C function may not exist
        if (!suffix) return ts_value_make_bool(true);
        TsString* suffixStr = (TsString*)suffix;
        int64_t strLen = str->Length();
        int64_t suffixLen = suffixStr->Length();
        if (suffixLen > strLen) return ts_value_make_bool(false);
        TsString* tail = (TsString*)ts_string_slice(str, strLen - suffixLen, strLen);
        return ts_value_make_bool(ts_string_eq(tail, suffixStr));
    }
    static TsValue* ts_string_includes_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* search = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!search) search = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_bool(ts_string_includes(str, search));
    }
    static TsValue* ts_string_indexOf_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* search = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!search) search = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_int(ts_string_indexOf(str, search));
    }
    static TsValue* ts_string_substring_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t end = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : ts_string_length(str);
        return ts_value_make_string((TsString*)ts_string_substring(str, start, end));
    }
    static TsValue* ts_string_slice_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t end = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : ts_string_length(str);
        return ts_value_make_string((TsString*)ts_string_slice(str, start, end));
    }
    static TsValue* ts_string_toLowerCase_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_toLowerCase((TsString*)ctx));
    }
    static TsValue* ts_string_toUpperCase_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_toUpperCase((TsString*)ctx));
    }
    static TsValue* ts_string_trim_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_trim((TsString*)ctx));
    }
    static TsValue* ts_string_split_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* sep = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!sep) sep = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_object(ts_string_split(str, sep));
    }
    static TsValue* ts_string_replace_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* pattern = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!pattern) pattern = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* replacement = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        if (!replacement) replacement = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_string((TsString*)ts_string_replace(str, pattern, replacement));
    }
    static TsValue* ts_string_repeat_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t count = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_string_repeat(str, count));
    }
    static TsValue* ts_string_charAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_string_charAt(str, index));
    }
    static TsValue* ts_string_charCodeAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_int(ts_string_charCodeAt(str, index));
    }
    static TsValue* ts_string_padStart_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t targetLength = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        void* padString = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_padStart(str, targetLength, padString));
    }
    static TsValue* ts_string_padEnd_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t targetLength = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        void* padString = (argc >= 2 && argv && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_padEnd(str, targetLength, padString));
    }

    // Native wrapper for number.toFixed() - ctx is a TsValue* containing the number
    static TsValue* ts_number_toString_native(void* ctx, int argc, TsValue** argv) {
        TsValue* numVal = (TsValue*)ctx;
        double value = 0.0;
        if (numVal->type == ValueType::NUMBER_INT) {
            value = (double)numVal->i_val;
        } else if (numVal->type == ValueType::NUMBER_DBL) {
            value = numVal->d_val;
        }
        int64_t radix = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 10;
        return ts_value_make_string((TsString*)ts_number_to_string(value, radix));
    }

    static TsValue* ts_number_toFixed_native(void* ctx, int argc, TsValue** argv) {
        TsValue* numVal = (TsValue*)ctx;
        double value = 0.0;
        if (numVal->type == ValueType::NUMBER_INT) {
            value = (double)numVal->i_val;
        } else if (numVal->type == ValueType::NUMBER_DBL) {
            value = numVal->d_val;
        }
        int64_t digits = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_number_to_fixed(value, digits));
    }

    // Helper: try implicit conversion through virtual base chain to find TsObject
    // For stream classes (TsReadable/TsWritable), TsObject is a virtual base NOT at offset 0.
    // We use the C++ implicit conversion which follows the vbtable to find the virtual base.
    static TsValue ts_try_virtual_dispatch_via_vbase(void* obj, const char* keyStr) {
        TsValue undefined;
        undefined.type = ValueType::UNDEFINED;
        undefined.i_val = 0;

        // Guard: skip virtual dispatch for non-heap pointers.
        // Raw integers, code section pointers, and static data pointers would crash
        // when we try to follow vtable chains via reinterpret_cast.
        if (!GC_base(obj)) {
            return undefined;
        }

        // DISABLED: The vbase dispatch path uses reinterpret_cast<TsReadable*> which reads
        // arbitrary GC memory as vtable pointers, causing NX faults that SEH cannot reliably
        // catch. No stream class actually implements GetPropertyVirtual() with real behavior -
        // only TsPromise does, and it inherits directly from TsObject (no virtual inheritance),
        // so the direct cast fallback in ts_try_virtual_property_dispatch handles it correctly.
        (void)keyStr; // suppress unused warning
        return undefined;
    }

    // Separate function for virtual property dispatch to allow __try/__except on MSVC
    // (cannot mix __try with C++ objects that have destructors in the same function)
    static TsValue ts_try_virtual_property_dispatch(void* obj, const char* keyStr) {
        TsValue undefined;
        undefined.type = ValueType::UNDEFINED;
        undefined.i_val = 0;

        // First try virtual base dispatch (handles stream classes with virtual inheritance)
        // Must be tried FIRST because the direct (TsObject*)obj cast below reads the wrong
        // vtable slot for virtual-inheritance classes and may return garbage UNDEFINED
        // without crashing (so __except is never triggered).
        TsValue vbaseResult = ts_try_virtual_dispatch_via_vbase(obj, keyStr);
        if (vbaseResult.type != ValueType::UNDEFINED) {
            return vbaseResult;
        }

        // Fall back to direct cast - ONLY for TsPromise which is the only TsObject subclass
        // that implements GetPropertyVirtual() with real behavior.
        // We must check magic at offset 16 (TsObject::magic field) to identify TsPromise.
        // Safety: first byte distinguishes TsObject (vtable ptr, large address) from TsValue
        // (ValueType enum, 0-10). We only proceed if it looks like a TsObject.
        if (GC_base(obj)) {
            uint8_t firstByte = *(uint8_t*)obj;
            if (firstByte > 10) {
                // Looks like a vtable pointer (TsObject-derived), not a TsValue enum
                uint32_t magic16 = *(uint32_t*)((uint8_t*)obj + 16);
                if (magic16 == 0x50524F4D) { // TsPromise::MAGIC "PROM"
                    TsObject* tsObj = (TsObject*)obj;
                    TsValue result = tsObj->GetPropertyVirtual(keyStr);
                    if (result.type != ValueType::UNDEFINED) {
                        return result;
                    }
                }
            }
        }
        return undefined;
    }

    TsValue* ts_object_get_property(void* obj, const char* keyStr) {
        if (!obj) {
            return ts_value_make_undefined();
        }

        if (!keyStr) {
            return ts_value_make_undefined();
        }

        // Guard: reject obviously invalid pointers (raw integers, null-ish values).
        // When the compiler does expr.toString() on a number result, the integer value
        // gets passed as 'obj' (e.g., 0x1D = 29). Dereferencing that would crash.
        uintptr_t addr = (uintptr_t)obj;
        if (addr < 0x10000) {
            return ts_value_make_undefined();
        }

        // Try to detect TsValue* at the start
        // TsValue has type enum (0-10) at offset 0, then 7 zero padding bytes, then value at offset 8
        // Real object pointers have a C++ vtable pointer at offset 0 which is a large 8-byte address
        // Use the full 8-byte value to distinguish: TsValue type+padding = 0-10, vtable ptr = large address
        uint64_t first8 = *(uint64_t*)obj;
        if (first8 <= 10) {  // TsValue: type (0-10) + 7 zero padding bytes = small value
            TsValue* maybeVal = (TsValue*)obj;
            // Additional check: for UNDEFINED, ptr_val should be null/zero
            if (maybeVal->type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
            // Handle NUMBER_INT - dispatch number methods (toString, toFixed, etc.)
            if (maybeVal->type == ValueType::NUMBER_INT) {
                if (strcmp(keyStr, "toString") == 0) {
                    return ts_value_make_native_function((void*)ts_number_toString_native, obj);
                }
                if (strcmp(keyStr, "toFixed") == 0) {
                    return ts_value_make_native_function((void*)ts_number_toFixed_native, obj);
                }
                return ts_value_make_undefined();
            }
            // Handle NUMBER_DBL - dispatch number methods
            if (maybeVal->type == ValueType::NUMBER_DBL) {
                if (strcmp(keyStr, "toString") == 0) {
                    return ts_value_make_native_function((void*)ts_number_toString_native, obj);
                }
                if (strcmp(keyStr, "toFixed") == 0) {
                    return ts_value_make_native_function((void*)ts_number_toFixed_native, obj);
                }
                return ts_value_make_undefined();
            }
            // For object types, unwrap if ptr_val is valid (non-zero)
            if ((maybeVal->type == ValueType::OBJECT_PTR || maybeVal->type == ValueType::ARRAY_PTR ||
                 maybeVal->type == ValueType::PROMISE_PTR || maybeVal->type == ValueType::STRING_PTR) && maybeVal->ptr_val) {
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
            if (strcmp(keyStr, "hasIndices") == 0) {
                return ts_value_make_bool(re->HasIndices());
            }
            if (strcmp(keyStr, "lastIndex") == 0) {
                return ts_value_make_int(re->GetLastIndex());
            }
            return ts_value_make_undefined();
        }

        // Check for TsRegExpMatchArray (magic at offset 0) - RegExp.exec() result
        if (magic0 == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            TsRegExpMatchArray* match = (TsRegExpMatchArray*)obj;
            if (strcmp(keyStr, "index") == 0) {
                return ts_value_make_int(match->GetMatchIndex());
            }
            if (strcmp(keyStr, "input") == 0) {
                return ts_value_make_string(match->GetInput());
            }
            if (strcmp(keyStr, "indices") == 0) {
                TsArray* indices = match->GetIndices();
                if (indices) {
                    return ts_value_make_object(indices);
                }
                return ts_value_make_undefined();
            }
            if (strcmp(keyStr, "groups") == 0) {
                void* groups = match->GetGroups();
                if (groups) {
                    return ts_value_make_object(groups);
                }
                return ts_value_make_undefined();
            }
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(match->Length());
            }
            // Check for numeric index - delegate to underlying matches array
            char* endptr;
            long index = strtol(keyStr, &endptr, 10);
            if (*endptr == '\0' && index >= 0 && index < match->Length()) {
                return (TsValue*)match->Get((size_t)index);
            }
            return ts_value_make_undefined();
        }

        // Check for TsTypedArray (magic at offset 16 - after C++ vtable (8) + TsObject::vtable (8))
        if (magic16 == 0x54415252) { // TsTypedArray::MAGIC ("TARR")
            TsTypedArray* ta = (TsTypedArray*)obj;
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int((int64_t)ta->GetLength());
            }
            if (strcmp(keyStr, "byteLength") == 0) {
                return ts_value_make_int((int64_t)ta->GetByteLength());
            }
            if (strcmp(keyStr, "byteOffset") == 0) {
                return ts_value_make_int((int64_t)ta->GetByteOffset());
            }
            if (strcmp(keyStr, "BYTES_PER_ELEMENT") == 0) {
                return ts_value_make_int((int64_t)ta->GetElementSize());
            }
            if (strcmp(keyStr, "buffer") == 0) {
                return ts_value_make_object(ta->GetBuffer());
            }
            // Check for numeric index
            char* endptr;
            long index = strtol(keyStr, &endptr, 10);
            if (*endptr == '\0' && index >= 0) {
                return ts_value_make_double(ta->Get((size_t)index));
            }
            return ts_value_make_undefined();
        }

        // Check for TsMap (magic at offset 16 after vtables) - also try offset 20 and 24
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsMap* map = (TsMap*)obj;

            // Walk the prototype chain looking for the property
            TsMap* currentMap = map;
            while (currentMap != nullptr) {
                // First check for a getter (__getter_<propertyName>)
                std::string getterKey = std::string("__getter_") + keyStr;
                TsValue gk;
                gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterKey.c_str());
                TsValue getterVal = currentMap->Get(gk);
                if (getterVal.type != ValueType::UNDEFINED) {
                    // Found a getter - invoke it with 'this' as the ORIGINAL object
                    TsValue* boxedObj = (TsValue*)ts_alloc(sizeof(TsValue));
                    boxedObj->type = ValueType::OBJECT_PTR;
                    boxedObj->ptr_val = obj;  // Use original object for 'this'
                    return ts_function_call_with_this(&getterVal, boxedObj, 0, nullptr);
                }

                // No getter - look up the property directly
                TsValue k;
                k.type = ValueType::STRING_PTR;
                k.ptr_val = TsString::GetInterned(keyStr);
                TsValue val = currentMap->Get(k);
                if (val.type != ValueType::UNDEFINED) {
                    return ts_property_return_value(val);
                }

                // Move to the next prototype in the chain
                currentMap = currentMap->GetPrototype();
            }

            // If not found in the prototype chain, check Object.prototype methods
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
            TsString* strObj = (TsString*)obj;
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(strObj->Length());
            }
            // Return native function wrappers for string methods
            if (strcmp(keyStr, "startsWith") == 0) return ts_value_make_native_function((void*)ts_string_startsWith_native, strObj);
            if (strcmp(keyStr, "endsWith") == 0) return ts_value_make_native_function((void*)ts_string_endsWith_native, strObj);
            if (strcmp(keyStr, "includes") == 0) return ts_value_make_native_function((void*)ts_string_includes_native, strObj);
            if (strcmp(keyStr, "indexOf") == 0) return ts_value_make_native_function((void*)ts_string_indexOf_native, strObj);
            if (strcmp(keyStr, "substring") == 0) return ts_value_make_native_function((void*)ts_string_substring_native, strObj);
            if (strcmp(keyStr, "slice") == 0) return ts_value_make_native_function((void*)ts_string_slice_native, strObj);
            if (strcmp(keyStr, "toLowerCase") == 0) return ts_value_make_native_function((void*)ts_string_toLowerCase_native, strObj);
            if (strcmp(keyStr, "toUpperCase") == 0) return ts_value_make_native_function((void*)ts_string_toUpperCase_native, strObj);
            if (strcmp(keyStr, "trim") == 0) return ts_value_make_native_function((void*)ts_string_trim_native, strObj);
            if (strcmp(keyStr, "split") == 0) return ts_value_make_native_function((void*)ts_string_split_native, strObj);
            if (strcmp(keyStr, "replace") == 0) return ts_value_make_native_function((void*)ts_string_replace_native, strObj);
            if (strcmp(keyStr, "repeat") == 0) return ts_value_make_native_function((void*)ts_string_repeat_native, strObj);
            if (strcmp(keyStr, "charAt") == 0) return ts_value_make_native_function((void*)ts_string_charAt_native, strObj);
            if (strcmp(keyStr, "charCodeAt") == 0) return ts_value_make_native_function((void*)ts_string_charCodeAt_native, strObj);
            if (strcmp(keyStr, "padStart") == 0) return ts_value_make_native_function((void*)ts_string_padStart_native, strObj);
            if (strcmp(keyStr, "padEnd") == 0) return ts_value_make_native_function((void*)ts_string_padEnd_native, strObj);
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_string(strObj);
            if (strcmp(keyStr, "valueOf") == 0) return ts_value_make_string(strObj);
            return ts_value_make_undefined();
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

            // Function.prototype.bind: create a bound function wrapper
            if (strcmp(keyStr, "bind") == 0) {
                TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                target->type = ValueType::OBJECT_PTR;
                target->ptr_val = func;
                return ts_value_make_native_function((void*)ts_function_bind_native, (void*)target);
            }
            
            return ts_value_make_undefined();
        }

        // Virtual property dispatch for polymorphic TsObject subclasses
        // (e.g., TsBuffer, TsIncomingMessage) that override GetPropertyVirtual()
        // Only attempt this on objects that are NOT known non-TsObject types
        // (TsArray, TsString are NOT TsObject subclasses and would crash on virtual call)
        if (magic0 != 0x41525259 && magic8 != 0x41525259 && magic16 != 0x41525259 &&  // TsArray
            magic0 != 0x53545247 && magic8 != 0x53545247 &&                              // TsString
            magic0 != 0x52454758 &&                                                       // TsRegExp
            magic8 != 0x48454144 && magic16 != 0x48454144) {                              // TsHeaders
            TsValue result = ts_try_virtual_property_dispatch(obj, keyStr);
            if (result.type != ValueType::UNDEFINED) {
                return ts_property_return_value(result);
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

    // Helper to check if a value is a TsProxy
    static TsProxy* ts_extract_proxy(TsValue* boxedFunc) {
        if (!boxedFunc) return nullptr;
        if (boxedFunc->type != ValueType::OBJECT_PTR) return nullptr;
        void* ptr = boxedFunc->ptr_val;
        if (!ptr) return nullptr;

        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)ptr);
        return proxy;
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

    // Helper to call a function with up to 3 args, respecting the function's declared arity
    // This is critical for Array.map/filter/etc where JS allows callbacks with fewer params
    TsValue* ts_call_with_arity(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }

        // Use the function's declared arity if known, otherwise default to 3
        int arity = func->arity;
        if (arity < 0) {
            // Arity unknown - default to all 3 args (original behavior)
            arity = 3;
        }

        switch (arity) {
            case 0:
                return ts_call_0(boxedFunc);
            case 1:
                return ts_call_1(boxedFunc, arg1);
            case 2:
                return ts_call_2(boxedFunc, arg1, arg2);
            case 3:
            default:
                return ts_call_3(boxedFunc, arg1, arg2, arg3);
        }
    }

    // Helper to check if a funcPtr inside a TsFunction is actually a TsClosure
    // This happens when ts_value_make_function wraps a closure pointer
    static TsClosure* ts_funcptr_as_closure(void* funcPtr) {
        if (!funcPtr) return nullptr;
        // Only check magic if the pointer is in the GC heap (not a code pointer)
        if (!GC_base(funcPtr)) return nullptr;
        TsObject* obj = (TsObject*)funcPtr;
        if (obj->magic == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)obj;
        }
        return nullptr;
    }

    // Helper to extract TsClosure from a boxed or raw value
    static TsClosure* ts_extract_closure(TsValue* boxedFunc) {
        if (!boxedFunc) return nullptr;

        // Check if boxedFunc is a raw TsClosure pointer (magic at offset 8)
        TsObject* obj = (TsObject*)boxedFunc;
        if (obj && obj->magic == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)obj;
        }

        // Check if boxedFunc is a TsValue wrapping a TsClosure (OBJECT_PTR)
        if (boxedFunc->type == ValueType::OBJECT_PTR && boxedFunc->ptr_val) {
            TsObject* inner = (TsObject*)boxedFunc->ptr_val;
            if (inner && inner->magic == 0x434C5352) {  // 'CLSR'
                return (TsClosure*)inner;
            }
        }

        return nullptr;
    }

    TsValue* ts_call_0(TsValue* boxedFunc) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn0)(void*);
            return ((Fn0)closure->func_ptr)(closure);
        }

        // Check for Proxy
        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            // Create empty args array and call apply trap
            TsArray* argsArr = TsArray::Create(0);
            return proxy->apply(nullptr, (TsValue*)argsArr, 0);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            auto result = ((TsFunctionPtr)func->funcPtr)(func->context, 0, nullptr);
            return result;
        } else {
            // Check if funcPtr is actually a TsClosure (wrapped via ts_value_make_function)
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn0)(void*);
                return ((Fn0)innerClosure->func_ptr)(innerClosure);
            }
            typedef TsValue* (*Fn0)(void*);
            auto result = ((Fn0)func->funcPtr)(func->context);
            return result;
        }
    }

    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            // Call the closure's function with the closure as context
            typedef TsValue* (*Fn1)(void*, TsValue*);
            return ((Fn1)closure->func_ptr)(closure, arg1);
        }

        // Check for Proxy
        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(1);
            argsArr->Push((int64_t)arg1);
            return proxy->apply(nullptr, (TsValue*)argsArr, 1);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[1] = { arg1 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 1, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn1)(void*, TsValue*);
                return ((Fn1)innerClosure->func_ptr)(innerClosure, arg1);
            }
            typedef TsValue* (*Fn1)(void*, TsValue*);
            return ((Fn1)func->funcPtr)(func->context, arg1);
        }
    }

    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
            return ((Fn2)closure->func_ptr)(closure, arg1, arg2);
        }

        // Check for Proxy
        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(2);
            argsArr->Push((int64_t)arg1);
            argsArr->Push((int64_t)arg2);
            return proxy->apply(nullptr, (TsValue*)argsArr, 2);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[2] = { arg1, arg2 };
            TsValue* result = ((TsFunctionPtr)func->funcPtr)(func->context, 2, argv);
            return result;
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
                return ((Fn2)innerClosure->func_ptr)(innerClosure, arg1, arg2);
            }
            typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
            TsValue* result = ((Fn2)func->funcPtr)(func->context, arg1, arg2);
            return result;
        }
    }

    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
            return ((Fn3)closure->func_ptr)(closure, arg1, arg2, arg3);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(3);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3);
            return proxy->apply(nullptr, (TsValue*)argsArr, 3);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[3] = { arg1, arg2, arg3 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 3, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
                return ((Fn3)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3);
            }
            typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
            return ((Fn3)func->funcPtr)(func->context, arg1, arg2, arg3);
        }
    }

    TsValue* ts_call_4(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn4)closure->func_ptr)(closure, arg1, arg2, arg3, arg4);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(4);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4);
            return proxy->apply(nullptr, (TsValue*)argsArr, 4);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[4] = { arg1, arg2, arg3, arg4 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 4, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn4)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4);
            }
            typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn4)func->funcPtr)(func->context, arg1, arg2, arg3, arg4);
        }
    }

    TsValue* ts_call_5(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn5)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(5);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5);
            return proxy->apply(nullptr, (TsValue*)argsArr, 5);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[5] = { arg1, arg2, arg3, arg4, arg5 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 5, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn5)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5);
            }
            typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn5)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5);
        }
    }

    TsValue* ts_call_6(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn6)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(6);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5); argsArr->Push((int64_t)arg6);
            return proxy->apply(nullptr, (TsValue*)argsArr, 6);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 6, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn6)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5, arg6);
            }
            typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn6)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6);
        }
    }

    TsValue* ts_call_7(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn7)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn7)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(7);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5); argsArr->Push((int64_t)arg6); argsArr->Push((int64_t)arg7);
            return proxy->apply(nullptr, (TsValue*)argsArr, 7);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 7, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn7)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn7)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
            }
            typedef TsValue* (*Fn7)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn7)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
        }
    }

    TsValue* ts_call_8(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        // Check for TsClosure first (raw closure pointer)
        TsObject* obj = (TsObject*)boxedFunc;
        if (obj && obj->magic == 0x434C5352) {  // 'CLSR'
            TsClosure* closure = (TsClosure*)obj;
            typedef TsValue* (*Fn8)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn8)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(8);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5); argsArr->Push((int64_t)arg6); argsArr->Push((int64_t)arg7); argsArr->Push((int64_t)arg8);
            return proxy->apply(nullptr, (TsValue*)argsArr, 8);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[8] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 8, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn8)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn8)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
            }
            typedef TsValue* (*Fn8)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn8)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
        }
    }

    TsValue* ts_call_9(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9) {
        // Check for TsClosure first (raw closure pointer)
        TsObject* obj = (TsObject*)boxedFunc;
        if (obj && obj->magic == 0x434C5352) {  // 'CLSR'
            TsClosure* closure = (TsClosure*)obj;
            typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn9)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(9);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5); argsArr->Push((int64_t)arg6); argsArr->Push((int64_t)arg7); argsArr->Push((int64_t)arg8); argsArr->Push((int64_t)arg9);
            return proxy->apply(nullptr, (TsValue*)argsArr, 9);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[9] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 9, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn9)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
            }
            typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn9)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
        }
    }

    TsValue* ts_call_10(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9, TsValue* arg10) {
        // Check for TsClosure first (raw closure pointer)
        TsObject* obj = (TsObject*)boxedFunc;
        if (obj && obj->magic == 0x434C5352) {  // 'CLSR'
            TsClosure* closure = (TsClosure*)obj;
            typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn10)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
        }

        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create(10);
            argsArr->Push((int64_t)arg1); argsArr->Push((int64_t)arg2); argsArr->Push((int64_t)arg3); argsArr->Push((int64_t)arg4); argsArr->Push((int64_t)arg5); argsArr->Push((int64_t)arg6); argsArr->Push((int64_t)arg7); argsArr->Push((int64_t)arg8); argsArr->Push((int64_t)arg9); argsArr->Push((int64_t)arg10);
            return proxy->apply(nullptr, (TsValue*)argsArr, 10);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();
        if (func->type == FunctionType::NATIVE) {
            TsValue* argv[10] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10 };
            return ((TsFunctionPtr)func->funcPtr)(func->context, 10, argv);
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            if (innerClosure) {
                typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((Fn10)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
            }
            typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn10)func->funcPtr)(func->context, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
        }
    }

    // ts_call_with_this_X functions: call a function with a specific 'this' binding
    // These temporarily patch the function's context before calling
    TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn0)(void*);
            return ((Fn0)closure->func_ptr)(closure);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_0(boxedFunc);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_1(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn1)(void*, TsValue*);
            return ((Fn1)closure->func_ptr)(closure, arg1);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_1(boxedFunc, arg1);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_2(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn2)(void*, TsValue*, TsValue*);
            return ((Fn2)closure->func_ptr)(closure, arg1, arg2);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_2(boxedFunc, arg1, arg2);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_3(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn3)(void*, TsValue*, TsValue*, TsValue*);
            return ((Fn3)closure->func_ptr)(closure, arg1, arg2, arg3);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_3(boxedFunc, arg1, arg2, arg3);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_4(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn4)closure->func_ptr)(closure, arg1, arg2, arg3, arg4);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_4(boxedFunc, arg1, arg2, arg3, arg4);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_5(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn5)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_5(boxedFunc, arg1, arg2, arg3, arg4, arg5);

        if (patchedCtx) func->context = savedCtx;
        return result;
    }

    TsValue* ts_call_with_this_6(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((Fn6)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6);
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return ts_value_make_undefined();

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_6(boxedFunc, arg1, arg2, arg3, arg4, arg5, arg6);

        if (patchedCtx) func->context = savedCtx;
        return result;
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

        // Check if this is a Proxy - dispatch through ownKeys trap
        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawPtr);
        if (proxy) {
            return proxy->ownKeys();
        }

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

    // Object.is(value1, value2) - ES6 SameValue comparison
    // Differs from === in that:
    // - Object.is(NaN, NaN) returns true
    // - Object.is(0, -0) returns false
    bool ts_object_is(TsValue* val1, TsValue* val2) {
        if (!val1 && !val2) return true;
        if (!val1 || !val2) return false;

        // If they're the exact same pointer, they're the same
        if (val1 == val2) return true;

        // Get the types
        ValueType type1 = val1->type;
        ValueType type2 = val2->type;

        // Different types are never the same value
        if (type1 != type2) return false;

        switch (type1) {
            case ValueType::UNDEFINED:
                // undefined === undefined
                return true;

            case ValueType::BOOLEAN:
                return val1->b_val == val2->b_val;

            case ValueType::NUMBER_INT:
                return val1->i_val == val2->i_val;

            case ValueType::NUMBER_DBL: {
                double d1 = val1->d_val;
                double d2 = val2->d_val;

                // Handle NaN - NaN is equal to NaN in SameValue
                bool isNaN1 = (d1 != d1);
                bool isNaN2 = (d2 != d2);
                if (isNaN1 && isNaN2) return true;
                if (isNaN1 || isNaN2) return false;

                // Handle +0 and -0 - they are different in SameValue
                if (d1 == 0.0 && d2 == 0.0) {
                    // Check sign bit using bit manipulation
                    // 1/+0 = +Infinity, 1/-0 = -Infinity
                    bool isNeg1 = (1.0 / d1) < 0.0;
                    bool isNeg2 = (1.0 / d2) < 0.0;
                    return isNeg1 == isNeg2;
                }

                return d1 == d2;
            }

            case ValueType::STRING_PTR: {
                TsString* str1 = (TsString*)val1->ptr_val;
                TsString* str2 = (TsString*)val2->ptr_val;
                if (!str1 && !str2) return true;
                if (!str1 || !str2) return false;
                // Compare strings by value
                const char* s1 = str1->ToUtf8();
                const char* s2 = str2->ToUtf8();
                return strcmp(s1, s2) == 0;
            }

            case ValueType::OBJECT_PTR:
            case ValueType::ARRAY_PTR:
            case ValueType::FUNCTION_PTR:
            case ValueType::PROMISE_PTR:
            case ValueType::BIGINT_PTR:
            case ValueType::SYMBOL_PTR:
                // For objects, arrays, functions, etc. compare by reference
                return val1->ptr_val == val2->ptr_val;

            default:
                return false;
        }
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
    TsValue* ts_object_getPrototypeOf(TsValue* obj) {
        if (!obj) return ts_value_make_undefined();

        // Unbox obj if needed
        void* objRaw = ts_value_get_object(obj);
        if (!objRaw) objRaw = obj;

        // Check if obj is a TsMap
        uint32_t magic = *(uint32_t*)((char*)objRaw + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            TsMap* objMap = (TsMap*)objRaw;
            TsMap* proto = objMap->GetPrototype();
            if (proto) {
                return ts_value_make_object(proto);
            }
            // null prototype - return null
            return ts_value_make_null();
        }

        // For non-TsMap objects, return null (no prototype chain for them yet)
        return ts_value_make_null();
    }

    // Object.create(proto) - creates new object with specified prototype
    // Creates a new empty object with its [[Prototype]] set to proto
    TsValue* ts_object_create(TsValue* proto) {
        // Create a new empty map
        TsMap* newObj = TsMap::Create();

        // If proto is null/undefined, return object with no prototype
        if (!proto || ts_value_is_nullish(proto)) {
            // Object.create(null) - no prototype chain
            newObj->SetPrototype(nullptr);
            return ts_value_make_object(newObj);
        }

        // Unbox proto if needed
        void* protoRaw = ts_value_get_object(proto);
        if (!protoRaw) protoRaw = proto;

        // Check if proto is a TsMap
        uint32_t magic = *(uint32_t*)((char*)protoRaw + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            // Set the prototype chain properly (don't copy properties)
            TsMap* protoMap = (TsMap*)protoRaw;
            newObj->SetPrototype(protoMap);
        }

        return ts_value_make_object(newObj);
    }

    // Object.setPrototypeOf(obj, proto) - sets the prototype of an object
    TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto) {
        if (!obj) return ts_value_make_undefined();

        // Unbox obj if needed
        void* objRaw = ts_value_get_object(obj);
        if (!objRaw) objRaw = obj;

        // Check if obj is a TsMap
        uint32_t magic = *(uint32_t*)((char*)objRaw + 16);
        if (magic != 0x4D415053) { // TsMap::MAGIC
            return obj;  // Not a TsMap, return unchanged
        }

        TsMap* objMap = (TsMap*)objRaw;

        // If proto is null/undefined, clear the prototype
        if (!proto || ts_value_is_nullish(proto)) {
            objMap->SetPrototype(nullptr);
            return obj;
        }

        // Unbox proto if needed
        void* protoRaw = ts_value_get_object(proto);
        if (!protoRaw) protoRaw = proto;

        // Check if proto is a TsMap
        uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
        if (protoMagic == 0x4D415053) { // TsMap::MAGIC
            TsMap* protoMap = (TsMap*)protoRaw;

            // Check for prototype chain cycles
            if (objMap->WouldCreateCycle(protoMap)) {
                // TypeError: Cyclic __proto__ value - just return obj unchanged
                return obj;
            }

            objMap->SetPrototype(protoMap);
        }

        return obj;
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

    // Object.defineProperty(obj, prop, descriptor) - defines a property on an object
    // Supports: value, get, set, writable (partial), enumerable (partial), configurable (partial)
    TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor) {
        if (!obj || !prop) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) {
            // obj might not be boxed - return unchanged
            return obj;
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {  // TsMap::MAGIC
            return obj;
        }

        TsMap* map = (TsMap*)rawPtr;

        // Check if object is extensible (for new properties) or frozen/sealed
        if (!map->IsExtensible()) {
            // Can only modify existing properties
        }
        if (map->IsFrozen()) {
            return obj;  // Cannot modify frozen objects
        }

        // Get the descriptor object
        void* descRaw = ts_value_get_object(descriptor);
        if (!descRaw) {
            return obj;  // descriptor must be an object
        }

        uint32_t descMagic = *(uint32_t*)((char*)descRaw + 16);
        if (descMagic != 0x4D415053) {
            return obj;  // descriptor must be an object
        }

        TsMap* descMap = (TsMap*)descRaw;

        // Get property key as string
        TsString* propStr = nullptr;
        if (prop->type == ValueType::STRING_PTR) {
            propStr = (TsString*)prop->ptr_val;
        } else {
            propStr = (TsString*)__ts_value_to_property_key((uint8_t)prop->type, prop->i_val);
        }
        if (!propStr) return obj;

        const char* propName = propStr->ToUtf8();
        if (!propName) return obj;

        TsValue propKey;
        propKey.type = ValueType::STRING_PTR;
        propKey.ptr_val = propStr;

        // Check for getter
        TsValue getKey;
        getKey.type = ValueType::STRING_PTR;
        getKey.ptr_val = TsString::GetInterned("get");

        if (descMap->Has(getKey)) {
            TsValue getter = descMap->Get(getKey);
            if (getter.type != ValueType::UNDEFINED) {
                // Store getter as __getter_<propName>
                std::string getterKey = std::string("__getter_") + propName;
                TsValue gk;
                gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterKey.c_str());
                map->Set(gk, getter);
            }
        }

        // Check for setter
        TsValue setKey;
        setKey.type = ValueType::STRING_PTR;
        setKey.ptr_val = TsString::GetInterned("set");

        if (descMap->Has(setKey)) {
            TsValue setter = descMap->Get(setKey);
            if (setter.type != ValueType::UNDEFINED) {
                // Store setter as __setter_<propName>
                std::string setterKey = std::string("__setter_") + propName;
                TsValue sk;
                sk.type = ValueType::STRING_PTR;
                sk.ptr_val = TsString::GetInterned(setterKey.c_str());
                map->Set(sk, setter);
            }
        }

        // Check for value (data descriptor)
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::GetInterned("value");

        if (descMap->Has(valueKey)) {
            TsValue value = descMap->Get(valueKey);
            map->Set(propKey, value);
        }

        return obj;
    }

    // Object.defineProperties(obj, descriptors) - defines multiple properties
    TsValue* ts_object_defineProperties(TsValue* obj, TsValue* descriptors) {
        if (!obj || !descriptors) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if target is a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {
            return obj;
        }

        // Get the descriptors object
        void* descRaw = ts_value_get_object(descriptors);
        if (!descRaw) descRaw = descriptors;

        uint32_t descMagic = *(uint32_t*)((char*)descRaw + 16);
        if (descMagic != 0x4D415053) {
            return obj;
        }

        TsMap* descMap = (TsMap*)descRaw;

        // Iterate over descriptor properties
        TsArray* keys = (TsArray*)descMap->GetKeys();
        int64_t len = keys->Length();

        for (int64_t i = 0; i < len; i++) {
            TsValue* key = (TsValue*)keys->Get(i);
            TsValue desc = descMap->Get(*key);

            // Call defineProperty for each
            TsValue descVal;
            descVal.type = ValueType::OBJECT_PTR;
            descVal.ptr_val = ts_value_get_object(&desc);
            if (!descVal.ptr_val) descVal.ptr_val = &desc;

            ts_object_defineProperty(obj, key, &descVal);
        }

        return obj;
    }

    // Object.getOwnPropertyDescriptor(obj, prop) - gets the descriptor for a property
    // Returns { value: ..., writable: true, enumerable: true, configurable: true }
    TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop) {
        if (!obj || !prop) return ts_value_make_object(nullptr);

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) {
            return ts_value_make_object(nullptr);  // undefined for non-objects
        }

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {
            return ts_value_make_object(nullptr);  // undefined for non-objects
        }

        TsMap* map = (TsMap*)rawPtr;

        // Get property key
        TsValue propKey = *prop;
        if (prop->type != ValueType::STRING_PTR) {
            propKey.type = ValueType::STRING_PTR;
            propKey.ptr_val = __ts_value_to_property_key((uint8_t)prop->type, prop->i_val);
        }

        // Check if property exists
        if (!map->Has(propKey)) {
            return ts_value_make_object(nullptr);  // undefined if not found
        }

        TsValue value = map->Get(propKey);

        // Create descriptor object
        TsMap* desc = TsMap::Create();

        // Set value
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::Create("value");
        desc->Set(valueKey, value);

        // Set writable: true (we always allow writes in our simplified model)
        TsValue writableKey;
        writableKey.type = ValueType::STRING_PTR;
        writableKey.ptr_val = TsString::Create("writable");
        TsValue trueVal;
        trueVal.type = ValueType::BOOLEAN;
        trueVal.b_val = true;
        desc->Set(writableKey, trueVal);

        // Set enumerable: true
        TsValue enumKey;
        enumKey.type = ValueType::STRING_PTR;
        enumKey.ptr_val = TsString::Create("enumerable");
        desc->Set(enumKey, trueVal);

        // Set configurable: true
        TsValue configKey;
        configKey.type = ValueType::STRING_PTR;
        configKey.ptr_val = TsString::Create("configurable");
        desc->Set(configKey, trueVal);

        return ts_value_make_object(desc);
    }

    // Object.getOwnPropertyDescriptors(obj) - gets descriptors for all own properties
    // Returns { prop1: descriptor1, prop2: descriptor2, ... }
    TsValue* ts_object_getOwnPropertyDescriptors(TsValue* obj) {
        // Create result object
        TsMap* result = TsMap::Create();

        if (!obj) return ts_value_make_object(result);

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check if it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {
            return ts_value_make_object(result);  // empty object for non-objects
        }

        TsMap* map = (TsMap*)rawPtr;

        // Iterate over all own properties
        TsArray* keys = (TsArray*)ts_map_keys(map);
        if (!keys) return ts_value_make_object(result);

        int64_t len = keys->Length();
        for (int64_t i = 0; i < len; i++) {
            int64_t keyRaw = keys->Get(i);
            TsValue* keyVal = (TsValue*)keyRaw;

            // Get the descriptor for this property
            TsValue* descriptor = ts_object_getOwnPropertyDescriptor(obj, keyVal);

            // Store descriptor in result with the property name as key
            if (descriptor && keyVal) {
                result->Set(*keyVal, *descriptor);
            }
        }

        return ts_value_make_object(result);
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

        // Check for TsMap (magic at offset 16, 20, or 24 depending on object layout)
        uint32_t magic16 = *(uint32_t*)((char*)rawPtr + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawPtr + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawPtr + 24);

        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC
            TsMap* map = (TsMap*)rawPtr;

            // Get the property name as a string
            void* propRaw = ts_value_get_object((TsValue*)prop);
            if (!propRaw) propRaw = prop;

            TsValue propVal;
            propVal.type = ValueType::STRING_PTR;
            propVal.ptr_val = propRaw;
            return ts_map_has_v(map, propVal);
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

    // ES2024 Object.groupBy(iterable, callbackFn)
    // Groups elements by the key returned from the callback
    TsValue* ts_object_groupBy(TsValue* iterable, TsValue* callbackFn) {
        TsMap* result = TsMap::Create();

        if (!iterable || !callbackFn) return ts_value_make_object(result);

        // Get raw array pointer
        void* rawPtr = ts_value_get_object(iterable);
        if (!rawPtr) rawPtr = iterable;

        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != TsArray::MAGIC) {
            return ts_value_make_object(result);
        }

        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();

        // Get the callback function
        void* cbRaw = ts_value_get_object(callbackFn);
        if (!cbRaw) cbRaw = callbackFn;

        for (int64_t i = 0; i < len; i++) {
            // Get raw element value
            int64_t rawVal = arr->Get(i);

            // Box properly using ts_value_box_any to detect strings, arrays, etc.
            TsValue* elem;
            if (rawVal > 0xFFFFFFFF || rawVal < 0) {
                // Looks like a pointer - use ts_value_box_any for proper type detection
                elem = ts_value_box_any((void*)rawVal);
            } else {
                // Small value - likely an integer
                elem = ts_value_make_int(rawVal);
            }
            if (!elem) continue;

            // Call callback with (element, index)
            TsValue* indexVal = ts_value_make_int(i);
            TsValue* keyResult = ts_call_2(callbackFn, elem, indexVal);

            if (!keyResult) continue;

            // Convert result to string key (typical usage)
            TsValue keyVal;
            if (keyResult->type == ValueType::STRING_PTR) {
                keyVal = *keyResult;
            } else if (keyResult->type == ValueType::NUMBER_INT) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)keyResult->i_val);
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (keyResult->type == ValueType::NUMBER_DBL) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", keyResult->d_val);
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (keyResult->type == ValueType::BOOLEAN) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(keyResult->b_val ? "true" : "false");
            } else if (keyResult->type == ValueType::UNDEFINED) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create("undefined");
            } else {
                // Default to string representation for other types
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create("[object Object]");
            }

            // Check if group already exists
            TsValue existing = result->Get(keyVal);
            TsArray* group;

            if (existing.type == ValueType::ARRAY_PTR && existing.ptr_val) {
                group = (TsArray*)existing.ptr_val;
            } else {
                // Create new group array
                group = TsArray::Create();
                TsValue groupVal;
                groupVal.type = ValueType::ARRAY_PTR;
                groupVal.ptr_val = group;
                result->Set(keyVal, groupVal);
            }

            // Add element to group
            group->Push((int64_t)elem);
        }

        return ts_value_make_object(result);
    }

    // ES2024 Map.groupBy(iterable, callbackFn)
    // Groups elements by the key returned from the callback, returns a Map
    TsValue* ts_map_groupBy(TsValue* iterable, TsValue* callbackFn) {
        TsMap* result = TsMap::Create();

        if (!iterable || !callbackFn) return ts_value_make_object(result);

        // Get raw array pointer
        void* rawPtr = ts_value_get_object(iterable);
        if (!rawPtr) rawPtr = iterable;

        // Check if it's an array
        uint32_t magic = *(uint32_t*)rawPtr;
        if (magic != TsArray::MAGIC) {
            return ts_value_make_object(result);
        }

        TsArray* arr = (TsArray*)rawPtr;
        int64_t len = arr->Length();

        // Get the callback function
        void* cbRaw = ts_value_get_object(callbackFn);
        if (!cbRaw) cbRaw = callbackFn;

        for (int64_t i = 0; i < len; i++) {
            // Get raw element value
            int64_t rawVal = arr->Get(i);

            // Box properly using ts_value_box_any to detect strings, arrays, etc.
            TsValue* elem;
            if (rawVal > 0xFFFFFFFF || rawVal < 0) {
                // Looks like a pointer - use ts_value_box_any for proper type detection
                elem = ts_value_box_any((void*)rawVal);
            } else {
                // Small value - likely an integer
                elem = ts_value_make_int(rawVal);
            }
            if (!elem) continue;

            // Call callback with (element, index)
            TsValue* indexVal = ts_value_make_int(i);
            TsValue* keyResult = ts_call_2(callbackFn, elem, indexVal);

            if (!keyResult) continue;

            // For Map.groupBy, we use the key as-is (not converted to string)
            // This allows objects, symbols, etc. as keys
            TsValue keyVal = *keyResult;

            // Check if group already exists
            TsValue existing = result->Get(keyVal);
            TsArray* group;

            if (existing.type == ValueType::ARRAY_PTR && existing.ptr_val) {
                group = (TsArray*)existing.ptr_val;
            } else {
                // Create new group array
                group = TsArray::Create();
                TsValue groupVal;
                groupVal.type = ValueType::ARRAY_PTR;
                groupVal.ptr_val = group;
                result->Set(keyVal, groupVal);
            }

            // Add element to group
            group->Push((int64_t)elem);
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

        // Check type field first - TsValue types are 0-10
        // A proper TsValue has type <= 10 AND bytes 1-3 are zero (padding)
        // A raw object pointer (vtable) may have first byte <= 10 by coincidence
        uint8_t typeField = *(uint8_t*)v;
        uint8_t byte1 = *((uint8_t*)v + 1);
        uint8_t byte2 = *((uint8_t*)v + 2);
        uint8_t byte3 = *((uint8_t*)v + 3);
        bool isProbablyTsValue = (typeField <= 10 && byte1 == 0 && byte2 == 0 && byte3 == 0);

        if (isProbablyTsValue) {
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

        // Not a TsValue - this is a raw pointer - check magic numbers
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247) return TsString::Create("string"); // TsString::MAGIC
        if (magic == 0x41525259) return TsString::Create("object"); // TsArray::MAGIC
        if (magic == 0x4D415053) return TsString::Create("object"); // TsMap::MAGIC at offset 0
        if (magic == 0x53455453) return TsString::Create("object"); // TsSet::MAGIC
        if (magic == 0x46554E43) return TsString::Create("function"); // TsFunction::MAGIC

        // Check other offsets for magic (TsObject-derived classes have vtable at 0, explicit vtable at 8, magic at 16)
        uint32_t magic16 = *(uint32_t*)((char*)v + 16);
        if (magic16 == 0x4D415053) return TsString::Create("object"); // TsMap::MAGIC at offset 16
        if (magic16 == 0x46554E43) return TsString::Create("function"); // TsFunction::MAGIC at offset 16

        uint32_t magic8 = *(uint32_t*)((char*)v + 8);
        if (magic8 == 0x46554E43) return TsString::Create("function");

        // Default to object for unknown raw pointers
        return TsString::Create("object");
    }

    TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key) {
        if (!obj || !key) return ts_value_make_undefined();

        // Detect if obj is a raw pointer (not a boxed TsValue*) by checking magic values
        // A TsValue* has type in first byte (0-10) with zero padding bytes
        // A TsArray* has magic "ARRY" (0x41525259) at offset 0
        // A TsMap*/TsObject* has magic at offset 16
        {
            uint32_t magic0 = *(uint32_t*)obj;
            // Check for TsArray magic at offset 0
            if (magic0 == 0x41525259) { // "ARRY" - this is a raw TsArray*
                TsArray* arr = (TsArray*)obj;
                if (key->type == ValueType::NUMBER_INT) {
                    return ts_array_get_as_value((void*)arr, key->i_val);
                } else if (key->type == ValueType::NUMBER_DBL) {
                    return ts_array_get_as_value((void*)arr, (int64_t)key->d_val);
                } else if (key->type == ValueType::STRING_PTR) {
                    TsString* keyStr = (TsString*)key->ptr_val;
                    if (keyStr) {
                        const char* k = keyStr->ToUtf8();
                        if (k && strcmp(k, "length") == 0) {
                            return ts_value_make_int(arr->Length());
                        }
                    }
                }
                return ts_value_make_undefined();
            }
            // Check for TsObject/TsMap magic at offset 16
            uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
            if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC || magic16 == 0x54415252) {
                // This is a raw TsObject* - wrap it and continue
                TsValue* wrapped = (TsValue*)ts_alloc(sizeof(TsValue));
                wrapped->type = ValueType::OBJECT_PTR;
                wrapped->ptr_val = (void*)obj;
                obj = wrapped;
            }
        }

        // Check if obj is ARRAY_PTR FIRST - TsArray is NOT a TsObject, so we can't use dynamic_cast on it
        if (obj->type == ValueType::ARRAY_PTR) {
            void* arrPtr = obj->ptr_val;
            if (!arrPtr) return ts_value_make_undefined();
            TsArray* arr = (TsArray*)arrPtr;
            if (key->type == ValueType::NUMBER_INT) {
                return ts_array_get_as_value(arrPtr, key->i_val);
            } else if (key->type == ValueType::NUMBER_DBL) {
                return ts_array_get_as_value(arrPtr, (int64_t)key->d_val);
            } else if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k && strcmp(k, "length") == 0) {
                        return ts_value_make_int(arr->Length());
                    }
                }
            }
            return ts_value_make_undefined();
        }

        // Check if obj is STRING_PTR - delegate string property access to ts_object_get_property
        if (obj->type == ValueType::STRING_PTR) {
            void* strPtr = obj->ptr_val;
            if (!strPtr) return ts_value_make_undefined();
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        return ts_object_get_property(strPtr, k);
                    }
                }
            }
            return ts_value_make_undefined();
        }

        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) {
            return ts_value_make_undefined();
        }

        // Check magic at offset 0 first (TsArray has magic at offset 0)
        // Note: ARRAY_PTR is handled early above, but this catches raw array pointers
        uint32_t magic0 = *(uint32_t*)rawObj;
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);

        // Only do dynamic_cast for Proxy check if we know this is a TsObject-derived class
        // TsObject::magic at offset 16 should be TsMap::MAGIC for plain objects
        // We check for Proxy AFTER verifying it's a valid TsObject to avoid crashes
        if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC || magic16 == 0x54415252) {
            // It's a TsObject-derived class - safe to use dynamic_cast
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                return proxy->get(key, nullptr);
            }
        }
        if (magic0 == 0x53545247) { // TsString::MAGIC = "STRG"
            // This is a raw TsString* - delegate to ts_object_get_property
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        return ts_object_get_property(rawObj, k);
                    }
                }
            }
            return ts_value_make_undefined();
        }
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
        if (magic16 == TsFunction::MAGIC) {
            TsFunction* func = (TsFunction*)rawObj;

            // Handle Function.prototype methods (bind, call, apply)
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        if (strcmp(k, "bind") == 0) {
                            TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                            target->type = ValueType::OBJECT_PTR;
                            target->ptr_val = func;
                            return ts_value_make_native_function((void*)ts_function_bind_native, (void*)target);
                        }
                        if (strcmp(k, "call") == 0) {
                            TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                            target->type = ValueType::OBJECT_PTR;
                            target->ptr_val = func;
                            return ts_value_make_native_function((void*)ts_function_call_native, (void*)target);
                        }
                        if (strcmp(k, "apply") == 0) {
                            TsValue* target = (TsValue*)ts_alloc(sizeof(TsValue));
                            target->type = ValueType::OBJECT_PTR;
                            target->ptr_val = func;
                            return ts_value_make_native_function((void*)ts_function_apply_native, (void*)target);
                        }
                        if (strcmp(k, "toString") == 0) {
                            return ts_value_make_native_function((void*)ts_function_toString_native, (void*)func);
                        }
                        if (strcmp(k, "length") == 0) {
                            return ts_value_make_int(0);
                        }
                        if (strcmp(k, "name") == 0) {
                            return ts_value_make_string(TsString::Create(""));
                        }
                    }
                }
            }

            if (!func->properties) {
                return ts_value_make_undefined();  // No properties set yet
            }
            // Use the properties map
            // key might be a proper TsValue* or a raw TsString* pointer from HIR
            TsValue funcKeyVal;
            uint8_t funcKeyType = *(uint8_t*)key;
            uint8_t funcKeyB1 = *((uint8_t*)key + 1);
            uint8_t funcKeyB2 = *((uint8_t*)key + 2);
            uint8_t funcKeyB3 = *((uint8_t*)key + 3);
            bool funcKeyIsTsValue = (funcKeyType <= 10 && funcKeyB1 == 0 && funcKeyB2 == 0 && funcKeyB3 == 0);

            if (funcKeyIsTsValue) {
                funcKeyVal = *key;
            } else {
                funcKeyVal.type = ValueType::STRING_PTR;
                funcKeyVal.ptr_val = (void*)key;
            }

            TsValue result = func->properties->Get(funcKeyVal);
            if (result.type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
            TsValue* heapResult = (TsValue*)ts_alloc(sizeof(TsValue));
            *heapResult = result;
            return heapResult;
        }

        // Check for TsTypedArray (magic at offset 16)
        if (magic16 == 0x54415252) { // TsTypedArray::MAGIC = "TARR"
            TsTypedArray* ta = (TsTypedArray*)rawObj;
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        if (strcmp(k, "length") == 0) {
                            return ts_value_make_int((int64_t)ta->GetLength());
                        }
                        if (strcmp(k, "byteLength") == 0) {
                            return ts_value_make_int((int64_t)ta->GetByteLength());
                        }
                        if (strcmp(k, "byteOffset") == 0) {
                            return ts_value_make_int((int64_t)ta->GetByteOffset());
                        }
                        if (strcmp(k, "BYTES_PER_ELEMENT") == 0) {
                            return ts_value_make_int((int64_t)ta->GetElementSize());
                        }
                        if (strcmp(k, "buffer") == 0) {
                            return ts_value_make_object(ta->GetBuffer());
                        }
                    }
                }
            } else if (key->type == ValueType::NUMBER_INT) {
                return ts_value_make_double(ta->Get((size_t)key->i_val));
            } else if (key->type == ValueType::NUMBER_DBL) {
                return ts_value_make_double(ta->Get((size_t)key->d_val));
            }
            return ts_value_make_undefined();
        }

        // Check if this is actually a TsMap before using map operations
        // TsMap::MAGIC is at offset 16 (after vtable ptr + explicit vtable field)
        uint32_t magic20 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 20);
        uint32_t magic24 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 24);
        if (magic16 != 0x4D415053 && magic20 != 0x4D415053 && magic24 != 0x4D415053) { // TsMap::MAGIC = "MAPS"
            // Not a map - try ts_object_get_property as fallback for TsObject subclasses
            if (key->type == ValueType::STRING_PTR) {
                TsString* keyStr = (TsString*)key->ptr_val;
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) {
                        return ts_object_get_property(rawObj, k);
                    }
                }
            }
            return ts_value_make_undefined();
        }

        // Use TsMap::Get which handles hashing correctly (by string content, not pointer address)
        TsMap* map = (TsMap*)rawObj;

        // First check for a getter (__getter_<propertyName>) - walk prototype chain
        if (key->type == ValueType::STRING_PTR && key->ptr_val) {
            TsString* keyStr = (TsString*)key->ptr_val;
            const char* propName = keyStr->ToUtf8();
            if (propName) {
                std::string getterKey = std::string("__getter_") + propName;
                TsValue gk;
                gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterKey.c_str());

                // Walk prototype chain for getter
                TsMap* currentMap = map;
                while (currentMap != nullptr) {
                    TsValue getterVal = currentMap->Get(gk);
                    if (getterVal.type != ValueType::UNDEFINED) {
                        // Found a getter - invoke it with 'this' as the object
                        TsValue* boxedObj = (TsValue*)ts_alloc(sizeof(TsValue));
                        boxedObj->type = ValueType::OBJECT_PTR;
                        boxedObj->ptr_val = rawObj;
                        return ts_function_call_with_this(&getterVal, boxedObj, 0, nullptr);
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }
        }

        // Walk prototype chain for property lookup
        TsValue result;
        result.type = ValueType::UNDEFINED;
        TsMap* currentMap = map;

        // Create proper TsValue key for map lookup
        // key might be a proper TsValue* or a raw TsString* pointer from HIR
        TsValue keyVal;
        uint8_t keyTypeField = *(uint8_t*)key;
        uint8_t keyByte1 = *((uint8_t*)key + 1);
        uint8_t keyByte2 = *((uint8_t*)key + 2);
        uint8_t keyByte3 = *((uint8_t*)key + 3);
        bool keyIsTsValue = (keyTypeField <= 10 && keyByte1 == 0 && keyByte2 == 0 && keyByte3 == 0);

        if (keyIsTsValue) {
            keyVal = *key;  // It's a proper TsValue - copy it
        } else {
            // It's a raw string pointer - wrap it
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = (void*)key;
        }

        while (currentMap != nullptr) {
            result = currentMap->Get(keyVal);
            if (result.type != ValueType::UNDEFINED) {
                break;  // Found the property
            }
            currentMap = currentMap->GetPrototype();
        }

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

        // Detect if obj is a raw pointer (not a boxed TsValue*) by checking magic values
        // A TsObject/TsMap has magic "MAPS" (0x4D415053) at offset 16
        {
            uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
            if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC || magic16 == 0x54415252) {
                // This is a raw TsObject* - wrap it as TsValue and continue
                TsValue* wrapped = (TsValue*)ts_alloc(sizeof(TsValue));
                wrapped->type = ValueType::OBJECT_PTR;
                wrapped->ptr_val = (void*)obj;
                obj = wrapped;
            }
        }

        // Check if this is a Proxy - dispatch through proxy trap
        void* rawObj = ts_value_get_object(obj);
        if (rawObj) {
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                proxy->set(key, value, nullptr);
                return;
            }
        }

        // Delegate to ts_object_set_prop_v which handles all cases
        ts_object_set_prop_v(*obj, *key, *value);
    }

    // HIR-friendly wrapper for setting object properties
    // Takes void* args that may be TsValue* or raw pointers
    void ts_object_set_property(void* obj, void* key, void* value) {
        // Forward to ts_object_set_dynamic after casting
        ts_object_set_dynamic((TsValue*)obj, (TsValue*)key, (TsValue*)value);
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
            TsMap* map = (TsMap*)rawObj;

            // First check for a setter (__setter_<propertyName>)
            const char* keyCStr = keyStr->ToUtf8();
            if (keyCStr) {
                std::string setterKey = std::string("__setter_") + keyCStr;
                TsValue sk;
                sk.type = ValueType::STRING_PTR;
                sk.ptr_val = TsString::GetInterned(setterKey.c_str());
                TsValue setterVal = map->Get(sk);
                if (setterVal.type != ValueType::UNDEFINED) {
                    // Found a setter - invoke it with 'this' as the object and value as argument
                    TsValue* boxedObj = (TsValue*)ts_alloc(sizeof(TsValue));
                    boxedObj->type = ValueType::OBJECT_PTR;
                    boxedObj->ptr_val = rawObj;
                    TsValue* boxedVal = (TsValue*)ts_alloc(sizeof(TsValue));
                    *boxedVal = value;
                    TsValue* args[] = { boxedVal };
                    ts_function_call_with_this(&setterVal, boxedObj, 1, args);
                    return value;
                }
            }

            // No setter - set property directly
            ts_map_set_v(rawObj, key, value);
            return value;
        }

        return value;
    }

    bool ts_object_has_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        // Check if this is a Proxy - dispatch through proxy trap
        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
        if (proxy) {
            return proxy->has(key);
        }

        TsString* keyStr = (TsString*)ts_value_get_string(key);
        if (!keyStr) return false;

        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawObj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawObj + 24);
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC
            TsMap* map = (TsMap*)rawObj;

            // Create a proper TsValue key from the keyStr for lookup
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = keyStr;

            // Walk the prototype chain to check for the property
            TsMap* currentMap = map;
            while (currentMap != nullptr) {
                if (currentMap->Has(keyVal)) {
                    return true;
                }
                currentMap = currentMap->GetPrototype();
            }
            return false;
        }

        return false;
    }

    bool ts_object_delete_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        // Check if this is a Proxy - dispatch through proxy trap
        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
        if (proxy) {
            return proxy->deleteProperty(key);
        }

        TsString* keyStr = (TsString*)ts_value_get_string(key);
        if (!keyStr) return false;

        // Check for TsMap using TsObject::magic at offset 16 (after vptr and vtable)
        uint32_t magic = *(uint32_t*)((char*)rawObj + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC "MAPS"
            TsMap* map = (TsMap*)rawObj;
            // Create a proper TsValue key from the keyStr for delete
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = keyStr;
            return map->Delete(keyVal);
        }

        return false;
    }

    // Wrapper for 'in' operator: checks if property exists (including inherited)
    bool ts_object_has_property(void* objArg, void* keyArg) {
        TsValue* obj = (TsValue*)objArg;
        TsValue* key = (TsValue*)keyArg;
        return ts_object_has_prop(obj, key);
    }

    // Wrapper for delete operator: removes property from object
    // Handles both raw pointers from HIR and boxed TsValue* pointers
    int ts_object_delete_property(void* objArg, void* keyArg) {
        if (!objArg || !keyArg) return 0;

        // Detect if obj is a TsValue or raw pointer
        // TsValue has type (0-10) in first byte, followed by 3 zero padding bytes
        uint8_t objType = *(uint8_t*)objArg;
        uint8_t objB1 = *((uint8_t*)objArg + 1);
        uint8_t objB2 = *((uint8_t*)objArg + 2);
        uint8_t objB3 = *((uint8_t*)objArg + 3);
        bool objIsTsValue = (objType <= 10 && objB1 == 0 && objB2 == 0 && objB3 == 0);

        // Get the raw TsMap pointer
        void* rawMap;
        if (objIsTsValue) {
            TsValue* objVal = (TsValue*)objArg;
            rawMap = ts_value_get_object(objVal);
        } else {
            // It's a raw TsMap* pointer
            rawMap = objArg;
        }
        if (!rawMap) return 0;

        // Check magic to confirm it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawMap + 16);
        if (magic != 0x4D415053) return 0; // Not a TsMap ("MAPS")

        TsMap* map = (TsMap*)rawMap;

        // Detect if key is a TsValue or raw TsString pointer
        uint8_t keyType = *(uint8_t*)keyArg;
        uint8_t keyB1 = *((uint8_t*)keyArg + 1);
        uint8_t keyB2 = *((uint8_t*)keyArg + 2);
        uint8_t keyB3 = *((uint8_t*)keyArg + 3);
        bool keyIsTsValue = (keyType <= 10 && keyB1 == 0 && keyB2 == 0 && keyB3 == 0);

        // Get the key string
        TsString* keyStr;
        if (keyIsTsValue) {
            TsValue* keyVal = (TsValue*)keyArg;
            keyStr = (TsString*)ts_value_get_string(keyVal);
        } else {
            // It's a raw TsString* pointer
            keyStr = (TsString*)keyArg;
        }
        if (!keyStr) return 0;

        // Create proper TsValue key for map delete
        TsValue keyVal;
        keyVal.type = ValueType::STRING_PTR;
        keyVal.ptr_val = keyStr;

        return map->Delete(keyVal) ? 1 : 0;
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

    TsValue* ts_object_defineProperty_native(void* context, int argc, TsValue** argv) {
        if (argc < 3) return (argc > 0) ? argv[0] : ts_value_make_undefined();
        return ts_object_defineProperty(argv[0], argv[1], argv[2]);
    }

    TsValue* ts_object_defineProperties_native(void* context, int argc, TsValue** argv) {
        if (argc < 2) return (argc > 0) ? argv[0] : ts_value_make_undefined();
        return ts_object_defineProperties(argv[0], argv[1]);
    }

    TsValue* ts_object_getOwnPropertyDescriptor_native(void* context, int argc, TsValue** argv) {
        if (argc < 2) return ts_value_make_undefined();
        return ts_object_getOwnPropertyDescriptor(argv[0], argv[1]);
    }

    TsValue* ts_object_getOwnPropertyDescriptors_native(void* context, int argc, TsValue** argv) {
        if (argc < 1) return ts_value_make_object(TsMap::Create());
        return ts_object_getOwnPropertyDescriptors(argv[0]);
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
    extern "C" TsValue* globalThis = nullptr;  // ES2020: alias for global
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

        // Get the property key as TsValue and check if the property exists
        TsValue* keyVal = argv[0];
        bool result = map->Has(*keyVal);
        return ts_value_make_bool(result);
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

        // Object.defineProperty
        TsValue dpKey; dpKey.type = ValueType::STRING_PTR; dpKey.ptr_val = TsString::Create("defineProperty");
        objectFunc->properties->Set(dpKey, *ts_value_make_native_function((void*)ts_object_defineProperty_native, nullptr));

        // Object.defineProperties
        TsValue dpsKey; dpsKey.type = ValueType::STRING_PTR; dpsKey.ptr_val = TsString::Create("defineProperties");
        objectFunc->properties->Set(dpsKey, *ts_value_make_native_function((void*)ts_object_defineProperties_native, nullptr));

        // Object.getOwnPropertyDescriptor
        TsValue gopdKey; gopdKey.type = ValueType::STRING_PTR; gopdKey.ptr_val = TsString::Create("getOwnPropertyDescriptor");
        objectFunc->properties->Set(gopdKey, *ts_value_make_native_function((void*)ts_object_getOwnPropertyDescriptor_native, nullptr));

        // Object.getOwnPropertyDescriptors (ES2017)
        TsValue gopdsKey; gopdsKey.type = ValueType::STRING_PTR; gopdsKey.ptr_val = TsString::Create("getOwnPropertyDescriptors");
        objectFunc->properties->Set(gopdsKey, *ts_value_make_native_function((void*)ts_object_getOwnPropertyDescriptors_native, nullptr));

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
        globalThis = global;  // ES2020: globalThis is an alias for global
        // global.global === global, globalThis.globalThis === globalThis
        globalMap->Set(makeKey("global"), *global);
        globalMap->Set(makeKey("globalThis"), *global);
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

    // ============================================================================
    // WeakMap - implemented as TsWeakMap wrapper (no true weak semantics with Boehm GC)
    // ============================================================================

    void* ts_weakmap_create() {
        return TsWeakMap::Create();
    }

    void* ts_weakmap_set(void* weakmap, void* key, TsValue* value) {
        if (!weakmap || !key) return weakmap;
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_value_get_object((TsValue*)key);
        if (!rawKey) rawKey = key;

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        if (value) {
            map->Set(keyVal, *value);
        } else {
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.ptr_val = nullptr;
            map->Set(keyVal, undef);
        }
        return weakmap;
    }

    TsValue* ts_weakmap_get(void* weakmap, void* key) {
        if (!weakmap || !key) return ts_value_make_undefined();
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_value_get_object((TsValue*)key);
        if (!rawKey) rawKey = key;

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        TsValue result = map->Get(keyVal);
        TsValue* heapResult = (TsValue*)ts_alloc(sizeof(TsValue));
        *heapResult = result;
        return heapResult;
    }

    bool ts_weakmap_has(void* weakmap, void* key) {
        if (!weakmap || !key) return false;
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_value_get_object((TsValue*)key);
        if (!rawKey) rawKey = key;

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        return map->Has(keyVal);
    }

    bool ts_weakmap_delete(void* weakmap, void* key) {
        if (!weakmap || !key) return false;
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_value_get_object((TsValue*)key);
        if (!rawKey) rawKey = key;

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        return map->Delete(keyVal);
    }

    // ============================================================================
    // WeakSet - implemented as TsWeakSet wrapper (no true weak semantics with Boehm GC)
    // ============================================================================

    void* ts_weakset_create() {
        return TsWeakSet::Create();
    }

    void* ts_weakset_add(void* weakset, void* value) {
        if (!weakset || !value) return weakset;
        TsSet* set = (TsSet*)weakset;

        void* rawValue = ts_value_get_object((TsValue*)value);
        if (!rawValue) rawValue = value;

        TsValue val;
        val.type = ValueType::OBJECT_PTR;
        val.ptr_val = rawValue;

        set->Add(val);
        return weakset;
    }

    bool ts_weakset_has(void* weakset, void* value) {
        if (!weakset || !value) return false;
        TsSet* set = (TsSet*)weakset;

        void* rawValue = ts_value_get_object((TsValue*)value);
        if (!rawValue) rawValue = value;

        TsValue val;
        val.type = ValueType::OBJECT_PTR;
        val.ptr_val = rawValue;

        return set->Has(val);
    }

    bool ts_weakset_delete(void* weakset, void* value) {
        if (!weakset || !value) return false;
        TsSet* set = (TsSet*)weakset;

        void* rawValue = ts_value_get_object((TsValue*)value);
        if (!rawValue) rawValue = value;

        TsValue val;
        val.type = ValueType::OBJECT_PTR;
        val.ptr_val = rawValue;

        return set->Delete(val);
    }

    // ============================================================
    // JSX Runtime Support
    // ============================================================

    // Creates a JSX element object with { type, props, children } structure
    // This mimics React.createElement's return value
    TsValue* ts_jsx_create_element(void* tagName, void* props, void* children) {
        // Create the element object
        TsMap* element = TsMap::Create();

        // Set 'type' property (the tag name string)
        // IMPORTANT: Zero the TsValue struct to ensure padding bytes are 0
        // ts_value_get_object relies on bytes 1-3 being zero to detect TsValue
        TsValue typeKey;
        memset(&typeKey, 0, sizeof(TsValue));
        typeKey.type = ValueType::STRING_PTR;
        typeKey.ptr_val = TsString::Create("type");

        TsValue typeVal;
        memset(&typeVal, 0, sizeof(TsValue));
        if (tagName) {
            // tagName is already a TsString*
            typeVal.type = ValueType::STRING_PTR;
            typeVal.ptr_val = tagName;
        } else {
            typeVal.type = ValueType::UNDEFINED;
            typeVal.ptr_val = nullptr;
        }
        element->Set(typeKey, typeVal);

        // Set 'props' property
        TsValue propsKey;
        memset(&propsKey, 0, sizeof(TsValue));
        propsKey.type = ValueType::STRING_PTR;
        propsKey.ptr_val = TsString::Create("props");

        TsValue propsVal;
        memset(&propsVal, 0, sizeof(TsValue));
        if (props) {
            // Unbox if needed
            void* rawProps = ts_value_get_object((TsValue*)props);
            if (!rawProps) rawProps = props;
            propsVal.type = ValueType::OBJECT_PTR;
            propsVal.ptr_val = rawProps;
        } else {
            // Empty props object
            propsVal.type = ValueType::OBJECT_PTR;
            propsVal.ptr_val = TsMap::Create();
        }
        element->Set(propsKey, propsVal);

        // Set 'children' property (the children array)
        TsValue childrenKey;
        memset(&childrenKey, 0, sizeof(TsValue));
        childrenKey.type = ValueType::STRING_PTR;
        childrenKey.ptr_val = TsString::Create("children");

        TsValue childrenVal;
        memset(&childrenVal, 0, sizeof(TsValue));
        if (children) {
            // Unbox if needed
            void* rawChildren = ts_value_get_object((TsValue*)children);
            if (!rawChildren) rawChildren = children;
            childrenVal.type = ValueType::ARRAY_PTR;
            childrenVal.ptr_val = rawChildren;
        } else {
            // Empty children array
            childrenVal.type = ValueType::ARRAY_PTR;
            childrenVal.ptr_val = TsArray::Create();
        }
        element->Set(childrenKey, childrenVal);

        return ts_value_make_object(element);
    }
}
