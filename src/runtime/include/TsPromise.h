#pragma once

#include "TsRuntime.h"
#include "TsObject.h"
#include <vector>
#include <functional>

namespace ts {

struct TsPromise;

struct AsyncContext : public TsObject {
    int state = 0;
    TsPromise* promise = nullptr;
    void (*resumeFn)(AsyncContext*, TsValue*);
    void* data = nullptr; // For local variables
    
    AsyncContext();
};

enum class PromiseState {
    Pending,
    Fulfilled,
    Rejected
};

struct TsPromise : public TsObject {
    PromiseState state = PromiseState::Pending;
    TsValue value; // Result or Error
    
    // Callbacks
    struct Callback {
        TsValue onFulfilled;
        TsValue onRejected;
        TsPromise* nextPromise;
        AsyncContext* asyncCtx;
    };
    std::vector<Callback> callbacks;

    TsPromise();
    void then(TsValue onFulfilled, TsValue onRejected = nullptr);
    void then_async(AsyncContext* asyncCtx);
};

extern "C" {
    TsPromise* ts_promise_create();
    void ts_promise_resolve_internal(TsPromise* promise, TsValue* value);
    void ts_promise_reject_internal(TsPromise* promise, TsValue* reason);
    TsValue* ts_promise_resolve(TsValue* value);
    TsValue* ts_promise_then(TsValue* promise, TsValue* callback);
    TsValue* ts_promise_await(TsValue* promise);
    void ts_async_await(TsValue* promise, AsyncContext* ctx);
    
    AsyncContext* ts_async_context_create();
    TsPromise* ts_async_get_promise(AsyncContext* ctx);
    void ts_async_resume(AsyncContext* ctx, TsValue* value);

#ifdef __cplusplus
}
#endif
} // namespace ts
