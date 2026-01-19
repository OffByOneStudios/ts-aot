#include "TsAsyncHooks.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include "TsArray.h"
#include "GC.h"
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
    if (thisArg && thisArg->type != ValueType::UNDEFINED) {
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
    if (callbacks && callbacks->type == ValueType::OBJECT_PTR) {
        TsMap* cbObj = dynamic_cast<TsMap*>((TsObject*)callbacks->ptr_val);
        if (cbObj) {
            TsValue initKey;
            initKey.type = ValueType::STRING_PTR;
            initKey.ptr_val = TsString::Create("init");
            TsValue initVal = cbObj->Get(initKey);
            if (initVal.type == ValueType::FUNCTION_PTR) {
                hook->initCallback = (TsValue*)ts_alloc(sizeof(TsValue));
                *hook->initCallback = initVal;
            }

            TsValue beforeKey;
            beforeKey.type = ValueType::STRING_PTR;
            beforeKey.ptr_val = TsString::Create("before");
            TsValue beforeVal = cbObj->Get(beforeKey);
            if (beforeVal.type == ValueType::FUNCTION_PTR) {
                hook->beforeCallback = (TsValue*)ts_alloc(sizeof(TsValue));
                *hook->beforeCallback = beforeVal;
            }

            TsValue afterKey;
            afterKey.type = ValueType::STRING_PTR;
            afterKey.ptr_val = TsString::Create("after");
            TsValue afterVal = cbObj->Get(afterKey);
            if (afterVal.type == ValueType::FUNCTION_PTR) {
                hook->afterCallback = (TsValue*)ts_alloc(sizeof(TsValue));
                *hook->afterCallback = afterVal;
            }

            TsValue destroyKey;
            destroyKey.type = ValueType::STRING_PTR;
            destroyKey.ptr_val = TsString::Create("destroy");
            TsValue destroyVal = cbObj->Get(destroyKey);
            if (destroyVal.type == ValueType::FUNCTION_PTR) {
                hook->destroyCallback = (TsValue*)ts_alloc(sizeof(TsValue));
                *hook->destroyCallback = destroyVal;
            }

            TsValue promiseResolveKey;
            promiseResolveKey.type = ValueType::STRING_PTR;
            promiseResolveKey.ptr_val = TsString::Create("promiseResolve");
            TsValue promiseResolveVal = cbObj->Get(promiseResolveKey);
            if (promiseResolveVal.type == ValueType::FUNCTION_PTR) {
                hook->promiseResolveCallback = (TsValue*)ts_alloc(sizeof(TsValue));
                *hook->promiseResolveCallback = promiseResolveVal;
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

extern "C" {

// =========================================================================
// Module-level functions
// =========================================================================

int64_t ts_async_hooks_executionAsyncId() {
    return ts::getAsyncContext().currentAsyncId;
}

int64_t ts_async_hooks_triggerAsyncId() {
    return ts::getAsyncContext().currentTriggerAsyncId;
}

void* ts_async_hooks_executionAsyncResource() {
    ts::TsAsyncResource* resource = ts::getAsyncContext().currentResource;
    if (resource) {
        return ts_value_make_object(resource);
    }
    return ts_value_make_undefined();
}

void* ts_async_hooks_createHook(void* callbacks) {
    // Unbox if needed
    void* rawPtr = ts_value_get_object((TsValue*)callbacks);
    if (!rawPtr) rawPtr = callbacks;

    TsValue* cbVal = (TsValue*)callbacks;
    ts::TsAsyncHook* hook = ts::TsAsyncHook::Create(cbVal);
    return ts_value_make_object(hook);
}

// =========================================================================
// AsyncLocalStorage functions
// =========================================================================

void* ts_async_local_storage_create() {
    ts::TsAsyncLocalStorage* als = ts::TsAsyncLocalStorage::Create();
    return ts_value_make_object(als);
}

void* ts_async_local_storage_getStore(void* als) {
    // Unbox if needed
    void* rawPtr = ts_value_get_object((TsValue*)als);
    if (!rawPtr) rawPtr = als;

    ts::TsAsyncLocalStorage* storage = dynamic_cast<ts::TsAsyncLocalStorage*>((TsObject*)rawPtr);
    if (!storage) return ts_value_make_undefined();

    return storage->getStore();
}

void* ts_async_local_storage_run(void* als, void* store, void* callback, int argc, void** args) {
    // Unbox AsyncLocalStorage
    void* rawPtr = ts_value_get_object((TsValue*)als);
    if (!rawPtr) rawPtr = als;

    ts::TsAsyncLocalStorage* storage = dynamic_cast<ts::TsAsyncLocalStorage*>((TsObject*)rawPtr);
    if (!storage) return ts_value_make_undefined();

    // Convert args
    TsValue** tsArgs = (TsValue**)args;

    return storage->run((TsValue*)store, (TsValue*)callback, argc, tsArgs);
}

void* ts_async_local_storage_exit(void* als, void* callback, int argc, void** args) {
    // Unbox AsyncLocalStorage
    void* rawPtr = ts_value_get_object((TsValue*)als);
    if (!rawPtr) rawPtr = als;

    ts::TsAsyncLocalStorage* storage = dynamic_cast<ts::TsAsyncLocalStorage*>((TsObject*)rawPtr);
    if (!storage) return ts_value_make_undefined();

    // Convert args
    TsValue** tsArgs = (TsValue**)args;

    return storage->exit((TsValue*)callback, argc, tsArgs);
}

void ts_async_local_storage_enterWith(void* als, void* store) {
    // Unbox AsyncLocalStorage
    void* rawPtr = ts_value_get_object((TsValue*)als);
    if (!rawPtr) rawPtr = als;

    ts::TsAsyncLocalStorage* storage = dynamic_cast<ts::TsAsyncLocalStorage*>((TsObject*)rawPtr);
    if (!storage) return;

    storage->enterWith((TsValue*)store);
}

void ts_async_local_storage_disable(void* als) {
    // Unbox AsyncLocalStorage
    void* rawPtr = ts_value_get_object((TsValue*)als);
    if (!rawPtr) rawPtr = als;

    ts::TsAsyncLocalStorage* storage = dynamic_cast<ts::TsAsyncLocalStorage*>((TsObject*)rawPtr);
    if (!storage) return;

    storage->disable();
}

// =========================================================================
// AsyncResource functions
// =========================================================================

void* ts_async_resource_create(void* type, int64_t triggerAsyncId) {
    TsString* typeStr = nullptr;

    // Unbox type string
    void* rawPtr = ts_value_get_object((TsValue*)type);
    if (rawPtr) {
        typeStr = dynamic_cast<TsString*>((TsObject*)rawPtr);
    }
    if (!typeStr) {
        TsValue* typeVal = (TsValue*)type;
        if (typeVal && typeVal->type == ValueType::STRING_PTR) {
            typeStr = (TsString*)typeVal->ptr_val;
        }
    }
    if (!typeStr) {
        typeStr = TsString::Create("UNKNOWN");
    }

    ts::TsAsyncResource* resource = ts::TsAsyncResource::Create(typeStr, triggerAsyncId);
    return ts_value_make_object(resource);
}

int64_t ts_async_resource_asyncId(void* resource) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)resource);
    if (!rawPtr) rawPtr = resource;

    ts::TsAsyncResource* res = dynamic_cast<ts::TsAsyncResource*>((TsObject*)rawPtr);
    if (!res) return -1;

    return res->getAsyncId();
}

int64_t ts_async_resource_triggerAsyncId(void* resource) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)resource);
    if (!rawPtr) rawPtr = resource;

    ts::TsAsyncResource* res = dynamic_cast<ts::TsAsyncResource*>((TsObject*)rawPtr);
    if (!res) return -1;

    return res->getTriggerAsyncId();
}

