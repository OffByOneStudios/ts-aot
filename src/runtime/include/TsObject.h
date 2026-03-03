#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "TsNanBox.h"

class TsString;

enum class ValueType : uint8_t {
    UNDEFINED = 0,
    NUMBER_INT,
    NUMBER_DBL,
    BOOLEAN,
    STRING_PTR,
    OBJECT_PTR,
    PROMISE_PTR,
    ARRAY_PTR,
    BIGINT_PTR,
    SYMBOL_PTR,
    FUNCTION_PTR  // Functions stored with their own type for reliable typeof
};

struct TaggedValue {
    ValueType type;
    uint8_t _padding[7] = {0, 0, 0, 0, 0, 0, 0};  // Explicit padding, zero-initialized
    union {
        int64_t i_val;
        double d_val;
        bool b_val;
        void* ptr_val;
    };

    TaggedValue() : type(ValueType::UNDEFINED), i_val(0) {}
    TaggedValue(std::nullptr_t) : type(ValueType::UNDEFINED), i_val(0) {}
    TaggedValue(int64_t v) : type(ValueType::NUMBER_INT), i_val(v) {}
    TaggedValue(double v) : type(ValueType::NUMBER_DBL), d_val(v) {}
    TaggedValue(bool v) : type(ValueType::BOOLEAN), i_val(v ? 1 : 0) {}  // Use i_val to zero all 8 bytes
    TaggedValue(void* v) : type(ValueType::OBJECT_PTR), ptr_val(v) {}
    TaggedValue(TsString* v) : type(ValueType::STRING_PTR), ptr_val(v) {}
    
    operator void*() const { return (type == ValueType::OBJECT_PTR || type == ValueType::STRING_PTR) ? ptr_val : nullptr; }
};

using TsValue = TaggedValue;

// === NaN-box ↔ TsValue struct conversion ===
// TsMap/TsSet still store TsValue structs (16 bytes). These helpers
// convert between the NaN-boxed TsValue* (8 bytes, returned by ts_value_make_*)
// and the TsValue struct (16 bytes, expected by TsMap::Set / TsMap::Get).
// Replace all  *ts_value_make_X()  patterns with  nanbox_to_tagged(ts_value_make_X())

// Convert NaN-boxed TsValue* → TsValue struct (for TsMap boundary)
// Use instead of: *ts_value_make_X()
inline TsValue nanbox_to_tagged(TsValue* v) {
    TsValue tv;
    tv.i_val = 0;
    if (!v) { tv.type = ValueType::UNDEFINED; return tv; }
    uint64_t nb = (uint64_t)(uintptr_t)v;
    if (nanbox_is_undefined(nb)) { tv.type = ValueType::UNDEFINED; return tv; }
    if (nanbox_is_null(nb)) { tv.type = ValueType::OBJECT_PTR; tv.ptr_val = nullptr; return tv; }
    if (nanbox_is_int32(nb)) { tv.type = ValueType::NUMBER_INT; tv.i_val = nanbox_to_int32(nb); return tv; }
    if (nanbox_is_double(nb)) { tv.type = ValueType::NUMBER_DBL; tv.d_val = nanbox_to_double(nb); return tv; }
    if (nanbox_is_bool(nb)) { tv.type = ValueType::BOOLEAN; tv.i_val = nanbox_to_bool(nb) ? 1 : 0; return tv; }
    if (nanbox_is_ptr(nb)) {
        void* ptr = nanbox_to_ptr(nb);
        // Read magic at offset 0 for TsString/TsArray, offset 16 for TsObject subclasses
        uint32_t magic0 = *(uint32_t*)ptr;
        if (magic0 == 0x53545247) { tv.type = ValueType::STRING_PTR; tv.ptr_val = ptr; return tv; }
        if (magic0 == 0x41525259) { tv.type = ValueType::ARRAY_PTR; tv.ptr_val = ptr; return tv; }
        if (magic0 == 0x464C4154) { tv.type = ValueType::OBJECT_PTR; tv.ptr_val = ptr; return tv; } // FLAT_MAGIC
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x50524F4D) { tv.type = ValueType::PROMISE_PTR; tv.ptr_val = ptr; return tv; }
        if (magic16 == 0x42494749) { tv.type = ValueType::BIGINT_PTR; tv.ptr_val = ptr; return tv; }
        if (magic16 == 0x53594D42) { tv.type = ValueType::SYMBOL_PTR; tv.ptr_val = ptr; return tv; }
        if (magic16 == 0x46554E43) { tv.type = ValueType::FUNCTION_PTR; tv.ptr_val = ptr; return tv; }
        if (magic16 == 0x434C5352) { tv.type = ValueType::FUNCTION_PTR; tv.ptr_val = ptr; return tv; } // TsClosure
        tv.type = ValueType::OBJECT_PTR; tv.ptr_val = ptr; return tv;
    }
    tv.type = ValueType::UNDEFINED; return tv;
}

