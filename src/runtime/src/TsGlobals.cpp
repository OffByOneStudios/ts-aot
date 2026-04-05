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
#include <limits>
#include <cmath>

extern "C" {

// Forward declarations for native wrapper functions
void* ts_get_builtin_function(void* nameStr);
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
TsValue* ts_function_call_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_function_apply_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_function_bind_native(void* ctx, int argc, TsValue** argv);
TsValue* ts_get_builtin_module(const char* name);
TsValue* ts_value_make_native_function(void* funcPtr, void* context);
TsValue* ts_value_make_object(void* ptr);
TsValue* ts_value_make_bool(bool val);
TsValue* ts_value_make_undefined();
TsValue* ts_value_make_string(void* str);
TsValue* ts_value_make_double(double d);
bool ts_value_is_undefined(TsValue* val);
void* ts_get_call_this();
void* ts_string_from_value(TsValue* val);
double ts_value_get_double(TsValue* v);
bool ts_value_to_bool(TsValue* v);
void* ts_value_get_object(TsValue* val);
TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
bool ts_object_has_property(void* objArg, void* keyArg);

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
    // hasOwnProperty(key): instance method — 1 arg, 'this' is the object
    addMethod(proto, "hasOwnProperty", (void*)+[](void* ctx, int argc, TsValue** argv) -> TsValue* {
        if (argc < 1 || !argv[0]) return ts_value_make_bool(false);
        void* thisObj = ctx;
        if (!thisObj) thisObj = ts_get_call_this();
        if (!thisObj) return ts_value_make_bool(false);
        // Use ts_object_has_property which handles flat objects, TsMaps, etc.
        return ts_value_make_bool(ts_object_has_property(thisObj, argv[0]));
    });
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
// String.prototype method wrappers — extract string from ctx or ts_get_call_this()
// These use ts_object_get_dynamic to call the actual method on the string instance.
static TsValue* string_proto_method(const char* methodName, void* ctx, int argc, TsValue** argv) {
    TsValue* target = (TsValue*)ctx;
    if (!target) target = (TsValue*)ts_get_call_this();
    if (!target) return ts_value_make_undefined();
    // Get the method from the string and call it
    TsValue* method = ts_object_get_dynamic(target, ts_value_make_string(TsString::Create(methodName)));
    if (!method || ts_value_is_undefined(method)) return ts_value_make_undefined();
    return ts_function_call_with_this(method, target, argc, argv);
}

#define STRING_PROTO_METHOD(name) \
    static TsValue* ts_string_proto_##name(void* ctx, int argc, TsValue** argv) { \
        return string_proto_method(#name, ctx, argc, argv); \
    }

STRING_PROTO_METHOD(indexOf)
STRING_PROTO_METHOD(lastIndexOf)
STRING_PROTO_METHOD(slice)
STRING_PROTO_METHOD(substring)
STRING_PROTO_METHOD(charAt)
STRING_PROTO_METHOD(charCodeAt)
STRING_PROTO_METHOD(includes)
STRING_PROTO_METHOD(startsWith)
STRING_PROTO_METHOD(endsWith)
STRING_PROTO_METHOD(trim)
STRING_PROTO_METHOD(split)
STRING_PROTO_METHOD(replace)
STRING_PROTO_METHOD(toLowerCase)
STRING_PROTO_METHOD(toUpperCase)
STRING_PROTO_METHOD(repeat)
STRING_PROTO_METHOD(padStart)
STRING_PROTO_METHOD(padEnd)
STRING_PROTO_METHOD(match)
STRING_PROTO_METHOD(search)
STRING_PROTO_METHOD(concat)
STRING_PROTO_METHOD(trimStart)
STRING_PROTO_METHOD(trimEnd)

#undef STRING_PROTO_METHOD

void* ts_get_global_String() {
    static void* cached = nullptr;
    if (!cached) {
        // String() as a callable function: converts argument to string
        auto stringFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc >= 1 && argv && argv[0]) {
                void* result = ts_string_from_value(argv[0]);
                return ts_value_make_string(result);
            }
            return ts_value_make_string(TsString::Create(""));
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+stringFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;

        // String.prototype with common methods
        TsMap* proto = TsMap::Create();
        addMethod(proto, "indexOf", (void*)ts_string_proto_indexOf);
        addMethod(proto, "lastIndexOf", (void*)ts_string_proto_lastIndexOf);
        addMethod(proto, "slice", (void*)ts_string_proto_slice);
        addMethod(proto, "substring", (void*)ts_string_proto_substring);
        addMethod(proto, "charAt", (void*)ts_string_proto_charAt);
        addMethod(proto, "charCodeAt", (void*)ts_string_proto_charCodeAt);
        addMethod(proto, "includes", (void*)ts_string_proto_includes);
        addMethod(proto, "startsWith", (void*)ts_string_proto_startsWith);
        addMethod(proto, "endsWith", (void*)ts_string_proto_endsWith);
        addMethod(proto, "trim", (void*)ts_string_proto_trim);
        addMethod(proto, "split", (void*)ts_string_proto_split);
        addMethod(proto, "replace", (void*)ts_string_proto_replace);
        addMethod(proto, "toLowerCase", (void*)ts_string_proto_toLowerCase);
        addMethod(proto, "toUpperCase", (void*)ts_string_proto_toUpperCase);
        addMethod(proto, "repeat", (void*)ts_string_proto_repeat);
        addMethod(proto, "padStart", (void*)ts_string_proto_padStart);
        addMethod(proto, "padEnd", (void*)ts_string_proto_padEnd);
        addMethod(proto, "match", (void*)ts_string_proto_match);
        addMethod(proto, "search", (void*)ts_string_proto_search);
        addMethod(proto, "concat", (void*)ts_string_proto_concat);
        addMethod(proto, "trimStart", (void*)ts_string_proto_trimStart);
        addMethod(proto, "trimEnd", (void*)ts_string_proto_trimEnd);

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->Set(protoKey, protoVal);

        ctorFunc->name = TsString::Create("String");
        cached = (void*)ctorVal;
    }
    return cached;
}

