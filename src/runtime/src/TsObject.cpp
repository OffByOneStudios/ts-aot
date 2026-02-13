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
#include "TsTextEncoding.h"
#include "GC.h"
#include "TsGC.h"  // For ts_gc_base()
#include "TsNanBox.h"
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

// GC scanner for module cache: keeps cached module objects alive during full GC.
// Minor GC fixup: fixes up nursery pointers when objects are promoted.
static struct ModuleCacheScanner {
    ModuleCacheScanner() {
        ts_gc_register_scanner([](void*) {
            for (auto& [key, val] : g_module_cache) {
                if (val) ts_gc_mark_object(val);
            }
        }, nullptr);
        ts_gc_register_minor_fixup([](void*) {
            for (auto& [key, val] : g_module_cache) {
                if (val) {
                    void* fixed = ts_gc_minor_lookup_forward(val);
                    if (fixed != val) val = (TsValue*)fixed;
                }
            }
        }, nullptr);
    }
} g_module_cache_scanner;

// Debug hook: captures the TsMap* backing lodash's synthetic module object.
// Used by TsMap.cpp to trace writes to module.exports.
extern "C" void* g_debug_lodash_module_map = nullptr;

// nanbox_from_tagged / nanbox_to_tagged are now in TsObject.h

// Helper: check if a NaN-boxed value represents a string pointer
static inline bool nanbox_is_string_ptr(uint64_t nb) {
    if (!nanbox_is_ptr(nb)) return false;
    void* ptr = nanbox_to_ptr(nb);
    uint32_t magic = *(uint32_t*)ptr;
    return magic == 0x53545247; // TsString::MAGIC "STRG"
}

// Helper: extract a numeric value from NaN-boxed TsValue* as double
static inline double nanbox_extract_double(TsValue* v) {
    if (!v) return 0.0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_int32(nb)) return (double)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return nanbox_to_double(nb);
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247) { // TsString
            try { return std::stod(((TsString*)ptr)->ToUtf8()); }
            catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
        }
    }
    if (nb == NANBOX_TRUE) return 1.0;
    if (nb == NANBOX_FALSE) return 0.0;
    return 0.0;
}

// Helper: extract int64 from NaN-boxed TsValue*
static inline int64_t nanbox_extract_int(TsValue* v) {
    if (!v) return 0;
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if (nanbox_is_int32(nb)) return (int64_t)nanbox_to_int32(nb);
    if (nanbox_is_double(nb)) return (int64_t)nanbox_to_double(nb);
    return 0;
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
    return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
}

TsValue* ts_value_make_null() {
    return nanbox_to_tsvalue_ptr(NANBOX_NULL);
}

