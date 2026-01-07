#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include <iostream>
#include <cstdio>
#include <windows.h>

namespace ts {

extern "C" {
    TsValue* Generator_next_internal(void* context, TsValue* value);
    TsValue* AsyncGenerator_next_internal(void* context, TsValue* value);
}

extern void* TsPromise_VTable[];

TsPromise::TsPromise() {
    vtable = TsPromise_VTable;
    state = PromiseState::Pending;
    handled = false;
}

AsyncContext::AsyncContext() {
    state = 0;
    error = false;
    yielded = false;
    promise = ts_promise_create();
    pendingNextPromise = nullptr;
    resumeFn = nullptr;
    data = nullptr;
    resumedValue = nullptr;
    execContext = nullptr;
}

TsValue* create_generator_result(TsValue value, bool done) {
    TsMap* map = TsMap::Create();
    map->Set(TsString::Create("value"), value);
    map->Set(TsString::Create("done"), TsValue(done));
    
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::OBJECT_PTR;
    res->ptr_val = map;
    return res;
}

TsGenerator::TsGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    
    TsValue nextFunc = *ts_value_make_function((void*)Generator_next_internal, this);
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = *ts_value_make_function((void*)[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this);
    this->Set(TsString::Create("[Symbol.iterator]"), iterFunc);
}

TsValue* TsGenerator::next(TsValue* value) {
    if (done) {
        return create_generator_result(TsValue(), true);
    }
    
    ctx->yielded = false;
    ctx->resumedValue = value;
    ctx->resumeFn(ctx);

    if (ctx->yielded) {
        return create_generator_result(ctx->yieldedValue, false);
    } else {
        done = true;
        return create_generator_result(ctx->yieldedValue, true);
    }
}

TsAsyncGenerator::TsAsyncGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    
    TsValue nextFunc = *ts_value_make_function((void*)AsyncGenerator_next_internal, this);
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = *ts_value_make_function((void*)[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this);
    this->Set(TsString::Create("[Symbol.asyncIterator]"), iterFunc);
}

TsPromise* TsAsyncGenerator::next(TsValue* value) {
    TsPromise* p = ts_promise_create();
    if (done) {
        ts_promise_resolve_internal(p, create_generator_result(TsValue(), true));
        return p;
    }
    
    ctx->pendingNextPromise = p;
    ctx->yielded = false;
    ctx->resumedValue = value;
    ctx->resumeFn(ctx);

    return p;
}

extern "C" {

void ts_async_yield(TsValue* value, AsyncContext* ctx) {
    ctx->yielded = true;
    ctx->yieldedValue = *value;
    
    if (ctx->pendingNextPromise) {
        ts_promise_resolve_internal(ctx->pendingNextPromise, create_generator_result(*value, false));
        ctx->pendingNextPromise = nullptr;
    }
}

TsGenerator* ts_generator_create(AsyncContext* ctx) {
    void* mem = ts_alloc(sizeof(TsGenerator));
    return new (mem) TsGenerator(ctx);
}

TsValue* Generator_next_internal(void* context, TsValue* value) {
    TsGenerator* gen = (TsGenerator*)context;
    if (!gen) return nullptr;
    return gen->next(value);
}

TsValue* Generator_next(TsValue* genVal, TsValue* value) {
    return Generator_next_internal(ts_value_get_object(genVal), value);
}

TsAsyncGenerator* ts_async_generator_create(AsyncContext* ctx) {
    void* mem = ts_alloc(sizeof(TsAsyncGenerator));
    TsAsyncGenerator* gen = new (mem) TsAsyncGenerator(ctx);
    ctx->generator = gen;
    return gen;
}

TsValue* AsyncGenerator_next_internal(void* context, TsValue* value) {
    TsAsyncGenerator* gen = (TsAsyncGenerator*)context;
    if (!gen) return nullptr;
    TsPromise* p = gen->next(value);
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = p;
    return res;
}

TsValue* AsyncGenerator_next(TsValue* genVal, TsValue* value) {
    return AsyncGenerator_next_internal(ts_value_get_object(genVal), value);
}

extern "C" TsValue* ts_async_iterator_get(TsValue* iterable) {
    if (!iterable) return nullptr;
    
    if (iterable->type == ValueType::OBJECT_PTR) {
        TsString* key = TsString::Create("[Symbol.asyncIterator]");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterable->ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterable->ptr_val, bucket, &method_type, &method_val);
            if (method_type == (uint8_t)ValueType::OBJECT_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*AsyncIterFunc)(void*);
                return ((AsyncIterFunc)func->funcPtr)(func->context);
            }
        }
    }
    
    return iterable;
}

