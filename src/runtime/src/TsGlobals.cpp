// TsGlobals.cpp - Global object getters for HIR pipeline
//
// These functions return proper runtime objects so that untyped JavaScript
// modules can access built-in globals (Object.keys, String.prototype, etc.)
// via dynamic property lookup.

#include "GC.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsString.h"
#include <unordered_map>
#include <string>

extern "C" {

// Forward declarations for native wrapper functions
TsValue* ts_object_keys_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_values_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_entries_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyNames_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyDescriptor_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getOwnPropertyDescriptors_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_getPrototypeOf_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_create_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_defineProperty_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_defineProperties_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_freeze_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_seal_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_preventExtensions_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isFrozen_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isSealed_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_isExtensible_native(void* context, int argc, TsValue** argv);
TsValue* ts_object_assign(TsValue* target, TsValue* source);
TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
bool ts_object_is(TsValue* val1, TsValue* val2);
TsValue* ts_array_isArray_native(void* context, int argc, TsValue** argv);
// Array instance method natives (defined in TsObject.cpp)
TsValue* ts_array_slice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_map_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_filter_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_forEach_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reduce_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_push_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_pop_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_join_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_indexOf_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_includes_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_some_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_every_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_find_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_findIndex_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_sort_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_reverse_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_splice_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_concat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_flat_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_shift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_array_unshift_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_json_stringify_native(void* context, int argc, TsValue** argv);
TsValue* ts_json_parse_native(void* context, int argc, TsValue** argv);
TsValue* ts_error_create(void* message);
TsValue* ts_get_builtin_module(const char* name);
TsValue* ts_value_make_native_function(void* funcPtr, void* context);
TsValue* ts_value_make_object(void* ptr);
TsValue* ts_value_make_bool(bool val);
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_string(void* str);
bool ts_value_is_undefined(TsValue* val);

// Helper: add a native function to a TsMap
static void addMethod(TsMap* map, const char* name, void* nativeFn) {
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::GetInterned(name);
    TsValue val;
    val.type = ValueType::FUNCTION_PTR;
    TsValue* fn = ts_value_make_native_function(nativeFn, nullptr);
    val.ptr_val = fn;
    map->Set(key, val);
}

// Native wrappers for functions that take 2+ args and don't have _native variants
static TsValue* ts_object_assign_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return argc > 0 ? argv[0] : ts_value_make_undefined();
    return ts_object_assign(argv[0], argv[1]);
}

static TsValue* ts_object_setPrototypeOf_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_undefined();
    return ts_object_setPrototypeOf(argv[0], argv[1]);
}

static TsValue* ts_object_is_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_bool(false);
    return ts_value_make_bool(ts_object_is(argv[0], argv[1]));
}

static TsValue* ts_object_hasOwn_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_bool(false);
    // hasOwn checks if an object has a property
    extern TsValue* ts_object_getOwnPropertyDescriptor(TsValue*, TsValue*);
    TsValue* desc = ts_object_getOwnPropertyDescriptor(argv[0], argv[1]);
    return ts_value_make_bool(desc != nullptr && !ts_value_is_undefined(desc));
}

// ========================================
// Object global
// ========================================
void* ts_get_global_Object() {
    static TsMap* cached = nullptr;
    if (cached) return cached;

    cached = TsMap::Create();
    // Static methods
    addMethod(cached, "keys", (void*)ts_object_keys_native);
    addMethod(cached, "values", (void*)ts_object_values_native);
    addMethod(cached, "entries", (void*)ts_object_entries_native);
    addMethod(cached, "assign", (void*)ts_object_assign_native);
    addMethod(cached, "create", (void*)ts_object_create_native);
    addMethod(cached, "defineProperty", (void*)ts_object_defineProperty_native);
    addMethod(cached, "defineProperties", (void*)ts_object_defineProperties_native);
    addMethod(cached, "getOwnPropertyDescriptor", (void*)ts_object_getOwnPropertyDescriptor_native);
    addMethod(cached, "getOwnPropertyDescriptors", (void*)ts_object_getOwnPropertyDescriptors_native);
    addMethod(cached, "getOwnPropertyNames", (void*)ts_object_getOwnPropertyNames_native);
    addMethod(cached, "getPrototypeOf", (void*)ts_object_getPrototypeOf_native);
    addMethod(cached, "setPrototypeOf", (void*)ts_object_setPrototypeOf_native);
    addMethod(cached, "freeze", (void*)ts_object_freeze_native);
    addMethod(cached, "seal", (void*)ts_object_seal_native);
    addMethod(cached, "preventExtensions", (void*)ts_object_preventExtensions_native);
    addMethod(cached, "isFrozen", (void*)ts_object_isFrozen_native);
    addMethod(cached, "isSealed", (void*)ts_object_isSealed_native);
    addMethod(cached, "isExtensible", (void*)ts_object_isExtensible_native);
    addMethod(cached, "is", (void*)ts_object_is_native);
    addMethod(cached, "hasOwn", (void*)ts_object_hasOwn_native);

    // Object.prototype — a TsMap that serves as the base prototype
    TsMap* proto = TsMap::Create();
    // hasOwnProperty is commonly accessed
    addMethod(proto, "hasOwnProperty", (void*)ts_object_hasOwn_native);
    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    cached->Set(protoKey, protoVal);

    return cached;
}