void* ts_async_resource_runInAsyncScope(void* resource, void* fn, void* thisArg, int argc, void** args) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)resource);
    if (!rawPtr) rawPtr = resource;

    ts::TsAsyncResource* res = dynamic_cast<ts::TsAsyncResource*>((TsObject*)rawPtr);
    if (!res) return ts_value_make_undefined();

    TsValue** tsArgs = (TsValue**)args;
    return res->runInAsyncScope((TsValue*)fn, (TsValue*)thisArg, argc, tsArgs);
}

void* ts_async_resource_bind(void* resource, void* fn) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)resource);
    if (!rawPtr) rawPtr = resource;

    ts::TsAsyncResource* res = dynamic_cast<ts::TsAsyncResource*>((TsObject*)rawPtr);
    if (!res) return fn;

    return res->bind((TsValue*)fn);
}

void* ts_async_resource_emitDestroy(void* resource) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)resource);
    if (!rawPtr) rawPtr = resource;

    ts::TsAsyncResource* res = dynamic_cast<ts::TsAsyncResource*>((TsObject*)rawPtr);
    if (!res) return resource;

    res->emitDestroy();
    return ts_value_make_object(res);
}

// =========================================================================
// AsyncHook functions
// =========================================================================

void* ts_async_hook_enable(void* hook) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)hook);
    if (!rawPtr) rawPtr = hook;

    ts::TsAsyncHook* h = dynamic_cast<ts::TsAsyncHook*>((TsObject*)rawPtr);
    if (!h) return hook;

    h->enable();
    return ts_value_make_object(h);
}

void* ts_async_hook_disable(void* hook) {
    // Unbox
    void* rawPtr = ts_value_get_object((TsValue*)hook);
    if (!rawPtr) rawPtr = hook;

    ts::TsAsyncHook* h = dynamic_cast<ts::TsAsyncHook*>((TsObject*)rawPtr);
    if (!h) return hook;

    h->disable();
    return ts_value_make_object(h);
}

} // extern "C"
