// Async hooks module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsAsyncLocalStorage, TsAsyncResource, TsAsyncHook)

#include "TsAsyncHooksExt.h"
#include "TsAsyncHooks.h"
#include "TsRuntime.h"
#include "TsNanBox.h"
#include "TsMap.h"
#include "TsArray.h"
#include "GC.h"

extern "C" {

// Module-level functions
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

// AsyncLocalStorage functions
void* ts_async_local_storage_create() {
    ts::TsAsyncLocalStorage* als = ts::TsAsyncLocalStorage::Create();
    return als;
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

// AsyncResource functions
void* ts_async_resource_create(void* type, int64_t triggerAsyncId) {
    TsString* typeStr = nullptr;

    // Unbox type string
    void* rawPtr = ts_value_get_object((TsValue*)type);
    if (rawPtr) {
        typeStr = dynamic_cast<TsString*>((TsObject*)rawPtr);
    }
    if (!typeStr) {
        TsValue typeDec = nanbox_to_tagged((TsValue*)type);
        if (typeDec.type == ValueType::STRING_PTR) {
            typeStr = (TsString*)typeDec.ptr_val;
        }
    }
    if (!typeStr) {
        typeStr = TsString::Create("UNKNOWN");
    }

    ts::TsAsyncResource* resource = ts::TsAsyncResource::Create(typeStr, triggerAsyncId);
    return resource;
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

// AsyncHook functions
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