// ========================================
// Array global
// ========================================
void* ts_get_global_Array() {
    static TsMap* cached = nullptr;
    if (cached) return cached;

    cached = TsMap::Create();
    addMethod(cached, "isArray", (void*)ts_array_isArray_native);

    // Array.prototype — populated with instance methods so
    // Array.prototype.slice.call(arr, ...) pattern works (used by Express)
    TsMap* proto = TsMap::Create();
    addMethod(proto, "slice", (void*)ts_array_slice_native);
    addMethod(proto, "map", (void*)ts_array_map_native);
    addMethod(proto, "filter", (void*)ts_array_filter_native);
    addMethod(proto, "forEach", (void*)ts_array_forEach_native);
    addMethod(proto, "reduce", (void*)ts_array_reduce_native);
    addMethod(proto, "push", (void*)ts_array_push_native);
    addMethod(proto, "pop", (void*)ts_array_pop_native);
    addMethod(proto, "join", (void*)ts_array_join_native);
    addMethod(proto, "indexOf", (void*)ts_array_indexOf_native);
    addMethod(proto, "includes", (void*)ts_array_includes_native);
    addMethod(proto, "some", (void*)ts_array_some_native);
    addMethod(proto, "every", (void*)ts_array_every_native);
    addMethod(proto, "find", (void*)ts_array_find_native);
    addMethod(proto, "findIndex", (void*)ts_array_findIndex_native);
    addMethod(proto, "sort", (void*)ts_array_sort_native);
    addMethod(proto, "reverse", (void*)ts_array_reverse_native);
    addMethod(proto, "splice", (void*)ts_array_splice_native);
    addMethod(proto, "concat", (void*)ts_array_concat_native);
    addMethod(proto, "flat", (void*)ts_array_flat_native);
    addMethod(proto, "shift", (void*)ts_array_shift_native);
    addMethod(proto, "unshift", (void*)ts_array_unshift_native);

    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    cached->Set(protoKey, protoVal);

    return cached;
}

// ========================================
// String global
// ========================================
void* ts_get_global_String() {
    static TsMap* cached = nullptr;
    if (!cached) {
        cached = TsMap::Create();
        // String.prototype — empty for now; string instance methods are
        // handled directly by NaN-boxing dispatch in ts_object_get_property
        TsMap* proto = TsMap::Create();
        TsValue protoKey;
        protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal;
        protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        cached->Set(protoKey, protoVal);
    }
    return cached;
}

// ========================================
// Error global
// ========================================
static TsValue* ts_error_create_native(void* ctx, int argc, TsValue** argv) {
    if (argc > 0) return ts_error_create(argv[0]);
    return ts_error_create(nullptr);
}

void* ts_get_global_Error() {
    static TsMap* cached = nullptr;
    if (cached) return cached;

    cached = TsMap::Create();
    // Error.captureStackTrace — no-op stub
    addMethod(cached, "captureStackTrace", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        return ts_value_make_undefined();
    });

    // Error.prototype
    TsMap* proto = TsMap::Create();
    TsValue nameKey;
    nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal;
    nameVal.type = ValueType::STRING_PTR;
    nameVal.ptr_val = TsString::Create("Error");
    proto->Set(nameKey, nameVal);

    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    cached->Set(protoKey, protoVal);

    return cached;
}

// ========================================
// JSON global
// ========================================
void* ts_get_global_JSON() {
    static TsMap* cached = nullptr;
    if (cached) return cached;

    cached = TsMap::Create();
    addMethod(cached, "stringify", (void*)ts_json_stringify_native);
    addMethod(cached, "parse", (void*)ts_json_parse_native);
    return cached;
}

// ========================================
// Other constructor globals — minimal stubs
// These return TsMap objects so typeof returns "object" and
// property access (.prototype) doesn't crash.
// ========================================

static TsMap* makeSimpleConstructorGlobal(const char* name) {
    TsMap* ctor = TsMap::Create();
    TsMap* proto = TsMap::Create();
    // Set .name
    TsValue nameKey;
    nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal;
    nameVal.type = ValueType::STRING_PTR;
    nameVal.ptr_val = TsString::Create(name);
    proto->Set(nameKey, nameVal);

    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctor->Set(protoKey, protoVal);
    return ctor;
}