// Convert TsValue struct → NaN-boxed TsValue* pointer (for TsMap::Get results)
inline TsValue* nanbox_from_tagged(const TsValue& tv) {
    switch (tv.type) {
    case ValueType::UNDEFINED:
        return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    case ValueType::NUMBER_INT: {
        int64_t i = tv.i_val;
        if (i >= INT32_MIN && i <= INT32_MAX)
            return (TsValue*)(uintptr_t)nanbox_int32((int32_t)i);
        return (TsValue*)(uintptr_t)nanbox_double((double)i);
    }
    case ValueType::NUMBER_DBL:
        return (TsValue*)(uintptr_t)nanbox_double(tv.d_val);
    case ValueType::BOOLEAN:
        return (TsValue*)(uintptr_t)nanbox_bool(tv.i_val != 0);
    case ValueType::OBJECT_PTR:
        if (!tv.ptr_val) return (TsValue*)(uintptr_t)NANBOX_NULL;
        return (TsValue*)tv.ptr_val;
    case ValueType::STRING_PTR:
    case ValueType::ARRAY_PTR:
    case ValueType::PROMISE_PTR:
    case ValueType::BIGINT_PTR:
    case ValueType::SYMBOL_PTR:
    case ValueType::FUNCTION_PTR:
        if (tv.ptr_val) return (TsValue*)tv.ptr_val;
        return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    default:
        return (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
    }
}

// Forward declarations for AsXxx() methods
class TsReadable;
class TsWritable;
class TsDuplex;
class TsTransform;
class TsSocket;
class TsServer;

class TsObject {
public:
    virtual ~TsObject() {}
    
    // Safe casting helpers - use these instead of C-style casts
    // These handle virtual inheritance correctly
    virtual class TsEventEmitter* AsEventEmitter() { return nullptr; }
    virtual TsReadable* AsReadable() { return nullptr; }
    virtual TsWritable* AsWritable() { return nullptr; }
    virtual TsDuplex* AsDuplex() { return nullptr; }
    virtual TsTransform* AsTransform() { return nullptr; }
    virtual TsSocket* AsSocket() { return nullptr; }
    virtual TsServer* AsServer() { return nullptr; }

    // Virtual property dispatch - subclasses override to handle their own properties
    virtual TsValue GetPropertyVirtual(const char* key) { TsValue v; v.type = ValueType::UNDEFINED; v.i_val = 0; return v; }
    virtual int64_t GetLengthVirtual() { return -1; }
    virtual TsValue GetElementVirtual(int64_t index) { TsValue v; v.type = ValueType::UNDEFINED; v.i_val = 0; return v; }

    void* vtable;
    uint32_t magic;
};

enum class FunctionType {
    COMPILED,
    NATIVE
};

class TsMap;  // Forward declaration

class TsFunction : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x46554E43; // "FUNC"
    void* funcPtr;
    void* context;
    FunctionType type;
    int arity = -1;  // Number of parameters (excluding context). -1 = unknown/vararg
    TsMap* properties = nullptr;  // For storing properties like _.chunk
    TsString* name = nullptr;  // Function name for .name and .toString()
    TsFunction(void* fp, void* ctx = nullptr, FunctionType t = FunctionType::COMPILED, int a = -1)
        : funcPtr(fp), context(ctx), type(t), arity(a) {
        magic = MAGIC;
    }
};

typedef TaggedValue* (*TsFunctionPtr)(void* context, int argc, TaggedValue** argv);
typedef TaggedValue* (*TsFunctionPtrNoArgs)(void* context);

