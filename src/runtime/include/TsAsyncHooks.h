#pragma once

#include "TsObject.h"
#include "TsMap.h"
#include <vector>
#include <functional>

namespace ts {

// Forward declarations
struct TsAsyncLocalStorage;
struct TsAsyncResource;
struct TsAsyncHook;

// =========================================================================
// TsAsyncLocalStorage - Provides async context propagation
// =========================================================================
struct TsAsyncLocalStorage : public TsObject {
    static constexpr uint32_t MAGIC = 0x414C5354; // "ALST"

    TsValue* store = nullptr;  // Current store value
    bool enabled = true;

    TsAsyncLocalStorage();
    static TsAsyncLocalStorage* Create();

    TsValue* getStore();
    TsValue* run(TsValue* store, TsValue* callback, int argc, TsValue** args);
    TsValue* exit(TsValue* callback, int argc, TsValue** args);
    void enterWith(TsValue* store);
    void disable();
};

// =========================================================================
// TsAsyncResource - Low-level resource tracking for async operations
// =========================================================================
struct TsAsyncResource : public TsObject {
    static constexpr uint32_t MAGIC = 0x41525343; // "ARSC"

    int64_t asyncId;
    int64_t triggerAsyncId;
    TsString* type;

    TsAsyncResource(TsString* type, int64_t triggerAsyncId = -1);
    static TsAsyncResource* Create(TsString* type, int64_t triggerAsyncId = -1);

    int64_t getAsyncId();
    int64_t getTriggerAsyncId();
    TsValue* runInAsyncScope(TsValue* fn, TsValue* thisArg, int argc, TsValue** args);
    TsValue* bind(TsValue* fn);
    TsAsyncResource* emitDestroy();
};

// =========================================================================
// TsAsyncHook - Tracks async operations via callbacks
// =========================================================================
struct TsAsyncHook : public TsObject {
    static constexpr uint32_t MAGIC = 0x4148534B; // "AHSK"

    TsValue* initCallback = nullptr;
    TsValue* beforeCallback = nullptr;
    TsValue* afterCallback = nullptr;
    TsValue* destroyCallback = nullptr;
    TsValue* promiseResolveCallback = nullptr;
    bool enabled = false;

    TsAsyncHook();
    static TsAsyncHook* Create(TsValue* callbacks);

    TsAsyncHook* enable();
    TsAsyncHook* disable();
};

// =========================================================================
// Async context management (global state)
// =========================================================================
struct AsyncContext {
    int64_t currentAsyncId = 1;
    int64_t currentTriggerAsyncId = 0;
    int64_t nextAsyncId = 2;
    TsAsyncResource* currentResource = nullptr;
    std::vector<TsAsyncHook*> hooks;
    std::vector<TsAsyncLocalStorage*> storages;
};

AsyncContext& getAsyncContext();

} // namespace ts

extern "C" {
    // Module-level functions
    int64_t ts_async_hooks_executionAsyncId();
    int64_t ts_async_hooks_triggerAsyncId();
    void* ts_async_hooks_executionAsyncResource();

    // createHook
    void* ts_async_hooks_createHook(void* callbacks);

    // AsyncLocalStorage
    void* ts_async_local_storage_create();
    void* ts_async_local_storage_getStore(void* als);
    void* ts_async_local_storage_run(void* als, void* store, void* callback, int argc, void** args);
    void* ts_async_local_storage_exit(void* als, void* callback, int argc, void** args);
    void ts_async_local_storage_enterWith(void* als, void* store);
    void ts_async_local_storage_disable(void* als);

    // AsyncResource
    void* ts_async_resource_create(void* type, int64_t triggerAsyncId);
    int64_t ts_async_resource_asyncId(void* resource);
    int64_t ts_async_resource_triggerAsyncId(void* resource);
    void* ts_async_resource_runInAsyncScope(void* resource, void* fn, void* thisArg, int argc, void** args);
    void* ts_async_resource_bind(void* resource, void* fn);
    void* ts_async_resource_emitDestroy(void* resource);

    // AsyncHook
    void* ts_async_hook_enable(void* hook);
    void* ts_async_hook_disable(void* hook);
}
