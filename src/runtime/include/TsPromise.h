#pragma once

#include "TsRuntime.h"
#include "TsObject.h"
#include <vector>
#include <functional>

#include "TsMap.h"

namespace ts {

struct TsPromise;

struct TsAsyncGenerator;

struct AsyncContext : public TsObject {
    int state = 0;
    bool error = false;
    bool yielded = false;
    TsValue yieldedValue;
    TsPromise* promise = nullptr;
    TsPromise* pendingNextPromise = nullptr;
    TsAsyncGenerator* generator = nullptr;
    void (*resumeFn)(AsyncContext*);
    void* data = nullptr; // For local variables
    TsValue* resumedValue = nullptr; // Stores value from last resume
    void* execContext = nullptr; // Execution context for nested function creation

    AsyncContext();
};

struct TsGenerator : public TsMap {
    AsyncContext* ctx;
    bool done = false;
    
    TsGenerator(AsyncContext* ctx);
    TsValue* next(TsValue* value = nullptr);
};

struct TsAsyncGenerator : public TsMap {
    AsyncContext* ctx;
    bool done = false;
    std::vector<TsPromise*> nextQueue;
    
    TsAsyncGenerator(AsyncContext* ctx);
    TsPromise* next(TsValue* value = nullptr);
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
    TsValue* ts_promise_resolve(void* context, TsValue* value);
    TsValue* ts_promise_reject(void* context, TsValue* reason);
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
    void ts_async_yield(TsValue* value, AsyncContext* ctx);

    TsGenerator* ts_generator_create(AsyncContext* ctx);
    TsValue* Generator_next(TsValue* gen, TsValue* value);
    
    TsAsyncGenerator* ts_async_generator_create(AsyncContext* ctx);
    TsValue* AsyncGenerator_next(TsValue* gen, TsValue* value);
    void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done);

#ifdef __cplusplus
}
#endif
} // namespace ts