extern "C" TsValue* ts_async_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) return nullptr;
    
    if (iterator->type == ValueType::OBJECT_PTR) {
        TsString* key = TsString::Create("next");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterator->ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterator->ptr_val, bucket, &method_type, &method_val);
            if (method_type == (uint8_t)ValueType::OBJECT_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*NextFunc)(void*, TsValue*);
                return ((NextFunc)func->funcPtr)(func->context, value);
            }
        }
    }
    
    return AsyncGenerator_next(iterator, value);
}

void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done) {
    if (!ctx->generator) return;
    ctx->generator->done = done;
    if (ctx->pendingNextPromise) {
        TsValue* res = create_generator_result(*value, done);
        ts_promise_resolve_internal(ctx->pendingNextPromise, res);
        ctx->pendingNextPromise = nullptr;
    }
}

void ts_async_resume(AsyncContext* ctx, TsValue* value) {
    // CRITICAL: value might be a stack pointer that becomes invalid after this function returns
    // Allocate a heap copy to ensure it remains valid when the state machine resumes
    TsValue* heapValue = (TsValue*)ts_alloc(sizeof(TsValue));
    *heapValue = *value;
    ctx->resumedValue = heapValue;

    if (ctx->resumeFn) {
        ctx->resumeFn(ctx);
    }
}

AsyncContext* ts_async_context_create() {
    return new (ts_alloc(sizeof(AsyncContext))) AsyncContext();
}

TsPromise* ts_async_get_promise(AsyncContext* ctx) {
    return ctx->promise;
}

TsPromise* ts_promise_create() {
    void* mem = ts_alloc(sizeof(TsPromise));
    return new (mem) TsPromise();
}

struct PromiseResolveTask {
    TsPromise* promise;
    TsValue value;
};

struct PromiseCallbackTask {
    TsPromise* promise;
    TsPromise::Callback cb;
};

void ts_promise_run_callback(TsPromise* promise, TsPromise::Callback& cb, TsValue& value) {
    if (cb.asyncCtx) {
        cb.asyncCtx->error = (promise->state == PromiseState::Rejected);
        ts_async_resume(cb.asyncCtx, &value);
        return;
    }

    if (cb.onFinally.type == ValueType::OBJECT_PTR && cb.onFinally.ptr_val) {
        ts_call_0(&cb.onFinally);
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, &value);
            } else {
                ts_promise_reject_internal(cb.nextPromise, &value);
            }
        }
        return;
    }

    TsValue handler = (promise->state == PromiseState::Fulfilled) ? cb.onFulfilled : cb.onRejected;

    if ((handler.type == ValueType::OBJECT_PTR || handler.type == ValueType::FUNCTION_PTR) && handler.ptr_val) {
        TsValue* result = ts_call_1(&handler, &value);
        if (cb.nextPromise) {
            if (result) {
                // If result is a promise, we should really chain it, but for now we just resolve with it.
                // TODO: Proper promise chaining if result is a TsPromise*
                ts_promise_resolve_internal(cb.nextPromise, result);
            } else {
                TsValue undefined;
                undefined.type = ValueType::UNDEFINED;
                ts_promise_resolve_internal(cb.nextPromise, &undefined);
            }
        }
    } else {
        // No handler, propagate
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, &value);
            } else {
                ts_promise_reject_internal(cb.nextPromise, &value);
            }
        }
    }
}

