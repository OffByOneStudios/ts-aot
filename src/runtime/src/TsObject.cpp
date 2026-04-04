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
#include "TsConsString.h"
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
#include "TsFlatObject.h"
#include "TsNanBox.h"
#include "TsDate.h"
#include "TsRuntime.h"

// Virtual-inheritance HTTP class dispatch, registered by TsHttp.cpp at startup.
// Can't include TsHttp.h here (pulls in TsHeaders from separate extension lib).
// Instead, TsHttp.cpp registers vtable pointers and dispatch callbacks.
typedef TsValue (*VtableDispatchFn)(void* obj, const char* key);
typedef bool (*VtableSetDispatchFn)(void* obj, const char* key, TsValue value);
struct VtableDispatchEntry {
    uint64_t vtable;
    VtableDispatchFn dispatch;
    VtableSetDispatchFn setDispatch;
    bool isEventEmitter;
};
static VtableDispatchEntry g_vtable_dispatch[8];
static int g_vtable_dispatch_count = 0;

extern "C" void ts_register_vtable_dispatch(uint64_t vtable, VtableDispatchFn fn, bool isEventEmitter) {
    if (g_vtable_dispatch_count < 8) {
        g_vtable_dispatch[g_vtable_dispatch_count++] = {vtable, fn, nullptr, isEventEmitter};
    }
}

extern "C" void ts_register_vtable_set_dispatch(uint64_t vtable, VtableSetDispatchFn fn) {
    for (int i = 0; i < g_vtable_dispatch_count; i++) {
        if (g_vtable_dispatch[i].vtable == vtable) {
            g_vtable_dispatch[i].setDispatch = fn;
            return;
        }
    }
}

// Check if a vtable address belongs to a registered EventEmitter subclass.
// Used by events extension to handle virtual-inheritance on Linux.
extern "C" bool ts_is_registered_event_emitter(uint64_t vtable) {
    for (int i = 0; i < g_vtable_dispatch_count; i++) {
        if (vtable == g_vtable_dispatch[i].vtable && g_vtable_dispatch[i].isEventEmitter) {
            return true;
        }
    }
    return false;
}

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
#include <csetjmp>
#ifdef _MSC_VER
#include <excpt.h>
#endif

// Exception handling (from Core.cpp) — for safe getter invocation
extern "C" void* ts_push_exception_handler();
extern "C" void ts_pop_exception_handler();
extern "C" void ts_set_exception(TsValue* exception);
extern "C" TsValue* ts_get_exception();
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <spdlog/spdlog.h>
#include <unicode/regex.h>
#include <unicode/unistr.h>

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

// Global side-map for dynamically assigned properties on native TsObject subclasses.
// Native objects (TsServerResponse, TsIncomingMessage, etc.) are C++ objects with fixed
// struct layouts — they can't store arbitrary JS properties. This map associates an
// external TsMap* property bag with any native object pointer, enabling patterns like
// Express's setPrototypeOf(res, app.response) which copies methods onto native objects.
static std::unordered_map<void*, TsMap*> g_native_object_props;

static struct NativePropsScanner {
    NativePropsScanner() {
        ts_gc_register_scanner([](void*) {
            for (auto& [key, val] : g_native_object_props) {
                if (val) ts_gc_mark_object(val);
            }
        }, nullptr);
        ts_gc_register_minor_fixup([](void*) {
            for (auto& [key, val] : g_native_object_props) {
                if (val) {
                    void* fixed = ts_gc_minor_lookup_forward(val);
                    if (fixed != val) val = (TsMap*)fixed;
                }
            }
        }, nullptr);
    }
} g_native_props_scanner;

static TsMap* getNativeProps(void* obj) {
    auto it = g_native_object_props.find(obj);
    return (it != g_native_object_props.end()) ? it->second : nullptr;
}

static TsMap* getOrCreateNativeProps(void* obj) {
    auto it = g_native_object_props.find(obj);
    if (it != g_native_object_props.end()) return it->second;
    TsMap* props = TsMap::Create();
    g_native_object_props[obj] = props;
    return props;
}

// Debug hook: captures the TsMap* backing lodash's synthetic module object.
// Used by TsMap.cpp to trace writes to module.exports.
extern "C" void* g_debug_lodash_module_map = nullptr;

// ============================================================================
// Flat object EventEmitter delegation
// When a class extends EventEmitter and is compiled as a flat object,
// EventEmitter methods (on, emit, once, etc.) are delegated to a lazily-
// created backing TsEventEmitter stored in the overflow map under "__emitter__".
// ============================================================================

// Get or create the backing TsEventEmitter for any object (flat or TsMap).
// Uses TsMap storage under key "__emitter__" for the backing emitter.
static TsEventEmitter* obj_get_or_create_emitter(void* obj) {
    TsMap* storageMap = nullptr;

    if (is_flat_object(obj)) {
        uint32_t shapeId = flat_object_shape_id(obj);
        ShapeDescriptor* desc = ts_shape_lookup(shapeId);
        if (!desc) return nullptr;

        void** overflowPtr = flat_object_overflow_ptr(obj, desc->numSlots);
        storageMap = (TsMap*)*overflowPtr;

        // Check if __emitter__ already exists
        if (storageMap) {
            TsString* key = TsString::Create("__emitter__");
            TsValue result = storageMap->Get(TsValue(key));
            if (result.type != ValueType::UNDEFINED && result.ptr_val) {
                return (TsEventEmitter*)result.ptr_val;
            }
        }

        // Create new TsEventEmitter
        void* mem = ts_gc_alloc(sizeof(TsEventEmitter));
        TsEventEmitter* emitter = new (mem) TsEventEmitter();

        // Store in overflow map
        if (!storageMap) {
            storageMap = TsMap::Create();
            *overflowPtr = storageMap;
            ts_gc_write_barrier(overflowPtr, storageMap);
        }
        TsString* key = TsString::Create("__emitter__");
        TsValue emitterVal;
        emitterVal.type = ValueType::OBJECT_PTR;
        emitterVal.ptr_val = emitter;
        storageMap->Set(TsValue(key), emitterVal);
        return emitter;
    } else {
        // TsMap-based object
        storageMap = (TsMap*)obj;
        TsString* key = TsString::Create("__emitter__");
        TsValue result = storageMap->Get(TsValue(key));
        if (result.type != ValueType::UNDEFINED && result.ptr_val) {
            return (TsEventEmitter*)result.ptr_val;
        }

        // Create new TsEventEmitter
        void* mem = ts_gc_alloc(sizeof(TsEventEmitter));
        TsEventEmitter* emitter = new (mem) TsEventEmitter();

        TsValue emitterVal;
        emitterVal.type = ValueType::OBJECT_PTR;
        emitterVal.ptr_val = emitter;
        storageMap->Set(TsValue(key), emitterVal);
        return emitter;
    }
}


// Helper: extract raw string pointer from NaN-boxed TsValue*
static void* flat_ee_unbox_string(TsValue* v) {
    uint64_t nb = (uint64_t)(uintptr_t)v;
    if (nanbox_is_ptr(nb)) return nanbox_to_ptr(nb);
    return v;  // already raw pointer
}

// Native function wrappers for flat object EventEmitter delegation.
// These receive the flat object as context, get its backing emitter, and delegate.
// IMPORTANT: argv[] entries are NaN-boxed TsValue*, but ts_event_emitter_on etc.
// expect raw TsString* for event name. We need to unbox the event name.
// The callback can stay NaN-boxed since TsEventEmitter stores/calls it as void*.
static TsValue* flat_ee_on_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2 || !ctx) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    void* eventStr = flat_ee_unbox_string(argv[0]);
    e->On(((TsString*)eventStr)->ToUtf8(), argv[1]);
    return (TsValue*)(uintptr_t)nanbox_ptr(ctx);  // return this for chaining
}

static TsValue* flat_ee_once_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2 || !ctx) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    void* eventStr = flat_ee_unbox_string(argv[0]);
    e->Once(((TsString*)eventStr)->ToUtf8(), argv[1]);
    return (TsValue*)(uintptr_t)nanbox_ptr(ctx);
}

static TsValue* flat_ee_emit_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !ctx) return (TsValue*)(uintptr_t)nanbox_bool(false);
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)nanbox_bool(false);
    void* eventStr = flat_ee_unbox_string(argv[0]);
    // Collect remaining args
    int emitArgc = argc - 1;
    void** emitArgv = nullptr;
    if (emitArgc > 0) {
        emitArgv = (void**)argv + 1;  // argv[1..] are the emit arguments
    }
    bool result = e->Emit(((TsString*)eventStr)->ToUtf8(), emitArgc, emitArgv);
    return (TsValue*)(uintptr_t)nanbox_bool(result);
}

static TsValue* flat_ee_off_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2 || !ctx) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    void* eventStr = flat_ee_unbox_string(argv[0]);
    e->RemoveListener(((TsString*)eventStr)->ToUtf8(), argv[1]);
    return (TsValue*)(uintptr_t)nanbox_ptr(ctx);
}

static TsValue* flat_ee_removeAllListeners_native(void* ctx, int argc, TsValue** argv) {
    if (!ctx) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    if (argc >= 1) {
        void* eventStr = flat_ee_unbox_string(argv[0]);
        e->RemoveAllListeners(((TsString*)eventStr)->ToUtf8());
    } else {
        e->RemoveAllListeners(nullptr);
    }
    return (TsValue*)(uintptr_t)nanbox_ptr(ctx);
}

static TsValue* flat_ee_listenerCount_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1 || !ctx) return (TsValue*)(uintptr_t)nanbox_int32(0);
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)nanbox_int32(0);
    void* eventStr = flat_ee_unbox_string(argv[0]);
    int count = e->ListenerCount(((TsString*)eventStr)->ToUtf8());
    return (TsValue*)(uintptr_t)nanbox_int32(count);
}