extern "C" {
    TsValue* ts_value_make_int(int64_t v);
    TsValue* ts_value_make_double(double v);
    TsValue* ts_value_make_bool(bool v);
    TsValue* ts_value_make_string(void* str);
    TsValue* ts_value_make_object(void* obj);
    TsValue* ts_value_make_promise(void* promise);
    TsValue* ts_value_make_array(void* arr);
    TsValue* ts_ensure_boxed(void* v);     // Ensure value is boxed TsValue* (for default params)
    TsValue* ts_value_box_any(void* ptr);  // Box any pointer by runtime type detection
    TsValue* ts_value_make_function(void* funcPtr, void* context);
    TsValue* ts_value_make_function_with_arity(void* funcPtr, void* context, int arity);
    TsValue* ts_value_make_function_named(void* funcPtr, void* context, void* name);
    TsValue* ts_value_make_native_function(void* funcPtr, void* context);
    bool ts_value_is_undefined(TsValue* v);
    bool ts_value_is_null(TsValue* v);
    TsValue* ts_call_0(TsValue* boxedFunc);
    TsValue* ts_call_1(TsValue* boxedFunc, TsValue* arg1);
    TsValue* ts_call_2(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2);
    TsValue* ts_call_3(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3);
    TsValue* ts_call_with_arity(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3);  // Respects func's declared arity
    TsValue* ts_call_4(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4);
    TsValue* ts_call_5(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5);
    TsValue* ts_call_6(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6);
    TsValue* ts_call_7(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7);
    TsValue* ts_call_8(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8);
    TsValue* ts_call_9(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9);
    TsValue* ts_call_10(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8, TsValue* arg9, TsValue* arg10);
    // ts_call_with_this_X - call with explicit 'this' binding (for method calls)
    TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg);
    TsValue* ts_call_with_this_1(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1);
    TsValue* ts_call_with_this_2(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2);
    TsValue* ts_call_with_this_3(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3);
    TsValue* ts_call_with_this_4(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4);
    TsValue* ts_call_with_this_5(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5);
    TsValue* ts_call_with_this_6(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6);
    TsValue* ts_call_with_this_7(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7);
    TsValue* ts_call_with_this_8(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8);
    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv);
    TsValue* ts_call_n(TsValue* boxedFunc, int64_t argc, TsValue** argv);
    TsValue* ts_function_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int argc, TsValue** argv);
    TsValue* ts_function_apply(TsValue* boxedFunc, TsValue* thisArg, TsValue* argsArray);

    // Arguments object support
    int64_t ts_get_last_call_argc();
    void* ts_create_arguments_from_params(
        void* p0, void* p1, void* p2, void* p3, void* p4,
        void* p5, void* p6, void* p7, void* p8, void* p9);

    TsValue* ts_object_get_property(void* obj, const char* key);
    
    // Unboxing helpers - safely extract raw pointers from boxed values
    void* ts_value_get_object(TsValue* val);
    int64_t ts_value_get_int(TsValue* val);
    double ts_value_get_double(TsValue* val);
    bool ts_value_get_bool(TsValue* val);
    void* ts_value_get_string(TsValue* val);
    void* ts_value_get_function(TsValue* val);  // Extract funcPtr from TsFunction
    void* ts_value_get_context(TsValue* val);   // Extract context from TsFunction
    
    // Comparison helpers
    bool ts_value_strict_eq_bool(TsValue* lhs, TsValue* rhs);
    TsValue* ts_value_strict_eq(TsValue* lhs, TsValue* rhs);  // Implements === semantics
    
    // Slow path arithmetic
    TsValue* ts_value_add(TsValue* a, TsValue* b);
    TsValue* ts_value_sub(TsValue* a, TsValue* b);
    TsValue* ts_value_inc(TsValue* a);  // ++: coerce to number, add 1
    TsValue* ts_value_dec(TsValue* a);  // --: coerce to number, subtract 1
    TsValue* ts_value_mul(TsValue* a, TsValue* b);
    TsValue* ts_value_div(TsValue* a, TsValue* b);
    TsValue* ts_value_mod(TsValue* a, TsValue* b);
    
    // Slow path comparison
    TsValue* ts_value_eq(TsValue* a, TsValue* b);
    TsValue* ts_value_lt(TsValue* a, TsValue* b);
    TsValue* ts_value_gt(TsValue* a, TsValue* b);
    TsValue* ts_value_lte(TsValue* a, TsValue* b);
    TsValue* ts_value_gte(TsValue* a, TsValue* b);
    
    // Slow path property access (pointer-based)
    TsValue* ts_object_get_dynamic(TsValue* obj, TsValue* key);
    TsValue* ts_array_get_dynamic(TsValue* arr, TsValue* index);
    void ts_array_set_dynamic(TsValue* arr, TsValue* index, TsValue* value);
    void ts_object_set_dynamic(TsValue* obj, TsValue* key, TsValue* value);  // Handles both arrays and maps
    void ts_object_set_property(void* obj, void* key, void* value);  // HIR-friendly wrapper for ts_object_set_dynamic
    bool ts_object_has_prop(TsValue* obj, TsValue* key);
    bool ts_object_delete_prop(TsValue* obj, TsValue* key);
    
    // Value-passing variants - avoid heap allocation (16 bytes, 2 registers)
    TsValue ts_object_get_prop_v(TsValue obj, TsValue key);
    TsValue ts_object_set_prop_v(TsValue obj, TsValue key, TsValue value);
    
    // Inline IR helpers - scalar-based API to avoid struct passing
    void* __ts_object_get_map(void* obj);
    void* __ts_value_to_property_key(uint8_t type, int64_t value);
    
    // Type info
    TsString* ts_value_typeof(TsValue* v);
    
    // Object static methods
    TsValue* ts_object_keys(TsValue* obj);
    TsValue* ts_object_values(TsValue* obj);
    TsValue* ts_object_entries(TsValue* obj);
    bool ts_object_is(TsValue* val1, TsValue* val2);
    TsValue* ts_object_assign(TsValue* target, TsValue* source);
    bool ts_object_has_own(TsValue* obj, TsValue* prop);
    TsValue* ts_object_from_entries(TsValue* entries);
    TsValue* ts_object_getOwnPropertyNames(TsValue* obj);
    TsValue* ts_object_getPrototypeOf(TsValue* obj);
    TsValue* ts_object_create(TsValue* proto);
    TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
    TsValue* ts_object_freeze(TsValue* obj);
    TsValue* ts_object_seal(TsValue* obj);
    TsValue* ts_object_preventExtensions(TsValue* obj);
    TsValue* ts_object_isFrozen(TsValue* obj);
    TsValue* ts_object_isSealed(TsValue* obj);
    TsValue* ts_object_isExtensible(TsValue* obj);
    TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor);
    TsValue* ts_object_defineProperties(TsValue* obj, TsValue* descriptors);
    TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop);
    TsValue* ts_object_getOwnPropertyDescriptors(TsValue* obj);

    // ES2024 groupBy methods
    TsValue* ts_object_groupBy(TsValue* iterable, TsValue* callbackFn);
    TsValue* ts_map_groupBy(TsValue* iterable, TsValue* callbackFn);

    // WeakMap - implemented as regular Map (no true weak semantics with Boehm GC)
    void* ts_weakmap_create();
    void* ts_weakmap_set(void* weakmap, void* key, TsValue* value);
    TsValue* ts_weakmap_get(void* weakmap, void* key);
    bool ts_weakmap_has(void* weakmap, void* key);
    bool ts_weakmap_delete(void* weakmap, void* key);

    // WeakSet - implemented as regular Set (no true weak semantics with Boehm GC)
    void* ts_weakset_create();
    void* ts_weakset_add(void* weakset, void* value);
    bool ts_weakset_has(void* weakset, void* value);
    bool ts_weakset_delete(void* weakset, void* value);

    // JSX Runtime Support
    TsValue* ts_jsx_create_element(void* tagName, void* props, void* children);

    void ts_runtime_init();
    extern TsValue* Object;
    extern TsValue* Array;
    extern TsValue* Math;
    extern TsValue* JSON;
    extern TsValue* console;
    extern TsValue* process;
    extern TsValue* Buffer;
    extern TsValue* global;
    extern TsValue* globalThis;  // ES2020: alias for global
    TsValue* parseInt(TsValue* arg, ...);
    TsValue* parseFloat(TsValue* arg);
    TsValue* isNaN(TsValue* arg);
    TsValue* isFinite(TsValue* arg);
    double ts_number_parseFloat(TsValue* arg);
    int64_t ts_number_parseInt(TsValue* arg);
    double ts_number_isNaN(TsValue* arg);
    double ts_number_isFinite(TsValue* arg);
}
