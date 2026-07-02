#include "TsReflect.h"
#include "TsProxy.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "TsFlatObject.h"
#include "TsError.h"
#include "TsClosure.h"
#include "TsObject.h"
#include "TsTyped.h"
#include "GC.h"
// Type tags (TsFunction/TsClosure/TsArray) are enrolled in their headers.

extern "C" {
    void ts_throw(TsValue* err);
    void* ts_error_create_typed(const char* type, const char* message);
}

// Reflect provides static methods for interceptable JavaScript operations
// These methods directly access targets without going through Proxy traps

// ECMA-262: Reflect.{get,set,has,deleteProperty,ownKeys,getPrototypeOf,
// setPrototypeOf,isExtensible,preventExtensions,getOwnPropertyDescriptor,
// defineProperty} step 1 require Type(target) is Object, else a TypeError.
// A NaN-boxed primitive (number/bool/null/undefined), or a Symbol/String
// primitive, is NOT an Object. Throws on a non-object; returns the unboxed
// object pointer otherwise. (Forward-declared here, defined below.)
static void* reflect_require_object(void* targetArg, const char* msg);

extern "C" TsValue* ts_reflect_get(void* targetArg, void* propArg, void* receiverArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.get called on non-object");

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Direct property access on target (bypasses Proxy)
    return ts_object_get_dynamic(ts_value_box_any(target), (TsValue*)propArg);
}

extern "C" int64_t ts_reflect_set(void* targetArg, void* propArg, void* valueArg, void* receiverArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.set called on non-object");

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Direct property set on target (bypasses Proxy)
    ts_object_set_dynamic(ts_value_box_any(target), (TsValue*)propArg, (TsValue*)valueArg);
    return 1;  // Return true - simplified, real impl checks writable
}

extern "C" int64_t ts_reflect_has(void* targetArg, void* propArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.has called on non-object");

    return ts_object_has_prop(ts_value_box_any(target), (TsValue*)propArg) ? 1 : 0;
}

extern "C" int64_t ts_reflect_deleteProperty(void* targetArg, void* propArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.deleteProperty called on non-object");

    return ts_object_delete_prop(ts_value_box_any(target), (TsValue*)propArg) ? 1 : 0;
}

extern "C" TsValue* ts_reflect_apply(void* targetArg, void* thisArgArg, void* argsArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return ts_value_make_undefined();

    TsValue* funcVal = ts_value_box_any(target);
    TsValue* thisVal = thisArgArg ? (TsValue*)thisArgArg : ts_value_make_undefined();
    TsValue* argsVal = (TsValue*)argsArg;

    return ts_function_apply(funcVal, thisVal, argsVal);
}