void ts_promise_settle_microtask(void* data) {
    auto task = static_cast<PromiseResolveTask*>(data);
    TsPromise* promise = task->promise;

    if (promise->state == PromiseState::Rejected && !promise->handled) {
        ts_console_log_value(&promise->value);
    }

    for (auto& cb : promise->callbacks) {
        ts_promise_run_callback(promise, cb, promise->value);
    }
    promise->callbacks.clear();
}

void ts_promise_callback_microtask(void* data) {
    auto task = static_cast<PromiseCallbackTask*>(data);
    ts_promise_run_callback(task->promise, task->cb, task->promise->value);
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value);
void ts_promise_reject_internal(TsPromise* promise, TsValue* reason);

TsValue* ts_promise_resolve_internal_helper(void* context, TsValue* val) {
    ts_promise_resolve_internal((TsPromise*)context, val);
    return nullptr;
}

TsValue* ts_promise_reject_internal_helper(void* context, TsValue* reason) {
    ts_promise_reject_internal((TsPromise*)context, reason);
    return nullptr;
}

// Native function wrappers for Promise constructor (variadic calling convention)
TsValue* ts_promise_resolve_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* value = (argc > 0) ? argv[0] : nullptr;
    ts_promise_resolve_internal((TsPromise*)context, value);
    return nullptr;
}