// ========================================
// Error constructors — callable native functions with .prototype
// ========================================

// Helper: create a callable error constructor global
// Returns a TsFunction that, when called via `new`, creates an error TsMap
// with .message, .name, and .stack properties.
static void* makeErrorConstructor(const char* errorName) {
    // Create the constructor as a native function
    auto constructorFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
        // ctx is the error name string (TsString*)
        // 'this' is the new object from ts_new_from_constructor_impl
        void* thisVal = ts_get_call_this();
        void* raw = thisVal ? ts_value_get_object((TsValue*)thisVal) : nullptr;

        // If called via 'new', set .message on 'this'
        if (raw) {
            uint32_t m16 = *(uint32_t*)((char*)raw + 16);
            uint32_t m20 = *(uint32_t*)((char*)raw + 20);
            if (m16 == 0x4D415053 || m20 == 0x4D415053) {
                TsMap* obj = (TsMap*)raw;
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                if (argc >= 1 && argv && argv[0]) {
                    TsValue msgVal = nanbox_to_tagged(argv[0]);
                    obj->Set(msgKey, msgVal);
                } else {
                    TsValue emptyVal; emptyVal.type = ValueType::STRING_PTR;
                    emptyVal.ptr_val = TsString::Create("");
                    obj->Set(msgKey, emptyVal);
                }
                return (TsValue*)thisVal;
            }
        }

        // Fallback: create a standalone error object
        if (argc > 0 && argv) return ts_error_create(argv[0]);
        return ts_error_create(nullptr);
    };

    // Create the TsFunction for the constructor
    TsValue* ctorVal = ts_value_make_native_function((void*)+constructorFn, nullptr);

    // Extract the raw TsFunction to set .prototype and .captureStackTrace
    void* ctorRaw = ts_value_get_object(ctorVal);
    TsFunction* ctorFunc = (TsFunction*)ctorRaw;

    // Create Error.prototype with .name
    TsMap* proto = TsMap::Create();
    TsValue nameKey; nameKey.type = ValueType::STRING_PTR;
    nameKey.ptr_val = TsString::GetInterned("name");
    TsValue nameVal; nameVal.type = ValueType::STRING_PTR;
    nameVal.ptr_val = TsString::Create(errorName);
    proto->Set(nameKey, nameVal);

    // Set .prototype on the constructor function's properties map
    if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
    TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctorFunc->properties->Set(protoKey, protoVal);

    // Set .name on the constructor
    ctorFunc->name = TsString::Create(errorName);

    return (void*)ctorVal;
}

