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
    TsValue* delegateIterator = nullptr; // For yield* delegation
    void* syncGenerator = nullptr; // Back-pointer to TsGenerator (set by ts_generator_create)

    AsyncContext();
};

struct TsGenerator : public TsMap {
    static constexpr uint32_t MAGIC = 0x47454E52; // "GENR"
    AsyncContext* ctx;
    bool done = false;

    TsGenerator(AsyncContext* ctx);
    TsValue* next(TsValue* value = nullptr);
};

struct TsAsyncGenerator : public TsMap {
    static constexpr uint32_t MAGIC = 0x4147454E; // "AGEN"
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
    static constexpr uint32_t MAGIC = 0x50524F4D; // "PROM"
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

    // Generator state machine helpers
    void ts_async_context_set_resume_fn(AsyncContext* ctx, void (*fn)(AsyncContext*));
    int ts_async_context_get_state(AsyncContext* ctx);
    void ts_async_context_set_state(AsyncContext* ctx, int state);
    void ts_async_context_yield(AsyncContext* ctx, TsValue* value);
    TsValue* ts_async_context_get_resumed_value(AsyncContext* ctx);

    TsGenerator* ts_generator_create(AsyncContext* ctx);
    TsValue* Generator_next(TsValue* gen, TsValue* value);
    void ts_generator_return(TsGenerator* gen, TsValue* value);
    void ts_generator_return_via_ctx(AsyncContext* ctx, TsValue* value);
    TsValue* ts_generator_yield(TsValue* value);

    TsAsyncGenerator* ts_async_generator_create(AsyncContext* ctx);
    void ts_async_generator_return(TsAsyncGenerator* gen, TsValue* value);
    TsValue* AsyncGenerator_next(TsValue* gen, TsValue* value);
    void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done);

    // yield* delegation support
    TsValue* ts_iterator_get(TsValue* iterable);
    TsValue* ts_iterator_next(TsValue* iterator, TsValue* value);
    bool ts_iterator_result_done(TsValue* result);
    TsValue* ts_iterator_result_value(TsValue* result);
    void ts_async_context_set_delegate_iterator(AsyncContext* ctx, TsValue* iter);
    TsValue* ts_async_context_get_delegate_iterator(AsyncContext* ctx);

    // ES2024 Promise.withResolvers()
    TsValue* ts_promise_withResolvers();

#ifdef __cplusplus
}
#endif
} // namespace ts