extern "C" TsValue* ts_reflect_construct(void* targetArg, void* argsArg, void* newTargetArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError", "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // If newTarget is provided, validate it's a constructor (function/closure).
    // If not provided, default to target (per ES spec).
    // Use ts_value_get_object to safely extract an object pointer — returns
    // null for primitives (numbers, booleans, strings, undefined, null).
    if (newTargetArg) {
        void* rawNt = ts_value_get_object((TsValue*)newTargetArg);
        if (!rawNt) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
        TsFunction* ntf = ts_cast<TsFunction>(rawNt);
        if (!ntf && !ts_is<TsClosure>(rawNt)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
        // Per ES spec, newTarget must also have [[Construct]]. Built-in
        // prototype methods (Array.prototype.X) have is_constructor=false.
        if (ntf && !ntf->is_constructor) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
    }

    // Check if target is a callable function or closure (tag at offsetof(magic)).
    TsFunction* tf = ts_cast<TsFunction>(target);
    if (!tf && !ts_is<TsClosure>(target)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // Per ES spec, built-in prototype methods (Array.prototype.X etc.) have
    // no [[Construct]] — Reflect.construct must throw TypeError. The
    // is_constructor flag is set on TsFunction at registration time.
    if (tf && !tf->is_constructor) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // Get arguments array — TsArray is NOT a TsObject subclass,
    // so check magic at offset 0 directly instead of dynamic_cast.
    void* argsRaw = ts_nanbox_safe_unbox(argsArg);
    int64_t len = 0;
    TsArray* argsArray = nullptr;

    if (argsRaw) {
        if (TsArray* a = ts_cast<TsArray>(argsRaw)) {
            argsArray = a;
            len = a->Length();
        }
    }

    // Construct through the unified construct path — it allocates `this` from
    // the constructor's prototype, runs the real constructor body, and handles
    // user functions, BOUND functions, and builtin constructor globals (Array,
    // Map, ...). The old stub just tsCall'd the target (dropping args past the
    // first and never setting `this`), so Reflect.construct(F,[1,2]) returned
    // undefined.
    int argc = (int)(len > 0 ? (len > 1024 ? 1024 : len) : 0);
    TsValue* argv[1024];
    for (int i = 0; i < argc && argsArray; i++)
        argv[i] = (TsValue*)argsArray->Get(i);
    // ES 28.1.2 step 6: the constructor runs with [[newTarget]] = newTarget
    // (defaults to target).
    extern TsValue* ts_new_from_constructor_with_target(TsValue*, TsValue*, int, TsValue**);
    TsValue* result = ts_new_from_constructor_with_target((TsValue*)targetArg,
        (TsValue*)(newTargetArg ? newTargetArg : targetArg), argc, argc ? argv : nullptr);

    // ECMA-262 OrdinaryCreateFromConstructor: the new object's [[Prototype]]
    // comes from newTarget.prototype, not target.prototype. Re-link when a
    // distinct newTarget was supplied.
    if (newTargetArg && newTargetArg != targetArg && result &&
        !ts_value_is_undefined(result) && !ts_value_is_null(result)) {
        void* rnt = ts_value_get_object((TsValue*)newTargetArg);
        if (rnt) {
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            extern TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
            TsValue* ntProto = ts_object_get_property(rnt, "prototype");
            if (ntProto && !ts_value_is_undefined(ntProto) && !ts_value_is_null(ntProto))
                ts_object_setPrototypeOf(result, ntProto);
        }
    }
    return result;
}

extern "C" TsValue* ts_reflect_getPrototypeOf(void* targetArg) {
    // ECMA-262 step 1: Type(target) must be Object.
    reflect_require_object(targetArg,
        "Reflect.getPrototypeOf called on non-object");
    // ts-aot doesn't have a prototype chain currently
    return ts_value_make_undefined();
}

extern "C" int64_t ts_reflect_setPrototypeOf(void* targetArg, void* protoArg) {
    // ECMA-262 step 1: Type(target) must be Object.
    reflect_require_object(targetArg,
        "Reflect.setPrototypeOf called on non-object");
    // ts-aot doesn't support prototype chain modification
    return 0;
}

// ECMA-262: Reflect.{isExtensible,preventExtensions,getOwnPropertyDescriptor,
// defineProperty,...} step 1 require Type(target) is Object, else a TypeError.
// A NaN-boxed primitive (number/bool/null/undefined), or a Symbol/String
// primitive, is NOT an Object; dynamic_cast<TsMap*> on such a value was UB and
// crashed in _RTDynamicCast (e.g. Reflect.isExtensible(Symbol())). Throws on a
// non-object; returns the unboxed object pointer otherwise.
static void* reflect_require_object(void* targetArg, const char* msg) {
    void* t = ts_nanbox_safe_unbox(targetArg);
    if (!t || *(uint32_t*)t == 0x53594D42 /*SYMB*/ || *(uint32_t*)t == 0x53545247 /*STRG*/) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return nullptr;  // unreachable
    }
    return t;
}

extern "C" int64_t ts_reflect_isExtensible(void* targetArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.isExtensible called on non-object");

    if (is_flat_object(target)) return 1;  // Flat objects are extensible (via overflow)

    TsMap* obj = ts_cast<TsMap>(target);
    if (obj) {
        return obj->IsExtensible() ? 1 : 0;
    }
    return 1;  // Default: objects are extensible
}

extern "C" int64_t ts_reflect_preventExtensions(void* targetArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.preventExtensions called on non-object");

    if (is_flat_object(target)) return 0;  // Can't prevent extensions on flat objects

    TsMap* obj = ts_cast<TsMap>(target);
    if (obj) {
        obj->PreventExtensions();
        return 1;
    }
    return 0;
}

extern "C" TsValue* ts_reflect_getOwnPropertyDescriptor(void* targetArg, void* propArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.getOwnPropertyDescriptor called on non-object");

    // Convert flat objects for interop
    if (is_flat_object(target)) {
        target = ts_flat_object_to_map(target);
    }

    TsMap* obj = ts_cast<TsMap>(target);
    if (!obj) return ts_value_make_undefined();

    TsValue propVal = nanbox_to_tagged((TsValue*)propArg);
    TsString* key = nullptr;

    if (propVal.type == ValueType::STRING_PTR) {
        key = (TsString*)propVal.ptr_val;
    } else {
        return ts_value_make_undefined();
    }

    TsValue val = obj->Get(key);
    if (val.type == ValueType::UNDEFINED) {
        return ts_value_make_undefined();
    }

    // Create descriptor object
    TsMap* descriptor = TsMap::Create();

    // Data descriptor
    descriptor->Set(TsString::Create("value"), val);

    TsValue trueVal;
    trueVal.type = ValueType::BOOLEAN;
    trueVal.i_val = 1;

    descriptor->Set(TsString::Create("writable"), trueVal);
    descriptor->Set(TsString::Create("enumerable"), trueVal);
    descriptor->Set(TsString::Create("configurable"), trueVal);

    return ts_value_make_object(descriptor);
}

extern "C" int64_t ts_reflect_defineProperty(void* targetArg, void* propArg, void* descriptorArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.defineProperty called on non-object");

    if (is_flat_object(target)) {
        target = ts_flat_object_to_map(target);
    }

    TsMap* obj = ts_cast<TsMap>(target);
    if (!obj) return 0;

    void* descRaw = ts_nanbox_safe_unbox(descriptorArg);
    if (descRaw && is_flat_object(descRaw)) {
        descRaw = ts_flat_object_to_map(descRaw);
    }
    TsMap* descriptor = ts_cast<TsMap>(descRaw);
    if (!descriptor) return 0;

    TsValue propVal = nanbox_to_tagged((TsValue*)propArg);
    TsString* key = nullptr;

    if (propVal.type == ValueType::STRING_PTR) {
        key = (TsString*)propVal.ptr_val;
    } else {
        return 0;
    }

    // Get value from descriptor
    TsValue valueDescVal = descriptor->Get(TsString::Create("value"));
    if (valueDescVal.type != ValueType::UNDEFINED) {
        obj->Set(key, valueDescVal);
        return 1;
    }

    return 0;
}

extern "C" TsValue* ts_reflect_ownKeys(void* targetArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.ownKeys called on non-object");

    // Use ts_object_keys which returns an array
    return ts_object_keys(ts_value_box_any(target));
}

// ============================================================================
// Mangled-name aliases for `Reflect.X(...)` call expressions in untyped JS.
// The analyzer mangles `Reflect.construct(target, args)` to the symbol
// `Reflect_construct_any_any`. Rather than teaching the analyzer to route
// these to ts_reflect_X, we expose the exact mangled symbols that forward
// to the underlying helpers. Each variant matches one or more arg-count
// shapes (e.g., construct/3 supports the optional newTarget parameter).
// ============================================================================

extern "C" TsValue* Reflect_construct_any_any(void* target, void* args) {
    return ts_reflect_construct(target, args, /*newTarget=*/target);
}

extern "C" TsValue* Reflect_construct_any_any_any(void* target, void* args, void* newTarget) {
    return ts_reflect_construct(target, args, newTarget);
}

extern "C" TsValue* Reflect_apply_any_any_any(void* target, void* thisArg, void* args) {
    return ts_reflect_apply(target, thisArg, args);
}

extern "C" TsValue* Reflect_get_any_any(void* target, void* prop) {
    return ts_reflect_get(target, prop, /*receiver=*/target);
}

extern "C" TsValue* Reflect_get_any_any_any(void* target, void* prop, void* receiver) {
    return ts_reflect_get(target, prop, receiver);
}

extern "C" TsValue* Reflect_has_any_any(void* target, void* prop) {
    return ts_value_make_bool(ts_reflect_has(target, prop) != 0);
}

extern "C" TsValue* Reflect_ownKeys_any(void* target) {
    return ts_reflect_ownKeys(target);
}

extern "C" TsValue* Reflect_getPrototypeOf_any(void* target) {
    return ts_reflect_getPrototypeOf(target);
}

extern "C" TsValue* Reflect_isExtensible_any(void* target) {
    return ts_value_make_bool(ts_reflect_isExtensible(target) != 0);
}

extern "C" TsValue* Reflect_preventExtensions_any(void* target) {
    return ts_value_make_bool(ts_reflect_preventExtensions(target) != 0);
}