static TsValue* flat_ee_eventNames_native(void* ctx, int argc, TsValue** argv) {
    if (!ctx) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    TsEventEmitter* e = obj_get_or_create_emitter(ctx);
    if (!e) return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    void* names = e->EventNames();
    return names ? (TsValue*)(uintptr_t)nanbox_ptr(names) : (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
}

// Check if a property name is an EventEmitter method and return the appropriate wrapper
static TsValue* flat_try_ee_method(void* obj, const char* keyStr) {
    if (strcmp(keyStr, "on") == 0 || strcmp(keyStr, "addListener") == 0)
        return ts_value_make_native_function((void*)flat_ee_on_native, obj);
    if (strcmp(keyStr, "once") == 0)
        return ts_value_make_native_function((void*)flat_ee_once_native, obj);
    if (strcmp(keyStr, "emit") == 0)
        return ts_value_make_native_function((void*)flat_ee_emit_native, obj);
    if (strcmp(keyStr, "off") == 0 || strcmp(keyStr, "removeListener") == 0)
        return ts_value_make_native_function((void*)flat_ee_off_native, obj);
    if (strcmp(keyStr, "removeAllListeners") == 0)
        return ts_value_make_native_function((void*)flat_ee_removeAllListeners_native, obj);
    if (strcmp(keyStr, "listenerCount") == 0)
        return ts_value_make_native_function((void*)flat_ee_listenerCount_native, obj);
    if (strcmp(keyStr, "eventNames") == 0)
        return ts_value_make_native_function((void*)flat_ee_eventNames_native, obj);
    return nullptr;
}

// nanbox_from_tagged / nanbox_to_tagged are now in TsObject.h

// Helper: check if a NaN-boxed value represents a string pointer
static inline bool nanbox_is_string_ptr(uint64_t nb) {
    if (!nanbox_is_ptr(nb)) return false;
    void* ptr = nanbox_to_ptr(nb);
    return ts_is_any_string(ptr);
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
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) { // TsString or TsConsString
            TsString* s = ts_ensure_flat(ptr);
            try { return std::stod(s->ToUtf8()); }
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

// Global argument count for the most recent function call.
// Used to implement the 'arguments' object in the JS slow path.
// Set before each call by ts_call_N/ts_call_with_this_N/ts_function_call_with_this.
static int64_t ts_last_call_argc = 0;

extern "C" {

void ts_set_call_this(void* thisArg) {
    ts_call_this_value = thisArg;
}

void* ts_get_call_this() {
    // Don't clear after read - functions may reference 'this' multiple times
    // (e.g., this._events[evt], this._eventsCount, this.removeListener)
    // Clearing is handled by save/restore in ts_function_call_with_this.
    return ts_call_this_value;
}

int64_t ts_get_last_call_argc() {
    return ts_last_call_argc;
}

void ts_set_last_call_argc(int64_t argc) {
    ts_last_call_argc = argc;
}

// Create an arguments array from function parameters.
// p0-p9: the parameter values (up to 10), padded with undefined from compiler.
// Uses ts_last_call_argc to determine how many were actually passed.
void* ts_create_arguments_from_params(
    void* p0, void* p1, void* p2, void* p3, void* p4,
    void* p5, void* p6, void* p7, void* p8, void* p9) {
    int64_t argc = ts_last_call_argc;
    if (argc < 0) argc = 0;
    if (argc > 10) argc = 10;
    TsArray* arr = TsArray::Create();
    void* params[] = {p0, p1, p2, p3, p4, p5, p6, p7, p8, p9};
    for (int64_t i = 0; i < argc; i++) {
        arr->Push((int64_t)params[i]);
    }
    return ts_value_make_object(arr);
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
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) { // TsString or TsConsString
            return ts_value_make_string(ts_ensure_flat(ptr));
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

    TsValue* ts_value_make_function_named(void* funcPtr, void* context, void* name) {
        TsFunction* func = new (ts_alloc(sizeof(TsFunction))) TsFunction(funcPtr, context, FunctionType::COMPILED, -1);
        func->name = (TsString*)name;
        return (TsValue*)func;
    }

    TsValue* ts_value_make_native_function(void* funcPtr, void* context) {
        void* mem = ts_alloc(sizeof(TsFunction));
        TsFunction* func = new (mem) TsFunction(funcPtr, context, FunctionType::NATIVE);
        func->magic = TsFunction::MAGIC;
        return (TsValue*)func;
    }

    // Built-in function wrappers for first-class value use
    // These wrap runtime functions as native function callbacks (void* ctx, int argc, TsValue** argv)

    static TsValue* builtin_encodeURIComponent_native(void* ctx, int argc, TsValue** argv) {
        void* arg = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return (TsValue*)ts_encode_uri_component(arg);
    }
    static TsValue* builtin_decodeURIComponent_native(void* ctx, int argc, TsValue** argv) {
        void* arg = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return (TsValue*)ts_decode_uri_component(arg);
    }
    static TsValue* builtin_encodeURI_native(void* ctx, int argc, TsValue** argv) {
        void* arg = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return (TsValue*)ts_encode_uri(arg);
    }
    static TsValue* builtin_decodeURI_native(void* ctx, int argc, TsValue** argv) {
        void* arg = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return (TsValue*)ts_decode_uri(arg);
    }
    static TsValue* builtin_parseInt_native(void* ctx, int argc, TsValue** argv) {
        TsValue* arg = (argc >= 1 && argv) ? argv[0] : nullptr;
        return (TsValue*)ts_value_make_int(ts_number_parseInt(arg));
    }
    static TsValue* builtin_parseFloat_native(void* ctx, int argc, TsValue** argv) {
        TsValue* arg = (argc >= 1 && argv) ? argv[0] : nullptr;
        return (TsValue*)ts_value_make_double(ts_number_parseFloat(arg));
    }

    void* ts_get_builtin_function(void* nameStr) {
        TsString* name = ts_ensure_flat(nameStr);
        if (!name) return nullptr;
        const char* n = name->ToUtf8();
        if (!n) return nullptr;
        if (strcmp(n, "encodeURIComponent") == 0) return ts_value_make_native_function((void*)builtin_encodeURIComponent_native, nullptr);
        if (strcmp(n, "decodeURIComponent") == 0) return ts_value_make_native_function((void*)builtin_decodeURIComponent_native, nullptr);
        if (strcmp(n, "encodeURI") == 0) return ts_value_make_native_function((void*)builtin_encodeURI_native, nullptr);
        if (strcmp(n, "decodeURI") == 0) return ts_value_make_native_function((void*)builtin_decodeURI_native, nullptr);
        if (strcmp(n, "parseInt") == 0) return ts_value_make_native_function((void*)builtin_parseInt_native, nullptr);
        if (strcmp(n, "parseFloat") == 0) return ts_value_make_native_function((void*)builtin_parseFloat_native, nullptr);
        return nullptr;
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
        // It's a pointer - check if it's a TsString or TsConsString
        void* ptr = nanbox_to_ptr(nb);
        uint32_t magic = *(uint32_t*)ptr;
        if (magic == 0x53545247) { // TsString::MAGIC
            return ptr;
        }
        if (magic == TsConsString::MAGIC) {
            return ((TsConsString*)ptr)->Flatten();
        }
        // Not a string - try to convert
        return ts_string_from_value(v);
    }

    // Extract raw string pointer WITHOUT flattening CONS strings.
    // Like ts_value_get_string but preserves TsConsString* as-is.
    // Used by string concat to avoid O(n) flatten on each += operation.
    void* ts_string_extract_ptr(void* v) {
        if (!v) return nullptr;
        // Fast path: already a raw string pointer (TsString* or TsConsString*)
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
            return v;
        }
        // Might be a boxed TsValue* - try to extract
        uint8_t tag = *(uint8_t*)v;
        if (tag <= 10) {
            uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)v);
            if (!nanbox_is_number(nb) && !nanbox_is_special(nb)) {
                void* ptr = nanbox_to_ptr(nb);
                if (ptr) {
                    uint32_t m = *(uint32_t*)ptr;
                    if (m == 0x53545247 || m == TsConsString::MAGIC) {
                        return ptr;
                    }
                }
            }
            // Convert non-string values to string
            void* result = ts_string_from_value((TsValue*)v);
            if (result) return result;
        }
        return TsString::Create("");
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
            bool lIsStr = (lmagic == 0x53545247 || lmagic == TsConsString::MAGIC);
            bool rIsStr = (rmagic == 0x53545247 || rmagic == TsConsString::MAGIC);
            if (lIsStr && rIsStr) {
                TsString* ls = ts_ensure_flat(lp);
                TsString* rs = ts_ensure_flat(rp);
                return std::strcmp(ls->ToUtf8(), rs->ToUtf8()) == 0;
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
        if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) return ts_string_like_length(rawPtr);

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
            if (m == 0x53545247 || m == TsConsString::MAGIC) { // TsString or TsConsString
                TsString* s = ts_ensure_flat(rawPtr);
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

    TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv);

    // Forward declaration for ts_string_search_regexp (defined in TsString.cpp)
    int64_t ts_string_search_regexp(void* str, void* regexp);

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
        if (argc >= 2 && argv && argv[1]) {
            int64_t startPos = ts_value_get_int(argv[1]);
            return ts_value_make_int(ts_string_indexOf_from(str, search, startPos));
        }
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
        if (argc >= 1 && argv && argv[0]) {
            // Check if separator is a RegExp
            void* rawSep = ts_value_get_object((TsValue*)argv[0]);
            if (!rawSep) rawSep = (void*)argv[0];
            uint32_t magic = *(uint32_t*)rawSep;
            if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                return ts_value_make_object(ts_string_split_regexp(str, rawSep));
            }
            // String separator
            void* sep = ts_value_get_string(argv[0]);
            if (!sep) sep = (void*)argv[0];
            return ts_value_make_object(ts_string_split(str, sep));
        }
        return ts_value_make_object(ts_string_split(str, nullptr));
    }
    // Helper: check if a TsValue is callable (closure or function)
    static bool ts_value_is_callable(TsValue* val) {
        if (!val) return false;
        uint64_t nb = nanbox_from_tsvalue_ptr(val);
        if (!nanbox_is_ptr(nb)) return false;
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return false;
        // Check for TsClosure magic at offset 16
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x434C5352) return true; // TsClosure::MAGIC "CLSR"
        if (magic16 == 0x46554E43) return true; // TsFunction::MAGIC "FUNC"
        return false;
    }

    // Helper: call callback with variable number of TsValue* args
    static TsValue* ts_call_variadic(TsValue* fn, TsValue** args, int count) {
        switch (count) {
            case 0: return ts_call_0(fn);
            case 1: return ts_call_1(fn, args[0]);
            case 2: return ts_call_2(fn, args[0], args[1]);
            case 3: return ts_call_3(fn, args[0], args[1], args[2]);
            case 4: return ts_call_4(fn, args[0], args[1], args[2], args[3]);
            case 5: return ts_call_5(fn, args[0], args[1], args[2], args[3], args[4]);
            case 6: return ts_call_6(fn, args[0], args[1], args[2], args[3], args[4], args[5]);
            case 7: return ts_call_7(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
            case 8: return ts_call_8(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            case 9: return ts_call_9(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
            case 10: return ts_call_10(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
            default: return ts_call_10(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
        }
    }

    // String.replace with regex and callback function
    static TsValue* ts_string_replace_callback_regex(TsString* str, TsRegExp* regexp, TsValue* callback) {
        icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
        if (!matcher) return ts_value_make_string(str);

        icu::UnicodeString input = str->ToUnicodeString();
        matcher->reset(input);

        bool isGlobal = regexp->IsGlobal();
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString result;
        int32_t lastEnd = 0;

        while (matcher->find()) {
            int32_t matchStart = matcher->start(status);
            int32_t matchEnd = matcher->end(status);

            // Append text before this match
            result.append(input, lastEnd, matchStart - lastEnd);

            // Build callback args: (match, g1, g2, ..., offset, originalString)
            int32_t groupCount = matcher->groupCount();
            int totalArgs = 1 + groupCount + 2; // match + groups + offset + input
            std::vector<TsValue*> args;
            args.reserve(totalArgs);

            // Full match (group 0)
            {
                icu::UnicodeString matchStr = matcher->group(0, status);
                std::string utf8;
                matchStr.toUTF8String(utf8);
                args.push_back(ts_value_make_string(TsString::Create(utf8.c_str())));
            }

            // Capture groups (1..groupCount)
            for (int32_t i = 1; i <= groupCount; i++) {
                int32_t gs = matcher->start(i, status);
                if (gs == -1) {
                    args.push_back(ts_value_make_undefined());
                } else {
                    icu::UnicodeString group = matcher->group(i, status);
                    std::string gUtf8;
                    group.toUTF8String(gUtf8);
                    args.push_back(ts_value_make_string(TsString::Create(gUtf8.c_str())));
                }
            }

            // Offset (index of match in original string)
            args.push_back(ts_value_make_int(matchStart));

            // Original string
            args.push_back(ts_value_make_string(str));

            // Call the callback
            TsValue* callResult = ts_call_variadic(callback, args.data(), (int)args.size());

            // Convert result to string and append
            if (callResult) {
                TsString* replStr = (TsString*)ts_string_from_value(callResult);
                if (replStr) {
                    icu::UnicodeString replU = replStr->ToUnicodeString();
                    result.append(replU);
                }
            }

            lastEnd = matchEnd;

            // For zero-length matches, advance by 1 to avoid infinite loop
            if (matchStart == matchEnd) {
                if (matchEnd < input.length()) {
                    result.append(input[matchEnd]);
                    lastEnd = matchEnd + 1;
                } else {
                    break;
                }
            }

            if (!isGlobal) break;
        }

        // Append remaining text after last match
        if (lastEnd < input.length()) {
            result.append(input, lastEnd, input.length() - lastEnd);
        }

        std::string utf8Result;
        result.toUTF8String(utf8Result);
        return ts_value_make_string(TsString::Create(utf8Result.c_str()));
    }

    // String.replace with string pattern and callback function
    static TsValue* ts_string_replace_callback_string(TsString* str, TsString* pattern, TsValue* callback) {
        const char* haystack = str->ToUtf8();
        const char* needle = pattern->ToUtf8();
        if (!haystack || !needle) return ts_value_make_string(str);

        const char* found = strstr(haystack, needle);
        if (!found) return ts_value_make_string(str);

        int64_t offset = found - haystack;
        size_t needleLen = strlen(needle);

        // Build callback args: (match, offset, originalString)
        TsValue* args[3];
        args[0] = ts_value_make_string(pattern);
        args[1] = ts_value_make_int(offset);
        args[2] = ts_value_make_string(str);

        TsValue* callResult = ts_call_3(callback, args[0], args[1], args[2]);

        TsString* replStr = callResult ? (TsString*)ts_string_from_value(callResult) : TsString::Create("undefined");
        const char* replUtf8 = replStr->ToUtf8();

        std::string result;
        result.append(haystack, offset);
        result.append(replUtf8);
        result.append(haystack + offset + needleLen);

        return ts_value_make_string(TsString::Create(result.c_str()));
    }

    static TsValue* ts_string_replace_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        if (argc < 1 || !argv) return ts_value_make_string(str);

        // Check if replacement (argv[1]) is a callback function
        bool replIsCallback = (argc >= 2 && argv[1] && ts_value_is_callable(argv[1]));

        // Extract and unbox pattern
        void* rawPattern = argv[0] ? ts_value_get_object((TsValue*)argv[0]) : nullptr;
        if (!rawPattern) rawPattern = (void*)argv[0];

        if (rawPattern) {
            uint32_t magic = *(uint32_t*)rawPattern;
            if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                if (replIsCallback) {
                    return ts_string_replace_callback_regex(str, (TsRegExp*)rawPattern, argv[1]);
                }
                // String replacement
                void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
                if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;
                return ts_value_make_string((TsString*)ts_string_replace_regexp(str, rawPattern, replacement));
            }
        }

        // Pattern is a string
        void* pattern = argv[0] ? ts_value_get_string(argv[0]) : nullptr;
        if (!pattern) pattern = (void*)argv[0];

        if (replIsCallback) {
            TsString* strPattern = (TsString*)pattern;
            if (!strPattern) strPattern = TsString::Create("");
            return ts_string_replace_callback_string(str, strPattern, argv[1]);
        }

        void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;
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

    // Native wrapper for string.toString() / string.valueOf() - just returns the string itself
    static TsValue* ts_string_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ctx);
    }

    // Native wrappers for missing string methods in dynamic dispatch
    static TsValue* ts_string_lastIndexOf_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* searchString = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!searchString) searchString = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_int(ts_string_lastIndexOf(str, searchString));
    }
    static TsValue* ts_string_trimStart_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_trimStart((TsString*)ctx));
    }
    static TsValue* ts_string_trimEnd_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string((TsString*)ts_string_trimEnd((TsString*)ctx));
    }
    static TsValue* ts_string_replaceAll_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        if (argc < 1 || !argv) return ts_value_make_string(str);

        // Extract replacement string
        void* replacement = (argc >= 2 && argv[1]) ? ts_value_get_string(argv[1]) : nullptr;
        if (!replacement) replacement = (argc >= 2 && argv[1]) ? (void*)argv[1] : nullptr;

        // Check if pattern is a RegExp
        void* rawPattern = argv[0] ? ts_value_get_object((TsValue*)argv[0]) : nullptr;
        if (!rawPattern) rawPattern = (void*)argv[0];
        if (rawPattern) {
            uint32_t magic = *(uint32_t*)rawPattern;
            if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                return ts_value_make_string((TsString*)ts_string_replace_regexp(str, rawPattern, replacement));
            }
        }

        // Pattern is a string
        void* pattern = argv[0] ? ts_value_get_string(argv[0]) : nullptr;
        if (!pattern) pattern = (void*)argv[0];
        return ts_value_make_string((TsString*)ts_string_replaceAll(str, pattern, replacement));
    }
    static TsValue* ts_string_at_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_string((TsString*)ts_string_at(str, index));
    }
    static TsValue* ts_string_concat_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* other = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        if (!other) return ts_value_make_string(str);
        return ts_value_make_string((TsString*)ts_string_concat(str, other));
    }
    static TsValue* ts_string_match_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* result = ts_string_match_regexp(str, regexp);
        return result ? ts_value_make_object(result) : (TsValue*)ts_value_make_null();
    }
    static TsValue* ts_string_search_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        return ts_value_make_int(ts_string_search_regexp(str, regexp));
    }
    static TsValue* ts_string_matchAll_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* regexp = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* result = ts_string_matchAll_regexp(str, regexp);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_string_codePointAt_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        return ts_value_make_int(ts_string_codePointAt(str, index));
    }
    static TsValue* ts_string_normalize_native(void* ctx, int argc, TsValue** argv) {
        TsString* str = (TsString*)ctx;
        void* form = (argc >= 1 && argv && argv[0]) ? ts_value_get_string(argv[0]) : nullptr;
        return ts_value_make_string((TsString*)ts_string_normalize(str, form));
    }

    // ============================================================
    // Native wrappers for array methods (ctx = TsArray*)
    // ============================================================

    // Helper: resolve array from ctx or this (for Array.prototype methods)
    static TsArray* resolve_array_ctx(void* ctx) {
        // If ctx looks like a valid TsArray, use it directly
        if (ctx) {
            uint32_t m = *(uint32_t*)ctx;
            if (m == 0x41525259) return (TsArray*)ctx; // TsArray::MAGIC
        }
        // Fallback: get from 'this' (used by Array.prototype.method.call(arr, ...))
        void* thisVal = ts_get_call_this();
        if (thisVal) {
            // Unbox if needed
            void* raw = ts_value_get_object((TsValue*)thisVal);
            if (!raw) raw = thisVal;
            uint32_t m = *(uint32_t*)raw;
            if (m == 0x41525259) return (TsArray*)raw;
        }
        return nullptr;
    }

    // P0: Extremely common methods
    TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_map(arr, callback, thisArg);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_filter(arr, callback, thisArg);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        ts_array_forEach(arr, callback, thisArg);
        return ts_value_make_undefined();
    }
    TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* initialValue = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_reduce(arr, callback, initialValue);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        for (int i = 0; i < argc; i++) {
            ts_array_push(arr, (void*)argv[i]);
        }
        return ts_value_make_int(arr->Length());
    }
    TsValue* ts_array_pop_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* result = ts_array_pop(arr);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* separator = nullptr;
        if (argc >= 1 && argv && argv[0]) {
            separator = ts_value_get_string(argv[0]);
            if (!separator) separator = (void*)argv[0];
        }
        void* result = ts_array_join(arr, separator);
        return result ? ts_value_make_string((TsString*)result) : ts_value_make_string(TsString::Create(""));
    }
    TsValue* ts_array_indexOf_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        return ts_value_make_int(ts_array_indexOf(arr, value));
    }
    TsValue* ts_array_includes_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        return ts_value_make_bool(ts_array_includes(arr, value));
    }
    TsValue* ts_array_slice_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = resolve_array_ctx(ctx);
        if (!arr) return ts_value_make_object(ts_array_create());
        int64_t start = 0, end = arr->Length();
        if (argc >= 1 && argv && argv[0]) {
            start = ts_value_get_int(argv[0]);
        }
        if (argc >= 2 && argv && argv[1]) {
            end = ts_value_get_int(argv[1]);
        }
        void* result = ts_array_slice(arr, start, end);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }

    // TypedArray native methods
    static TsValue* ts_typed_array_slice_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        int64_t len = (int64_t)ta->GetLength();
        int64_t start = 0, end = len;
        if (argc >= 1 && argv && argv[0]) {
            start = ts_value_get_int(argv[0]);
            if (start < 0) start = std::max((int64_t)0, len + start);
        }
        if (argc >= 2 && argv && argv[1]) {
            end = ts_value_get_int(argv[1]);
            if (end < 0) end = std::max((int64_t)0, len + end);
        }
        if (start > len) start = len;
        if (end > len) end = len;
        if (end < start) end = start;
        size_t newLen = (size_t)(end - start);
        TsTypedArray* result = TsTypedArray::Create(newLen, ta->GetElementSize(), ta->IsClamped(), ta->GetType());
        for (size_t i = 0; i < newLen; i++) {
            result->Set(i, ta->Get((size_t)start + i));
        }
        return ts_value_make_object(result);
    }
    static TsValue* ts_typed_array_set_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        // set(source, offset?) - copy elements from source array
        if (argc < 1 || !argv || !argv[0]) return ts_value_make_undefined();
        void* src = ts_value_get_object(argv[0]);
        if (!src) src = (void*)argv[0];
        int64_t offset = 0;
        if (argc >= 2 && argv[1]) offset = ts_value_get_int(argv[1]);
        // Check if source is a TypedArray
        uint32_t srcMagic16 = *(uint32_t*)((char*)src + 16);
        if (srcMagic16 == TsTypedArray::MAGIC) {
            TsTypedArray* srcTa = (TsTypedArray*)src;
            for (size_t i = 0; i < srcTa->GetLength() && (size_t)(offset + i) < ta->GetLength(); i++) {
                ta->Set((size_t)(offset + i), srcTa->Get(i));
            }
        } else {
            // Assume it's a regular array
            TsArray* srcArr = (TsArray*)src;
            for (int64_t i = 0; i < srcArr->Length() && (offset + i) < (int64_t)ta->GetLength(); i++) {
                double val = srcArr->GetElementDouble(i);
                ta->Set((size_t)(offset + i), val);
            }
        }
        return ts_value_make_undefined();
    }
    static TsValue* ts_typed_array_subarray_native(void* ctx, int argc, TsValue** argv) {
        // subarray creates a new view (we just copy for now)
        return ts_typed_array_slice_native(ctx, argc, argv);
    }
    static TsValue* ts_typed_array_fill_native(void* ctx, int argc, TsValue** argv) {
        TsTypedArray* ta = (TsTypedArray*)ctx;
        double fillVal = 0;
        if (argc >= 1 && argv && argv[0]) fillVal = ts_value_get_double(argv[0]);
        int64_t len = (int64_t)ta->GetLength();
        int64_t start = 0, end = len;
        if (argc >= 2 && argv && argv[1]) start = ts_value_get_int(argv[1]);
        if (argc >= 3 && argv && argv[2]) end = ts_value_get_int(argv[2]);
        if (start < 0) start = std::max((int64_t)0, len + start);
        if (end < 0) end = std::max((int64_t)0, len + end);
        if (start > len) start = len;
        if (end > len) end = len;
        for (int64_t i = start; i < end; i++) {
            ta->Set((size_t)i, fillVal);
        }
        return ts_value_make_object(ta);
    }

    // P1: Common methods
    TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_bool(ts_array_some(arr, callback, thisArg));
    }
    TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_bool(ts_array_every(arr, callback, thisArg));
    }
    TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        struct TaggedValue* result = ts_array_find(arr, callback, thisArg);
        return result ? nanbox_from_tagged(*result) : ts_value_make_undefined();
    }
    TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_int(ts_array_findIndex(arr, callback, thisArg));
    }
    TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* comparator = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* result = ts_array_sort(arr, comparator);
        return result ? ts_value_make_object(result) : ts_value_make_object(arr);
    }
    TsValue* ts_array_reverse_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* result = ts_array_reverse(arr);
        return result ? ts_value_make_object(result) : ts_value_make_object(arr);
    }
    TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t deleteCount = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : arr->Length() - start;
        if (start < 0) start = arr->Length() + start;
        if (start < 0) start = 0;
        if (start > arr->Length()) start = arr->Length();
        if (deleteCount < 0) deleteCount = 0;
        if (deleteCount > arr->Length() - start) deleteCount = arr->Length() - start;

        // Create result array with deleted elements
        TsArray* result = TsArray::Create(deleteCount > 0 ? deleteCount : 4);
        for (int64_t i = 0; i < deleteCount; i++) {
            result->Push(arr->Get(start + i));
        }

        // Build items array from remaining args
        int itemCount = (argc > 2) ? argc - 2 : 0;

        // Remove deleted elements and insert new ones
        // First, collect elements after the splice point
        TsArray* tail = TsArray::Create(4);
        for (int64_t i = start + deleteCount; i < arr->Length(); i++) {
            tail->Push(arr->Get(i));
        }

        // Truncate array to start point
        while (arr->Length() > start) {
            arr->Pop();
        }

        // Insert new items
        for (int i = 0; i < itemCount; i++) {
            ts_array_push(arr, (void*)argv[i + 2]);
        }

        // Re-add tail elements
        for (int64_t i = 0; i < tail->Length(); i++) {
            arr->Push(tail->Get(i));
        }

        return ts_value_make_object(result);
    }
    TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* other = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        // Unbox the other arg if needed
        if (other) {
            void* raw = ts_value_get_object((TsValue*)other);
            if (raw) other = raw;
        }
        void* result = ts_array_concat(arr, other);
        return result ? ts_value_make_object(result) : ts_value_make_object(arr);
    }

    // P2: Moderate methods
    TsValue* ts_array_flat_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t depth = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 1;
        void* result = ts_array_flat(arr, depth);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_flatMap_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_flatMap(arr, callback, thisArg);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_at_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        void* result = ts_array_at(arr, index);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_shift_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* result = ts_array_shift(arr);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        for (int i = argc - 1; i >= 0; i--) {
            ts_array_unshift(arr, (void*)argv[i]);
        }
        return ts_value_make_int(arr->Length());
    }
    static TsValue* ts_array_fill_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* value = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        int64_t start = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : 0;
        int64_t end = (argc >= 3 && argv && argv[2]) ? ts_value_get_int(argv[2]) : arr->Length();
        ts_array_fill(arr, value, start, end);
        return ts_value_make_object(arr);
    }
    static TsValue* ts_array_reduceRight_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* initialValue = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_reduceRight(arr, callback, initialValue);
        return result ? (TsValue*)result : ts_value_make_undefined();
    }
    static TsValue* ts_array_lastIndexOf_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t value = (argc >= 1 && argv) ? (int64_t)argv[0] : 0;
        return ts_value_make_int(ts_array_lastIndexOf(arr, value));
    }

    // P3: Less common methods
    static TsValue* ts_array_entries_native(void* ctx, int argc, TsValue** argv) {
        void* items = ts_array_entries((TsArray*)ctx);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_keys_native(void* ctx, int argc, TsValue** argv) {
        void* items = ts_array_keys((TsArray*)ctx);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_values_native(void* ctx, int argc, TsValue** argv) {
        void* items = ts_array_values((TsArray*)ctx);
        return items ? (TsValue*)ts_create_array_iterator(items) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_toReversed_native(void* ctx, int argc, TsValue** argv) {
        void* result = ts_array_toReversed((TsArray*)ctx);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_toSorted_native(void* ctx, int argc, TsValue** argv) {
        void* result = ts_array_toSorted((TsArray*)ctx);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_toSpliced_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t start = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t deleteCount = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : arr->Length() - start;
        // Collect items as an array
        TsArray* items = nullptr;
        if (argc > 2) {
            items = TsArray::Create(argc - 2);
            for (int i = 2; i < argc; i++) {
                ts_array_push(items, (void*)argv[i]);
            }
        }
        void* result = ts_array_toSpliced(arr, start, deleteCount, items, items ? items->Length() : 0);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_copyWithin_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t target = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        int64_t start = (argc >= 2 && argv && argv[1]) ? ts_value_get_int(argv[1]) : 0;
        int64_t end = (argc >= 3 && argv && argv[2]) ? ts_value_get_int(argv[2]) : arr->Length();
        ts_array_copyWithin(arr, target, start, end);
        return ts_value_make_object(arr);
    }
    static TsValue* ts_array_with_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        int64_t index = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 0;
        void* value = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        void* result = ts_array_with(arr, index, value);
        return result ? ts_value_make_object(result) : ts_value_make_object(ts_array_create());
    }
    static TsValue* ts_array_findLast_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        struct TaggedValue* result = ts_array_findLast(arr, callback, thisArg);
        return result ? nanbox_from_tagged(*result) : ts_value_make_undefined();
    }
    static TsValue* ts_array_findLastIndex_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* callback = (argc >= 1 && argv) ? (void*)argv[0] : nullptr;
        void* thisArg = (argc >= 2 && argv) ? (void*)argv[1] : nullptr;
        return ts_value_make_int(ts_array_findLastIndex(arr, callback, thisArg));
    }
    static TsValue* ts_array_toString_native(void* ctx, int argc, TsValue** argv) {
        TsArray* arr = (TsArray*)ctx;
        void* result = ts_array_join(arr, (void*)TsString::Create(","));
        return result ? ts_value_make_string((TsString*)result) : ts_value_make_string(TsString::Create(""));
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
    static TsValue* ts_number_valueOf_native(void* ctx, int argc, TsValue** argv) {
        return (TsValue*)ctx; // Return the NaN-boxed number as-is
    }
    static TsValue* ts_number_toPrecision_native(void* ctx, int argc, TsValue** argv) {
        double value = nanbox_extract_double((TsValue*)ctx);
        if (argc < 1 || !argv || !argv[0]) {
            return ts_value_make_string((TsString*)ts_number_to_string(value, 10));
        }
        int64_t precision = ts_value_get_int(argv[0]);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*g", (int)precision, value);
        return ts_value_make_string(TsString::Create(buf));
    }
    static TsValue* ts_number_toExponential_native(void* ctx, int argc, TsValue** argv) {
        double value = nanbox_extract_double((TsValue*)ctx);
        int64_t digits = (argc >= 1 && argv && argv[0]) ? ts_value_get_int(argv[0]) : 6;
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*e", (int)digits, value);
        return ts_value_make_string(TsString::Create(buf));
    }

    // Native wrappers for boolean methods
    static TsValue* ts_boolean_toString_native(void* ctx, int argc, TsValue** argv) {
        bool value = nanbox_is_bool(nanbox_from_tsvalue_ptr((TsValue*)ctx)) &&
                     nanbox_to_bool(nanbox_from_tsvalue_ptr((TsValue*)ctx));
        return ts_value_make_string(TsString::Create(value ? "true" : "false"));
    }
    static TsValue* ts_boolean_valueOf_native(void* ctx, int argc, TsValue** argv) {
        return (TsValue*)ctx; // Return the NaN-boxed boolean as-is
    }

    // Native wrappers for Date instance methods
    static TsValue* ts_date_getTime_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetTime());
    }
    static TsValue* ts_date_getFullYear_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetFullYear());
    }
    static TsValue* ts_date_getMonth_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetMonth());
    }
    static TsValue* ts_date_getDate_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetDate());
    }
    static TsValue* ts_date_getHours_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetHours());
    }
    static TsValue* ts_date_getMinutes_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetMinutes());
    }
    static TsValue* ts_date_getSeconds_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetSeconds());
    }
    static TsValue* ts_date_getMilliseconds_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetMilliseconds());
    }
    static TsValue* ts_date_getUTCFullYear_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCFullYear());
    }
    static TsValue* ts_date_getUTCMonth_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCMonth());
    }
    static TsValue* ts_date_getUTCDate_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCDate());
    }
    static TsValue* ts_date_getUTCHours_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCHours());
    }
    static TsValue* ts_date_getUTCMinutes_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCMinutes());
    }
    static TsValue* ts_date_getUTCSeconds_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCSeconds());
    }
    static TsValue* ts_date_getUTCMilliseconds_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetUTCMilliseconds());
    }
    static TsValue* ts_date_toISOString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(((TsDate*)ctx)->ToISOString());
    }
    static TsValue* ts_date_toJSON_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(((TsDate*)ctx)->ToJSON());
    }
    static TsValue* ts_date_toString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(((TsDate*)ctx)->ToString());
    }
    static TsValue* ts_date_toDateString_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_string(((TsDate*)ctx)->ToDateString());
    }
    static TsValue* ts_date_valueOf_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(((TsDate*)ctx)->GetTime());
    }
    // Date.now() static method
    static TsValue* ts_date_now_native(void* ctx, int argc, TsValue** argv) {
        return ts_value_make_int(TsDate::Now());
    }

    // Native wrappers for RegExp instance methods (.test() and .exec())
    static TsValue* ts_regexp_test_native(void* ctx, int argc, TsValue** argv) {
        TsRegExp* re = (TsRegExp*)ctx;
        void* str = (argc >= 1 && argv && argv[0]) ? (void*)argv[0] : nullptr;
        int32_t result = RegExp_test(re, str);
        return (TsValue*)ts_value_make_bool(result != 0);
    }
    static TsValue* ts_regexp_exec_native(void* ctx, int argc, TsValue** argv) {
        TsRegExp* re = (TsRegExp*)ctx;
        void* str = (argc >= 1 && argv && argv[0]) ? (void*)argv[0] : nullptr;
        void* result = RegExp_exec(re, str);
        if (!result) return (TsValue*)ts_value_make_null();
        return (TsValue*)ts_value_make_object(result);
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
        // by the caller).
        //
        // Check magic at offset 16 for non-virtual-inheritance classes (direct TsObject subclasses).
        // For virtual-inheritance classes (TsIncomingMessage, TsServerResponse, etc.), TsObject::magic
        // is at a large offset (e.g., 176) due to MSVC's virtual base layout. For these, we validate
        // the C++ vtable pointer and call GetPropertyVirtual via the primary vtable, which correctly
        // dispatches even for virtual-inheritance classes.
        // Virtual property dispatch for TsObject subclasses.
        // Note: do NOT guard with ts_gc_base() — some objects (e.g., TsHttpServer)
        // are allocated with malloc (for libuv compatibility) and won't be in the GC heap.
        // The magic16 whitelist check is sufficient to validate the pointer.
        {
            uint32_t magic16 = *(uint32_t*)((uint8_t*)obj + 16);
            // Check for TsSet first - dispatch through its own vtable
            if (magic16 == 0x53455453) {  // TsSet::MAGIC "SETS"
                extern TsValue* ts_set_get_property(void* obj, void* propName);
                TsString* propStr = TsString::Create(keyStr);
                TsValue* result = ts_set_get_property(obj, propStr);
                if (result && !ts_value_is_undefined(result)) {
                    return result;
                }
            }
            if (magic16 == 0x50524F4D ||  // TsPromise::MAGIC "PROM"
                magic16 == 0x54584E43 ||  // TsTextEncoder::MAGIC "TXNC"
                magic16 == 0x54584443 ||  // TsTextDecoder::MAGIC "TXDC"
                magic16 == 0x43524551 ||  // TsClientRequest::MAGIC "CREQ"
                magic16 == 0x53524553 ||  // TsServerResponse::MAGIC "SRES"
                magic16 == 0x4F55544D ||  // TsOutgoingMessage::MAGIC "OUTM"
                magic16 == 0x494E434D ||  // TsIncomingMessage::MAGIC "INCM"
                magic16 == 0x48535256 ||  // TsHttpServer::MAGIC "HSRV"
                magic16 == 0x52455350 ||  // TsResponse::MAGIC "RESP"
                magic16 == 0x52455155 ||  // TsRequest::MAGIC "REQU"
                magic16 == 0x48454144 ||  // TsHeaders::MAGIC "HEAD"
                magic16 == 0x48415348 ||  // TsCryptoHash::MAGIC "HASH"
                magic16 == 0x484D4143) {  // TsCryptoHmac::MAGIC "HMAC"
                TsObject* tsObj = (TsObject*)obj;
                TsValue result = tsObj->GetPropertyVirtual(keyStr);
                if (result.type != ValueType::UNDEFINED) {
                    // GetPropertyVirtual returns a TsValue struct. We need to
                    // convert it to a TsValue* (NaN-boxed pointer). The implicit
                    // operator void*() on TaggedValue only handles OBJECT_PTR and
                    // STRING_PTR — FUNCTION_PTR would return nullptr. So we must
                    // explicitly return the ptr_val for function types.
                    if (result.type == ValueType::FUNCTION_PTR) {
                        return (TsValue*)result.ptr_val;
                    }
                    // For OBJECT_PTR/STRING_PTR, the implicit conversion works
                    return result;
                }
            }
            // Virtual-inheritance classes (stream classes) have TsObject::magic at a large offset
            // (not offset 16) due to MSVC's virtual base layout. We can't use (TsObject*)obj cast
            // because it doesn't adjust for virtual inheritance. Instead, match the primary vtable
            // pointer against known class vtables and cast to the concrete type.
            uint64_t vtableAddr = *(uint64_t*)obj;
            for (int i = 0; i < g_vtable_dispatch_count; i++) {
                if (vtableAddr == g_vtable_dispatch[i].vtable) {
                    TsValue result = g_vtable_dispatch[i].dispatch(obj, keyStr);
                    if (result.type != ValueType::UNDEFINED) {
                        if (result.type == ValueType::FUNCTION_PTR) return (TsValue*)result.ptr_val;
                        return result;
                    }
                    break;
                }
            }
        }
        return undefined;
    }

    // Forward declarations for Object.prototype methods (defined later in this file)
    static TsValue* ts_object_isPrototypeOf_native(void* ctx, int argc, TsValue** argv);
    static TsValue* ts_object_propertyIsEnumerable_native(void* ctx, int argc, TsValue** argv);

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
            // Number methods: toString, toFixed, valueOf, toPrecision, toExponential
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_native_function((void*)ts_number_toString_native, obj);
            if (strcmp(keyStr, "toFixed") == 0) return ts_value_make_native_function((void*)ts_number_toFixed_native, obj);
            if (strcmp(keyStr, "valueOf") == 0) return ts_value_make_native_function((void*)ts_number_valueOf_native, obj);
            if (strcmp(keyStr, "toPrecision") == 0) return ts_value_make_native_function((void*)ts_number_toPrecision_native, obj);
            if (strcmp(keyStr, "toExponential") == 0) return ts_value_make_native_function((void*)ts_number_toExponential_native, obj);
            return ts_value_make_undefined();
        }
        if (nanbox_is_bool(nb)) {
            // Boolean methods: toString, valueOf
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_native_function((void*)ts_boolean_toString_native, obj);
            if (strcmp(keyStr, "valueOf") == 0) return ts_value_make_native_function((void*)ts_boolean_valueOf_native, obj);
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
        // Check for flat inline-slot object (magic at offset 0)
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            TsValue* result = (TsValue*)ts_flat_object_get_property(obj, keyStr);
            // If property not found in flat object, check Object.prototype methods
            uint64_t resultNb = nanbox_from_tsvalue_ptr(result);
            if (resultNb == NANBOX_UNDEFINED) {
                if (strcmp(keyStr, "hasOwnProperty") == 0) {
                    return ts_value_make_native_function((void*)ts_object_hasOwnProperty_native, nullptr);
                }
                if (strcmp(keyStr, "toString") == 0) {
                    return ts_value_make_native_function((void*)ts_object_toString_native, nullptr);
                }
                if (strcmp(keyStr, "valueOf") == 0) {
                    return ts_value_make_native_function((void*)ts_object_valueOf_native, nullptr);
                }
                if (strcmp(keyStr, "constructor") == 0) {
                    extern TsValue* Object;
                    if (Object) return Object;
                }
                if (strcmp(keyStr, "isPrototypeOf") == 0) {
                    return ts_value_make_native_function((void*)ts_object_isPrototypeOf_native, nullptr);
                }
                if (strcmp(keyStr, "propertyIsEnumerable") == 0) {
                    return ts_value_make_native_function((void*)ts_object_propertyIsEnumerable_native, nullptr);
                }
                // Check for EventEmitter methods on flat objects extending EventEmitter
                TsValue* eeMethod = flat_try_ee_method(obj, keyStr);
                if (eeMethod) return eeMethod;
            }
            return result;
        }

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
            if (strcmp(keyStr, "test") == 0) {
                return ts_value_make_native_function((void*)ts_regexp_test_native, re);
            }
            if (strcmp(keyStr, "exec") == 0) {
                return ts_value_make_native_function((void*)ts_regexp_exec_native, re);
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

        // Check for TsDate (magic at offset 0)
        if (magic0 == 0x44415445) { // TsDate::MAGIC ("DATE")
            TsDate* date = (TsDate*)obj;
            if (strcmp(keyStr, "getTime") == 0) return ts_value_make_native_function((void*)ts_date_getTime_native, date);
            if (strcmp(keyStr, "getFullYear") == 0) return ts_value_make_native_function((void*)ts_date_getFullYear_native, date);
            if (strcmp(keyStr, "getMonth") == 0) return ts_value_make_native_function((void*)ts_date_getMonth_native, date);
            if (strcmp(keyStr, "getDate") == 0) return ts_value_make_native_function((void*)ts_date_getDate_native, date);
            if (strcmp(keyStr, "getHours") == 0) return ts_value_make_native_function((void*)ts_date_getHours_native, date);
            if (strcmp(keyStr, "getMinutes") == 0) return ts_value_make_native_function((void*)ts_date_getMinutes_native, date);
            if (strcmp(keyStr, "getSeconds") == 0) return ts_value_make_native_function((void*)ts_date_getSeconds_native, date);
            if (strcmp(keyStr, "getMilliseconds") == 0) return ts_value_make_native_function((void*)ts_date_getMilliseconds_native, date);
            if (strcmp(keyStr, "getUTCFullYear") == 0) return ts_value_make_native_function((void*)ts_date_getUTCFullYear_native, date);
            if (strcmp(keyStr, "getUTCMonth") == 0) return ts_value_make_native_function((void*)ts_date_getUTCMonth_native, date);
            if (strcmp(keyStr, "getUTCDate") == 0) return ts_value_make_native_function((void*)ts_date_getUTCDate_native, date);
            if (strcmp(keyStr, "getUTCHours") == 0) return ts_value_make_native_function((void*)ts_date_getUTCHours_native, date);
            if (strcmp(keyStr, "getUTCMinutes") == 0) return ts_value_make_native_function((void*)ts_date_getUTCMinutes_native, date);
            if (strcmp(keyStr, "getUTCSeconds") == 0) return ts_value_make_native_function((void*)ts_date_getUTCSeconds_native, date);
            if (strcmp(keyStr, "getUTCMilliseconds") == 0) return ts_value_make_native_function((void*)ts_date_getUTCMilliseconds_native, date);
            if (strcmp(keyStr, "toISOString") == 0) return ts_value_make_native_function((void*)ts_date_toISOString_native, date);
            if (strcmp(keyStr, "toJSON") == 0) return ts_value_make_native_function((void*)ts_date_toJSON_native, date);
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_native_function((void*)ts_date_toString_native, date);
            if (strcmp(keyStr, "toDateString") == 0) return ts_value_make_native_function((void*)ts_date_toDateString_native, date);
            if (strcmp(keyStr, "valueOf") == 0) return ts_value_make_native_function((void*)ts_date_valueOf_native, date);
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
            // Methods
            if (strcmp(keyStr, "slice") == 0) {
                return ts_value_make_native_function((void*)ts_typed_array_slice_native, ta);
            }
            if (strcmp(keyStr, "set") == 0) {
                return ts_value_make_native_function((void*)ts_typed_array_set_native, ta);
            }
            if (strcmp(keyStr, "subarray") == 0) {
                return ts_value_make_native_function((void*)ts_typed_array_subarray_native, ta);
            }
            if (strcmp(keyStr, "fill") == 0) {
                return ts_value_make_native_function((void*)ts_typed_array_fill_native, ta);
            }
            // Check for numeric index
            char* endptr;
            long index = strtol(keyStr, &endptr, 10);
            if (*endptr == '\0' && index >= 0) {
                return ts_value_make_double(ta->Get((size_t)index));
            }
            return ts_value_make_undefined();
        }

        // Check for global constructor sentinels (e.g., Date.now(), Date.parse())
        // Global constructors from ts_get_global_Date() etc. are static strings like "Date"
        if (magic0 == 0x65746144 && *((char*)obj + 4) == '\0') { // "Date" (little-endian)
            if (strcmp(keyStr, "now") == 0) return ts_value_make_native_function((void*)ts_date_now_native, nullptr);
            return ts_value_make_undefined();
        }

        // Check for TsMap (magic at offset 16 after vtables) - also try offset 20 and 24
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC ("MAPS")
            TsMap* map = (TsMap*)obj;

            // Handle Map .size property (computed, not stored as key-value)
            if (strcmp(keyStr, "size") == 0) {
                return ts_value_make_int(map->Size());
            }

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
            if (strcmp(keyStr, "constructor") == 0) {
                extern TsValue* Object;
                if (Object) return Object;
            }
            if (strcmp(keyStr, "isPrototypeOf") == 0) {
                return ts_value_make_native_function((void*)ts_object_isPrototypeOf_native, nullptr);
            }
            if (strcmp(keyStr, "propertyIsEnumerable") == 0) {
                return ts_value_make_native_function((void*)ts_object_propertyIsEnumerable_native, nullptr);
            }
            // Check for EventEmitter methods on TsMap-backed objects extending EventEmitter
            {
                TsValue* eeMethod = flat_try_ee_method(obj, keyStr);
                if (eeMethod) return eeMethod;
            }

            return ts_value_make_undefined();
        }

        // 2. Fallback to magic-based checks for built-ins
        if (magic0 == 0x41525259 || magic8 == 0x41525259 || magic16 == 0x41525259) { // TsArray::MAGIC ("ARRY")
            TsArray* arr = (TsArray*)obj;
            if (strcmp(keyStr, "length") == 0) return ts_value_make_int(arr->Length());
            // P0: Extremely common methods
            if (strcmp(keyStr, "map") == 0) return ts_value_make_native_function((void*)ts_array_map_native, arr);
            if (strcmp(keyStr, "filter") == 0) return ts_value_make_native_function((void*)ts_array_filter_native, arr);
            if (strcmp(keyStr, "forEach") == 0) return ts_value_make_native_function((void*)ts_array_forEach_native, arr);
            if (strcmp(keyStr, "reduce") == 0) return ts_value_make_native_function((void*)ts_array_reduce_native, arr);
            if (strcmp(keyStr, "push") == 0) return ts_value_make_native_function((void*)ts_array_push_native, arr);
            if (strcmp(keyStr, "pop") == 0) return ts_value_make_native_function((void*)ts_array_pop_native, arr);
            if (strcmp(keyStr, "join") == 0) return ts_value_make_native_function((void*)ts_array_join_native, arr);
            if (strcmp(keyStr, "indexOf") == 0) return ts_value_make_native_function((void*)ts_array_indexOf_native, arr);
            if (strcmp(keyStr, "includes") == 0) return ts_value_make_native_function((void*)ts_array_includes_native, arr);
            if (strcmp(keyStr, "slice") == 0) return ts_value_make_native_function((void*)ts_array_slice_native, arr);
            // P1: Common methods
            if (strcmp(keyStr, "some") == 0) return ts_value_make_native_function((void*)ts_array_some_native, arr);
            if (strcmp(keyStr, "every") == 0) return ts_value_make_native_function((void*)ts_array_every_native, arr);
            if (strcmp(keyStr, "find") == 0) return ts_value_make_native_function((void*)ts_array_find_native, arr);
            if (strcmp(keyStr, "findIndex") == 0) return ts_value_make_native_function((void*)ts_array_findIndex_native, arr);
            if (strcmp(keyStr, "sort") == 0) return ts_value_make_native_function((void*)ts_array_sort_native, arr);
            if (strcmp(keyStr, "reverse") == 0) return ts_value_make_native_function((void*)ts_array_reverse_native, arr);
            if (strcmp(keyStr, "splice") == 0) return ts_value_make_native_function((void*)ts_array_splice_native, arr);
            if (strcmp(keyStr, "concat") == 0) return ts_value_make_native_function((void*)ts_array_concat_native, arr);
            // P2: Moderate methods
            if (strcmp(keyStr, "flat") == 0) return ts_value_make_native_function((void*)ts_array_flat_native, arr);
            if (strcmp(keyStr, "flatMap") == 0) return ts_value_make_native_function((void*)ts_array_flatMap_native, arr);
            if (strcmp(keyStr, "at") == 0) return ts_value_make_native_function((void*)ts_array_at_native, arr);
            if (strcmp(keyStr, "shift") == 0) return ts_value_make_native_function((void*)ts_array_shift_native, arr);
            if (strcmp(keyStr, "unshift") == 0) return ts_value_make_native_function((void*)ts_array_unshift_native, arr);
            if (strcmp(keyStr, "fill") == 0) return ts_value_make_native_function((void*)ts_array_fill_native, arr);
            if (strcmp(keyStr, "reduceRight") == 0) return ts_value_make_native_function((void*)ts_array_reduceRight_native, arr);
            if (strcmp(keyStr, "lastIndexOf") == 0) return ts_value_make_native_function((void*)ts_array_lastIndexOf_native, arr);
            // P3: Less common methods
            if (strcmp(keyStr, "entries") == 0) return ts_value_make_native_function((void*)ts_array_entries_native, arr);
            if (strcmp(keyStr, "keys") == 0) return ts_value_make_native_function((void*)ts_array_keys_native, arr);
            if (strcmp(keyStr, "values") == 0) return ts_value_make_native_function((void*)ts_array_values_native, arr);
            if (strcmp(keyStr, "toReversed") == 0) return ts_value_make_native_function((void*)ts_array_toReversed_native, arr);
            if (strcmp(keyStr, "toSorted") == 0) return ts_value_make_native_function((void*)ts_array_toSorted_native, arr);
            if (strcmp(keyStr, "toSpliced") == 0) return ts_value_make_native_function((void*)ts_array_toSpliced_native, arr);
            if (strcmp(keyStr, "copyWithin") == 0) return ts_value_make_native_function((void*)ts_array_copyWithin_native, arr);
            if (strcmp(keyStr, "with") == 0) return ts_value_make_native_function((void*)ts_array_with_native, arr);
            if (strcmp(keyStr, "findLast") == 0) return ts_value_make_native_function((void*)ts_array_findLast_native, arr);
            if (strcmp(keyStr, "findLastIndex") == 0) return ts_value_make_native_function((void*)ts_array_findLastIndex_native, arr);
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_native_function((void*)ts_array_toString_native, arr);
            return ts_value_make_undefined();
        }
        if (magic0 == 0x53545247 || magic8 == 0x53545247 || magic16 == 0x53545247 || magic0 == TsConsString::MAGIC) { // TsString or TsConsString
            TsString* strObj = ts_ensure_flat(obj);
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
            if (strcmp(keyStr, "toString") == 0) return ts_value_make_native_function((void*)ts_string_toString_native, strObj);
            if (strcmp(keyStr, "valueOf") == 0) return ts_value_make_native_function((void*)ts_string_toString_native, strObj);
            if (strcmp(keyStr, "lastIndexOf") == 0) return ts_value_make_native_function((void*)ts_string_lastIndexOf_native, strObj);
            if (strcmp(keyStr, "trimStart") == 0) return ts_value_make_native_function((void*)ts_string_trimStart_native, strObj);
            if (strcmp(keyStr, "trimEnd") == 0) return ts_value_make_native_function((void*)ts_string_trimEnd_native, strObj);
            if (strcmp(keyStr, "trimLeft") == 0) return ts_value_make_native_function((void*)ts_string_trimStart_native, strObj);
            if (strcmp(keyStr, "trimRight") == 0) return ts_value_make_native_function((void*)ts_string_trimEnd_native, strObj);
            if (strcmp(keyStr, "replaceAll") == 0) return ts_value_make_native_function((void*)ts_string_replaceAll_native, strObj);
            if (strcmp(keyStr, "at") == 0) return ts_value_make_native_function((void*)ts_string_at_native, strObj);
            if (strcmp(keyStr, "concat") == 0) return ts_value_make_native_function((void*)ts_string_concat_native, strObj);
            if (strcmp(keyStr, "match") == 0) return ts_value_make_native_function((void*)ts_string_match_native, strObj);
            if (strcmp(keyStr, "search") == 0) return ts_value_make_native_function((void*)ts_string_search_native, strObj);
            if (strcmp(keyStr, "matchAll") == 0) return ts_value_make_native_function((void*)ts_string_matchAll_native, strObj);
            if (strcmp(keyStr, "codePointAt") == 0) return ts_value_make_native_function((void*)ts_string_codePointAt_native, strObj);
            if (strcmp(keyStr, "normalize") == 0) return ts_value_make_native_function((void*)ts_string_normalize_native, strObj);
            return ts_value_make_undefined();
        }
        // TsHeaders: handled via virtual dispatch (GetPropertyVirtual) below.
        // Previously had a FakeHeaders fast-path here with wrong struct layout
        // that read TsObject::magic (offset 16) as a TsMap* pointer.
        {
            bool isEventEmitter = (magic8 == 0x45564E54 || magic16 == 0x45564E54); // TsEventEmitter::MAGIC ("EVNT")
            // Virtual-inheritance EventEmitter subclasses have magic at large offset (not 8/16).
            // Detect them by vtable pointer match against registered dispatch entries.
            if (!isEventEmitter && ts_gc_base(obj)) {
                uint64_t vt = *(uint64_t*)obj;
                for (int i = 0; i < g_vtable_dispatch_count; i++) {
                    if (vt == g_vtable_dispatch[i].vtable && g_vtable_dispatch[i].isEventEmitter) {
                        isEventEmitter = true;
                        break;
                    }
                }
            }
            if (isEventEmitter) {
                if (strcmp(keyStr, "on") == 0) {
                    void* fn = ts_builtin_lookup_special("event_emitter_on");
                    if (fn) return ts_value_make_function(fn, obj);
                }
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
                ts_gc_write_barrier(&func->properties, func->properties);
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
            
            // Handle .length - return arity (parameter count)
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(func->arity >= 0 ? func->arity : 0);
            }

            // Handle .name specially - return function name if set
            if (strcmp(keyStr, "name") == 0) {
                if (func->name) return ts_value_make_string(func->name);
                return ts_value_make_string(TsString::Create(""));
            }

            // Handle Function.prototype methods directly on the function
            if (strcmp(keyStr, "toString") == 0) {
                return ts_value_make_native_function((void*)ts_function_toString_native, (void*)func);
            }

            // Function.prototype.call / apply
            // Needed for patterns like: (function(){ ... }.call(this));
            if (strcmp(keyStr, "call") == 0) {
                // Box the function properly for ts_function_call_with_this
                TsValue* target = ts_value_make_object(func);
                return ts_value_make_native_function((void*)ts_function_call_native, (void*)target);
            }
            if (strcmp(keyStr, "apply") == 0) {
                TsValue* target = ts_value_make_object(func);
                return ts_value_make_native_function((void*)ts_function_apply_native, (void*)target);
            }

            // Function.prototype.bind: create a bound function wrapper
            if (strcmp(keyStr, "bind") == 0) {
                TsValue* target = ts_value_make_object(func);
                return ts_value_make_native_function((void*)ts_function_bind_native, (void*)target);
            }
            
            return ts_value_make_undefined();
        }

        // Check for TsClosure (magic at offset 16) - closures need .name, .toString(), .bind, .call, .apply, .prototype
        if (magic16 == 0x434C5352) { // TsClosure::MAGIC ("CLSR")
            TsClosure* closure = (TsClosure*)obj;
            // Walk properties + prototype chain (like TsMap path)
            if (closure->properties) {
                TsValue k;
                k.type = ValueType::STRING_PTR;
                k.ptr_val = TsString::GetInterned(keyStr);
                TsMap* currentMap = closure->properties;
                while (currentMap) {
                    // Check for getter
                    std::string getterKey = std::string("__getter_") + keyStr;
                    TsValue gk;
                    gk.type = ValueType::STRING_PTR;
                    gk.ptr_val = TsString::GetInterned(getterKey.c_str());
                    TsValue getterVal = currentMap->Get(gk);
                    if (getterVal.type != ValueType::UNDEFINED) {
                        TsValue* boxedObj = (TsValue*)obj;
                        TsValue* getterFunc = nanbox_from_tagged(getterVal);
                        return ts_function_call_with_this(getterFunc, boxedObj, 0, nullptr);
                    }
                    // Check for direct property
                    TsValue val = currentMap->Get(k);
                    if (val.type != ValueType::UNDEFINED) {
                        return nanbox_from_tagged(val);
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }
            // Handle .prototype - lazily create like TsFunction
            if (strcmp(keyStr, "prototype") == 0) {
                if (!closure->properties) {
                    closure->properties = TsMap::Create();
                ts_gc_write_barrier(&closure->properties, closure->properties);
                }
                TsValue protoKey;
                protoKey.type = ValueType::STRING_PTR;
                protoKey.ptr_val = TsString::GetInterned("prototype");
                TsValue existing = closure->properties->Get(protoKey);
                if (existing.type != ValueType::UNDEFINED) {
                    return nanbox_from_tagged(existing);
                }
                TsMap* proto = TsMap::Create();
                TsValue protoStruct;
                protoStruct.type = ValueType::OBJECT_PTR;
                protoStruct.ptr_val = proto;
                closure->properties->Set(protoKey, protoStruct);
                return ts_value_make_object(proto);
            }
            if (strcmp(keyStr, "name") == 0) {
                if (closure->name) return ts_value_make_string(closure->name);
                return ts_value_make_string(TsString::Create(""));
            }
            if (strcmp(keyStr, "toString") == 0) {
                return ts_value_make_native_function((void*)ts_function_toString_native, (void*)closure);
            }
            if (strcmp(keyStr, "length") == 0) {
                return ts_value_make_int(closure->arity);
            }
            if (strcmp(keyStr, "call") == 0) {
                TsValue* target = ts_value_make_object(closure);
                return ts_value_make_native_function((void*)ts_function_call_native, (void*)target);
            }
            if (strcmp(keyStr, "apply") == 0) {
                TsValue* target = ts_value_make_object(closure);
                return ts_value_make_native_function((void*)ts_function_apply_native, (void*)target);
            }
            if (strcmp(keyStr, "bind") == 0) {
                TsValue* target = ts_value_make_object(closure);
                return ts_value_make_native_function((void*)ts_function_bind_native, (void*)target);
            }
            return ts_value_make_undefined();
        }

        // Virtual property dispatch for polymorphic TsObject subclasses
        // (e.g., TsBuffer, TsIncomingMessage) that override GetPropertyVirtual()
        // Only attempt this on objects that are NOT known non-TsObject types
        // (TsArray, TsString are NOT TsObject subclasses and would crash on virtual call)
        if (magic0 != 0x41525259 && magic8 != 0x41525259 && magic16 != 0x41525259 &&  // TsArray
            magic0 != 0x53545247 && magic8 != 0x53545247 && magic0 != TsConsString::MAGIC && // TsString/TsConsString
            magic0 != 0x52454758) {                                                       // TsRegExp
            TsValue result = ts_try_virtual_property_dispatch(obj, keyStr);
            if (result.type != ValueType::UNDEFINED) {
                return nanbox_from_tagged(result);
            }
        }

        // Fallback: check side-map + prototype chain for native objects
        {
            TsMap* props = getNativeProps(obj);
            if (props) {
                TsValue k;
                k.type = ValueType::STRING_PTR;
                k.ptr_val = TsString::GetInterned(keyStr);

                // Walk prototype chain (same pattern as TsMap path)
                TsMap* currentMap = props;
                while (currentMap) {
                    // Check for getter
                    TsString* getterKeyStr = TsString::FindInterned(
                        (std::string("__getter_") + keyStr).c_str());
                    if (getterKeyStr) {
                        TsValue gk;
                        gk.type = ValueType::STRING_PTR;
                        gk.ptr_val = getterKeyStr;
                        TsValue getterVal = currentMap->Get(gk);
                        if (getterVal.type != ValueType::UNDEFINED) {
                            TsValue* getterFunc = nanbox_from_tagged(getterVal);
                            void* handler = ts_push_exception_handler();
                            jmp_buf* env = (jmp_buf*)handler;
                            if (setjmp(*env) == 0) {
                                TsValue* result = ts_function_call_with_this(
                                    getterFunc, (TsValue*)obj, 0, nullptr);
                                ts_pop_exception_handler();
                                return result;
                            } else {
                                ts_pop_exception_handler();
                                ts_set_exception(nullptr);
                                return ts_value_make_undefined();
                            }
                        }
                    }

                    // Check for direct property
                    TsValue val = currentMap->Get(k);
                    if (val.type != ValueType::UNDEFINED) {
                        return nanbox_from_tagged(val);
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }
        }

        return ts_value_make_undefined();
    }

    // Native wrappers for Function.prototype.call/apply
    // ctx: TsValue* that boxes the target function (OBJECT_PTR or FUNCTION_PTR)
    // When ctx is nullptr (e.g., from Function.prototype.call via addMethod),
    // the target comes from the caller's 'this' via ts_get_call_this().
    TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv) {
        TsValue* target = (TsValue*)ctx;
        if (!target) target = (TsValue*)ts_get_call_this();
        TsValue* thisArg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();
        TsValue** args = (argc > 1 && argv) ? (argv + 1) : nullptr;
        int callArgc = argc > 1 ? (argc - 1) : 0;
        return ts_function_call_with_this(target, thisArg, callArgc, args);
    }

    TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv) {
        TsValue* target = (TsValue*)ctx;
        if (!target) target = (TsValue*)ts_get_call_this();
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
            if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
                keyCStr = ts_ensure_flat(propName)->ToUtf8();
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

        // Flat objects are not TsObject-derived, skip them
        uint32_t magic0 = *(uint32_t*)ptr;
        if (magic0 == 0x464C4154) return nullptr; // FLAT_MAGIC

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
        ts_last_call_argc = 0;
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            // Pad with undefined args - callee may expect more params than caller provides
            // (JS semantics: missing args are undefined)
            TsValue* u = ts_value_make_undefined();
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)closure->func_ptr)(closure, u, u, u, u);
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
            TsValue* u = ts_value_make_undefined();
            if (innerClosure) {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((FnPad)innerClosure->func_ptr)(innerClosure, u, u, u, u);
            }
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)func->funcPtr)(func->context, u, u, u, u);
        }
    }

    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1) {
        ts_last_call_argc = 1;
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            // Validate func_ptr is not a GC object (would indicate corruption)
            void* fp = closure->func_ptr;
            if (fp && ts_gc_base(fp)) {
                return ts_value_make_undefined();
            }
            // Pad with undefined args - callee may expect more params than caller provides
            TsValue* u = ts_value_make_undefined();
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)closure->func_ptr)(closure, arg1, u, u, u);
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
            TsValue* u = ts_value_make_undefined();
            if (innerClosure) {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((FnPad)innerClosure->func_ptr)(innerClosure, arg1, u, u, u);
            }
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)func->funcPtr)(func->context, arg1, u, u, u);
        }
    }

    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2) {
        ts_last_call_argc = 2;
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            void* fp = closure->func_ptr;
            // Guard: func_ptr must be in executable memory (.text), not heap
            if (fp && ts_gc_base(fp)) {
                return ts_value_make_undefined();
            }
            TsValue* u = ts_value_make_undefined();
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)fp)(closure, arg1, arg2, u, u);
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
        if (!func) {
            return ts_value_make_undefined();
        }
        if (func->type == FunctionType::NATIVE) {
            void* fp = func->funcPtr;
            if (fp && ts_gc_base(fp)) {
                return ts_value_make_undefined();
            }
            TsValue* argv[2] = { arg1, arg2 };
            TsValue* result = ((TsFunctionPtr)fp)(func->context, 2, argv);
            return result;
        } else {
            TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
            TsValue* u = ts_value_make_undefined();
            if (innerClosure) {
                void* fp = innerClosure->func_ptr;
                if (fp && ts_gc_base(fp)) {
                    return ts_value_make_undefined();
                }
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((FnPad)fp)(innerClosure, arg1, arg2, u, u);
            }
            void* fp = func->funcPtr;
            if (fp && ts_gc_base(fp)) {
                return ts_value_make_undefined();
            }
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            TsValue* result = ((FnPad)fp)(func->context, arg1, arg2, u, u);
            return result;
        }
    }

    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        ts_last_call_argc = 3;
        // Check for TsClosure first (raw or boxed)
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            // Pad with undefined args - callee may expect more params than caller provides
            TsValue* u = ts_value_make_undefined();
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)closure->func_ptr)(closure, arg1, arg2, arg3, u);
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
            TsValue* u = ts_value_make_undefined();
            if (innerClosure) {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                return ((FnPad)innerClosure->func_ptr)(innerClosure, arg1, arg2, arg3, u);
            }
            typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
            return ((FnPad)func->funcPtr)(func->context, arg1, arg2, arg3, u);
        }
    }

    TsValue* ts_call_4(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        ts_last_call_argc = 4;
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
        ts_last_call_argc = 5;
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
        ts_last_call_argc = 6;
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
        ts_last_call_argc = 7;
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
        ts_last_call_argc = 8;
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
        ts_last_call_argc = 9;
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
        ts_last_call_argc = 10;
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
        ts_last_call_argc = 0;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* u = ts_value_make_undefined();
            TsValue* result;
            if (closure->is_method) {
                // Method trampolines expect (ctx, this) - pass thisArg, pad extra
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, thisArg, u, u, u);
            } else {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, u, u, u, u);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_0(boxedFunc);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_1(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1) {
        ts_last_call_argc = 1;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            void* volatile fp = closure->func_ptr;
            if (!fp || ts_gc_base((void*)fp)) {
                ts_call_this_value = savedThis;
                return ts_value_make_undefined();
            }
            TsValue* u = ts_value_make_undefined();
            TsValue* result;
            if (closure->is_method) {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)(void*)fp)(closure, thisArg, arg1, u, u);
            } else {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)(void*)fp)(closure, arg1, u, u, u);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_1(boxedFunc, arg1);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_2(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2) {
        ts_last_call_argc = 2;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        // Check for TsClosure first - closures already have captured context
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* u = ts_value_make_undefined();
            TsValue* result;
            if (closure->is_method) {
                // Method trampolines expect (ctx, this, arg1, arg2) - pass thisArg, pad extra
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, thisArg, arg1, arg2, u);
            } else {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, arg1, arg2, u, u);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_2(boxedFunc, arg1, arg2);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_3(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        ts_last_call_argc = 3;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* u = ts_value_make_undefined();
            TsValue* result;
            if (closure->is_method) {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, thisArg, arg1, arg2, arg3, u);
            } else {
                typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnPad)closure->func_ptr)(closure, arg1, arg2, arg3, u, u);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_3(boxedFunc, arg1, arg2, arg3);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_4(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        ts_last_call_argc = 4;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            if (closure->is_method) {
                typedef TsValue* (*FnM)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((FnM)closure->func_ptr)(closure, thisArg, arg1, arg2, arg3, arg4);
            } else {
                typedef TsValue* (*Fn4)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
                result = ((Fn4)closure->func_ptr)(closure, arg1, arg2, arg3, arg4);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_4(boxedFunc, arg1, arg2, arg3, arg4);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_5(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        ts_last_call_argc = 5;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            typedef TsValue* (*Fn5)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            result = ((Fn5)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5);
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_5(boxedFunc, arg1, arg2, arg3, arg4, arg5);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_6(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        ts_last_call_argc = 6;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            typedef TsValue* (*Fn6)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            result = ((Fn6)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6);
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_6(boxedFunc, arg1, arg2, arg3, arg4, arg5, arg6);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_7(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        ts_last_call_argc = 7;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            typedef TsValue* (*Fn7)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            result = ((Fn7)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_7(boxedFunc, arg1, arg2, arg3, arg4, arg5, arg6, arg7);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_call_with_this_8(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        ts_last_call_argc = 8;
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            typedef TsValue* (*Fn8)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
            result = ((Fn8)closure->func_ptr)(closure, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return ts_value_make_undefined(); }

        void* savedCtx = func->context;
        bool patchedCtx = false;
        if (!func->context) {
            func->context = thisArg;
            patchedCtx = true;
        }

        TsValue* result = ts_call_8(boxedFunc, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);

        if (patchedCtx) func->context = savedCtx;
        ts_call_this_value = savedThis;
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

    TsValue* ts_call_n(TsValue* boxedFunc, int64_t argc, TsValue** argv) {
        return ts_function_call(boxedFunc, static_cast<int>(argc), argv);
    }

    // Helper for "new ConstructorFunction(...args)" in the slow path.
    // Creates a new object, sets its prototype from constructor.prototype,
    // calls the constructor with this=newObject, and returns the new object.
    static TsValue* ts_new_from_constructor_impl(TsValue* constructorFn, int argc, TsValue** argv) {
        // Guard: null/undefined constructor — return a basic object with .message if args provided
        if (!constructorFn || ts_value_is_undefined(constructorFn) || ts_value_is_null(constructorFn)) {
            TsMap* obj = TsMap::Create();
            if (argc >= 1 && argv && argv[0]) {
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                obj->Set(msgKey, nanbox_to_tagged(argv[0]));
            }
            return ts_value_make_object(obj);
        }

        // 1. Create a new TsMap object
        TsMap* newObj = TsMap::Create();

        // 2. Get constructor.prototype and set it as the new object's prototype
        TsString* protoKey = TsString::Create("prototype");
        TsValue* protoVal = ts_object_get_dynamic(constructorFn, ts_value_make_string(protoKey));
        if (protoVal && !ts_value_is_undefined(protoVal) && !ts_value_is_null(protoVal)) {
            uint64_t protoNb = nanbox_from_tsvalue_ptr(protoVal);
            if (nanbox_is_ptr(protoNb)) {
                void* protoRaw = nanbox_to_ptr(protoNb);
                if (protoRaw) {
                    // Check if it's a TsMap (check magic at standard offsets)
                    uint32_t m16 = *(uint32_t*)((char*)protoRaw + 16);
                    uint32_t m20 = *(uint32_t*)((char*)protoRaw + 20);
                    uint32_t m24 = *(uint32_t*)((char*)protoRaw + 24);
                    if (m16 == 0x4D415053 || m20 == 0x4D415053 || m24 == 0x4D415053) {
                        newObj->SetPrototype((TsMap*)protoRaw);
                    }
                }
            }
        }

        // 3. Box the new object as 'this'
        TsValue* thisArg = ts_value_make_object(newObj);

        // 4. Call the constructor with this = new object
        // Guard: if constructor is not callable (e.g., TsMap stub), store args as .message
        TsClosure* asClosure = ts_extract_closure(constructorFn);
        TsFunction* asFunc = ts_extract_function(constructorFn);
        if (!asClosure && !asFunc) {
            if (argc >= 1 && argv && argv[0]) {
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                newObj->Set(msgKey, nanbox_to_tagged(argv[0]));
            }
            return thisArg;
        }
        TsValue* result = ts_function_call_with_this(constructorFn, thisArg, argc, argv);

        // 5. If the constructor returned an object, use that instead (JS spec)
        // Per ECMAScript: if the constructor returns ANY object (not a primitive),
        // that object is the result of `new`. This includes functions, arrays, etc.
        if (result && !ts_value_is_undefined(result) && !ts_value_is_null(result)) {
            uint64_t rNb = nanbox_from_tsvalue_ptr(result);
            if (nanbox_is_ptr(rNb)) {
                void* rPtr = nanbox_to_ptr(rNb);
                if (rPtr && (uintptr_t)rPtr > 0x10000) {
                    return result;
                }
            }
        }

        // 6. Return the new object
        return thisArg;
    }

    TsValue* ts_new_from_constructor_0(TsValue* constructorFn) {
        return ts_new_from_constructor_impl(constructorFn, 0, nullptr);
    }

    TsValue* ts_new_from_constructor_1(TsValue* constructorFn, TsValue* arg1) {
        TsValue* argv[] = { arg1 };
        return ts_new_from_constructor_impl(constructorFn, 1, argv);
    }

    TsValue* ts_new_from_constructor_2(TsValue* constructorFn, TsValue* arg1, TsValue* arg2) {
        TsValue* argv[] = { arg1, arg2 };
        return ts_new_from_constructor_impl(constructorFn, 2, argv);
    }

    TsValue* ts_new_from_constructor_3(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsValue* argv[] = { arg1, arg2, arg3 };
        return ts_new_from_constructor_impl(constructorFn, 3, argv);
    }

    TsValue* ts_new_from_constructor_4(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4 };
        return ts_new_from_constructor_impl(constructorFn, 4, argv);
    }

    TsValue* ts_new_from_constructor_5(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5 };
        return ts_new_from_constructor_impl(constructorFn, 5, argv);
    }

    TsValue* ts_new_from_constructor_6(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6 };
        return ts_new_from_constructor_impl(constructorFn, 6, argv);
    }

    TsValue* ts_new_from_constructor_7(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
        return ts_new_from_constructor_impl(constructorFn, 7, argv);
    }

    TsValue* ts_new_from_constructor_8(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
        return ts_new_from_constructor_impl(constructorFn, 8, argv);
    }

    TsValue* ts_function_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int argc, TsValue** argv) {
        ts_last_call_argc = argc;
        // Save/set/restore the global 'this' context so that functions compiled
        // with ts_get_call_this() (function declarations and function expressions
        // that reference 'this') can find the thisArg. This is essential for:
        // - Constructor functions called via 'new' (ts_new_from_constructor)
        // - Function.prototype.call/apply
        // - Prototype method calls on dynamically-typed objects
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        // Handle closures - pass closure as first argument to preserve captured state.
        // The thisArg is already saved in ts_call_this_value above, so functions
        // can access 'this' via ts_get_call_this(). Closures need their own pointer
        // as the first argument to access captured cells (ts_closure_get_cell).
        // This matches the calling convention used by ts_call_N functions.
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            switch (argc) {
                case 0: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)closure->func_ptr)(closure);
                    break;
                }
                case 1: {
                    typedef TsValue* (*Fn)(void*, TsValue*);
                    result = ((Fn)closure->func_ptr)(closure, argv[0]);
                    break;
                }
                case 2: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*);
                    result = ((Fn)closure->func_ptr)(closure, argv[0], argv[1]);
                    break;
                }
                case 3: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*, TsValue*);
                    result = ((Fn)closure->func_ptr)(closure, argv[0], argv[1], argv[2]);
                    break;
                }
                default: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)closure->func_ptr)(closure);
                    break;
                }
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            ts_call_this_value = savedThis;
            return ts_value_make_undefined();
        }

        // Check if funcPtr wraps a closure (via ts_value_make_function wrapping a TsClosure*)
        // In this case, call the inner closure's function with thisArg directly
        TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
        if (innerClosure) {
            TsValue* result;
            switch (argc) {
                case 0: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg);
                    break;
                }
                case 1: {
                    typedef TsValue* (*Fn)(void*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0]);
                    break;
                }
                case 2: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0], argv[1]);
                    break;
                }
                case 3: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0], argv[1], argv[2]);
                    break;
                }
                default: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg);
                    break;
                }
            }
            ts_call_this_value = savedThis;
            return result;
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
        ts_call_this_value = savedThis;
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

        // Guard against small integer values (NaN-boxed ints, undefined, null)
        if ((uintptr_t)rawPtr < 0x10000) return ts_value_make_array(TsArray::Create(0));

        // Check string — for...in on a string enumerates character indices
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x53545247) { // TsString::MAGIC "STRG"
            TsString* str = (TsString*)rawPtr;
            int64_t len = str->Length();
            TsArray* arr = TsArray::Create(len);
            for (int64_t i = 0; i < len; i++) {
                arr->Push((int64_t)(uintptr_t)ts_value_make_string(TsString::FromInt(i)));
            }
            return ts_value_make_array(arr);
        }

        // Check flat object (magic at offset 0)
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_keys(rawPtr));
        }

        // Check TsMap::magic at offset 16 (after vptr + explicit vtable field)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            // Object.keys() returns only enumerable own properties
            extern void* ts_map_enumerable_keys(void*);
            return ts_value_make_array(ts_map_enumerable_keys(rawPtr));
        }

        // Check if this is a Proxy - only attempt dynamic_cast on known TsObject types
        // (TsString, TsArray, etc. are NOT TsObject subclasses — dynamic_cast crashes)
        if (magic == 0x50524F58) { // TsProxy::MAGIC "PROX"
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawPtr);
            if (proxy) {
                return proxy->ownKeys();
            }
        }

        // Not a map - return empty array
        return ts_value_make_array(TsArray::Create(0));
    }
    
    // Object.values(obj) - returns array of values
    TsValue* ts_object_values(TsValue* obj) {
        if (!obj) return ts_value_make_array(TsArray::Create(0));

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check flat object (magic at offset 0)
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_values(rawPtr));
        }

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

        // Check flat object (magic at offset 0)
        uint32_t magic0_e = *(uint32_t*)rawPtr;
        if (magic0_e == 0x464C4154) { // FLAT_MAGIC
            return ts_value_make_array((TsArray*)ts_flat_object_entries(rawPtr));
        }

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

        // Handle flat objects
        if (is_flat_object(rawPtr)) {
            return ts_value_make_array((TsArray*)ts_flat_object_keys(rawPtr));
        }

        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
            return ts_value_make_array(ts_map_keys(rawPtr));
        }

        // Handle TsFunction and TsClosure - delegate to their properties map
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsMap* props = ((TsFunction*)rawPtr)->properties;
            if (props) return ts_value_make_array(ts_map_keys(props));
        }
        if (magic == 0x434C5352) { // TsClosure magic
            TsMap* props = ((TsClosure*)rawPtr)->properties;
            if (props) return ts_value_make_array(ts_map_keys(props));
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
            newObj->SetPrototype(nullptr);
            return ts_value_make_object(newObj);
        }

        // Unbox proto if needed
        void* protoRaw = ts_value_get_object(proto);
        if (!protoRaw) protoRaw = proto;

        // Check if proto is a TsMap
        uint32_t magic = *(uint32_t*)((char*)protoRaw + 16);
        if (magic == 0x4D415053) { // TsMap::MAGIC
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

        // Check if obj is a TsMap or TsClosure
        uint32_t magic = *(uint32_t*)((char*)objRaw + 16);

        // Handle TsClosure: set prototype pointer on closure->properties
        if (magic == 0x434C5352) { // TsClosure::MAGIC
            TsClosure* closure = (TsClosure*)objRaw;

            if (!proto || ts_value_is_nullish(proto)) {
                if (closure->properties) closure->properties->SetPrototype(nullptr);
                return obj;
            }

            void* protoRaw = ts_value_get_object(proto);
            if (!protoRaw) protoRaw = proto;

            // Extract source TsMap from proto (convert flat objects)
            TsMap* sourceMap = nullptr;
            if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);
            uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
            if (protoMagic == 0x4D415053) {
                sourceMap = (TsMap*)protoRaw;
            } else if (protoMagic == 0x434C5352) {
                sourceMap = ((TsClosure*)protoRaw)->properties;
            }

            if (sourceMap) {
                if (!closure->properties) {
                    closure->properties = TsMap::Create();
                    ts_gc_write_barrier(&closure->properties, closure->properties);
                }
                if (!closure->properties->WouldCreateCycle(sourceMap)) {
                    closure->properties->SetPrototype(sourceMap);
                }
            }
            return obj;
        }

        if (magic != 0x4D415053) { // TsMap::MAGIC
            // Generic TsObject subclass (native C++ objects like TsServerResponse):
            // set prototype pointer on the side-map instead of copying properties.
            if (!proto || ts_value_is_nullish(proto)) {
                TsMap* props = getNativeProps(objRaw);
                if (props) props->SetPrototype(nullptr);
                return obj;
            }

            void* protoRaw = ts_value_get_object(proto);
            if (!protoRaw) protoRaw = proto;

            // Extract source TsMap from proto (convert flat objects)
            TsMap* sourceMap = nullptr;
            if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);
            uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
            if (protoMagic == 0x4D415053) {
                sourceMap = (TsMap*)protoRaw;
            } else if (protoMagic == 0x434C5352) {
                sourceMap = ((TsClosure*)protoRaw)->properties;
            }

            if (sourceMap) {
                TsMap* props = getOrCreateNativeProps(objRaw);
                if (!props->WouldCreateCycle(sourceMap)) {
                    props->SetPrototype(sourceMap);
                }
            }
            return obj;
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

        // Convert flat objects to TsMap
        if (is_flat_object(protoRaw)) protoRaw = ts_flat_object_to_map(protoRaw);

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

        // Convert flat objects to TsMap first
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

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

        // Convert flat objects to TsMap first
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

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

        // Convert flat objects to TsMap first
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

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

        // Flat objects are never frozen (they haven't been converted)
        if (is_flat_object(rawPtr)) {
            return ts_value_make_bool(false);
        }

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

        // Flat objects are never sealed (they haven't been converted)
        if (is_flat_object(rawPtr)) {
            return ts_value_make_bool(false);
        }

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

        // Flat objects are always extensible (via overflow map)
        if (is_flat_object(rawPtr)) {
            return ts_value_make_bool(true);
        }

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

        // Convert flat object to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Check if it's a TsMap (or extract properties map from function/closure)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)rawPtr;
            if (!func->properties) func->properties = TsMap::Create();
                ts_gc_write_barrier(&func->properties, func->properties);
            rawPtr = func->properties;
            magic = 0x4D415053;
        } else if (magic == 0x434C5352) { // TsClosure magic
            TsClosure* clos = (TsClosure*)rawPtr;
            if (!clos->properties) {
                clos->properties = TsMap::Create();
                ts_gc_write_barrier(&clos->properties, clos->properties);
            }
            rawPtr = clos->properties;
            magic = 0x4D415053;
        }
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

        // Convert flat descriptor to TsMap
        if (is_flat_object(descRaw)) {
            descRaw = ts_flat_object_to_map(descRaw);
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

        // Extract property attribute flags from descriptor.
        // Per JS spec: missing flags default to false for new properties via defineProperty,
        // but preserve existing value for properties that already exist.
        // Attribute flag constants (match TsHashTable::ATTR_*)
        constexpr uint8_t ATTR_ENUMERABLE   = 0x01;
        constexpr uint8_t ATTR_WRITABLE     = 0x02;
        constexpr uint8_t ATTR_CONFIGURABLE = 0x04;

        uint8_t existingAttrs = map->GetPropertyAttrs(propKey);
        bool propertyExists = map->Has(propKey);
        uint8_t attrs = 0;

        // enumerable
        TsValue enumKey;
        enumKey.type = ValueType::STRING_PTR;
        enumKey.ptr_val = TsString::GetInterned("enumerable");
        if (descMap->Has(enumKey)) {
            TsValue ev = descMap->Get(enumKey);
            if (ev.type == ValueType::BOOLEAN ? ev.i_val : (ev.type != ValueType::UNDEFINED && ev.ptr_val))
                attrs |= ATTR_ENUMERABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_ENUMERABLE);
        }

        // writable
        TsValue writableKey;
        writableKey.type = ValueType::STRING_PTR;
        writableKey.ptr_val = TsString::GetInterned("writable");
        if (descMap->Has(writableKey)) {
            TsValue wv = descMap->Get(writableKey);
            if (wv.type == ValueType::BOOLEAN ? wv.i_val : (wv.type != ValueType::UNDEFINED && wv.ptr_val))
                attrs |= ATTR_WRITABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_WRITABLE);
        }

        // configurable
        TsValue configKey;
        configKey.type = ValueType::STRING_PTR;
        configKey.ptr_val = TsString::GetInterned("configurable");
        if (descMap->Has(configKey)) {
            TsValue cv = descMap->Get(configKey);
            if (cv.type == ValueType::BOOLEAN ? cv.i_val : (cv.type != ValueType::UNDEFINED && cv.ptr_val))
                attrs |= ATTR_CONFIGURABLE;
        } else if (propertyExists) {
            attrs |= (existingAttrs & ATTR_CONFIGURABLE);
        }

        // Store getters/setters with non-enumerable attrs (they're synthetic)
        // (getter/setter code above already stored them — mark them non-enumerable)
        if (descMap->Has(getKey)) {
            std::string gk2 = std::string("__getter_") + propName;
            TsValue gk2v;
            gk2v.type = ValueType::STRING_PTR;
            gk2v.ptr_val = TsString::GetInterned(gk2.c_str());
            map->SetPropertyAttrs(gk2v, 0); // non-enumerable
        }
        if (descMap->Has(setKey)) {
            std::string sk2 = std::string("__setter_") + propName;
            TsValue sk2v;
            sk2v.type = ValueType::STRING_PTR;
            sk2v.ptr_val = TsString::GetInterned(sk2.c_str());
            map->SetPropertyAttrs(sk2v, 0); // non-enumerable
        }

        // Check for value (data descriptor) — store with extracted attributes
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::GetInterned("value");

        if (descMap->Has(valueKey)) {
            TsValue value = descMap->Get(valueKey);
            map->SetWithAttrs(propKey, value, attrs);
        } else if (!descMap->Has(getKey) && !descMap->Has(setKey)) {
            // No value, getter, or setter — just update attributes on existing property
            if (propertyExists) {
                map->SetPropertyAttrs(propKey, attrs);
            }
        }

        return obj;
    }

    // Object.defineProperties(obj, descriptors) - defines multiple properties
    TsValue* ts_object_defineProperties(TsValue* obj, TsValue* descriptors) {
        if (!obj || !descriptors) return obj;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Convert flat objects to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Check if target is a TsMap
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic != 0x4D415053) {
            return obj;
        }

        // Get the descriptors object
        void* descRaw = ts_value_get_object(descriptors);
        if (!descRaw) descRaw = descriptors;

        // Convert flat descriptor object to TsMap
        if (is_flat_object(descRaw)) {
            descRaw = ts_flat_object_to_map(descRaw);
        }

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

            // Convert tagged TsValue to NaN-boxed TsValue* for ts_object_defineProperty
            TsValue* descNb = nanbox_from_tagged(desc);
            ts_object_defineProperty(obj, key, descNb);
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

        // Convert flat object to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

        // Check if it's a TsMap (or extract properties map from function/closure)
        uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
        if (magic == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)rawPtr;
            if (!func->properties) return ts_value_make_object(nullptr);
            rawPtr = func->properties;
            magic = 0x4D415053;
        } else if (magic == 0x434C5352) { // TsClosure magic
            TsClosure* clos = (TsClosure*)rawPtr;
            if (!clos->properties) return ts_value_make_object(nullptr);
            rawPtr = clos->properties;
            magic = 0x4D415053;
        }
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

        // Set value — use interned strings for keys so property lookup matches
        TsValue valueKey;
        valueKey.type = ValueType::STRING_PTR;
        valueKey.ptr_val = TsString::GetInterned("value");
        desc->Set(valueKey, value);

        // Read back actual property attribute flags
        uint8_t attrs = map->GetPropertyAttrs(propKey);

        TsValue writableKey;
        writableKey.type = ValueType::STRING_PTR;
        writableKey.ptr_val = TsString::GetInterned("writable");
        TsValue writableVal;
        writableVal.type = ValueType::BOOLEAN;
        writableVal.i_val = (attrs & 0x02) ? 1 : 0; // ATTR_WRITABLE
        desc->Set(writableKey, writableVal);

        TsValue enumKey;
        enumKey.type = ValueType::STRING_PTR;
        enumKey.ptr_val = TsString::GetInterned("enumerable");
        TsValue enumVal;
        enumVal.type = ValueType::BOOLEAN;
        enumVal.i_val = (attrs & 0x01) ? 1 : 0; // ATTR_ENUMERABLE
        desc->Set(enumKey, enumVal);

        TsValue configKey;
        configKey.type = ValueType::STRING_PTR;
        configKey.ptr_val = TsString::GetInterned("configurable");
        TsValue configVal;
        configVal.type = ValueType::BOOLEAN;
        configVal.i_val = (attrs & 0x04) ? 1 : 0; // ATTR_CONFIGURABLE
        desc->Set(configKey, configVal);

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

        // Convert flat object to TsMap
        if (is_flat_object(rawPtr)) {
            rawPtr = ts_flat_object_to_map(rawPtr);
        }

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

        // Check for flat source object
        uint32_t sourceMagic0 = *(uint32_t*)sourceRaw;
        bool sourceIsFlat = (sourceMagic0 == 0x464C4154); // FLAT_MAGIC

        // Check for flat target object
        uint32_t targetMagic0 = *(uint32_t*)targetRaw;
        bool targetIsFlat = (targetMagic0 == 0x464C4154); // FLAT_MAGIC

        if (sourceIsFlat) {
            // Copy from flat source to target
            uint32_t shapeId = flat_object_shape_id(sourceRaw);
            ShapeDescriptor* desc = ts_shape_lookup(shapeId);
            if (desc) {
                for (uint32_t i = 0; i < desc->numSlots; i++) {
                    uint64_t val = *(uint64_t*)((char*)sourceRaw + 16 + i * 8);
                    TsString* keyStr = TsString::Create(desc->propNames[i]);
                    if (targetIsFlat) {
                        ts_flat_object_set_property(targetRaw, desc->propNames[i], (void*)(uintptr_t)val);
                    } else {
                        uint32_t targetMagic16 = *(uint32_t*)((char*)targetRaw + 16);
                        if (targetMagic16 == 0x4D415053) {
                            TsMap* targetMap = (TsMap*)targetRaw;
                            TsValue tv = nanbox_to_tagged((TsValue*)(uintptr_t)val);
                            targetMap->Set(TsValue(keyStr), tv);
                        }
                    }
                }
            }
            return target;
        }

        // Check both are TsMaps (magic at offset 16 - see TsObject.h layout)
        uint32_t targetMagic = *(uint32_t*)((char*)targetRaw + 16);
        uint32_t sourceMagic = *(uint32_t*)((char*)sourceRaw + 16);

        if (sourceMagic != 0x4D415053) {
            return target;
        }

        TsMap* sourceMap = (TsMap*)sourceRaw;

        // Get entries from source and copy to target
        TsArray* entries = (TsArray*)sourceMap->GetEntries();
        int64_t len = entries->Length();
        for (int64_t i = 0; i < len; i++) {
            TsArray* entry = (TsArray*)entries->Get(i);
            TsValue* key = (TsValue*)entry->Get(0);
            TsValue* val = (TsValue*)entry->Get(1);
            if (targetIsFlat) {
                TsString* keyStr = (TsString*)ts_nanbox_safe_unbox(key);
                if (keyStr) {
                    const char* k = keyStr->ToUtf8();
                    if (k) ts_flat_object_set_property(targetRaw, k, val);
                }
            } else if (targetMagic == 0x4D415053) {
                TsMap* targetMap = (TsMap*)targetRaw;
                targetMap->Set(nanbox_to_tagged(key), nanbox_to_tagged(val));
            }
        }

        return target;
    }
    
    // Object.hasOwn(obj, prop) - check if object has own property
    bool ts_object_has_own(TsValue* obj, TsValue* prop) {
        if (!obj || !prop) return false;

        void* rawPtr = ts_value_get_object(obj);
        if (!rawPtr) rawPtr = obj;

        // Check for flat object first
        uint32_t magic0 = *(uint32_t*)rawPtr;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            void* propRaw = ts_nanbox_safe_unbox(prop);
            if (!propRaw) return false;
            TsString* keyStr = (TsString*)propRaw;
            const char* k = keyStr->ToUtf8();
            if (!k) return false;
            return ts_flat_object_has_property(rawPtr, k);
        }

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

    TsValue* ts_value_inc(TsValue* a) {
        if (!a) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double d = nanbox_extract_double(a);
        return ts_value_make_double(d + 1.0);
    }

    TsValue* ts_value_dec(TsValue* a) {
        if (!a) return ts_value_make_double(std::numeric_limits<double>::quiet_NaN());
        double d = nanbox_extract_double(a);
        return ts_value_make_double(d - 1.0);
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

        // Both strings: compare by content
        if (nanbox_is_string_ptr(nba) && nanbox_is_string_ptr(nbb)) {
            TsString* s1 = (TsString*)nanbox_to_ptr(nba);
            TsString* s2 = (TsString*)nanbox_to_ptr(nbb);
            return ts_value_make_bool(s1->Equals(s2));
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
            if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) return TsString::Create("string");
            if (magic0 == 0x41525259) return TsString::Create("object");
            if (magic0 == 0x4D415053) return TsString::Create("object");
            if (magic0 == 0x53455453) return TsString::Create("object");
            if (magic0 == 0x46554E43) return TsString::Create("function");
            if (magic0 == 0x464C4154) return TsString::Create("object"); // FLAT_MAGIC

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
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Handle TsRegExpMatchArray (integer index access for match[0], match[1], etc.)
        if (magic0 == 0x524D4154) { // TsRegExpMatchArray::MAGIC ("RMAT")
            TsRegExpMatchArray* match = (TsRegExpMatchArray*)rawObj;
            if (keyIsInt && keyIdx >= 0 && keyIdx < match->Length()) {
                return (TsValue*)match->Get((size_t)keyIdx);
            }
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Handle TsString or TsConsString
        if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) {
            // Handle integer index: str[0], str[1], etc.
            if (keyIsInt) {
                TsString* str = ts_ensure_flat(rawObj);
                if (keyIdx >= 0 && keyIdx < str->Length()) {
                    TsString* ch = str->CharAt(keyIdx);
                    return ts_value_make_string(ch);
                }
                return ts_value_make_undefined();
            }
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Handle flat object
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return (TsValue*)ts_flat_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);

        // Handle TsHeaders (TsObject::magic at offset 16) — bracket access for header names
        // Express does this.headers[lc] which goes through ts_object_get_dynamic
        if (magic16 == 0x48454144) { // TsHeaders::MAGIC "HEAD"
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Handle TsBuffer (TsObject::magic at offset 16)
        if (magic16 == 0x42554646) { // TsBuffer::MAGIC "BUFF"
            TsBuffer* buf = (TsBuffer*)rawObj;
            if (keyIsInt) {
                if (keyIdx >= 0 && (size_t)keyIdx < buf->GetLength()) {
                    return ts_value_make_int(buf->GetData()[keyIdx]);
                }
                return ts_value_make_undefined();
            }
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Handle TsTypedArray (TsObject::magic at offset 16)
        if (magic16 == 0x54415252) { // TsTypedArray::MAGIC "TARR"
            // Check for Proxy first
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                return proxy->get(key, nullptr);
            }
            TsTypedArray* ta = (TsTypedArray*)rawObj;
            if (keyIsInt) {
                if (keyIdx >= 0 && (size_t)keyIdx < ta->GetLength()) {
                    return ts_value_make_double(ta->Get((size_t)keyIdx));
                }
                return ts_value_make_undefined();
            }
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) return ts_object_get_property(rawObj, k);
            }
            return ts_value_make_undefined();
        }

        // Only do dynamic_cast for Proxy check if we know this is a TsObject-derived class
        if (magic16 == 0x4D415053 || magic16 == TsFunction::MAGIC) {
            // Validate that rawObj actually has a C++ vtable before dynamic_cast.
            // Read the first 8 bytes as a potential vtable pointer. On Windows x64,
            // valid vtable pointers are in the executable image (0x00007FFx range).
            // Objects without C++ vtables (e.g., stack locals, raw structs) will have
            // garbage at offset 0, causing dynamic_cast to crash.
            uint64_t vtableAddr = *(uint64_t*)rawObj;
            bool hasValidVtable = (vtableAddr >> 32) >= 0x00007FF0 && (vtableAddr >> 32) <= 0x00007FFF;
            if (hasValidVtable) {
                TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
                if (proxy) {
                    return proxy->get(key, nullptr);
                }
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
                        return ts_value_make_int(func->arity >= 0 ? func->arity : 0);
                    }
                    if (strcmp(k, "name") == 0) {
                        if (func->name) return ts_value_make_string(func->name);
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

        // Check if this is a TsClosure and get its properties
        if (magic16 == 0x434C5352) { // TsClosure::MAGIC ("CLSR")
            TsClosure* closure = (TsClosure*)rawObj;
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) {
                    if (strcmp(k, "name") == 0) {
                        if (closure->name) return ts_value_make_string(closure->name);
                        return ts_value_make_string(TsString::Create(""));
                    }
                    if (strcmp(k, "bind") == 0) {
                        return ts_value_make_native_function((void*)ts_function_bind_native, (void*)closure);
                    }
                    if (strcmp(k, "call") == 0) {
                        return ts_value_make_native_function((void*)ts_function_call_native, (void*)closure);
                    }
                    if (strcmp(k, "apply") == 0) {
                        return ts_value_make_native_function((void*)ts_function_apply_native, (void*)closure);
                    }
                    if (strcmp(k, "toString") == 0) {
                        return ts_value_make_native_function((void*)ts_function_toString_native, (void*)closure);
                    }
                    if (strcmp(k, "length") == 0) {
                        return ts_value_make_int(closure->arity);
                    }
                }
            }
            // Check closure properties (e.g., .prototype)
            if (closure->properties) {
                TsValue funcKeyVal = nanbox_to_tagged(key);
                TsValue result = closure->properties->Get(funcKeyVal);
                if (result.type != ValueType::UNDEFINED) {
                    return nanbox_from_tagged(result);
                }
            }
            // Handle .prototype lazily - create it if accessed for the first time
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k && strcmp(k, "prototype") == 0) {
                    if (!closure->properties) {
                        closure->properties = TsMap::Create();
                ts_gc_write_barrier(&closure->properties, closure->properties);
                    }
                    TsValue protoKey;
                    protoKey.type = ValueType::STRING_PTR;
                    protoKey.ptr_val = TsString::GetInterned("prototype");
                    TsValue existing = closure->properties->Get(protoKey);
                    if (existing.type != ValueType::UNDEFINED) {
                        return nanbox_from_tagged(existing);
                    }
                    TsMap* proto = TsMap::Create();
                    TsValue protoStruct;
                    protoStruct.type = ValueType::OBJECT_PTR;
                    protoStruct.ptr_val = proto;
                    closure->properties->Set(protoKey, protoStruct);
                    return ts_value_make_object(proto);
                }
            }
            return ts_value_make_undefined();
        }

        // Check for TsTypedArray (magic at offset 16)
        if (magic16 == 0x54415252) { // TsTypedArray::MAGIC = "TARR"
            TsTypedArray* ta = (TsTypedArray*)rawObj;
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k) {
                    // Properties
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
                    // Methods
                    if (strcmp(k, "slice") == 0) {
                        return ts_value_make_native_function((void*)ts_typed_array_slice_native, ta);
                    }
                    if (strcmp(k, "set") == 0) {
                        return ts_value_make_native_function((void*)ts_typed_array_set_native, ta);
                    }
                    if (strcmp(k, "subarray") == 0) {
                        return ts_value_make_native_function((void*)ts_typed_array_subarray_native, ta);
                    }
                    if (strcmp(k, "fill") == 0) {
                        return ts_value_make_native_function((void*)ts_typed_array_fill_native, ta);
                    }
                    // indexOf/includes/find/findIndex etc. could be added later
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

        // Check for TsSet (magic 0x53455453 "SETS" at offset 16, 20, or 24)
        uint32_t magic20 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 20);
        uint32_t magic24 = *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(rawObj) + 24);
        if (magic16 == 0x53455453 || magic20 == 0x53455453 || magic24 == 0x53455453) {
            // TsSet - dispatch through ts_set_get_property
            extern TsValue* ts_set_get_property(void* obj, void* propName);
            if (keyStr) {
                return ts_set_get_property(rawObj, keyStr);
            }
            return ts_value_make_undefined();
        }

        // Check if this is actually a TsMap before using map operations
        // TsMap::MAGIC is at offset 16 (after vtable ptr + explicit vtable field)
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

        // For TsObject subclasses with GetPropertyVirtual (TsHttpServer, etc.),
        // delegate to ts_object_get_property which checks the magic16 whitelist.
        if (keyStr) {
            const char* k = keyStr->ToUtf8();
            if (k && magic16 != 0x4D415053) {
                // Not a TsMap — try ts_object_get_property for GetPropertyVirtual dispatch
                TsValue* result = ts_object_get_property(rawObj, k);
                if (result && !ts_value_is_undefined(result)) {
                    return result;
                }
            }
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
                        TsValue* getterFunc = nanbox_from_tagged(getterVal);
                        return ts_function_call_with_this(getterFunc, obj, 0, nullptr);
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }
        }

        // Intercept __proto__ accessor
        if (keyStr) {
            const char* k = keyStr->ToUtf8();
            if (k && strcmp(k, "__proto__") == 0) {
                TsMap* proto = map->GetPrototype();
                if (proto) return ts_value_make_object(proto);
                return ts_value_make_null();
            }
        }

        // Handle Map/Set .size property (computed, not stored)
        if (keyStr) {
            const char* k = keyStr->ToUtf8();
            if (k && strcmp(k, "size") == 0) {
                return ts_value_make_int(map->Size());
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

        // Check flat object (magic at offset 0)
        uint32_t magic0_sd = *(uint32_t*)rawObj;
        if (magic0_sd == 0x464C4154) { // FLAT_MAGIC
            // Need to extract key as C string
            uint64_t keyNb = nanbox_from_tsvalue_ptr(key);
            if (nanbox_is_ptr(keyNb)) {
                void* keyPtr = nanbox_to_ptr(keyNb);
                if (keyPtr && ts_is_any_string(keyPtr)) {
                    const char* keyCStr = ts_ensure_flat(keyPtr)->ToUtf8();
                    if (keyCStr) {
                        ts_flat_object_set_property(rawObj, keyCStr, value);
                        return;
                    }
                }
            }
        }

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
                // Check for TsTypedArray (magic at offset 16)
                uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
                if (magic16 == 0x54415252) { // TsTypedArray::MAGIC
                    TsTypedArray* ta = (TsTypedArray*)rawObj;
                    TsValue result;
                    if (idx < 0 || (size_t)idx >= ta->GetLength()) {
                        result.type = ValueType::UNDEFINED;
                        result.i_val = 0;
                    } else {
                        result.type = ValueType::NUMBER_DBL;
                        result.d_val = ta->Get((size_t)idx);
                    }
                    return result;
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
            // Check for TsTypedArray (magic at offset 16)
            uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
            if (magic16 == 0x54415252) { // TsTypedArray::MAGIC
                TsTypedArray* ta = (TsTypedArray*)rawObj;
                if (idx >= 0 && (size_t)idx < ta->GetLength()) {
                    double dval = 0;
                    if (value.type == ValueType::NUMBER_DBL) dval = value.d_val;
                    else if (value.type == ValueType::NUMBER_INT) dval = (double)value.i_val;
                    ta->Set((size_t)idx, dval);
                }
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

        // Check for flat inline-slot object (magic at offset 0)
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            if (keyStr) {
                const char* keyCStr = keyStr->ToUtf8();
                if (keyCStr) {
                    // Convert TaggedValue to NaN-boxed for ts_flat_object_set_property
                    TsValue* nbValue = nanbox_from_tagged(value);
                    ts_flat_object_set_property(rawObj, keyCStr, nbValue);
                    return value;
                }
            }
        }

        // Check for TsFunction (can have properties like _.chunk)
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC ("FUNC")
            TsFunction* func = (TsFunction*)rawObj;
            if (!func->properties) {
                func->properties = TsMap::Create();
                ts_gc_write_barrier(&func->properties, func->properties);
            }
            func->properties->Set(key, value);
            return value;
        }

        // Check for TsClosure (can have properties like .prototype)
        if (magic16 == 0x434C5352) { // TsClosure::MAGIC ("CLSR")
            TsClosure* closure = (TsClosure*)rawObj;
            if (!closure->properties) {
                closure->properties = TsMap::Create();
                ts_gc_write_barrier(&closure->properties, closure->properties);
            }
            closure->properties->Set(key, value);
            return value;
        }

        // Check if it's a map
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC
            TsMap* map = (TsMap*)rawObj;

            // Check for a setter (__setter_<propertyName>), walking prototype chain
            const char* keyCStr = keyStr->ToUtf8();
            if (keyCStr) {
                std::string setterKey = std::string("__setter_") + keyCStr;
                TsValue sk;
                sk.type = ValueType::STRING_PTR;
                sk.ptr_val = TsString::GetInterned(setterKey.c_str());
                TsMap* currentMap = map;
                while (currentMap) {
                    TsValue setterVal = currentMap->Get(sk);
                    if (setterVal.type != ValueType::UNDEFINED) {
                        // Found a setter - invoke with 'this' as the ORIGINAL object
                        TsValue* boxedObj = nanbox_from_tagged(obj);
                        TsValue* setterFunc = nanbox_from_tagged(setterVal);
                        TsValue* boxedVal = nanbox_from_tagged(value);
                        TsValue* args[] = { boxedVal };
                        ts_function_call_with_this(setterFunc, boxedObj, 1, args);
                        return value;
                    }
                    currentMap = currentMap->GetPrototype();
                }
            }

            // Intercept __proto__ assignment
            if (keyCStr && strcmp(keyCStr, "__proto__") == 0) {
                void* protoPtr = value.ptr_val;
                if (value.type == ValueType::OBJECT_PTR && protoPtr) {
                    // Check if value is a TsMap
                    uint32_t pm16 = *(uint32_t*)((char*)protoPtr + 16);
                    uint32_t pm20 = *(uint32_t*)((char*)protoPtr + 20);
                    uint32_t pm24 = *(uint32_t*)((char*)protoPtr + 24);
                    if (pm16 == 0x4D415053 || pm20 == 0x4D415053 || pm24 == 0x4D415053) {
                        if (!map->WouldCreateCycle((TsMap*)protoPtr)) {
                            map->SetPrototype((TsMap*)protoPtr);
                        }
                        return value;
                    }
                }
                // Setting __proto__ to null/undefined clears prototype
                if (value.type == ValueType::UNDEFINED || protoPtr == nullptr) {
                    map->SetPrototype(nullptr);
                    return value;
                }
                return value;
            }

            // No setter - set property directly
            ts_map_set_v(rawObj, key, value);
            return value;
        }

        // Check vtable setter dispatch for native C++ objects (e.g., TsServerResponse.statusCode)
        {
            uint64_t vt = *(uint64_t*)rawObj;
            const char* keyCStr = keyStr ? keyStr->ToUtf8() : nullptr;
            if (keyCStr) {
                for (int i = 0; i < g_vtable_dispatch_count; i++) {
                    if (vt == g_vtable_dispatch[i].vtable && g_vtable_dispatch[i].setDispatch) {
                        if (g_vtable_dispatch[i].setDispatch(rawObj, keyCStr, value)) {
                            return value;  // Setter handled it
                        }
                        break;
                    }
                }
            }
        }

        // Fallback: store on side-map for native TsObject subclasses
        // This enables dynamic property writes on objects like TsServerResponse
        {
            TsMap* props = getOrCreateNativeProps(rawObj);
            props->Set(key, value);
        }
        return value;
    }

    bool ts_object_has_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        // Check for flat object (no C++ vtable — dynamic_cast would crash)
        uint32_t magic0 = *(uint32_t*)rawObj;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            TsString* keyStr = (TsString*)ts_value_get_string(key);
            if (!keyStr) return false;
            const char* k = keyStr->ToUtf8();
            if (!k) return false;
            return ts_flat_object_has_property(rawObj, k);
        }

        // Non-TsObject types at offset 0 — return early without dynamic_cast
        if (magic0 == 0x53545247 || magic0 == 0x434F4E53) return false; // TsString, TsConsString
        if (magic0 == 0x41525259) { // TsArray
            TsString* keyStr2 = (TsString*)ts_value_get_string(key);
            if (!keyStr2) return false;
            const char* k = keyStr2->ToUtf8();
            if (!k) return false;
            if (strcmp(k, "length") == 0) return true;
            char* end = nullptr;
            long idx = strtol(k, &end, 10);
            if (end != k && *end == '\0' && idx >= 0 && idx < ts_array_length(rawObj)) return true;
            return false;
        }
        if (magic0 == 0x4D415053 || magic0 == 0x53455453) return false; // TsMap/TsSet at offset 0
        if (magic0 == 0x46554E43) return false; // Native function at offset 0

        // TsMap magic at offset 16/20/24 — TsProxy extends TsMap so dynamic_cast is safe here
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawObj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawObj + 24);
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) {
            // Safe to dynamic_cast: TsMap is a TsObject derivative
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                return proxy->has(key);
            }

            TsString* keyStr = (TsString*)ts_value_get_string(key);
            if (!keyStr) return false;

            TsMap* map = (TsMap*)rawObj;
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = keyStr;

            TsMap* currentMap = map;
            while (currentMap != nullptr) {
                if (currentMap->Has(keyVal)) {
                    return true;
                }
                currentMap = currentMap->GetPrototype();
            }
            return false;
        }

        // TsFunction — check its properties map (e.g., String.prototype)
        if (magic16 == 0x46554E43) { // TsFunction::MAGIC
            TsFunction* func = (TsFunction*)rawObj;
            if (func->properties) {
                TsString* keyStr = (TsString*)ts_value_get_string(key);
                if (keyStr) {
                    TsValue keyVal;
                    keyVal.type = ValueType::STRING_PTR;
                    keyVal.ptr_val = keyStr;
                    if (func->properties->Has(keyVal)) return true;
                }
            }
            // Also check built-in function properties
            TsString* keyStr = (TsString*)ts_value_get_string(key);
            if (keyStr) {
                const char* k = keyStr->ToUtf8();
                if (k && (strcmp(k, "name") == 0 || strcmp(k, "length") == 0 ||
                          strcmp(k, "bind") == 0 || strcmp(k, "call") == 0 ||
                          strcmp(k, "apply") == 0)) return true;
            }
            return false;
        }
        // Non-TsObject types at offset 16 — return early
        if (magic16 == 0x434C5352) return false; // TsClosure
        if (magic16 == 0x42494749 || magic16 == 0x53594D42) return false; // BigInt, Symbol

        // Check side-map for dynamically assigned properties on native objects
        {
            TsMap* props = getNativeProps(rawObj);
            if (props) {
                TsString* keyStr = (TsString*)ts_value_get_string(key);
                if (keyStr) {
                    TsValue keyVal;
                    keyVal.type = ValueType::STRING_PTR;
                    keyVal.ptr_val = keyStr;
                    if (props->Has(keyVal)) return true;
                }
            }
        }

        // Catch-all: unknown type (sentinels, etc.) — safe fallthrough, no dynamic_cast
        return false;
    }

    bool ts_object_delete_prop(TsValue* obj, TsValue* key) {
        if (!obj || !key) return false;
        void* rawObj = ts_value_get_object(obj);
        if (!rawObj) return false;

        // Check for flat object (no C++ vtable — dynamic_cast would crash)
        uint32_t magic0 = *(uint32_t*)rawObj;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            return false;
        }

        // TsMap magic at offset 16/20/24 — TsProxy extends TsMap so dynamic_cast is safe here
        uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
        uint32_t magic20 = *(uint32_t*)((char*)rawObj + 20);
        uint32_t magic24 = *(uint32_t*)((char*)rawObj + 24);
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) {
            // Safe to dynamic_cast: TsMap is a TsObject derivative
            TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
            if (proxy) {
                return proxy->deleteProperty(key);
            }

            TsString* keyStr = (TsString*)ts_value_get_string(key);
            if (!keyStr) return false;

            TsMap* map = (TsMap*)rawObj;
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = keyStr;
            return map->Delete(keyVal);
        }

        // Catch-all: non-TsObject types (strings, arrays, sentinels, etc.) — no dynamic_cast
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

        // Check for flat object first
        uint32_t magic0 = *(uint32_t*)rawMap;
        if (magic0 == 0x464C4154) { // FLAT_MAGIC
            // Decode key string
            TsString* keyStr = (TsString*)ts_value_get_string((TsValue*)keyArg);
            if (!keyStr) return 0;
            const char* keyCStr = keyStr->ToUtf8();
            if (!keyCStr) return 0;

            // Find the slot and set to undefined
            uint32_t shapeId = flat_object_shape_id(rawMap);
            ShapeDescriptor* desc = ts_shape_lookup(shapeId);
            if (desc) {
                for (uint32_t i = 0; i < desc->numSlots; i++) {
                    if (strcmp(desc->propNames[i], keyCStr) == 0) {
                        uint64_t* slotPtr = (uint64_t*)((char*)rawMap + 16 + i * 8);
                        *slotPtr = NANBOX_UNDEFINED;
                        return 1;
                    }
                }
            }
            // Also check overflow map
            if (desc) {
                void* overflow = *(void**)((char*)rawMap + 16 + desc->numSlots * 8);
                if (overflow) {
                    TsMap* overflowMap = (TsMap*)overflow;
                    TsValue kv;
                    kv.type = ValueType::STRING_PTR;
                    kv.ptr_val = keyStr;
                    return overflowMap->Delete(kv) ? 1 : 0;
                }
            }
            return 1; // delete on non-existent property returns true
        }

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
    // parseFloat(value) - global JS function
    // Called from untyped JS as: call ptr @parseFloat(ptr)
    extern "C" TsValue* parseFloat(TsValue* arg) {
        if (!arg) return ts_value_make_double(NAN);

        uint64_t nb = nanbox_from_tsvalue_ptr(arg);

        // If already a number, return as double
        if (nanbox_is_int32(nb)) {
            return ts_value_make_double((double)nanbox_to_int32(nb));
        }
        if (nanbox_is_double(nb)) {
            return (TsValue*)arg;  // Already a double
        }

        // Convert to string first
        TsString* str = nullptr;
        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (ptr) {
                if (ts_is_any_string(ptr)) {
                    str = ts_ensure_flat(ptr);
                }
            }
        }
        if (!str) return ts_value_make_double(NAN);

        const char* cstr = str->ToUtf8();
        if (!cstr || *cstr == '\0') return ts_value_make_double(NAN);

        // Skip leading whitespace
        while (*cstr == ' ' || *cstr == '\t' || *cstr == '\n' || *cstr == '\r' ||
               *cstr == '\f' || *cstr == '\v') cstr++;

        char* endptr;
        double result = strtod(cstr, &endptr);
        if (endptr == cstr) return ts_value_make_double(NAN);  // No valid conversion
        return ts_value_make_double(result);
    }

    // parseInt(value, radix?) - global JS function
    // Called from untyped JS as: call ptr @parseInt(ptr) or call ptr @parseInt(ptr, ptr)
    extern "C" TsValue* parseInt(TsValue* arg, ...) {
        if (!arg) return ts_value_make_double(NAN);

        uint64_t nb = nanbox_from_tsvalue_ptr(arg);

        // If already a number, truncate to integer
        if (nanbox_is_int32(nb)) {
            return ts_value_make_int(nanbox_to_int32(nb));
        }
        if (nanbox_is_double(nb)) {
            double d = nanbox_to_double(nb);
            if (std::isnan(d) || std::isinf(d)) return ts_value_make_double(NAN);
            return ts_value_make_int((int64_t)d);
        }

        // Convert to string
        TsString* str = nullptr;
        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (ptr) {
                if (ts_is_any_string(ptr)) {
                    str = ts_ensure_flat(ptr);
                }
            }
        }
        if (!str) return ts_value_make_double(NAN);

        const char* cstr = str->ToUtf8();
        if (!cstr || *cstr == '\0') return ts_value_make_double(NAN);

        // Skip leading whitespace
        while (*cstr == ' ' || *cstr == '\t' || *cstr == '\n' || *cstr == '\r' ||
               *cstr == '\f' || *cstr == '\v') cstr++;

        // Default radix is 10, but handle 0x prefix for hex
        int radix = 10;
        if (cstr[0] == '0' && (cstr[1] == 'x' || cstr[1] == 'X')) {
            radix = 16;
            cstr += 2;
        } else if (cstr[0] == '0' && (cstr[1] == 'o' || cstr[1] == 'O')) {
            radix = 8;
            cstr += 2;
        } else if (cstr[0] == '0' && (cstr[1] == 'b' || cstr[1] == 'B')) {
            radix = 2;
            cstr += 2;
        }

        char* endptr;
        long long result = strtoll(cstr, &endptr, radix);
        if (endptr == cstr) return ts_value_make_double(NAN);
        return ts_value_make_int((int64_t)result);
    }

    // Typed versions for BuiltinRegistry
    extern "C" double ts_number_parseFloat(TsValue* arg) {
        TsValue* result = parseFloat(arg);
        return ts_value_get_double(result);
    }

    extern "C" int64_t ts_number_parseInt(TsValue* arg) {
        TsValue* result = parseInt(arg);
        uint64_t nb = nanbox_from_tsvalue_ptr(result);
        if (nanbox_is_int32(nb)) return nanbox_to_int32(nb);
        if (nanbox_is_double(nb)) {
            double d = nanbox_to_double(nb);
            if (std::isnan(d)) return 0;
            return (int64_t)d;
        }
        return 0;
    }

    // isNaN(value) - global JS function for untyped code
    extern "C" TsValue* isNaN(TsValue* arg) {
        if (!arg) return ts_value_make_bool(true);
        uint64_t nb = nanbox_from_tsvalue_ptr(arg);
        if (nanbox_is_int32(nb)) return ts_value_make_bool(false);
        if (nanbox_is_double(nb)) return ts_value_make_bool(std::isnan(nanbox_to_double(nb)));
        return ts_value_make_bool(true);  // Non-numeric → NaN
    }

    extern "C" double ts_number_isNaN(TsValue* arg) {
        TsValue* result = isNaN(arg);
        return ts_value_get_bool(result) ? 1.0 : 0.0;
    }

    // isFinite(value) - global JS function for untyped code
    extern "C" TsValue* isFinite(TsValue* arg) {
        if (!arg) return ts_value_make_bool(false);
        uint64_t nb = nanbox_from_tsvalue_ptr(arg);
        if (nanbox_is_int32(nb)) return ts_value_make_bool(true);
        if (nanbox_is_double(nb)) return ts_value_make_bool(std::isfinite(nanbox_to_double(nb)));
        return ts_value_make_bool(false);
    }

    extern "C" double ts_number_isFinite(TsValue* arg) {
        TsValue* result = isFinite(arg);
        return ts_value_get_bool(result) ? 1.0 : 0.0;
    }
    
    // Prototype method implementations
    
    // Function.prototype.toString - returns "function name() { [native code] }" for compiled functions
    static TsValue* ts_function_toString_native(void* ctx, int argc, TsValue** argv) {
        if (ctx) {
            // ctx may be TsFunction* or TsClosure* - check magic to determine type
            TsObject* obj = (TsObject*)ctx;
            if (obj->magic == TsFunction::MAGIC) {
                TsFunction* func = (TsFunction*)ctx;
                if (func->name) {
                    std::string result = "function " + std::string(func->name->ToUtf8()) + "() { [native code] }";
                    return ts_value_make_string(TsString::Create(result.c_str()));
                }
            } else if (obj->magic == 0x434C5352) { // TsClosure CLSR
                TsClosure* closure = (TsClosure*)ctx;
                if (closure->name) {
                    std::string result = "function " + std::string(closure->name->ToUtf8()) + "() { [native code] }";
                    return ts_value_make_string(TsString::Create(result.c_str()));
                }
            }
        }
        return ts_value_make_string(TsString::Create("function() { [native code] }"));
    }
    
    // Object.prototype.hasOwnProperty(key)
    static TsValue* ts_object_hasOwnProperty_native(void* ctx, int argc, TsValue** argv) {
        if (argc < 1 || !argv[0]) {
            return ts_value_make_bool(false);
        }

        // 'this' is passed as context for method calls.
        // When called via .call(thisArg), check ts_get_call_this() as fallback.
        if (!ctx) ctx = ts_get_call_this();
        if (!ctx) {
            return ts_value_make_bool(false);
        }

        // Try to get the object from context (could be boxed TsValue or raw pointer)
        void* obj = ts_nanbox_safe_unbox(ctx);

        // Handle flat objects
        if (obj && is_flat_object(obj)) {
            TsValue* keyVal = argv[0];
            TsValue keyTV = nanbox_to_tagged(keyVal);
            if (keyTV.type == ValueType::STRING_PTR && keyTV.ptr_val) {
                TsString* keyStr = (TsString*)keyTV.ptr_val;
                const char* k = keyStr->ToUtf8();
                if (k) return ts_value_make_bool(ts_flat_object_has_property(obj, k));
            }
            return ts_value_make_bool(false);
        }

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
    
    // Object.prototype.toString() - returns "[object Type]" based on this value
    static TsValue* ts_object_toString_native(void* ctx, int argc, TsValue** argv) {
        // When called via .call(thisArg), the compiler sets ts_call_this_value
        // but passes ctx=nullptr (the function's original context).
        // Check ts_get_call_this() as fallback.
        if (!ctx) ctx = ts_get_call_this();
        if (!ctx) return ts_value_make_string(TsString::Create("[object Undefined]"));

        uint64_t nb = (uint64_t)(uintptr_t)ctx;

        // Check NaN-boxed special values
        if (nb == NANBOX_UNDEFINED) return ts_value_make_string(TsString::Create("[object Undefined]"));
        if (nb == NANBOX_NULL) return ts_value_make_string(TsString::Create("[object Null]"));
        if (nb == NANBOX_TRUE || nb == NANBOX_FALSE) return ts_value_make_string(TsString::Create("[object Boolean]"));
        if (nanbox_is_int32(nb)) return ts_value_make_string(TsString::Create("[object Number]"));
        if (nanbox_is_double(nb)) return ts_value_make_string(TsString::Create("[object Number]"));

        // Pointer types
        if (nanbox_is_ptr(nb) && nb > NANBOX_UNDEFINED) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) return ts_value_make_string(TsString::Create("[object Null]"));

            uint32_t magic0 = *(uint32_t*)ptr;
            // TsString (magic at offset 0)
            if (magic0 == 0x53545247 || magic0 == TsConsString::MAGIC) return ts_value_make_string(TsString::Create("[object String]"));
            // TsArray (magic at offset 0)
            if (magic0 == 0x41525259) return ts_value_make_string(TsString::Create("[object Array]"));
            // TsRegExp (magic at offset 0)
            if (magic0 == 0x52454758) return ts_value_make_string(TsString::Create("[object RegExp]"));
            // Flat object (FLAT_MAGIC at offset 0)
            if (magic0 == 0x464C4154) return ts_value_make_string(TsString::Create("[object Object]"));

            // Check magic at offset 16 for closures/functions
            uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
            if (magic16 == 0x434C5352) return ts_value_make_string(TsString::Create("[object Function]"));

            // TsFunction check
            TsFunction* func = dynamic_cast<TsFunction*>((TsObject*)ptr);
            if (func) return ts_value_make_string(TsString::Create("[object Function]"));

            // TsMap (general object)
            TsMap* map = dynamic_cast<TsMap*>((TsObject*)ptr);
            if (map) return ts_value_make_string(TsString::Create("[object Object]"));
        }

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

    // Object.prototype.isPrototypeOf(obj) - checks if this is in obj's prototype chain
    static TsValue* ts_object_isPrototypeOf_native(void* ctx, int argc, TsValue** argv) {
        // Basic implementation: check if ctx is in the prototype chain of argv[0]
        if (!ctx || argc == 0 || !argv[0]) return ts_value_make_bool(false);

        uint64_t targetNb = nanbox_from_tsvalue_ptr(argv[0]);
        if (!nanbox_is_ptr(targetNb)) return ts_value_make_bool(false);

        void* target = nanbox_to_ptr(targetNb);
        TsMap* targetMap = dynamic_cast<TsMap*>((TsObject*)target);
        if (!targetMap) return ts_value_make_bool(false);

        // Walk prototype chain of target looking for ctx
        void* ctxObj = ts_nanbox_safe_unbox(ctx);
        TsMap* current = targetMap->GetPrototype();
        while (current) {
            if ((void*)current == ctxObj) return ts_value_make_bool(true);
            current = current->GetPrototype();
        }
        return ts_value_make_bool(false);
    }

    // Object.prototype.propertyIsEnumerable(propName) - checks if property is enumerable
    static TsValue* ts_object_propertyIsEnumerable_native(void* ctx, int argc, TsValue** argv) {
        // For TsMap objects, all own properties are enumerable
        if (!ctx || argc == 0) return ts_value_make_bool(false);
        void* obj = ts_nanbox_safe_unbox(ctx);
        if (!obj) return ts_value_make_bool(false);

        TsMap* map = dynamic_cast<TsMap*>((TsObject*)obj);
        if (!map) return ts_value_make_bool(false);

        TsValue* keyVal = argv[0];
        if (!keyVal) return ts_value_make_bool(false);
        TsValue keyTV = nanbox_to_tagged(keyVal);
        return ts_value_make_bool(map->Has(keyTV));
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
                ts_gc_write_barrier(&objectFunc->properties, objectFunc->properties);
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
                ts_gc_write_barrier(&arrayFunc->properties, arrayFunc->properties);
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

        // Global functions - parseInt/parseFloat are now actual C functions,
        // create native function wrappers for the global object
        TsValue* parseIntWrapper = ts_value_make_native_function((void*)ts_parseInt_native, nullptr);
        TsValue* parseFloatWrapper = ts_value_make_native_function((void*)ts_parseFloat_native, nullptr);

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
        globalMap->Set(makeKey("parseInt"), nanbox_to_tagged(parseIntWrapper));
        globalMap->Set(makeKey("parseFloat"), nanbox_to_tagged(parseFloatWrapper));
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
                ts_gc_write_barrier(&func->properties, func->properties);
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
        objectProtoMap->Set(makeKey("isPrototypeOf"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_isPrototypeOf_native, nullptr)));
        objectProtoMap->Set(makeKey("propertyIsEnumerable"), nanbox_to_tagged(ts_value_make_native_function((void*)ts_object_propertyIsEnumerable_native, nullptr)));
        objectProtoMap->Set(makeKey("constructor"), nanbox_to_tagged(Object));
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

    TsValue* ts_module_get_cached(TsValue* path) {
        TsString* s = (TsString*)ts_value_get_string(path);
        if (!s) return ts_value_make_undefined();
        std::string pathStr = s->ToUtf8();
        TsValue* moduleObj = ts_module_get(pathStr.c_str());
        if (!moduleObj) return ts_value_make_undefined();

        // Extract module.exports using ts_object_get_dynamic
        // which handles both flat objects and TsMaps
        TsString* exportsKey = TsString::Create("exports");
        TsValue* boxedKey = ts_value_make_string(exportsKey);
        TsValue* result = ts_object_get_dynamic(moduleObj, boxedKey);
        return result;
    }

    // Debug utility: dump all registered modules to stderr
    // Callable from CDB: `!call test_express_diag!ts_modules_dump_all`
    void ts_modules_dump_all() {
        fprintf(stderr, "[ts-aot] Module cache (%zu entries):\n", g_module_cache.size());
        for (auto& [key, val] : g_module_cache) {
            uint32_t magic0 = 0, magic16 = 0;
            void* raw = val ? ts_value_get_object((TsValue*)val) : nullptr;
            if (raw) {
                magic0 = *(uint32_t*)raw;
                magic16 = *(uint32_t*)((char*)raw + 16);
            }
            fprintf(stderr, "  [%p m0=%08X m16=%08X] %s\n",
                    (void*)val, magic0, magic16, key.c_str());
        }
        fflush(stderr);
    }

    TsValue* ts_module_get_default(TsValue* path) {
        TsValue* exports = ts_module_get_cached(path);
        if (ts_value_is_undefined(exports)) return exports;

        // Check __esModule flag (Babel/TypeScript CJS interop convention)
        // If exports.__esModule is truthy, return exports.default instead of exports
        TsString* esKey = TsString::Create("__esModule");
        TsValue* flag = ts_object_get_dynamic(exports, ts_value_make_string(esKey));
        if (!ts_value_is_undefined(flag) && !ts_value_is_null(flag)) {
            bool boolVal = ts_value_get_bool(flag);
            if (boolVal) {
                TsString* defKey = TsString::Create("default");
                return ts_object_get_dynamic(exports, ts_value_make_string(defKey));
            }
        }
        return exports;
    }

    // ============================================================
    // Built-in module require support for untyped JS modules.
    // When untyped JS does require('fs'), require('path'), etc.,
    // we create TsMap namespace objects with wrapped extension functions.
    // ============================================================

    // Generic thunk: forwards args to the real extension function.
    // The real function pointer is stored in closure->cells (repurposed, num_captures=0).
    static TsValue* builtin_fn_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void* (*ExtFn)(void*, void*, void*);
        void* result = ((ExtFn)(void*)cl->cells)((void*)a1, (void*)a2, (void*)a3);
        if (!result) return ts_value_make_undefined();
        return (TsValue*)result;
    }

    // Thunk for functions returning bool (e.g., fs.existsSync)
    static TsValue* builtin_bool_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef bool (*ExtFn)(void*);
        bool result = ((ExtFn)(void*)cl->cells)((void*)a1);
        return ts_value_make_bool(result);
    }

    // Thunk for functions returning void (e.g., fs.unlinkSync)
    static TsValue* builtin_void_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void (*ExtFn)(void*, void*, void*);
        ((ExtFn)(void*)cl->cells)((void*)a1, (void*)a2, (void*)a3);
        return ts_value_make_undefined();
    }

    // Thunk for variadic functions (path.resolve, path.join).
    // Collects args into a TsArray, then calls the extension function.
    static TsValue* builtin_variadic_thunk(void* ctx, TsValue* a1, TsValue* a2, TsValue* a3) {
        TsClosure* cl = (TsClosure*)ctx;
        typedef void* (*ExtFn)(void*);
        TsArray* arr = TsArray::Create();
        if (a1 && !ts_value_is_undefined(a1)) arr->Push((int64_t)(uintptr_t)a1);
        if (a2 && !ts_value_is_undefined(a2)) arr->Push((int64_t)(uintptr_t)a2);
        if (a3 && !ts_value_is_undefined(a3)) arr->Push((int64_t)(uintptr_t)a3);
        void* result = ((ExtFn)(void*)cl->cells)((void*)arr);
        if (!result) return ts_value_make_undefined();
        return (TsValue*)result;
    }

    // Add a wrapped extension function to a module TsMap.
    static void add_builtin_fn(TsMap* mod, const char* name, void* real_fn, void* thunk_fn) {
        TsClosure* cl = TsClosure::Create(thunk_fn, 0);
        cl->cells = (TsCell**)real_fn;  // Repurpose cells to store real fn ptr
        cl->name = TsString::Create(name);
        TsString* key = TsString::Create(name);
        mod->Set(nanbox_to_tagged(ts_value_make_string(key)),
                 nanbox_to_tagged(ts_value_make_object(cl)));
    }

    // Add a string property to a module TsMap.
    static void add_builtin_str_prop(TsMap* mod, const char* name, const char* value) {
        TsString* key = TsString::Create(name);
        TsString* val = TsString::Create(value);
        mod->Set(nanbox_to_tagged(ts_value_make_string(key)),
                 nanbox_to_tagged(ts_value_make_string(val)));
    }

    // ---- Builtin function registry ----
    // Extensions register their functions here at static init time.
    // create_builtin_module looks them up instead of hard-referencing symbols,
    // so extensions that aren't linked don't create unresolved symbol errors.
    struct BuiltinFnEntry {
        void* fn_ptr;
        void* thunk_ptr;
    };

    // Key: "module\0function" (e.g., "fs\0readFileSync")
    static std::unordered_map<std::string, BuiltinFnEntry>& getBuiltinRegistry() {
        static std::unordered_map<std::string, BuiltinFnEntry> registry;
        return registry;
    }

    // Key: "module\0property" -> string value (e.g., "path\0sep" -> "\\")
    static std::unordered_map<std::string, std::string>& getBuiltinStrProps() {
        static std::unordered_map<std::string, std::string> props;
        return props;
    }

    // Key: "event_emitter.on" etc. for special runtime lookups
    static std::unordered_map<std::string, void*>& getBuiltinSpecialFns() {
        static std::unordered_map<std::string, void*> fns;
        return fns;
    }

    static std::string makeRegistryKey(const char* module, const char* name) {
        std::string key(module);
        key += '\0';
        key += name;
        return key;
    }

    // Post-init callbacks for builtin modules (e.g., events adds EventEmitter)
    static std::unordered_map<std::string, void(*)(void*)>& getBuiltinPostInits() {
        static std::unordered_map<std::string, void(*)(void*)> callbacks;
        return callbacks;
    }

    static TsValue* create_builtin_module(const std::string& name) {
        // Check g_module_cache for a previously created built-in module
        std::string cacheKey = "__builtin:" + name;
        TsValue* cached = ts_module_get(cacheKey.c_str());
        if (cached) return cached;

        TsMap* mod = TsMap::Create();

        // Look up all registered functions for this module
        auto& registry = getBuiltinRegistry();
        std::string prefix = name + '\0';
        for (auto& [key, entry] : registry) {
            if (key.size() > prefix.size() &&
                key.compare(0, prefix.size(), prefix) == 0) {
                const char* fnName = key.c_str() + prefix.size();
                add_builtin_fn(mod, fnName, entry.fn_ptr, entry.thunk_ptr);
            }
        }

        // Look up all registered string properties for this module
        auto& strProps = getBuiltinStrProps();
        for (auto& [key, value] : strProps) {
            if (key.size() > prefix.size() &&
                key.compare(0, prefix.size(), prefix) == 0) {
                const char* propName = key.c_str() + prefix.size();
                add_builtin_str_prop(mod, propName, value.c_str());
            }
        }

        // Module not registered at all: return empty TsMap (won't crash)

        // Set "default" and "exports" to point back to the module itself.
        // ESM `import crypto from 'crypto'` does a "default" lookup on the module.
        TsString* defaultKey = TsString::Create("default");
        mod->Set(nanbox_to_tagged(ts_value_make_string(defaultKey)),
                 nanbox_to_tagged(ts_value_make_object(mod)));
        TsString* exportsKey = TsString::Create("exports");
        mod->Set(nanbox_to_tagged(ts_value_make_string(exportsKey)),
                 nanbox_to_tagged(ts_value_make_object(mod)));

        // Call post-init callback if registered (e.g., events adds EventEmitter)
        auto& postInits = getBuiltinPostInits();
        auto postInitIt = postInits.find(name);
        if (postInitIt != postInits.end()) {
            postInitIt->second((void*)mod);
        }

        TsValue* result = (TsValue*)mod;
        // Cache in g_module_cache so the GC scanner keeps it alive
        TsValue* pathVal = ts_value_make_string(TsString::Create(cacheKey.c_str()));
        ts_module_register(pathVal, result);
        return result;
    }

