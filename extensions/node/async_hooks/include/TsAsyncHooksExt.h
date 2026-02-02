#ifndef TS_ASYNC_HOOKS_EXT_H
#define TS_ASYNC_HOOKS_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Module-level functions
int64_t ts_async_hooks_executionAsyncId();
int64_t ts_async_hooks_triggerAsyncId();
void* ts_async_hooks_executionAsyncResource();
void* ts_async_hooks_createHook(void* callbacks);

// AsyncLocalStorage functions
void* ts_async_local_storage_create();
void* ts_async_local_storage_getStore(void* als);
void* ts_async_local_storage_run(void* als, void* store, void* callback, int argc, void** args);
void* ts_async_local_storage_exit(void* als, void* callback, int argc, void** args);
void ts_async_local_storage_enterWith(void* als, void* store);
void ts_async_local_storage_disable(void* als);

// AsyncResource functions
void* ts_async_resource_create(void* type, int64_t triggerAsyncId);
int64_t ts_async_resource_asyncId(void* resource);
int64_t ts_async_resource_triggerAsyncId(void* resource);
void* ts_async_resource_runInAsyncScope(void* resource, void* fn, void* thisArg, int argc, void** args);
void* ts_async_resource_bind(void* resource, void* fn);
void* ts_async_resource_emitDestroy(void* resource);

// AsyncHook functions
void* ts_async_hook_enable(void* hook);
void* ts_async_hook_disable(void* hook);

#ifdef __cplusplus
}
#endif

#endif // TS_ASYNC_HOOKS_EXT_H
