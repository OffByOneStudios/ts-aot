#include "TsReflect.h"
#include "TsProxy.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "TsFlatObject.h"
#include "GC.h"

// Reflect provides static methods for interceptable JavaScript operations
// These methods directly access targets without going through Proxy traps

extern "C" TsValue* ts_reflect_get(void* targetArg, void* propArg, void* receiverArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return ts_value_make_undefined();

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Direct property access on target (bypasses Proxy)
    return ts_object_get_dynamic(ts_value_box_any(target), (TsValue*)propArg);
}

extern "C" int64_t ts_reflect_set(void* targetArg, void* propArg, void* valueArg, void* receiverArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Direct property set on target (bypasses Proxy)
    ts_object_set_dynamic(ts_value_box_any(target), (TsValue*)propArg, (TsValue*)valueArg);
    return 1;  // Return true - simplified, real impl checks writable
}

extern "C" int64_t ts_reflect_has(void* targetArg, void* propArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

    return ts_object_has_prop(ts_value_box_any(target), (TsValue*)propArg) ? 1 : 0;
}

extern "C" int64_t ts_reflect_deleteProperty(void* targetArg, void* propArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

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
    if (!target) return ts_value_make_undefined();

    // If newTarget is null, use target
    if (!newTargetArg) newTargetArg = target;

    // Get arguments array
    void* argsRaw = ts_nanbox_safe_unbox(argsArg);

    if (argsRaw && is_flat_object(argsRaw)) {
        return ts_value_make_undefined();
    }

    TsArray* argsArray = dynamic_cast<TsArray*>((TsObject*)argsRaw);
    if (!argsArray) {
        return ts_value_make_undefined();
    }

    // Check if target is a function
    uint32_t magic = *(uint32_t*)target;
    if (magic != TsFunction::MAGIC) {
        return ts_value_make_undefined();
    }

    TsFunction* func = (TsFunction*)target;
    TsValue boxedFunc;
    boxedFunc.type = ValueType::FUNCTION_PTR;
    boxedFunc.ptr_val = func;

    int64_t len = argsArray->Length();
    if (len == 0) {
        return ts_call_0(&boxedFunc);
    } else if (len == 1) {
        TsValue* arg0 = (TsValue*)argsArray->Get(0);
        return ts_call_1(&boxedFunc, arg0);
    } else if (len == 2) {
        TsValue* arg0 = (TsValue*)argsArray->Get(0);
        TsValue* arg1 = (TsValue*)argsArray->Get(1);
        return ts_call_2(&boxedFunc, arg0, arg1);
    } else if (len == 3) {
        TsValue* arg0 = (TsValue*)argsArray->Get(0);
        TsValue* arg1 = (TsValue*)argsArray->Get(1);
        TsValue* arg2 = (TsValue*)argsArray->Get(2);
        return ts_call_3(&boxedFunc, arg0, arg1, arg2);
    }

    return ts_value_make_undefined();
}

extern "C" TsValue* ts_reflect_getPrototypeOf(void* targetArg) {
    // ts-aot doesn't have a prototype chain currently
    return ts_value_make_undefined();
}

extern "C" int64_t ts_reflect_setPrototypeOf(void* targetArg, void* protoArg) {
    // ts-aot doesn't support prototype chain modification
    return 0;
}

extern "C" int64_t ts_reflect_isExtensible(void* targetArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

    if (is_flat_object(target)) return 1;  // Flat objects are extensible (via overflow)

    TsMap* obj = dynamic_cast<TsMap*>((TsObject*)target);
    if (obj) {
        return obj->IsExtensible() ? 1 : 0;
    }
    return 1;  // Default: objects are extensible
}

extern "C" int64_t ts_reflect_preventExtensions(void* targetArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

    if (is_flat_object(target)) return 0;  // Can't prevent extensions on flat objects

    TsMap* obj = dynamic_cast<TsMap*>((TsObject*)target);
    if (obj) {
        obj->PreventExtensions();
        return 1;
    }
    return 0;
}

extern "C" TsValue* ts_reflect_getOwnPropertyDescriptor(void* targetArg, void* propArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return ts_value_make_undefined();

    // Convert flat objects for interop
    if (is_flat_object(target)) {
        target = ts_flat_object_to_map(target);
    }

    TsMap* obj = dynamic_cast<TsMap*>((TsObject*)target);
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
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return 0;

    if (is_flat_object(target)) {
        target = ts_flat_object_to_map(target);
    }

    TsMap* obj = dynamic_cast<TsMap*>((TsObject*)target);
    if (!obj) return 0;

    void* descRaw = ts_nanbox_safe_unbox(descriptorArg);
    if (descRaw && is_flat_object(descRaw)) {
        descRaw = ts_flat_object_to_map(descRaw);
    }
    TsMap* descriptor = dynamic_cast<TsMap*>((TsObject*)descRaw);
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
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return ts_value_make_array(TsArray::Create());

    // Use ts_object_keys which returns an array
    return ts_object_keys(ts_value_box_any(target));
}