TsValue* ts_promise_reject_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* reason = (argc > 0) ? argv[0] : nullptr;
    ts_promise_reject_internal((TsPromise*)context, reason);
    return nullptr;
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value) {
    if (!promise) {
        return;
    }
    if (promise->state != PromiseState::Pending) {
        return;
    }

    if (value && value->type == ValueType::PROMISE_PTR) {
        TsPromise* other = (TsPromise*)value->ptr_val;
        TsValue onFulfilled;
        onFulfilled.type = ValueType::OBJECT_PTR;
        TsFunction* f1 = (TsFunction*)ts_alloc(sizeof(TsFunction));
        f1->funcPtr = (void*)ts_promise_resolve_internal_helper;
        f1->context = promise;
        onFulfilled.ptr_val = f1;

        TsValue onRejected;
        onRejected.type = ValueType::OBJECT_PTR;
        TsFunction* f2 = (TsFunction*)ts_alloc(sizeof(TsFunction));
        f2->funcPtr = (void*)ts_promise_reject_internal_helper;
        f2->context = promise;
        onRejected.ptr_val = f2;

        other->then(onFulfilled, onRejected);
        return;
    }

    promise->state = PromiseState::Fulfilled;
    promise->value = value ? *value : TsValue();

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

void ts_promise_reject_internal(TsPromise* promise, TsValue* reason) {
    if (promise->state != PromiseState::Pending) {
        return;
    }
    promise->state = PromiseState::Rejected;
    promise->value = reason ? *reason : TsValue();

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

TsValue* ts_promise_resolve(void* context, TsValue* value) {
    if (value && value->type == ValueType::PROMISE_PTR) {
        return value;
    }
    TsPromise* p = ts_promise_create();
    ts_promise_resolve_internal(p, value);
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = p;
    return res;
}

TsValue* ts_promise_reject(void* context, TsValue* reason) {
    TsPromise* p = ts_promise_create();
    ts_promise_reject_internal(p, reason);
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = p;
    return res;
}

TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected) {
    if (!promise || promise->type != ValueType::PROMISE_PTR) return nullptr;
    TsPromise* p = (TsPromise*)promise->ptr_val;
    TsPromise* next = p->then(onFulfilled ? *onFulfilled : TsValue(), onRejected ? *onRejected : TsValue());
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = next;
    return res;
}

TsValue* ts_promise_catch(TsValue* promise, TsValue* onRejected) {
    if (!promise || promise->type != ValueType::PROMISE_PTR) return nullptr;
    TsPromise* p = (TsPromise*)promise->ptr_val;
    TsPromise* next = p->catch_error(onRejected ? *onRejected : TsValue());
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = next;
    return res;
}

TsValue* ts_promise_finally(TsValue* promise, TsValue* onFinally) {
    if (!promise || promise->type != ValueType::PROMISE_PTR) return nullptr;
    TsPromise* p = (TsPromise*)promise->ptr_val;
    TsPromise* next = p->finally(onFinally ? *onFinally : TsValue());
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = next;
    return res;
}

TsValue* ts_promise_new(TsValue* executor) {
    if (!executor) {
        return nullptr;
    }

    // Create a new promise
    TsPromise* promise = ts_promise_create();

    // Create resolve and reject functions using the variadic wrappers
    TsValue* resolveArg = ts_value_make_native_function(
        (void*)ts_promise_resolve_wrapper,
        promise
    );

    TsValue* rejectArg = ts_value_make_native_function(
        (void*)ts_promise_reject_wrapper,
        promise
    );

    // Call the executor with (resolve, reject)
    if (executor) {
        ts_call_2(executor, resolveArg, rejectArg);
    }

    // Return the promise
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::PROMISE_PTR;
    res->ptr_val = promise;
    return res;
}

TsValue* ts_promise_await(TsValue* promise) {
    if (!promise || promise->type != ValueType::PROMISE_PTR) return promise;
    TsPromise* p = (TsPromise*)promise->ptr_val;
    
    // If already settled, return value immediately
    if (p->state != PromiseState::Pending) {
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        *res = p->value;
        return res;
    }
    
    while (p->state == PromiseState::Pending) {
        ts_run_microtasks();
    }
    
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    *res = p->value;
    return res;
}

void ts_async_await(TsValue* promise, AsyncContext* ctx) {
    if (!promise || promise->type != ValueType::PROMISE_PTR || !promise->ptr_val) {
        ts_async_resume(ctx, promise);
        return;
    }
    TsPromise* p = (TsPromise*)promise->ptr_val;
    p->then_async(ctx);
}

struct PromiseAllContext {
    TsPromise* mainPromise;
    TsArray* results;
    size_t remaining;
};

struct PromiseAllElementContext {
    PromiseAllContext* allCtx;
    size_t index;
};

TsValue* ts_promise_all_fulfilled_helper(void* context, TsValue* val) {
    auto ctx = (PromiseAllElementContext*)context;
    ctx->allCtx->results->Set(ctx->index, (int64_t)val);
    ctx->allCtx->remaining--;
    if (ctx->allCtx->remaining == 0) {
        ts_promise_resolve_internal(ctx->allCtx->mainPromise, ts_value_make_array(ctx->allCtx->results));
    }
    return nullptr;
}

TsValue* ts_promise_all_rejected_helper(void* context, TsValue* reason) {
    auto ctx = (PromiseAllElementContext*)context;
    ts_promise_reject_internal(ctx->allCtx->mainPromise, reason);
    return nullptr;
}

TsValue* ts_promise_all(TsValue* iterableVal) {
    if (!iterableVal || (iterableVal->type != ValueType::OBJECT_PTR && iterableVal->type != ValueType::ARRAY_PTR)) {
        return ts_promise_resolve(nullptr, ts_value_make_array(TsArray::Create(0)));
    }
    TsArray* iterable = (TsArray*)iterableVal->ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();

    if (total == 0) {
        ts_promise_resolve_internal(mainPromise, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(mainPromise);
    }

    PromiseAllContext* allCtx = (PromiseAllContext*)ts_alloc(sizeof(PromiseAllContext));
    allCtx->mainPromise = mainPromise;
    allCtx->results = TsArray::CreateSized(total);
    allCtx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);

        PromiseAllElementContext* elCtx = (PromiseAllElementContext*)ts_alloc(sizeof(PromiseAllElementContext));
        elCtx->allCtx = allCtx;
        elCtx->index = i;

        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_all_fulfilled_helper, elCtx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_all_rejected_helper, elCtx);

        ts_promise_then(p, onFulfilled, onRejected);
    }

    return ts_value_make_promise(mainPromise);
}

