#pragma once

#include "TsObject.h"

// Reflect provides static methods for interceptable JavaScript operations
// These methods directly access targets without going through Proxy traps

extern "C" {
    // Reflect.get(target, propertyKey [, receiver])
    TsValue* ts_reflect_get(void* target, void* prop, void* receiver);

    // Reflect.set(target, propertyKey, value [, receiver])
    int64_t ts_reflect_set(void* target, void* prop, void* value, void* receiver);

    // Reflect.has(target, propertyKey)
    int64_t ts_reflect_has(void* target, void* prop);

    // Reflect.deleteProperty(target, propertyKey)
    int64_t ts_reflect_deleteProperty(void* target, void* prop);

    // Reflect.apply(target, thisArgument, argumentsList)
    TsValue* ts_reflect_apply(void* target, void* thisArg, void* args);

    // Reflect.construct(target, argumentsList [, newTarget])
    TsValue* ts_reflect_construct(void* target, void* args, void* newTarget);

    // Reflect.getPrototypeOf(target)
    TsValue* ts_reflect_getPrototypeOf(void* target);

    // Reflect.setPrototypeOf(target, prototype)
    int64_t ts_reflect_setPrototypeOf(void* target, void* proto);

    // Reflect.isExtensible(target)
    int64_t ts_reflect_isExtensible(void* target);

    // Reflect.preventExtensions(target)
    int64_t ts_reflect_preventExtensions(void* target);

    // Reflect.getOwnPropertyDescriptor(target, propertyKey)
    TsValue* ts_reflect_getOwnPropertyDescriptor(void* target, void* prop);

    // Reflect.defineProperty(target, propertyKey, attributes)
    int64_t ts_reflect_defineProperty(void* target, void* prop, void* descriptor);

    // Reflect.ownKeys(target)
    TsValue* ts_reflect_ownKeys(void* target);
}