void* ts_get_global_Number() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Number");
    return cached;
}

void* ts_get_global_Boolean() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Boolean");
    return cached;
}

void* ts_get_global_Function() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Function");
    return cached;
}

void* ts_get_global_Date() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Date");
    return cached;
}

void* ts_get_global_RegExp() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("RegExp");
    return cached;
}

void* ts_get_global_Promise() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Promise");
    return cached;
}

void* ts_get_global_TypeError() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("TypeError");
    return cached;
}

void* ts_get_global_RangeError() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("RangeError");
    return cached;
}

void* ts_get_global_Symbol() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Symbol");
    return cached;
}

void* ts_get_global_Map() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Map");
    return cached;
}

void* ts_get_global_Set() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Set");
    return cached;
}

void* ts_get_global_WeakMap() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("WeakMap");
    return cached;
}

void* ts_get_global_WeakSet() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("WeakSet");
    return cached;
}

void* ts_get_global_Reflect() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Reflect");
    return cached;
}

void* ts_get_global_Proxy() {
    static TsMap* cached = nullptr;
    if (!cached) cached = makeSimpleConstructorGlobal("Proxy");
    return cached;
}

// ========================================
// Console, Math, Buffer, process — keep sentinels for typed path,
// but also support dynamic access
// ========================================

void* ts_get_global_console() {
    // Console methods are handled specially in HIR->LLVM lowering.
    // For untyped JS, return a sentinel — console.log etc. are lowered directly.
    static const char sentinel[] = "console";
    return (void*)sentinel;
}

void* ts_get_global_Math() {
    static const char sentinel[] = "Math";
    return (void*)sentinel;
}

void* ts_get_global_Buffer() {
    static const char sentinel[] = "Buffer";
    return (void*)sentinel;
}

void* ts_get_global_process() {
    static const char sentinel[] = "process";
    return (void*)sentinel;
}

void* ts_get_global_globalThis() {
    static const char sentinel[] = "globalThis";
    return (void*)sentinel;
}

// ========================================
// Node.js module globals — use builtin module system
// ========================================

static void* getModuleGlobal(const char* name) {
    static std::unordered_map<std::string, void*> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    void* mod = ts_get_builtin_module(name);
    cache[name] = mod;
    return mod;
}

void* ts_get_global_path() { return getModuleGlobal("path"); }
void* ts_get_global_fs() { return getModuleGlobal("fs"); }
void* ts_get_global_os() { return getModuleGlobal("os"); }
void* ts_get_global_url() { return getModuleGlobal("url"); }
void* ts_get_global_util() { return getModuleGlobal("util"); }
void* ts_get_global_crypto() { return getModuleGlobal("crypto"); }
void* ts_get_global_http() { return getModuleGlobal("http"); }
void* ts_get_global_https() { return getModuleGlobal("https"); }
void* ts_get_global_net() { return getModuleGlobal("net"); }
void* ts_get_global_dgram() { return getModuleGlobal("dgram"); }
void* ts_get_global_dns() { return getModuleGlobal("dns"); }
void* ts_get_global_tls() { return getModuleGlobal("tls"); }
void* ts_get_global_zlib() { return getModuleGlobal("zlib"); }
void* ts_get_global_stream() { return getModuleGlobal("stream"); }
void* ts_get_global_events() { return getModuleGlobal("events"); }
void* ts_get_global_querystring() { return getModuleGlobal("querystring"); }
void* ts_get_global_assert() { return getModuleGlobal("assert"); }
void* ts_get_global_child_process() { return getModuleGlobal("child_process"); }
void* ts_get_global_cluster() { return getModuleGlobal("cluster"); }
void* ts_get_global_timers() { return getModuleGlobal("timers"); }
void* ts_get_global_readline() { return getModuleGlobal("readline"); }
void* ts_get_global_perf_hooks() { return getModuleGlobal("perf_hooks"); }
void* ts_get_global_async_hooks() { return getModuleGlobal("async_hooks"); }
void* ts_get_global_tty() { return getModuleGlobal("tty"); }
void* ts_get_global_string_decoder() { return getModuleGlobal("string_decoder"); }
void* ts_get_global_buffer() { return getModuleGlobal("buffer"); }
void* ts_get_global_http2() { return getModuleGlobal("http2"); }
void* ts_get_global_inspector() { return getModuleGlobal("inspector"); }
void* ts_get_global_module() { return getModuleGlobal("module"); }
void* ts_get_global_vm() { return getModuleGlobal("vm"); }
void* ts_get_global_v8() { return getModuleGlobal("v8"); }

// Generic global lookup by name
void* ts_get_global(void* namePtr) {
    return nullptr;
}

} // extern "C"