TsValue* ts_promise_race_fulfilled_helper(void* context, TsValue* val) {
    TsPromise* mainPromise = (TsPromise*)context;
    ts_promise_resolve_internal(mainPromise, val);
    return nullptr;
}

TsValue* ts_promise_race_rejected_helper(void* context, TsValue* reason) {
    TsPromise* mainPromise = (TsPromise*)context;
    ts_promise_reject_internal(mainPromise, reason);
    return nullptr;
}

TsValue* ts_promise_race(TsValue* iterableVal) {
    if (!iterableVal || (iterableVal->type != ValueType::OBJECT_PTR && iterableVal->type != ValueType::ARRAY_PTR)) {
        ts::TsPromise* p = ts_promise_create();
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterableVal->ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_race_fulfilled_helper, mainPromise);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_race_rejected_helper, mainPromise);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    
    return ts_value_make_promise(mainPromise);
}

struct AllSettledContext {
    TsPromise* mainPromise;
    TsArray* results;
    size_t remaining;
};

struct AllSettledElementContext {
    AllSettledContext* ctx;
    size_t index;
};

TsValue* ts_promise_all_settled_fulfilled_helper(void* context, TsValue* val) {
    AllSettledElementContext* e = (AllSettledElementContext*)context;
    TsMap* obj = TsMap::Create();
    TsValue status;
    status.type = ValueType::STRING_PTR;
    status.ptr_val = ts_string_create("fulfilled");
    obj->Set((TsString*)ts_string_create("status"), status);
    obj->Set((TsString*)ts_string_create("value"), val ? *val : TsValue());
    e->ctx->results->Set(e->index, (int64_t)ts_value_make_object(obj));
    e->ctx->remaining--;
    if (e->ctx->remaining == 0) {
        ts_promise_resolve_internal(e->ctx->mainPromise, ts_value_make_array(e->ctx->results));
    }
    return nullptr;
}

TsValue* ts_promise_all_settled_rejected_helper(void* context, TsValue* reason) {
    AllSettledElementContext* e = (AllSettledElementContext*)context;
    TsMap* obj = TsMap::Create();
    TsValue status;
    status.type = ValueType::STRING_PTR;
    status.ptr_val = ts_string_create("rejected");
    obj->Set((TsString*)ts_string_create("status"), status);
    obj->Set((TsString*)ts_string_create("reason"), reason ? *reason : TsValue());
    e->ctx->results->Set(e->index, (int64_t)ts_value_make_object(obj));
    e->ctx->remaining--;
    if (e->ctx->remaining == 0) {
        ts_promise_resolve_internal(e->ctx->mainPromise, ts_value_make_array(e->ctx->results));
    }
    return nullptr;
}