// Expose create_builtin_module for TsGlobals.cpp module accessors
TsValue* ts_get_builtin_module(const char* name) {
    return create_builtin_module(std::string(name));
}

// ---- Public registration API for extensions ----
void ts_builtin_register(const char* module, const char* name, void* fn_ptr, int thunk_type) {
    static void* thunks[] = {
        (void*)builtin_fn_thunk,
        (void*)builtin_bool_thunk,
        (void*)builtin_void_thunk,
        (void*)builtin_variadic_thunk,
    };
    void* thunk = (thunk_type >= 0 && thunk_type <= 3) ? thunks[thunk_type] : thunks[0];
    auto key = makeRegistryKey(module, name);
    getBuiltinRegistry()[key] = {fn_ptr, thunk};
}

void ts_builtin_register_str_prop(const char* module, const char* name, const char* value) {
    auto key = makeRegistryKey(module, name);
    getBuiltinStrProps()[key] = value;
}

void ts_builtin_register_post_init(const char* module, void (*callback)(void*)) {
    getBuiltinPostInits()[module] = callback;
}

void ts_builtin_register_special(const char* name, void* fn_ptr) {
    getBuiltinSpecialFns()[name] = fn_ptr;
}

void* ts_builtin_lookup_special(const char* name) {
    auto& fns = getBuiltinSpecialFns();
    auto it = fns.find(name);
    return (it != fns.end()) ? it->second : nullptr;
}

    static bool is_builtin_module_name(const std::string& spec) {
        static const char* builtins[] = {
            "assert", "async_hooks", "buffer", "child_process", "cluster",
            "console", "crypto", "dgram", "dns", "events", "fs", "http",
            "http2", "https", "inspector", "module", "net", "os", "path",
            "perf_hooks", "process", "querystring", "readline", "stream",
            "string_decoder", "timers", "timers/promises", "tls", "tty",
            "url", "util", "zlib"
        };
        std::string s = spec;
        if (s.rfind("node:", 0) == 0) s = s.substr(5);
        for (const char* b : builtins) {
            if (s == b) return true;
        }
        return false;
    }

    TsValue* ts_require(TsValue* specifier, const char* referrerPath) {
        TsString* s = (TsString*)ts_value_get_string(specifier);
        if (!s) {
            return ts_value_make_undefined();
        }
        std::string spec = s->ToUtf8();

        // Strip "node:" prefix for built-in module lookup
        std::string lookupSpec = spec;
        if (lookupSpec.rfind("node:", 0) == 0) lookupSpec = lookupSpec.substr(5);

        // Check for built-in modules first (prevents crash on null referrerPath)
        if (is_builtin_module_name(lookupSpec)) {
            return create_builtin_module(lookupSpec);
        }

        // Guard against null referrerPath for non-builtin modules
        if (!referrerPath) {
            return ts_value_make_undefined();
        }

        try {
            fs::path resolved;
            std::string absPath;

            // Check for relative or absolute paths
            bool isRelative = spec.rfind("./", 0) == 0 || spec.rfind("../", 0) == 0 || spec.rfind("/", 0) == 0;
            // Windows absolute paths like "E:\..." or "C:/..."
            bool isAbsoluteWin = spec.size() >= 2 && std::isalpha((unsigned char)spec[0]) && (spec[1] == ':');
            if (isRelative) {
                resolved = fs::path(referrerPath).parent_path() / spec;
                absPath = finalize_module_path(resolved);
            } else if (isAbsoluteWin || (!spec.empty() && spec[0] == '/')) {
                // Already an absolute path
                absPath = finalize_module_path(spec);
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
                ts_gc_write_barrier(&func->properties, func->properties);
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

    // Object() called as function (not constructor) - JS semantics
    // Object() → empty object, Object(val) → val if object, else wrap
    void* ts_object_constructor(void* arg) {
        if (!arg) return TsMap::Create();

        uint64_t nb = (uint64_t)(uintptr_t)arg;

        // null/undefined → empty object
        if (nanbox_is_null(nb) || nanbox_is_undefined(nb)) {
            return TsMap::Create();
        }

        // If already a pointer (object/array/etc), return as-is
        if (nanbox_is_ptr(nb)) {
            return arg;
        }

        // Primitives: wrap in empty object (simplified - real JS wraps in Number/String/Boolean objects)
        return TsMap::Create();
    }

    // Object() with no args - create empty object
    void* ts_object_create_empty() {
        return TsMap::Create();
    }
}
