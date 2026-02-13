#include "TsProxy.h"
#include "TsMap.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

// TsProxy implementation

TsProxy::TsProxy(void* t, TsMap* h) : target(t), handler(h), revoked(false) {
    proxyMagic = PROXY_MAGIC;
}

TsProxy* TsProxy::Create(void* target, TsMap* handler) {
    void* mem = ts_alloc(sizeof(TsProxy));
    return new (mem) TsProxy(target, handler);
}

TsFunction* TsProxy::getTrap(const char* trapName) {
    if (revoked || !handler) return nullptr;

    TsString* keyStr = TsString::Create(trapName);
    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = keyStr;
    TsValue trapVal = handler->Get(key);

    if (trapVal.type == ValueType::UNDEFINED) {
        return nullptr;
    }

    // Extract function from trap value
    if (trapVal.type == ValueType::OBJECT_PTR || trapVal.type == ValueType::FUNCTION_PTR) {
        void* ptr = trapVal.ptr_val;
        if (ptr) {
            // Check magic at offset 16 for TsFunction (after C++ vtable (8) + TsObject vtable (8))
            uint32_t fnMagic16 = *(uint32_t*)((char*)ptr + 16);
            if (fnMagic16 == TsFunction::MAGIC) {
                return (TsFunction*)ptr;
            }
        }
    }

    return nullptr;
}

TsValue* TsProxy::get(TsValue* prop, void* receiver) {
    if (revoked) {
        return ts_value_make_undefined();
    }

    TsFunction* trap = getTrap("get");
    if (trap) {
        // Call trap(target, prop, receiver)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* receiverVal = receiver ? ts_value_box_any(receiver) : ts_value_box_any(this);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        return ts_call_3(&boxedTrap, targetVal, prop, receiverVal);
    }

    // No trap - forward to target
    if (!target) return ts_value_make_undefined();

    // Get property from target
    return ts_object_get_dynamic(ts_value_box_any(target), prop);
}