TsValue* ts_value_make_int(int64_t i) {
        // Encode as int32 if it fits, otherwise as double (matches JS semantics)
        if (i >= INT32_MIN && i <= INT32_MAX) {
            return nanbox_to_tsvalue_ptr(nanbox_int32((int32_t)i));
        }
        return nanbox_to_tsvalue_ptr(nanbox_double((double)i));
    }

    TsValue* ts_value_make_double(double d) {
        return nanbox_to_tsvalue_ptr(nanbox_double(d));
    }

    TsValue* ts_value_make_bool(bool b) {
        return nanbox_to_tsvalue_ptr(nanbox_bool(b));
    }

    TsValue* ts_value_make_string(void* s) {
        if (!s) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        // With NaN boxing, a pointer IS its own encoding.
        // Check for NaN-boxed non-pointer values passed by mistake.
        uint64_t nb = (uint64_t)(uintptr_t)s;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)s;  // Already NaN-boxed, return as-is
        }
        // It's a raw pointer (TsString* etc.) - NaN-boxed pointer IS the raw pointer
        return (TsValue*)s;
    }

    TsValue* ts_value_make_object(void* o) {
        if (!o) return nanbox_to_tsvalue_ptr(NANBOX_NULL);
        // With NaN boxing, a pointer IS its own encoding.
        // Check for NaN-boxed non-pointer values passed by mistake.
        uint64_t nb = (uint64_t)(uintptr_t)o;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)o;  // Already NaN-boxed, return as-is
        }
        return (TsValue*)o;
    }

    TsValue* ts_value_make_function_object(void* fnObj) {
        if (!fnObj) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)fnObj;
    }

    TsValue* ts_value_make_promise(void* promise) {
        if (!promise) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)promise;
    }

    TsValue* ts_value_make_array(void* arr) {
        if (!arr) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        return (TsValue*)arr;
    }

    TsValue* ts_ensure_boxed(void* v) {
        if (!v) return nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        // With NaN boxing, check if it's already a NaN-boxed value
        uint64_t nb = (uint64_t)(uintptr_t)v;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)v;  // Already NaN-boxed non-pointer
        }
        // It's a raw pointer - return as NaN-boxed pointer
        return (TsValue*)v;
    }

    bool ts_value_is_undefined(TsValue* v) {
        if (!v) return true;  // C++ nullptr treated as undefined
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        return nanbox_is_undefined(nb);
    }

    bool ts_value_is_null(TsValue* v) {
        if (!v) return false;  // C++ nullptr is not JavaScript null
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        return nanbox_is_null(nb);
    }

    // Box any pointer by detecting its runtime type
    // This is used when the compile-time type is 'any' but we need proper boxing
    TsValue* ts_value_box_any(void* ptr) {
        if (!ptr) {
            return ts_value_make_undefined();
        }

        // With NaN boxing, check if it's already a NaN-boxed value
        uint64_t nb = (uint64_t)(uintptr_t)ptr;
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return (TsValue*)ptr;  // Already NaN-boxed non-pointer
        }

        // Guard against obviously-invalid pointers
        if (nb < 0x10000) {
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
        
        // With NaN boxing, there's no old-style TsValue struct to detect.
        // If we get here, it's an unknown object.

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
        return (TsValue*)func;
    }

    TsValue* ts_value_make_function_with_arity(void* funcPtr, void* context, int arity) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, arity);
        return (TsValue*)func;
    }

    TsValue* ts_value_make_native_function(void* funcPtr, void* context) {
        void* mem = ts_alloc(sizeof(TsFunction));
        TsFunction* func = new (mem) TsFunction(funcPtr, context, FunctionType::NATIVE);
        func->magic = TsFunction::MAGIC;
        return (TsValue*)func;
    }

    void* ts_function_get_ptr(TsValue* val) {
        if (!val) return nullptr;
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (!nanbox_is_ptr(nb)) return nullptr;
        void* ptr = nanbox_to_ptr(nb);
        // Check for TsFunction magic at offset 16 (after vtable + explicit vtable)
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC
            return ((TsFunction*)ptr)->funcPtr;
        }
        return nullptr;
    }

    void* ts_value_get_function(TsValue* val) {
        return ts_function_get_ptr(val);
    }

    void* ts_value_get_context(TsValue* val) {
        if (!val) return nullptr;
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (!nanbox_is_ptr(nb)) return nullptr;
        void* ptr = nanbox_to_ptr(nb);
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC
            return ((TsFunction*)ptr)->context;
        }
        return nullptr;
    }

    bool ts_value_get_bool(TsValue* v) {
        if (!v) return false;
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        if (nb == NANBOX_TRUE) return true;
        if (nb == NANBOX_FALSE) return false;
        // Fallback to coercion
        return ts_value_to_bool(v);
    }

    void* ts_value_get_string(TsValue* v) {
        if (!v) return nullptr;
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        // Non-pointer NaN-boxed values: convert to string
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return ts_string_from_value(v);
        }
        // It's a pointer - check if it's a TsString
        void* ptr = nanbox_to_ptr(nb);
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247) { // TsString::MAGIC
            return ptr;
        }
        // Not a string - try to convert
        return ts_string_from_value(v);
    }

    void* ts_value_get_object(TsValue* v) {
        if (!v) return nullptr;
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        // Non-pointer NaN-boxed values are not objects
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            return nullptr;
        }
        // It's a pointer - return it directly
        return nanbox_to_ptr(nb);
    }

    // Safe unbox: extract heap pointer from void* that may be NaN-boxed.
    // Returns nullptr for NaN-boxed specials (undefined, null, true, false).
    // Returns the raw heap pointer for valid object pointers.
    void* ts_nanbox_safe_unbox(void* arg) {
        if (!arg) return nullptr;
        void* raw = ts_value_get_object((TsValue*)arg);
        if (raw) return raw;
        // NaN-boxed special or number - not a valid heap pointer
        if ((uint64_t)(uintptr_t)arg < 0x10000) return nullptr;
        // Raw pointer that wasn't NaN-boxed
        return arg;
    }

    // Strict equality comparison for NaN-boxed values (implements === semantics)
    bool ts_value_strict_eq_bool(TsValue* lhs, TsValue* rhs) {
        // Treat nullptr as undefined
        if (!lhs) lhs = nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);
        if (!rhs) rhs = nanbox_to_tsvalue_ptr(NANBOX_UNDEFINED);

        uint64_t lnb = nanbox_from_tsvalue_ptr(lhs);
        uint64_t rnb = nanbox_from_tsvalue_ptr(rhs);

        // Fast path: identical bit patterns
        if (lnb == rnb) {
            // NaN !== NaN
            if (nanbox_is_double(lnb)) {
                double d = nanbox_to_double(lnb);
                return d == d;  // false for NaN
            }
            return true;
        }

        // Both numbers (int32 or double): compare as doubles
        bool lnum = nanbox_is_number(lnb);
        bool rnum = nanbox_is_number(rnb);
        if (lnum && rnum) {
            double ld = nanbox_to_number(lnb);
            double rd = nanbox_to_number(rnb);
            if (std::isnan(ld) || std::isnan(rd)) return false;
            return ld == rd;
        }

        // Both are pointers
        bool lptr = nanbox_is_ptr(lnb);
        bool rptr = nanbox_is_ptr(rnb);
        if (lptr && rptr) {
            void* lp = nanbox_to_ptr(lnb);
            void* rp = nanbox_to_ptr(rnb);
            // Check for string comparison
            uint32_t lmagic = *(uint32_t*)lp;
            uint32_t rmagic = *(uint32_t*)rp;
            if (lmagic == 0x53545247 && rmagic == 0x53545247) {
                return std::strcmp(((TsString*)lp)->ToUtf8(), ((TsString*)rp)->ToUtf8()) == 0;
            }
            if (lmagic == 0x42494749 && rmagic == 0x42494749) { // BigInt
                return std::strcmp(((TsBigInt*)lp)->ToString(), ((TsBigInt*)rp)->ToString()) == 0;
            }
            // Check for TsFunction comparison - compare funcPtr
            uint32_t lm16 = *(uint32_t*)((char*)lp + 16);
            uint32_t rm16 = *(uint32_t*)((char*)rp + 16);
            if (lm16 == 0x46554E43 && rm16 == 0x46554E43) {
                return ((TsFunction*)lp)->funcPtr == ((TsFunction*)rp)->funcPtr;
            }
            // Object identity comparison
            return lp == rp;
        }

        // Different types (one special, one number, one pointer etc.) - not equal
        return false;
    }

    TsValue* ts_value_strict_eq(TsValue* lhs, TsValue* rhs) {
        return ts_value_make_bool(ts_value_strict_eq_bool(lhs, rhs));
    }

    // Note: ts_value_get_int and ts_value_get_double are defined in Primitives.cpp

    int64_t ts_value_length(TsValue* val) {
        if (!val) return 0;
        uint64_t nb = nanbox_from_tsvalue_ptr(val);

        // Non-pointer NaN-boxed values have no length
        if (!nanbox_is_ptr(nb)) return 0;

        void* rawPtr = nanbox_to_ptr(nb);
        if (!rawPtr) return 0;

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

        uint64_t nb = (uint64_t)(uintptr_t)param;
        void* rawPtr = nullptr;

        // Check NaN-box encoding
        if (nanbox_is_number(nb) || nanbox_is_special(nb)) {
            // Numbers, booleans, undefined, null have no elements
            return ts_value_make_undefined();
        }
        if (nanbox_is_ptr(nb)) {
            rawPtr = nanbox_to_ptr(nb);
            if (!rawPtr) return ts_value_make_undefined();
            // Check if it's a string
            uint32_t m = *(uint32_t*)rawPtr;
            if (m == 0x53545247) { // TsString
                TsString* s = (TsString*)rawPtr;
                return s->Substring(index, index + 1);
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

    // Native wrapper for number.toString() - ctx is a NaN-boxed number value
    static TsValue* ts_number_toString_native(void* ctx, int argc, TsValue** argv) {
        double value = nanbox_extract_double((TsValue*)ctx);
        int64_t radix = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 10;
        return ts_value_make_string((TsString*)ts_number_to_string(value, radix));
    }

    static TsValue* ts_number_toFixed_native(void* ctx, int argc, TsValue** argv) {
        double value = nanbox_extract_double((TsValue*)ctx);
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
        if (!ts_gc_base(obj)) {
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
        TsValue vbaseResult = ts_try_virtual_dispatch_via_vbase(obj, keyStr);
        if (vbaseResult.type != ValueType::UNDEFINED) {
            return vbaseResult;
        }

        // Fall back to direct cast for TsObject subclasses that implement GetPropertyVirtual().
        // With NaN boxing, obj is always a raw pointer here (non-pointer values are filtered
        // by the caller). Check magic at offset 16 for known subclasses.
        if (ts_gc_base(obj)) {
            uint32_t magic16 = *(uint32_t*)((uint8_t*)obj + 16);
            if (magic16 == 0x50524F4D ||  // TsPromise::MAGIC "PROM"
                magic16 == 0x54584E43 ||  // TsTextEncoder::MAGIC "TXNC"
                magic16 == 0x54584443) {  // TsTextDecoder::MAGIC "TXDC"
                TsObject* tsObj = (TsObject*)obj;
                TsValue result = tsObj->GetPropertyVirtual(keyStr);
                if (result.type != ValueType::UNDEFINED) {
                    return result;
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

        // NaN-box decode: obj may be a NaN-boxed TsValue* (number, bool, undefined, null)
        // or a raw pointer (TsObject*, TsArray*, TsString*, etc.)
        uint64_t nb = (uint64_t)(uintptr_t)obj;

        // Handle non-pointer NaN-boxed values
        if (nanbox_is_undefined(nb) || nanbox_is_null(nb)) {
            return ts_value_make_undefined();
        }
        if (nanbox_is_int32(nb) || nanbox_is_double(nb)) {
            // Number methods: toString, toFixed
            if (strcmp(keyStr, "toString") == 0) {
                return ts_value_make_native_function((void*)ts_number_toString_native, obj);
            }
            if (strcmp(keyStr, "toFixed") == 0) {
                return ts_value_make_native_function((void*)ts_number_toFixed_native, obj);
            }
            return ts_value_make_undefined();
        }
        if (nanbox_is_bool(nb)) {
            return ts_value_make_undefined();
        }

        // At this point, nb should be a pointer - validate it
        if (!nanbox_is_ptr(nb)) {
            return ts_value_make_undefined();
        }
        // Reject obviously invalid pointers (below 64KB)
        if (nb < 0x10000) {
            return ts_value_make_undefined();
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
                    TsValue* boxedObj = (TsValue*)obj;  // NaN-boxed pointer IS obj
                    TsValue* getterFunc = nanbox_from_tagged(getterVal);
                    return ts_function_call_with_this(getterFunc, boxedObj, 0, nullptr);
                }

                // No getter - look up the property directly
                TsValue k;
                k.type = ValueType::STRING_PTR;
                k.ptr_val = TsString::GetInterned(keyStr);
                TsValue val = currentMap->Get(k);
                if (val.type != ValueType::UNDEFINED) {
                    return nanbox_from_tagged(val);
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
            return nanbox_from_tagged(val);
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
                    return nanbox_from_tagged(val);
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
                    return nanbox_from_tagged(existing);
                }
                
                // Create a new empty object as the prototype
                TsMap* proto = TsMap::Create();
                TsValue protoStruct;
                protoStruct.type = ValueType::OBJECT_PTR;
                protoStruct.ptr_val = proto;
                func->properties->Set(protoKey, protoStruct);
                return ts_value_make_object(proto);
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
                // NaN-boxed pointer to the function IS the target
                TsValue* target = (TsValue*)func;
                return ts_value_make_native_function((void*)ts_function_call_native, (void*)target);
            }
            if (strcmp(keyStr, "apply") == 0) {
                TsValue* target = (TsValue*)func;
                return ts_value_make_native_function((void*)ts_function_apply_native, (void*)target);
            }

            // Function.prototype.bind: create a bound function wrapper
            if (strcmp(keyStr, "bind") == 0) {
                TsValue* target = (TsValue*)func;
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
                return nanbox_from_tagged(result);
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
        // Check if propName is a raw TsString* or a NaN-boxed value
        uint64_t pnb = (uint64_t)(uintptr_t)propName;
        if (nanbox_is_ptr(pnb) && pnb > NANBOX_UNDEFINED) {
            uint32_t magic = *(uint32_t*)propName;
            if (magic == 0x53545247) { // TsString::MAGIC
                keyCStr = ((TsString*)propName)->ToUtf8();
            }
        }
        if (!keyCStr) {
            TsString* key = (TsString*)ts_value_get_string((TsValue*)propName);
            if (key) {
                keyCStr = key->ToUtf8();
            }
        }

        if (!keyCStr) {
            return ts_value_make_undefined();
        }

        // Decode NaN-boxed val
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) return ts_value_make_undefined();
            return ts_object_get_property(ptr, keyCStr);
        }

        // Non-pointer values (numbers, booleans) - delegate to ts_object_get_property
        // which handles number methods etc.
        return ts_object_get_property(val, keyCStr);
    }

    // Helper to check if a value is a TsProxy
    static TsProxy* ts_extract_proxy(TsValue* boxedFunc) {
        if (!boxedFunc) return nullptr;
        uint64_t nb = nanbox_from_tsvalue_ptr(boxedFunc);
        if (!nanbox_is_ptr(nb)) return nullptr;
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return nullptr;

        // Only do dynamic_cast for objects that could be TsProxy
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 != 0x4D415053 && magic16 != 0x46554E43 && magic16 != 0x54415252) return nullptr;
        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)ptr);
        return proxy;
    }

    // Helper to safely extract TsFunction from boxed value
    static TsFunction* ts_extract_function(TsValue* boxedFunc) {
        if (!boxedFunc) {
            return nullptr;
        }
        uint64_t nb = nanbox_from_tsvalue_ptr(boxedFunc);
        if (!nanbox_is_ptr(nb)) {
            return nullptr;
        }
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) {
            return nullptr;
        }
        // Check if this is actually a TsFunction (has FUNC magic at offset 16)
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 != TsFunction::MAGIC) {
            return nullptr;
        }
        return (TsFunction*)ptr;
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
        if (!ts_gc_base(funcPtr)) return nullptr;
        TsObject* obj = (TsObject*)funcPtr;
        if (obj->magic == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)obj;
        }
        return nullptr;
    }

    // Helper to extract TsClosure from a boxed or raw value
    static TsClosure* ts_extract_closure(TsValue* boxedFunc) {
        if (!boxedFunc) return nullptr;

        uint64_t nb = nanbox_from_tsvalue_ptr(boxedFunc);
        // Non-pointer values can't be closures
        if (!nanbox_is_ptr(nb)) return nullptr;
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return nullptr;

        // Check if this object has the closure magic at offset 16
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)ptr;
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
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
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
        // Check for TsClosure first
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
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
        // Check for TsClosure first
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
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

        uint64_t nb1 = nanbox_from_tsvalue_ptr(val1);
        uint64_t nb2 = nanbox_from_tsvalue_ptr(val2);

        // Same bits = same value (except for doubles: NaN and -0)
        if (nb1 == nb2) return true;

        // Both int32: already handled by nb1==nb2
        // Both double: need special NaN/-0 handling
        if (nanbox_is_double(nb1) && nanbox_is_double(nb2)) {
            double d1 = nanbox_to_double(nb1);
            double d2 = nanbox_to_double(nb2);
            // NaN === NaN in SameValue
            if (d1 != d1 && d2 != d2) return true;
            if (d1 != d1 || d2 != d2) return false;
            // +0 !== -0 in SameValue
            if (d1 == 0.0 && d2 == 0.0) {
                return (1.0 / d1) > 0.0 == (1.0 / d2) > 0.0;
            }
            return d1 == d2;
        }

        // Both string pointers: compare by content
        if (nanbox_is_string_ptr(nb1) && nanbox_is_string_ptr(nb2)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nb1);
            TsString* s2 = (TsString*)nanbox_to_ptr(nb2);
            if (!s1 && !s2) return true;
            if (!s1 || !s2) return false;
            return strcmp(s1->ToUtf8(), s2->ToUtf8()) == 0;
        }

        // Different types or different pointers = not equal
        return false;
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
        TsString* propStr = (TsString*)ts_value_get_string(prop);
        if (!propStr) {
            // Try number-to-string coercion
            uint64_t propNb = nanbox_from_tsvalue_ptr(prop);
            if (nanbox_is_int32(propNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", nanbox_to_int32(propNb));
                propStr = TsString::Create(buf);
            } else if (nanbox_is_double(propNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.15g", nanbox_to_double(propNb));
                propStr = TsString::Create(buf);
            }
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
            TsValue desc = descMap->Get(nanbox_to_tagged(key));

            // Call defineProperty for each
            TsValue descVal;
            descVal.type = ValueType::OBJECT_PTR;
            // Direct field access — desc is a TsValue struct, not a NaN-boxed pointer
            descVal.ptr_val = desc.ptr_val;
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

        // Get property key via NaN-box decode
        TsValue propKey = nanbox_to_tagged(prop);

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
            targetMap->Set(nanbox_to_tagged(key), nanbox_to_tagged(val));
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
            void* propRaw = ts_nanbox_safe_unbox(prop);

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
            void* entryRaw = ts_nanbox_safe_unbox(entry);
            
            uint32_t entryMagic = *(uint32_t*)entryRaw;
            if (entryMagic != 0x41525259) continue;
            
            TsArray* pair = (TsArray*)entryRaw;
            if (pair->Length() < 2) continue;
            
            TsValue* key = (TsValue*)pair->Get(0);
            TsValue* val = (TsValue*)pair->Get(1);
            if (key && val) {
                result->Set(nanbox_to_tagged(key), nanbox_to_tagged(val));
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

            // Convert result to string key via NaN-box decode
            TsValue keyVal;
            uint64_t krNb = nanbox_from_tsvalue_ptr(keyResult);
            if (nanbox_is_string_ptr(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = nanbox_to_ptr(krNb);
            } else if (nanbox_is_int32(krNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", nanbox_to_int32(krNb));
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (nanbox_is_double(krNb)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", nanbox_to_double(krNb));
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(buf);
            } else if (nanbox_is_bool(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create(nanbox_to_bool(krNb) ? "true" : "false");
            } else if (nanbox_is_undefined(krNb)) {
                keyVal.type = ValueType::STRING_PTR;
                keyVal.ptr_val = TsString::Create("undefined");
            } else {
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
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);

        // String concatenation if either is a string
        if (nanbox_is_string_ptr(nba) || nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)ts_value_get_string(a);
            TsString* s2 = (TsString*)ts_value_get_string(b);
            if (!s1) s1 = TsString::Create("");
            if (!s2) s2 = TsString::Create("");
            return ts_value_make_string(TsString::Concat(s1, s2));
        }

        // Fast path: both int32
        if (nanbox_is_int32(nba) && nanbox_is_int32(nbb)) {
            int64_t result = (int64_t)nanbox_to_int32(nba) + (int64_t)nanbox_to_int32(nbb);
            return ts_value_make_int(result);
        }

        double d1 = nanbox_extract_double(a);
        double d2 = nanbox_extract_double(b);
        return ts_value_make_double(d1 + d2);
    }

    TsValue* ts_value_sub(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_int32(nba) && nanbox_is_int32(nbb)) {
            int64_t result = (int64_t)nanbox_to_int32(nba) - (int64_t)nanbox_to_int32(nbb);
            return ts_value_make_int(result);
        }
        return ts_value_make_double(nanbox_extract_double(a) - nanbox_extract_double(b));
    }

    TsValue* ts_value_mul(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_int32(nba) && nanbox_is_int32(nbb)) {
            int64_t result = (int64_t)nanbox_to_int32(nba) * (int64_t)nanbox_to_int32(nbb);
            return ts_value_make_int(result);
        }
        return ts_value_make_double(nanbox_extract_double(a) * nanbox_extract_double(b));
    }

    TsValue* ts_value_div(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        double d1 = nanbox_extract_double(a);
        double d2 = nanbox_extract_double(b);
        if (d2 == 0.0) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        return ts_value_make_double(d1 / d2);
    }

    TsValue* ts_value_mod(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_undefined();
        double d1 = nanbox_extract_double(a);
        double d2 = nanbox_extract_double(b);
        if (d2 == 0.0) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        return ts_value_make_double(std::fmod(d1, d2));
    }

    TsValue* ts_value_eq(TsValue* a, TsValue* b) {
        if (!a) a = ts_value_make_undefined();
        if (!b) b = ts_value_make_undefined();

        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);

        // Fast path: identical bit patterns
        if (nba == nbb) return ts_value_make_bool(true);

        // null == undefined
        bool a_nullish = nanbox_is_undefined(nba) || nanbox_is_null(nba);
        bool b_nullish = nanbox_is_undefined(nbb) || nanbox_is_null(nbb);
        if (a_nullish && b_nullish) return ts_value_make_bool(true);
        if (a_nullish || b_nullish) return ts_value_make_bool(false);

        // Same type: strict equal
        // Both numbers
        if (nanbox_is_number(nba) && nanbox_is_number(nbb)) {
            return ts_value_make_bool(nanbox_to_number(nba) == nanbox_to_number(nbb));
        }

        // Coerce to numbers
        return ts_value_make_bool(nanbox_extract_double(a) == nanbox_extract_double(b));
    }

    TsValue* ts_value_lt(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_string_ptr(nba) && nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nba);
            TsString* s2 = (TsString*)nanbox_to_ptr(nbb);
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) < 0);
        }
        return ts_value_make_bool(nanbox_extract_double(a) < nanbox_extract_double(b));
    }

    TsValue* ts_value_gt(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_string_ptr(nba) && nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nba);
            TsString* s2 = (TsString*)nanbox_to_ptr(nbb);
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) > 0);
        }
        return ts_value_make_bool(nanbox_extract_double(a) > nanbox_extract_double(b));
    }

    TsValue* ts_value_lte(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_string_ptr(nba) && nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nba);
            TsString* s2 = (TsString*)nanbox_to_ptr(nbb);
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) <= 0);
        }
        return ts_value_make_bool(nanbox_extract_double(a) <= nanbox_extract_double(b));
    }

    TsValue* ts_value_gte(TsValue* a, TsValue* b) {
        if (!a || !b) return ts_value_make_bool(false);
        uint64_t nba = nanbox_from_tsvalue_ptr(a);
        uint64_t nbb = nanbox_from_tsvalue_ptr(b);
        if (nanbox_is_string_ptr(nba) && nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nba);
            TsString* s2 = (TsString*)nanbox_to_ptr(nbb);
            return ts_value_make_bool(strcmp(s1->ToUtf8(), s2->ToUtf8()) >= 0);
        }
        return ts_value_make_bool(nanbox_extract_double(a) >= nanbox_extract_double(b));
    }

    TsString* ts_value_typeof(TsValue* v) {
        if (!v) return TsString::Create("undefined");

        uint64_t nb = nanbox_from_tsvalue_ptr(v);

        if (nanbox_is_undefined(nb)) return TsString::Create("undefined");
        if (nanbox_is_null(nb)) return TsString::Create("object"); // typeof null === "object"
        if (nanbox_is_int32(nb) || nanbox_is_double(nb)) return TsString::Create("number");
        if (nanbox_is_bool(nb)) return TsString::Create("boolean");

        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) return TsString::Create("object");

            // Check magic at various offsets
            uint32_t magic0 = *(uint32_t*)ptr;
            if (magic0 == 0x53545247) return TsString::Create("string");
            if (magic0 == 0x41525259) return TsString::Create("object");
            if (magic0 == 0x4D415053) return TsString::Create("object");
            if (magic0 == 0x53455453) return TsString::Create("object");
            if (magic0 == 0x46554E43) return TsString::Create("function");

            uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
            if (magic16 == 0x4D415053) return TsString::Create("object");
            if (magic16 == 0x46554E43) return TsString::Create("function");
            if (magic16 == 0x434C5352) return TsString::Create("function"); // TsClosure
            if (magic16 == 0x42494749) return TsString::Create("bigint");
            if (magic16 == 0x53594D42) return TsString::Create("symbol");

            uint32_t magic8 = *(uint32_t*)((char*)ptr + 8);
            if (magic8 == 0x46554E43) return TsString::Create("function");

            return TsString::Create("object");
        }

        return TsString::Create("undefined");
    }

    TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key) {
        if (!obj || !key) return ts_value_make_undefined();

        uint64_t objNb = nanbox_from_tsvalue_ptr(obj);
        uint64_t keyNb = nanbox_from_tsvalue_ptr(key);

        // Non-pointer obj: nothing to access
        if (!nanbox_is_ptr(objNb)) return ts_value_make_undefined();

        void* rawObj = nanbox_to_ptr(objNb);
        if (!rawObj) return ts_value_make_undefined();

        // Decode key
        bool keyIsInt = nanbox_is_int32(keyNb) || nanbox_is_double(keyNb);
        int64_t keyIdx = keyIsInt ? nanbox_to_int64(keyNb) : 0;
        TsString* keyStr = nullptr;
        if (nanbox_is_string_ptr(keyNb)) {
            keyStr = (TsString*)nanbox_to_ptr(keyNb);
        } else if (nanbox_is_ptr(keyNb) && !keyIsInt) {
            keyStr = (TsString*)ts_value_get_string(key);
        }

        // Check magic to determine object type
        uint32_t magic0 = *(uint32_t*)rawObj;

        // Handle TsArray
        if (magic0 == 0x41525259) {
            TsArray* arr = (TsArray*)rawObj;
            if (keyIsInt) {
                return ts_array_get_as_value(rawObj, keyIdx);
            }
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k && strcmp(k, "length") == 0) {
                    return ts_value_make_int(arr->Length());
                }
            }
            return ts_value_make_undefined();
        }

        // Handle TsString
        if (magic0 == 0x53545247) {
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);

        // Only do dynamic_cast for Proxy check if we know this is a TsObject-derived class
        if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC || magic16 == 0x54415252) {
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                return proxy->get(key, nullptr);
            }
        }
        
        // Check if this is a TsFunction and get its properties map
        if (magic16 == TsFunction::MAGIC) {
            TsFunction* func = (TsFunction*)rawObj;

            // Handle Function.prototype methods (bind, call, apply)
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) {
                    if (strcmp(k, "bind") == 0) {
                        return ts_value_make_native_function((void*)ts_function_bind_native, (void*)func);
                    }
                    if (strcmp(k, "call") == 0) {
                        return ts_value_make_native_function((void*)ts_function_call_native, (void*)func);
                    }
                    if (strcmp(k, "apply") == 0) {
                        return ts_value_make_native_function((void*)ts_function_apply_native, (void*)func);
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

            if (!func->properties) {
                return ts_value_make_undefined();
            }
            // Use nanbox_to_tagged to create TsValue key for map lookup
            TsValue funcKeyVal = nanbox_to_tagged(key);
            TsValue result = func->properties->Get(funcKeyVal);
            if (result.type == ValueType::UNDEFINED) {
                return ts_value_make_undefined();
            }
            return nanbox_from_tagged(result);
        }

        // Check for TsTypedArray (magic at offset 16)
        if (magic16 == 0x54415252) { // TsTypedArray::MAGIC = "TARR"
            TsTypedArray* ta = (TsTypedArray*)rawObj;
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
            } else if (keyIsInt) {
                return ts_value_make_double(ta->Get((size_t)keyIdx));
            }
            return ts_value_make_undefined();
        }

        // Check for TsTextEncoder (magic at offset 16)
        if (magic16 == 0x54584E43) { // TsTextEncoder::MAGIC "TXNC"
            TsTextEncoder* enc = (TsTextEncoder*)rawObj;
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k && strcmp(k, "encoding") == 0) return ts_value_make_string(enc->GetEncoding());
            }
            return ts_value_make_undefined();
        }
        // Check for TsTextDecoder (magic at offset 16)
        if (magic16 == 0x54584443) { // TsTextDecoder::MAGIC "TXDC"
            TsTextDecoder* dec = (TsTextDecoder*)rawObj;
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k && strcmp(k, "encoding") == 0) return ts_value_make_string(dec->GetEncoding());
                if (k && strcmp(k, "fatal") == 0) return ts_value_make_bool(dec->IsFatal());
                if (k && strcmp(k, "ignoreBOM") == 0) return ts_value_make_bool(dec->IgnoreBOM());
            }
            return ts_value_make_undefined();
        }

        // Check if this is actually a TsMap before using map operations
        // TsMap::MAGIC is at offset 16 (after vtable ptr + explicit vtable field)
        uint32_t magic20 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 20);
        uint32_t magic24 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 24);
        if (magic16 != 0x4D415053 && magic20 != 0x4D415053 && magic24 != 0x4D415053) {
            // Not a map - try ts_object_get_property as fallback
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) {
                    return ts_object_get_property(rawObj, k);
                }
            }
            return ts_value_make_undefined();
        }

        // Use TsMap::Get which handles hashing correctly (by string content, not pointer address)
        TsMap* map = (TsMap*)rawObj;

        // First check for a getter (__getter_<propertyName>) - walk prototype chain
        if (keyStr) {
            const char* propName = keyStr->ToUtf8();
            if (propName) {
                std::string getterKey = std::string("__getter_") + propName;
                TsValue gk;
                gk.type = ValueType::STRING_PTR;
                gk.ptr_val = TsString::GetInterned(getterKey.c_str());

                TsMap* currentMap = map;
                while (currentMap != nullptr) {
                    TsValue getterVal = currentMap->Get(gk);
                    if (getterVal.type != ValueType::UNDEFINED) {
                        TsValue* boxedObj = (TsValue*)rawObj;
                        TsValue* getterFunc = nanbox_from_tagged(getterVal);
                        return ts_function_call_with_this(getterFunc, boxedObj, 0, nullptr);
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }
        }

        // Walk prototype chain for property lookup
        TsValue result;
        result.type = ValueType::UNDEFINED;
        TsMap* currentMap = map;

        // Create proper TsValue key for map lookup via nanbox decode
        TsValue keyVal = nanbox_to_tagged(key);

        while (currentMap != nullptr) {
            result = currentMap->Get(keyVal);
            if (result.type != ValueType::UNDEFINED) {
                break;  // Found the property
            }
            currentMap = currentMap->GetPrototype();
        }

        if (result.type == ValueType::UNDEFINED) {
            // If not found in the map, check Object.prototype methods
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
            return ts_value_make_undefined();
        }

        return nanbox_from_tagged(result);
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

        uint64_t objNb = nanbox_from_tsvalue_ptr(obj);

        // Non-pointer obj: nothing to set on
        if (!nanbox_is_ptr(objNb)) return;

        void* rawObj = nanbox_to_ptr(objNb);
        if (!rawObj) return;

        // Check if this is a Proxy - dispatch through proxy trap
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC || magic16 == 0x54415252) {
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                proxy->set(key, value, nullptr);
                return;
            }
        }

        // Delegate to ts_object_set_prop_v which handles all cases
        TsValue objVal = nanbox_to_tagged(obj);
        TsValue keyVal = nanbox_to_tagged(key);
        TsValue valVal = nanbox_to_tagged(value);
        ts_object_set_prop_v(objVal, keyVal, valVal);
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
            // Direct field access — obj is a TsValue struct, not a NaN-boxed pointer
            void* rawObj = obj.ptr_val;
            if (rawObj) {
                uint32_t magic = *(uint32_t*)rawObj;
                if (magic == 0x41525259) { // TsArray::MAGIC
                    return ts_array_get_v(rawObj, idx);
                }
            }
        }

        // Coerce key to string — direct field access from TsValue struct
        TsString* keyStr = (key.type == ValueType::STRING_PTR) ? (TsString*)key.ptr_val : nullptr;
        if (!keyStr) {
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.i_val = 0;
            return undef;
        }

        // Convert obj struct back to NaN-boxed TsValue* for ts_value_get_property
        TsValue* nanboxedObj = nanbox_from_tagged(obj);
        TsValue* result = ts_value_get_property(nanboxedObj, (void*)keyStr);
        if (!result) {
            TsValue undef;
            undef.type = ValueType::UNDEFINED;
            undef.i_val = 0;
            return undef;
        }
        // result is NaN-boxed — convert back to TsValue struct
        return nanbox_to_tagged(result);
    }

    TsValue ts_object_set_prop_v(TsValue obj, TsValue key, TsValue value) {
        // Direct field access — obj is a TsValue struct, not a NaN-boxed pointer
        void* rawObj = obj.ptr_val;
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

        // Coerce key to string — direct field access from TsValue struct
        TsString* keyStr = (key.type == ValueType::STRING_PTR) ? (TsString*)key.ptr_val : nullptr;
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
            func->properties->Set(key, value);
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
                    TsValue* boxedObj = (TsValue*)rawObj;
                    TsValue* setterFunc = nanbox_from_tagged(setterVal);
                    TsValue* boxedVal = nanbox_from_tagged(value);
                    TsValue* args[] = { boxedVal };
                    ts_function_call_with_this(setterFunc, boxedObj, 1, args);
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
    // Args are NaN-boxed TsValue* pointers
    int ts_object_delete_property(void* objArg, void* keyArg) {
        if (!objArg || !keyArg) return 0;

        uint64_t objNb = nanbox_from_tsvalue_ptr((TsValue*)objArg);
        if (!nanbox_is_ptr(objNb)) return 0;

        void* rawMap = nanbox_to_ptr(objNb);
        if (!rawMap) return 0;

        // Check magic to confirm it's a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawMap + 16);
        if (magic != 0x4D415053) return 0; // Not a TsMap ("MAPS")

        TsMap* map = (TsMap*)rawMap;

        // Decode key via nanbox
        TsString* keyStr = (TsString*)ts_value_get_string((TsValue*)keyArg);
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
        uint64_t nb = nanbox_from_tsvalue_ptr(v);
        if (!nanbox_is_ptr(nb)) return ts_value_make_bool(false);
        void* raw = nanbox_to_ptr(nb);
        if (!raw) return ts_value_make_bool(false);
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
        void* obj = ts_nanbox_safe_unbox(ctx);

        // Check if it's a TsMap
        TsMap* map = dynamic_cast<TsMap*>((TsObject*)obj);
        if (!map) {
            return ts_value_make_bool(false);
        }

        // Get the property key as TsValue and check if the property exists
        TsValue* keyVal = argv[0];
        TsValue keyTV = nanbox_to_tagged(keyVal);
        bool result = map->Has(keyTV);
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
            return ts_value_make_object(TsMap::Create());
        }
        TsValue* val = argv[0];
        if (!val) {
            return ts_value_make_object(TsMap::Create());
        }
        // If already a pointer (object), return as-is
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (nanbox_is_ptr(nb)) {
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
            if (val) {
                uint64_t nb = nanbox_from_tsvalue_ptr(val);
                if (nanbox_is_int32(nb) || nanbox_is_double(nb)) {
                    return ts_value_make_object(TsArray::Create(nanbox_to_int64(nb)));
                }
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
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (nanbox_is_int32(nb)) {
            return ts_value_make_bool(false); // integers are never NaN
        }
        if (nanbox_is_double(nb)) {
            return ts_value_make_bool(std::isnan(nanbox_to_double(nb)));
        }
        if (nanbox_is_string_ptr(nb)) {
            TsString* str = (TsString*)nanbox_to_ptr(nb);
            if (!str || str->Length() == 0) {
                return ts_value_make_bool(true);
            }
            return ts_value_make_bool(true);
        }
        return ts_value_make_bool(true);
    }

    // isFinite(value) - returns true if value is finite
    TsValue* ts_isFinite_native(int argc, TsValue** argv, void* context) {
        if (argc < 1 || !argv[0]) {
            return ts_value_make_bool(false); // undefined is not finite
        }
        TsValue* val = argv[0];
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (nanbox_is_int32(nb)) {
            return ts_value_make_bool(true); // integers are always finite
        }
        if (nanbox_is_double(nb)) {
            return ts_value_make_bool(std::isfinite(nanbox_to_double(nb)));
        }
        return ts_value_make_bool(false);
    }

    void ts_runtime_init() {
        // Initialize Object global - make it callable
        TsValue* objectConstructor = ts_value_make_native_function((void*)ts_object_constructor_native, nullptr);
        
        // Get the TsFunction so we can add static methods as properties
        TsFunction* objectFunc = (TsFunction*)ts_value_get_object(objectConstructor);
        if (!objectFunc->properties) {
            objectFunc->properties = TsMap::Create();
        }
        
        // Object.keys
        TsValue keysKey; keysKey.type = ValueType::STRING_PTR; keysKey.ptr_val = TsString::Create("keys");
        objectFunc->properties->Set(keysKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_keys_native, nullptr)));
        
        // Object.values
        TsValue valuesKey; valuesKey.type = ValueType::STRING_PTR; valuesKey.ptr_val = TsString::Create("values");
        objectFunc->properties->Set(valuesKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_values_native, nullptr)));
        
        // Object.entries
        TsValue entriesKey; entriesKey.type = ValueType::STRING_PTR; entriesKey.ptr_val = TsString::Create("entries");
        objectFunc->properties->Set(entriesKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_entries_native, nullptr)));

        // Object.getOwnPropertyNames
        TsValue gopnKey; gopnKey.type = ValueType::STRING_PTR; gopnKey.ptr_val = TsString::Create("getOwnPropertyNames");
        objectFunc->properties->Set(gopnKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_getOwnPropertyNames_native, nullptr)));

        // Object.getPrototypeOf
        TsValue gpoKey; gpoKey.type = ValueType::STRING_PTR; gpoKey.ptr_val = TsString::Create("getPrototypeOf");
        objectFunc->properties->Set(gpoKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_getPrototypeOf_native, nullptr)));

        // Object.create
        TsValue createKey; createKey.type = ValueType::STRING_PTR; createKey.ptr_val = TsString::Create("create");
        objectFunc->properties->Set(createKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_create_native, nullptr)));

        // Object.freeze
        TsValue freezeKey; freezeKey.type = ValueType::STRING_PTR; freezeKey.ptr_val = TsString::Create("freeze");
        objectFunc->properties->Set(freezeKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_freeze_native, nullptr)));

        // Object.seal
        TsValue sealKey; sealKey.type = ValueType::STRING_PTR; sealKey.ptr_val = TsString::Create("seal");
        objectFunc->properties->Set(sealKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_seal_native, nullptr)));

        // Object.preventExtensions
        TsValue peKey; peKey.type = ValueType::STRING_PTR; peKey.ptr_val = TsString::Create("preventExtensions");
        objectFunc->properties->Set(peKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_preventExtensions_native, nullptr)));

        // Object.isFrozen
        TsValue isFrozenKey; isFrozenKey.type = ValueType::STRING_PTR; isFrozenKey.ptr_val = TsString::Create("isFrozen");
        objectFunc->properties->Set(isFrozenKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_isFrozen_native, nullptr)));

        // Object.isSealed
        TsValue isSealedKey; isSealedKey.type = ValueType::STRING_PTR; isSealedKey.ptr_val = TsString::Create("isSealed");
        objectFunc->properties->Set(isSealedKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_isSealed_native, nullptr)));

        // Object.isExtensible
        TsValue isExtensibleKey; isExtensibleKey.type = ValueType::STRING_PTR; isExtensibleKey.ptr_val = TsString::Create("isExtensible");
        objectFunc->properties->Set(isExtensibleKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_isExtensible_native, nullptr)));

        // Object.defineProperty
        TsValue dpKey; dpKey.type = ValueType::STRING_PTR; dpKey.ptr_val = TsString::Create("defineProperty");
        objectFunc->properties->Set(dpKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_defineProperty_native, nullptr)));

        // Object.defineProperties
        TsValue dpsKey; dpsKey.type = ValueType::STRING_PTR; dpsKey.ptr_val = TsString::Create("defineProperties");
        objectFunc->properties->Set(dpsKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_defineProperties_native, nullptr)));

        // Object.getOwnPropertyDescriptor
        TsValue gopdKey; gopdKey.type = ValueType::STRING_PTR; gopdKey.ptr_val = TsString::Create("getOwnPropertyDescriptor");
        objectFunc->properties->Set(gopdKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_getOwnPropertyDescriptor_native, nullptr)));

        // Object.getOwnPropertyDescriptors (ES2017)
        TsValue gopdsKey; gopdsKey.type = ValueType::STRING_PTR; gopdsKey.ptr_val = TsString::Create("getOwnPropertyDescriptors");
        objectFunc->properties->Set(gopdsKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_getOwnPropertyDescriptors_native, nullptr)));

        Object = objectConstructor;

        // Initialize console
        TsMap* consoleMap = TsMap::Create();
        TsValue logKey; logKey.type = ValueType::STRING_PTR; logKey.ptr_val = TsString::Create("log");
        consoleMap->Set(logKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_console_log_native, nullptr)));
        console = ts_value_make_object(consoleMap);

        // Initialize Array - make it callable
        TsValue* arrayConstructor = ts_value_make_native_function((void*)ts_array_constructor_native, nullptr);
        
        // Get the TsFunction so we can add static methods as properties
        TsFunction* arrayFunc = (TsFunction*)ts_value_get_object(arrayConstructor);
        if (!arrayFunc->properties) {
            arrayFunc->properties = TsMap::Create();
        }
        
        // Array.isArray
        TsValue isArrayKey; isArrayKey.type = ValueType::STRING_PTR; isArrayKey.ptr_val = TsString::Create("isArray");
        arrayFunc->properties->Set(isArrayKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_array_isArray_native, nullptr)));
        
        Array = arrayConstructor;

        // Initialize Math with minimal functions used by common JS libs
        TsMap* mathMap = TsMap::Create();
        TsValue randomKey; randomKey.type = ValueType::STRING_PTR; randomKey.ptr_val = TsString::Create("random");
        TsValue floorKey; floorKey.type = ValueType::STRING_PTR; floorKey.ptr_val = TsString::Create("floor");
        TsValue ceilKey; ceilKey.type = ValueType::STRING_PTR; ceilKey.ptr_val = TsString::Create("ceil");
        TsValue absKey; absKey.type = ValueType::STRING_PTR; absKey.ptr_val = TsString::Create("abs");
        TsValue maxKey; maxKey.type = ValueType::STRING_PTR; maxKey.ptr_val = TsString::Create("max");
        TsValue minKey; minKey.type = ValueType::STRING_PTR; minKey.ptr_val = TsString::Create("min");
        mathMap->Set(randomKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_random_native, nullptr)));
        mathMap->Set(floorKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_floor_native, nullptr)));
        mathMap->Set(ceilKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_ceil_native, nullptr)));
        mathMap->Set(absKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_abs_native, nullptr)));
        mathMap->Set(maxKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_max_native, nullptr)));
        mathMap->Set(minKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_math_min_native, nullptr)));
        Math = ts_value_make_object(mathMap);

        // Initialize JSON with parse/stringify
        TsMap* jsonMap = TsMap::Create();
        TsValue parseKey; parseKey.type = ValueType::STRING_PTR; parseKey.ptr_val = TsString::Create("parse");
        TsValue stringifyKey; stringifyKey.type = ValueType::STRING_PTR; stringifyKey.ptr_val = TsString::Create("stringify");
        jsonMap->Set(parseKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_json_parse_native, nullptr)));
        jsonMap->Set(stringifyKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_json_stringify_native, nullptr)));
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
            uint64_t ctorNb = nanbox_from_tsvalue_ptr(ctor);
            TsFunction* func = nanbox_is_ptr(ctorNb) ? (TsFunction*)nanbox_to_ptr(ctorNb) : nullptr;
            if (!func) return nullptr;
            if (!func->properties) {
                func->properties = TsMap::Create();
            }
            
            // Create prototype object with methods
            TsMap* protoMap = TsMap::Create();
            
            // Add toString method
            TsValue toStringKey = makeKey("toString");
            if (isFunction) {
                protoMap->Set(toStringKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_function_toString_native, nullptr)));
            } else {
                protoMap->Set(toStringKey, nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_toString_native, nullptr)));
            }
            
            // Add valueOf method
            protoMap->Set(makeKey("valueOf"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr)));
            
            // Add hasOwnProperty for Object-like prototypes
            protoMap->Set(makeKey("hasOwnProperty"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr)));
            
            // Add .prototype property
            TsValue protoKey;
            protoKey.type = ValueType::STRING_PTR;
            protoKey.ptr_val = TsString::Create("prototype");
            func->properties->Set(protoKey, nanbox_to_tagged(ts_value_make_object(protoMap)));
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
        objectProtoMap->Set(makeKey("toString"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_toString_native, nullptr)));
        objectProtoMap->Set(makeKey("valueOf"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr)));
        objectProtoMap->Set(makeKey("hasOwnProperty"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr)));
        objectFunc->properties->Set(protoKey, nanbox_to_tagged(ts_value_make_object(objectProtoMap)));
        
        // Create Array.prototype with methods
        TsMap* arrayProtoMap = TsMap::Create();
        arrayProtoMap->Set(makeKey("toString"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_toString_native, nullptr)));
        arrayProtoMap->Set(makeKey("valueOf"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr)));
        arrayFunc->properties->Set(protoKey, nanbox_to_tagged(ts_value_make_object(arrayProtoMap)));
        
        // Misc global values
        globalMap->Set(makeKey("undefined"), nanbox_to_tagged(ts_value_make_undefined()));
        globalMap->Set(makeKey("NaN"), nanbox_to_tagged(ts_value_make_double(std::numeric_limits<double>::quiet_NaN())));
        globalMap->Set(makeKey("Infinity"), nanbox_to_tagged(ts_value_make_double(std::numeric_limits<double>::infinity())));
        globalMap->Set(makeKey("isNaN"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_isNaN_native, nullptr)));
        globalMap->Set(makeKey("isFinite"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_isFinite_native, nullptr)));

        global = ts_value_make_object(globalMap);
        globalThis = global;  // ES2020: globalThis is an alias for global
        // global.global === global, globalThis.globalThis === globalThis
        globalMap->Set(makeKey("global"), *global);
        globalMap->Set(makeKey("globalThis"), *global);
    }

    void ts_module_register(TsValue* path, TsValue* exports) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return;
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
        TsString* s = (TsString*)ts_value_get_string(specifier);
        if (!s) {
            return ts_value_make_undefined();
        }
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
                uint64_t modNb = nanbox_from_tsvalue_ptr(moduleObj);
                if (nanbox_is_ptr(modNb)) {
                    void* rawMod = nanbox_to_ptr(modNb);
                    // Use inline map operations to get "exports" property
                    TsString* exportsStr = TsString::Create("exports");
                    uint64_t hash = (uint64_t)exportsStr;
                    int64_t bucket = __ts_map_find_bucket(rawMod, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)exportsStr);
                    if (bucket >= 0) {
                        TsValue result;
                        __ts_map_get_value_at(rawMod, bucket, reinterpret_cast<uint8_t*>(&result.type), &result.i_val);
                        if (result.type != ValueType::UNDEFINED) {
                            return nanbox_from_tagged(result);
                        }

                        // CommonJS default: module.exports starts as {}
                        TsMap* exportsMap = TsMap::Create();
                        TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                        TsValue exportsKey;
                        exportsKey.type = ValueType::STRING_PTR;
                        exportsKey.ptr_val = exportsStr;
                        ((TsMap*)rawMod)->Set(exportsKey, nanbox_to_tagged(exportsBoxed));
                        return exportsBoxed;
                    }

                    // No exports key at all: initialize to {}
                    TsMap* exportsMap = TsMap::Create();
                    TsValue* exportsBoxed = ts_value_make_object(exportsMap);
                    TsValue exportsKey;
                    exportsKey.type = ValueType::STRING_PTR;
                    exportsKey.ptr_val = exportsStr;
                    ((TsMap*)rawMod)->Set(exportsKey, nanbox_to_tagged(exportsBoxed));
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

        // NaN-boxed path: decode to get raw pointer
        uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)obj);
        void* raw = nullptr;
        if (nanbox_is_ptr(nb)) {
            raw = nanbox_to_ptr(nb);
        }
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

        void* rawKey = ts_nanbox_safe_unbox(key);

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        if (value) {
            map->Set(keyVal, nanbox_to_tagged(value));
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

        void* rawKey = ts_nanbox_safe_unbox(key);

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        TsValue result = map->Get(keyVal);
        return nanbox_from_tagged(result);
    }

    bool ts_weakmap_has(void* weakmap, void* key) {
        if (!weakmap || !key) return false;
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_nanbox_safe_unbox(key);

        TsValue keyVal;
        keyVal.type = ValueType::OBJECT_PTR;
        keyVal.ptr_val = rawKey;

        return map->Has(keyVal);
    }

    bool ts_weakmap_delete(void* weakmap, void* key) {
        if (!weakmap || !key) return false;
        TsMap* map = (TsMap*)weakmap;

        void* rawKey = ts_nanbox_safe_unbox(key);

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

        void* rawValue = ts_nanbox_safe_unbox(value);

        TsValue val;
        val.type = ValueType::OBJECT_PTR;
        val.ptr_val = rawValue;

        set->Add(val);
        return weakset;
    }

    bool ts_weakset_has(void* weakset, void* value) {
        if (!weakset || !value) return false;
        TsSet* set = (TsSet*)weakset;

        void* rawValue = ts_nanbox_safe_unbox(value);

        TsValue val;
        val.type = ValueType::OBJECT_PTR;
        val.ptr_val = rawValue;

        return set->Has(val);
    }

    bool ts_weakset_delete(void* weakset, void* value) {
        if (!weakset || !value) return false;
        TsSet* set = (TsSet*)weakset;

        void* rawValue = ts_nanbox_safe_unbox(value);

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
            void* rawProps = ts_nanbox_safe_unbox(props);
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
            void* rawChildren = ts_nanbox_safe_unbox(children);
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