void* ts_get_global_Error() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("Error");
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
    static void* cached = nullptr;
    if (!cached) {
        auto numberFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc >= 1 && argv && argv[0]) {
                double d = ts_value_get_double(argv[0]);
                return ts_value_make_double(d);
            }
            return ts_value_make_double(0.0);
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+numberFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsMap* proto = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->Set(protoKey, protoVal);

        ctorFunc->name = TsString::Create("Number");

        // Static properties on Number constructor
        auto setDouble = [&](const char* name, double val) {
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(name);
            TsValue v = nanbox_to_tagged(ts_value_make_double(val));
            ctorFunc->properties->Set(k, v);
        };
        setDouble("NaN", std::numeric_limits<double>::quiet_NaN());
        setDouble("POSITIVE_INFINITY", std::numeric_limits<double>::infinity());
        setDouble("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
        setDouble("MAX_SAFE_INTEGER", 9007199254740991.0);
        setDouble("MIN_SAFE_INTEGER", -9007199254740991.0);
        setDouble("EPSILON", 2.220446049250313e-16);
        setDouble("MAX_VALUE", 1.7976931348623157e+308);
        setDouble("MIN_VALUE", 5e-324);

        cached = (void*)ctorVal;
    }
    return cached;
}

void* ts_get_global_Boolean() {
    static void* cached = nullptr;
    if (!cached) {
        auto boolFn = [](void* ctx, int argc, TsValue** argv) -> TsValue* {
            if (argc >= 1 && argv && argv[0]) {
                bool b = ts_value_to_bool(argv[0]);
                return ts_value_make_bool(b);
            }
            return ts_value_make_bool(false);
        };

        TsValue* ctorVal = ts_value_make_native_function((void*)+boolFn, nullptr);
        void* ctorRaw = ts_value_get_object(ctorVal);
        TsFunction* ctorFunc = (TsFunction*)ctorRaw;

        if (!ctorFunc->properties) ctorFunc->properties = TsMap::Create();
        TsMap* proto = TsMap::Create();
        TsValue protoKey; protoKey.type = ValueType::STRING_PTR;
        protoKey.ptr_val = TsString::GetInterned("prototype");
        TsValue protoVal; protoVal.type = ValueType::OBJECT_PTR;
        protoVal.ptr_val = proto;
        ctorFunc->properties->Set(protoKey, protoVal);

        ctorFunc->name = TsString::Create("Boolean");
        cached = (void*)ctorVal;
    }
    return cached;
}

void* ts_get_global_Function() {
    static TsMap* cached = nullptr;
    if (!cached) {
        cached = TsMap::Create();
        TsMap* proto = TsMap::Create();

        // Function.prototype.call / apply / bind
        addMethod(proto, "call", (void*)ts_function_call_native);
        addMethod(proto, "apply", (void*)ts_function_apply_native);
        addMethod(proto, "bind", (void*)ts_function_bind_native);

        // Set ctor.prototype = proto
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
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("TypeError");
    return cached;
}

void* ts_get_global_RangeError() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("RangeError");
    return cached;
}

void* ts_get_global_ReferenceError() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("ReferenceError");
    return cached;
}

void* ts_get_global_SyntaxError() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("SyntaxError");
    return cached;
}

void* ts_get_global_URIError() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("URIError");
    return cached;
}

void* ts_get_global_EvalError() {
    static void* cached = nullptr;
    if (!cached) cached = makeErrorConstructor("EvalError");
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

// Generic global lookup by name (namePtr is a raw C string from createGlobalString)
void* ts_get_global(void* namePtr) {
    if (!namePtr) return nullptr;
    const char* name = (const char*)namePtr;
    // Try builtin functions (encodeURIComponent, decodeURIComponent, etc.)
    TsString* tsName = TsString::Create(name);
    void* builtin = ts_get_builtin_function(tsName);
    if (builtin) return builtin;
    return nullptr;
}

} // extern "C"
