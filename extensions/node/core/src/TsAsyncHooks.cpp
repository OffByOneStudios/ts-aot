#include "TsAsyncHooks.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsNanBox.h"
#include "GC.h"
#include "TsGC.h"
#include <cstring>

// Forward declare
extern "C" TsValue* ts_function_call(TsValue* fn, int argc, TsValue** args);
extern "C" TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** args);

namespace ts {

// =========================================================================
// Global async context
// =========================================================================
static AsyncContext* globalAsyncContext = nullptr;

AsyncContext& getAsyncContext() {
    if (!globalAsyncContext) {
        globalAsyncContext = (AsyncContext*)ts_alloc(sizeof(AsyncContext));
        new (globalAsyncContext) AsyncContext();
        ts_gc_register_root((void**)&globalAsyncContext);
    }
    return *globalAsyncContext;
}

// =========================================================================
// TsAsyncLocalStorage implementation
// =========================================================================
TsAsyncLocalStorage::TsAsyncLocalStorage() {
    magic = MAGIC;
    store = nullptr;
    enabled = true;
}

TsAsyncLocalStorage* TsAsyncLocalStorage::Create() {
    void* mem = ts_alloc(sizeof(TsAsyncLocalStorage));
    TsAsyncLocalStorage* als = new (mem) TsAsyncLocalStorage();
    getAsyncContext().storages.push_back(als);
    return als;
}

TsValue* TsAsyncLocalStorage::getStore() {
    if (!enabled || !store) {
        return ts_value_make_undefined();
    }
    return store;
}

TsValue* TsAsyncLocalStorage::run(TsValue* newStore, TsValue* callback, int argc, TsValue** args) {
    if (!enabled) {
        // If disabled, still run the callback
        return ts_function_call(callback, argc, args);
    }

    // Save old store
    TsValue* oldStore = store;

    // Set new store
    store = newStore;

    // Run callback
    TsValue* result = ts_function_call(callback, argc, args);

    // Restore old store
    store = oldStore;

    return result;
}

TsValue* TsAsyncLocalStorage::exit(TsValue* callback, int argc, TsValue** args) {
    // Save old store
    TsValue* oldStore = store;

    // Clear store
    store = nullptr;

    // Run callback
    TsValue* result = ts_function_call(callback, argc, args);

    // Restore old store
    store = oldStore;

    return result;
}

void TsAsyncLocalStorage::enterWith(TsValue* newStore) {
    if (enabled) {
        store = newStore;
    }
}

void TsAsyncLocalStorage::disable() {
    enabled = false;
    store = nullptr;
}

// =========================================================================
// TsAsyncResource implementation
// =========================================================================
TsAsyncResource::TsAsyncResource(TsString* typeStr, int64_t triggerId)
    : type(typeStr) {
    magic = MAGIC;

    AsyncContext& ctx = getAsyncContext();

    // Assign async ID
    asyncId = ctx.nextAsyncId++;

    // Use provided trigger ID or current execution async ID
    if (triggerId >= 0) {
        triggerAsyncId = triggerId;
    } else {
        triggerAsyncId = ctx.currentAsyncId;
    }

    // Notify hooks of init
    for (auto* hook : ctx.hooks) {
        if (hook->enabled && hook->initCallback) {
            TsValue* asyncIdVal = ts_value_make_int(asyncId);
            TsValue* typeVal = ts_value_make_string(type);
            TsValue* triggerIdVal = ts_value_make_int(triggerAsyncId);
            TsValue* resourceVal = ts_value_make_object(this);
            TsValue* args[] = { asyncIdVal, typeVal, triggerIdVal, resourceVal };
            ts_function_call(hook->initCallback, 4, args);
        }
    }
}

TsAsyncResource* TsAsyncResource::Create(TsString* type, int64_t triggerAsyncId) {
    void* mem = ts_alloc(sizeof(TsAsyncResource));
    return new (mem) TsAsyncResource(type, triggerAsyncId);
}

int64_t TsAsyncResource::getAsyncId() {
    return asyncId;
}

int64_t TsAsyncResource::getTriggerAsyncId() {
    return triggerAsyncId;
}

TsValue* TsAsyncResource::runInAsyncScope(TsValue* fn, TsValue* thisArg, int argc, TsValue** args) {
    AsyncContext& ctx = getAsyncContext();

    // Save current context
    int64_t prevAsyncId = ctx.currentAsyncId;
    int64_t prevTriggerAsyncId = ctx.currentTriggerAsyncId;
    TsAsyncResource* prevResource = ctx.currentResource;

    // Set new context
    ctx.currentAsyncId = asyncId;
    ctx.currentTriggerAsyncId = triggerAsyncId;
    ctx.currentResource = this;

    // Notify hooks of before
    for (auto* hook : ctx.hooks) {
        if (hook->enabled && hook->beforeCallback) {
            TsValue* asyncIdVal = ts_value_make_int(asyncId);
            TsValue* callArgs[] = { asyncIdVal };
            ts_function_call(hook->beforeCallback, 1, callArgs);
        }
    }

    // Run function
    TsValue* result;
    if (thisArg && !ts_value_is_undefined(thisArg)) {
        result = ts_function_call_with_this(fn, thisArg, argc, args);
    } else {
        result = ts_function_call(fn, argc, args);
    }

    // Notify hooks of after
    for (auto* hook : ctx.hooks) {
        if (hook->enabled && hook->afterCallback) {
            TsValue* asyncIdVal = ts_value_make_int(asyncId);
            TsValue* callArgs[] = { asyncIdVal };
            ts_function_call(hook->afterCallback, 1, callArgs);
        }
    }

    // Restore context
    ctx.currentAsyncId = prevAsyncId;
    ctx.currentTriggerAsyncId = prevTriggerAsyncId;
    ctx.currentResource = prevResource;

    return result;
}

// Wrapper context for bound functions
struct BoundContext {
    TsAsyncResource* resource;
    TsValue* originalFn;
};

TsValue* TsAsyncResource::bind(TsValue* fn) {
    // Create a wrapper that runs the function in async scope
    // For now, just return the function as-is (simplified implementation)
    return fn;
}

TsAsyncResource* TsAsyncResource::emitDestroy() {
    AsyncContext& ctx = getAsyncContext();

    // Notify hooks of destroy
    for (auto* hook : ctx.hooks) {
        if (hook->enabled && hook->destroyCallback) {
            TsValue* asyncIdVal = ts_value_make_int(asyncId);
            TsValue* args[] = { asyncIdVal };
            ts_function_call(hook->destroyCallback, 1, args);
        }
    }

    return this;
}

// =========================================================================
// TsAsyncHook implementation
// =========================================================================
TsAsyncHook::TsAsyncHook() {
    magic = MAGIC;
    enabled = false;
}

TsAsyncHook* TsAsyncHook::Create(TsValue* callbacks) {
    void* mem = ts_alloc(sizeof(TsAsyncHook));
    TsAsyncHook* hook = new (mem) TsAsyncHook();

    // Extract callbacks from object
    if (callbacks) {
        TsValue cbDecoded = nanbox_to_tagged(callbacks);
        if (cbDecoded.type == ValueType::OBJECT_PTR && cbDecoded.ptr_val) {
            TsMap* cbObj = dynamic_cast<TsMap*>((TsObject*)cbDecoded.ptr_val);
            if (cbObj) {
                TsValue initVal = cbObj->Get(TsString::Create("init"));
                if (initVal.type == ValueType::FUNCTION_PTR) {
                    hook->initCallback = nanbox_from_tagged(initVal);
                }

                TsValue beforeVal = cbObj->Get(TsString::Create("before"));
                if (beforeVal.type == ValueType::FUNCTION_PTR) {
                    hook->beforeCallback = nanbox_from_tagged(beforeVal);
                }

                TsValue afterVal = cbObj->Get(TsString::Create("after"));
                if (afterVal.type == ValueType::FUNCTION_PTR) {
                    hook->afterCallback = nanbox_from_tagged(afterVal);
                }

                TsValue destroyVal = cbObj->Get(TsString::Create("destroy"));
                if (destroyVal.type == ValueType::FUNCTION_PTR) {
                    hook->destroyCallback = nanbox_from_tagged(destroyVal);
                }

                TsValue promiseResolveVal = cbObj->Get(TsString::Create("promiseResolve"));
                if (promiseResolveVal.type == ValueType::FUNCTION_PTR) {
                    hook->promiseResolveCallback = nanbox_from_tagged(promiseResolveVal);
                }
            }
        }
    }

    return hook;
}

TsAsyncHook* TsAsyncHook::enable() {
    if (!enabled) {
        enabled = true;
        getAsyncContext().hooks.push_back(this);
    }
    return this;
}

TsAsyncHook* TsAsyncHook::disable() {
    if (enabled) {
        enabled = false;
        auto& hooks = getAsyncContext().hooks;
        for (auto it = hooks.begin(); it != hooks.end(); ++it) {
            if (*it == this) {
                hooks.erase(it);
                break;
            }
        }
    }
    return this;
}

} // namespace ts