extern "C" TsValue* ts_promise_allSettled(TsValue* iterableVal) {
    if (!iterableVal || (iterableVal->type != ValueType::OBJECT_PTR && iterableVal->type != ValueType::ARRAY_PTR)) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_resolve_internal(p, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterableVal->ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();
    if (total == 0) {
        ts_promise_resolve_internal(mainPromise, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(mainPromise);
    }

    AllSettledContext* ctx = (AllSettledContext*)ts_alloc(sizeof(AllSettledContext));
    ctx->mainPromise = mainPromise;
    ctx->results = TsArray::CreateSized(total);
    ctx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        AllSettledElementContext* elCtx = (AllSettledElementContext*)ts_alloc(sizeof(AllSettledElementContext));
        elCtx->ctx = ctx;
        elCtx->index = i;

        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_all_settled_fulfilled_helper, elCtx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_all_settled_rejected_helper, elCtx);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    return ts_value_make_promise(mainPromise);
}

struct AnyContext {
    TsPromise* mainPromise;
    TsArray* errors;
    size_t remaining;
};

TsValue* ts_promise_any_fulfilled_helper(void* context, TsValue* val) {
    AnyContext* ctx = (AnyContext*)context;
    ts_promise_resolve_internal(ctx->mainPromise, val);
    return nullptr;
}

TsValue* ts_promise_any_rejected_helper(void* context, TsValue* reason) {
    AnyContext* ctx = (AnyContext*)context;
    ctx->errors->Push((int64_t)reason);
    ctx->remaining--;
    if (ctx->remaining == 0) {
        ts_promise_reject_internal(ctx->mainPromise, ts_value_make_array(ctx->errors));
    }
    return nullptr;
}

extern "C" TsValue* ts_promise_any(TsValue* iterableVal) {
    if (!iterableVal || (iterableVal->type != ValueType::OBJECT_PTR && iterableVal->type != ValueType::ARRAY_PTR)) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_reject_internal(p, iterableVal);
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterableVal->ptr_val;
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();
    if (total == 0) {
        ts_promise_reject_internal(mainPromise, ts_value_make_object(TsArray::Create()));
        return ts_value_make_promise(mainPromise);
    }

    AnyContext* ctx = (AnyContext*)ts_alloc(sizeof(AnyContext));
    ctx->mainPromise = mainPromise;
    ctx->errors = TsArray::Create(total);
    ctx->remaining = total;

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_any_fulfilled_helper, ctx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_any_rejected_helper, ctx);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    return ts_value_make_promise(mainPromise);
}

} // extern "C"

TsPromise* TsPromise::then(TsValue onFulfilled, TsValue onRejected) {
    handled = true;
    Callback cb;
    cb.onFulfilled = onFulfilled;
    cb.onRejected = onRejected;
    cb.onFinally = TsValue();
    cb.nextPromise = ts_promise_create();
    cb.asyncCtx = nullptr;
    
    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;
        
        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
    return cb.nextPromise;
}

TsPromise* TsPromise::catch_error(TsValue onRejected) {
    return then(TsValue(), onRejected);
}

TsPromise* TsPromise::finally(TsValue onFinally) {
    handled = true;
    Callback cb;
    cb.onFulfilled = TsValue();
    cb.onRejected = TsValue();
    cb.onFinally = onFinally;
    cb.nextPromise = ts_promise_create();
    cb.asyncCtx = nullptr;

    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;
        
        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
    return cb.nextPromise;
}

void TsPromise::then_async(AsyncContext* asyncCtx) {
    handled = true;
    Callback cb;
    cb.onFulfilled = TsValue();
    cb.onRejected = TsValue();
    cb.onFinally = TsValue();
    cb.nextPromise = nullptr;
    cb.asyncCtx = asyncCtx;

    if (state != PromiseState::Pending) {
        auto task = static_cast<PromiseCallbackTask*>(ts_alloc(sizeof(PromiseCallbackTask)));
        task->promise = this;
        task->cb = cb;

        ts_queue_microtask(ts_promise_callback_microtask, task);
    } else {
        callbacks.push_back(cb);
    }
}

static TsValue* ts_promise_then_wrapper(void* context, TsValue* onFulfilled, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->then(onFulfilled ? *onFulfilled : TsValue(), onRejected ? *onRejected : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_catch_wrapper(void* context, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->catch_error(onRejected ? *onRejected : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_finally_wrapper(void* context, TsValue* onFinally) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->finally(onFinally ? *onFinally : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();
    
    if (strcmp(name, "then") == 0) {
        return ts_value_make_function((void*)ts_promise_then_wrapper, obj);
    } else if (strcmp(name, "catch") == 0) {
        return ts_value_make_function((void*)ts_promise_catch_wrapper, obj);
    } else if (strcmp(name, "finally") == 0) {
        return ts_value_make_function((void*)ts_promise_finally_wrapper, obj);
    }
    return ts_value_make_undefined();
}

void* TsPromise_VTable[] = {
    nullptr,
    (void*)ts_promise_get_property
};

} // namespace ts