bool TsProxy::set(TsValue* prop, TsValue* value, void* receiver) {
    if (revoked) {
        return false;
    }

    TsFunction* trap = getTrap("set");
    if (trap) {
        // Call trap(target, prop, value, receiver)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* receiverVal = receiver ? ts_value_box_any(receiver) : ts_value_box_any(this);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        TsValue* result = ts_call_4(&boxedTrap, targetVal, prop, value, receiverVal);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    ts_object_set_dynamic(ts_value_box_any(target), prop, value);
    return true;
}

bool TsProxy::has(TsValue* prop) {
    if (revoked) {
        return false;
    }

    TsFunction* trap = getTrap("has");
    if (trap) {
        // Call trap(target, prop)
        TsValue* targetVal = ts_value_box_any(target);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        TsValue* result = ts_call_2(&boxedTrap, targetVal, prop);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    return ts_object_has_prop(ts_value_box_any(target), prop);
}

bool TsProxy::deleteProperty(TsValue* prop) {
    if (revoked) {
        return false;
    }

    TsFunction* trap = getTrap("deleteProperty");
    if (trap) {
        // Call trap(target, prop)
        TsValue* targetVal = ts_value_box_any(target);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        TsValue* result = ts_call_2(&boxedTrap, targetVal, prop);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    return ts_object_delete_prop(ts_value_box_any(target), prop);
}

TsValue* TsProxy::apply(void* thisArg, TsValue* args, int argCount) {
    if (revoked) {
        return ts_value_make_undefined();
    }

    TsFunction* trap = getTrap("apply");
    if (trap) {
        // Call trap(target, thisArg, argumentsList)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* thisArgVal = thisArg ? ts_value_box_any(thisArg) : ts_value_make_undefined();
        TsValue* argsVal = ts_value_box_any(args);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        return ts_call_3(&boxedTrap, targetVal, thisArgVal, argsVal);
    }

    // No trap - forward call to target
    if (!target) return ts_value_make_undefined();

    TsValue* targetVal = ts_value_box_any(target);
    TsValue* thisVal = thisArg ? ts_value_box_any(thisArg) : ts_value_make_undefined();
    TsValue* argsArray = ts_value_box_any(args);

    return ts_function_apply(targetVal, thisVal, argsArray);
}

TsValue* TsProxy::construct(TsValue* args, int argCount, void* newTarget) {
    if (revoked) {
        return ts_value_make_undefined();
    }

    TsFunction* trap = getTrap("construct");
    if (trap) {
        // Call trap(target, argumentsList, newTarget)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* argsVal = ts_value_box_any(args);
        TsValue* newTargetVal = newTarget ? ts_value_box_any(newTarget) : targetVal;

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        return ts_call_3(&boxedTrap, targetVal, argsVal, newTargetVal);
    }

    // No trap - forward to target
    // For now, return undefined - full implementation needs Reflect.construct
    return ts_value_make_undefined();
}

TsValue* TsProxy::ownKeys() {
    if (revoked) {
        return ts_value_make_array(TsArray::Create());
    }

    TsFunction* trap = getTrap("ownKeys");
    if (trap) {
        // Call trap(target)
        TsValue* targetVal = ts_value_box_any(target);

        TsValue boxedTrap;
        boxedTrap.type = ValueType::FUNCTION_PTR;
        boxedTrap.ptr_val = trap;

        return ts_call_1(&boxedTrap, targetVal);
    }

    // No trap - return Object.keys(target)
    if (!target) {
        return ts_value_make_array(TsArray::Create());
    }

    // Get keys from target using ts_object_keys
    return ts_object_keys(ts_value_box_any(target));
}

// C API implementations

extern "C" TsValue* ts_proxy_create(void* targetArg, void* handlerArg) {
    // Unbox arguments
    void* target = ts_nanbox_safe_unbox(targetArg);

    void* handlerRaw = ts_nanbox_safe_unbox(handlerArg);

    TsMap* handler = dynamic_cast<TsMap*>((TsObject*)handlerRaw);

    // Create proxy
    TsProxy* proxy = TsProxy::Create(target, handler);

    return ts_value_make_object(proxy);
}

extern "C" TsValue* ts_proxy_revocable(void* targetArg, void* handlerArg) {
    // Create proxy
    TsValue* proxyVal = ts_proxy_create(targetArg, handlerArg);
    // proxyVal is NaN-boxed — extract proxy pointer
    TsProxy* proxy = (TsProxy*)ts_value_get_object(proxyVal);

    // Create revoke function
    struct RevokeContext {
        TsProxy* proxy;
    };
    RevokeContext* ctx = (RevokeContext*)ts_alloc(sizeof(RevokeContext));
    ctx->proxy = proxy;

    void* revokeFuncAddr = (void*)(+[](void* context) -> TsValue* {
        RevokeContext* ctx = (RevokeContext*)context;
        if (ctx->proxy) {
            ctx->proxy->revoked = true;
        }
        return ts_value_make_undefined();
    });
    TsFunction* revokeFunc = new (ts_alloc(sizeof(TsFunction))) TsFunction(revokeFuncAddr, ctx, FunctionType::COMPILED);

    TsValue revokeFuncVal;
    revokeFuncVal.type = ValueType::FUNCTION_PTR;
    revokeFuncVal.ptr_val = revokeFunc;

    // Create result object { proxy, revoke }
    TsMap* result = TsMap::Create();
    result->Set(TsString::Create("proxy"), *proxyVal);
    result->Set(TsString::Create("revoke"), revokeFuncVal);

    return ts_value_make_object(result);
}

extern "C" TsValue* ts_proxy_get(void* proxyArg, void* propArg, void* receiverArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    // Check if it's actually a proxy
    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Not a proxy, use regular property access
        return ts_object_get_dynamic((TsValue*)proxyArg, (TsValue*)propArg);
    }

    return proxy->get((TsValue*)propArg, receiverArg);
}

extern "C" int64_t ts_proxy_set(void* proxyArg, void* propArg, void* valueArg, void* receiverArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        ts_object_set_dynamic((TsValue*)proxyArg, (TsValue*)propArg, (TsValue*)valueArg);
        return 1;
    }

    return proxy->set((TsValue*)propArg, (TsValue*)valueArg, receiverArg) ? 1 : 0;
}

extern "C" int64_t ts_proxy_has(void* proxyArg, void* propArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_object_has_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    return proxy->has((TsValue*)propArg) ? 1 : 0;
}

extern "C" int64_t ts_proxy_delete(void* proxyArg, void* propArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_object_delete_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    return proxy->deleteProperty((TsValue*)propArg) ? 1 : 0;
}

extern "C" TsValue* ts_proxy_apply(void* proxyArg, void* thisArg, void* argsArg, int64_t argCount) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Not a proxy, call directly
        return ts_function_apply((TsValue*)proxyArg, (TsValue*)thisArg, (TsValue*)argsArg);
    }

    return proxy->apply(thisArg, (TsValue*)argsArg, (int)argCount);
}

extern "C" TsValue* ts_proxy_construct(void* proxyArg, void* argsArg, int64_t argCount, void* newTargetArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_value_make_undefined();
    }

    return proxy->construct((TsValue*)argsArg, (int)argCount, newTargetArg);
}

extern "C" TsValue* ts_proxy_ownKeys(void* proxyArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Return Object.keys for non-proxy
        return ts_object_keys((TsValue*)proxyArg);
    }

    return proxy->ownKeys();
}

extern "C" int64_t ts_is_proxy(void* objArg) {
    void* raw = ts_nanbox_safe_unbox(objArg);
    if (!raw) return 0;

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)raw);
    return proxy ? 1 : 0;
}
