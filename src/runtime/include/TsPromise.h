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
    bool handled = false;
    
    // Callbacks
    struct Callback {
        TsValue onFulfilled;
        TsValue onRejected;
        TsValue onFinally;
        TsPromise* nextPromise;
        AsyncContext* asyncCtx;
    };
    std::vector<Callback> callbacks;

    TsPromise();
    TsPromise* then(TsValue onFulfilled, TsValue onRejected = nullptr);
    TsPromise* catch_error(TsValue onRejected);
    TsPromise* finally(TsValue onFinally);
    void then_async(AsyncContext* asyncCtx);
};

extern "C" {
    TsPromise* ts_promise_create();
    void ts_promise_resolve_internal(TsPromise* promise, TsValue* value);
    void ts_promise_reject_internal(TsPromise* promise, TsValue* reason);
    TsValue* ts_promise_resolve(TsValue* value);
    TsValue* ts_promise_reject(TsValue* reason);
    TsValue* ts_promise_all(TsValue* iterable);
    TsValue* ts_promise_race(TsValue* iterable);
    TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected);
    TsValue* ts_promise_catch(TsValue* promise, TsValue* onRejected);
    TsValue* ts_promise_finally(TsValue* promise, TsValue* onFinally);
    TsValue* ts_promise_await(TsValue* promise);
    void ts_async_await(TsValue* promise, AsyncContext* ctx);
    
    AsyncContext* ts_async_context_create();
    TsPromise* ts_async_get_promise(AsyncContext* ctx);
    void ts_async_resume(AsyncContext* ctx, TsValue* value);

#ifdef __cplusplus
}
#endif
} // namespace ts
