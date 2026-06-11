#pragma once

#include "TsRuntime.h"
#include "TsObject.h"
#include <vector>
#include <functional>

#include "TsMap.h"

struct TsArray;  // global-namespace type (see TsArray.h)

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
    TsValue* thisValue = nullptr; // ECMA-262: `this` captured at generator creation,
                                  // restored before each resume so that `this` references
                                  // inside the generator body see the original receiver
                                  // (the value of ts_get_call_this() when the wrapper ran).
    int resumeMode = 0;           // Suspendable-agen resume completion kind:
                                  // 0 = next, 1 = throw, 2 = return.
                                  // resumedValue carries the argument for all modes.
    int64_t delegateIndex = 0;    // Cursor for yield* delegation over legacy
                                  // array-shaped "iterator-likes" (GEN-001
                                  // Stage 4b, ts_agen_delegate_step). Reset to
                                  // 0 whenever a new delegate iterator is set.

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
    // Eager-body yield queue: the compiler lowers an async-generator body to
    // run to completion inside the initial call (no state machine); each
    // `yield v` appends to this queue via ts_async_generator_yield and
    // next() drains it. TsArray field is GC-visible via the object scan.
    TsArray* pendingYields = nullptr;
    size_t yieldCursor = 0;
    TsValue returnValue;   // body's return value; surfaced once on first done-result
    TsValue pendingException;   // uncaught throw from the eager body; rejects the
    bool hasException = false;  // first next() promise once queued yields drain
    bool bodyStarted = false;   // parameter prologue finished (compiler marker);
                                // body throws reject next(), param throws stay sync
    // Suspendable-model fields (GEN-001 Stage 2; dead until the suspendable
    // lowering uses ts_async_generator_create_suspendable):
    bool executing = false;     // impl frame live (AsyncGeneratorEnqueue guard)
    bool suspendable = false;   // created via ts_async_generator_create_suspendable
    TsArray* requestQueue = nullptr; // queued re-entrant requests, flattened
                                     // [promise, value, mode] triples. TsArray
                                     // field is GC-visible via the object scan
                                     // (do NOT use std::vector for GC pointers,
                                     // .claude/rules/runtime-safety.md).

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

    TsValue GetPropertyVirtual(const char* key) override;
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

    // No-arg: the compiler's async-generator prologue calls this with zero
    // args (HIRToLLVM ~975); the context is allocated internally. The old
    // (AsyncContext*) signature was a latent ABI mismatch — ctx arrived as a
    // garbage register.
    TsAsyncGenerator* ts_async_generator_create();
    void ts_async_generator_return(TsAsyncGenerator* gen, TsValue* value);
    TsValue* ts_async_generator_yield(TsValue* value);
    TsValue* AsyncGenerator_next(TsValue* gen, TsValue* value);
    TsValue* AsyncGenerator_return(TsValue* gen, TsValue* value);
    TsValue* AsyncGenerator_throw(TsValue* gen, TsValue* exc);
    void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done);

    // Suspendable async-generator runtime (GEN-001 Stage 2; dead code until
    // the Stage-3 suspendable lowering emits calls to these).
    TsAsyncGenerator* ts_async_generator_create_suspendable(AsyncContext* ctx);
    void ts_agen_suspend_yield(AsyncContext* ctx, TsValue* v);
    void ts_agen_complete(AsyncContext* ctx, TsValue* v);
    void ts_agen_complete_reject(AsyncContext* ctx, TsValue* exc);
    TsValue* ts_agen_await_operand(TsValue* v);
    TsValue* ts_agen_get_async_iterator(TsValue* iterable);
    TsValue* ts_agen_delegate_step(AsyncContext* ctx, TsValue* iterator, TsValue* sentArg);
    // Stage 7: forward gen.throw/gen.return arriving inside a yield* to the
    // delegate iterator's throw/return method (27.6.3.7). Returns the inner
    // step-result object to continue delegation, or NULL after completing the
    // generator itself (ts_agen_complete already called).
    TsValue* ts_agen_delegate_resume(AsyncContext* ctx, TsValue* iterator, int mode, TsValue* arg);
    int ts_async_context_get_resume_mode(AsyncContext* ctx);
    void ts_async_context_set_resume_mode(AsyncContext* ctx, int mode);

    // yield* delegation support
    TsValue* ts_iterator_get(TsValue* iterable);
    TsValue* ts_iterator_next(TsValue* iterator, TsValue* value);
    bool ts_iterator_result_done(TsValue* result);
    TsValue* ts_iterator_result_value(TsValue* result);
    void ts_async_context_set_delegate_iterator(AsyncContext* ctx, TsValue* iter);
    TsValue* ts_async_context_get_delegate_iterator(AsyncContext* ctx);

    // Parameter storage for generator state machines
    void ts_async_context_set_data(AsyncContext* ctx, void* data);
    void* ts_async_context_get_data(AsyncContext* ctx);

    // `this` capture: wrapper stores the receiver so that resumes can
    // restore it via ts_set_call_this() before invoking the impl.
    void ts_async_context_set_this(AsyncContext* ctx, TsValue* thisArg);
    TsValue* ts_async_context_get_this(AsyncContext* ctx);

    // ES2024 Promise.withResolvers()
    TsValue* ts_promise_withResolvers();

#ifdef __cplusplus
}
#endif
} // namespace ts

// Enrolled at global scope (TsTagOf lives in the global namespace), qualified.
TS_DECLARE_TAG(ts::TsGenerator);       // inherits TsMap -> magic at offset 16
TS_DECLARE_TAG(ts::TsAsyncGenerator);  // inherits TsMap -> magic at offset 16
TS_DECLARE_TAG(ts::TsPromise);         // TsObject subclass -> magic at offset 16
