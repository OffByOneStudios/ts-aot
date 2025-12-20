#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include <iostream>
#include <cstdio>

namespace ts {

TsPromise::TsPromise() {
    state = PromiseState::Pending;
    handled = false;
}

AsyncContext::AsyncContext() {
    state = 0;
    promise = ts_promise_create();
    resumeFn = nullptr;
}

extern "C" {

void ts_async_resume(AsyncContext* ctx, TsValue* value) {
    if (ctx->resumeFn) {
        ctx->resumeFn(ctx, value);
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
        ts_async_resume(cb.asyncCtx, &value);
        return;
    }

    if (cb.onFinally.type == ValueType::OBJECT_PTR && cb.onFinally.ptr_val) {
        TsFunction* tsFunc = (TsFunction*)cb.onFinally.ptr_val;
        TsFunctionPtrNoArgs fn = (TsFunctionPtrNoArgs)tsFunc->funcPtr;
        fn(tsFunc->context); // Call with context
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
    
    if (handler.type == ValueType::OBJECT_PTR && handler.ptr_val) {
        TsFunction* tsFunc = (TsFunction*)handler.ptr_val;
        TsFunctionPtr fn = (TsFunctionPtr)tsFunc->funcPtr;
        TsValue* arg = (TsValue*)ts_alloc(sizeof(TsValue));
        *arg = value;
        TsValue* result = fn(tsFunc->context, arg);
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
        printf("Unhandled Promise Rejection: ");
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

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value) {
    if (promise->state != PromiseState::Pending) return;

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
    if (promise->state != PromiseState::Pending) return;
    promise->state = PromiseState::Rejected;
    promise->value = reason ? *reason : TsValue();

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

TsValue* ts_promise_resolve(TsValue* value) {
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

TsValue* ts_promise_reject(TsValue* reason) {
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
        if (promise) printf("ts_async_await: not a promise, type=%d, ptr=%p\n", (int)promise->type, promise->ptr_val);
        else printf("ts_async_await: null promise\n");
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
        return ts_promise_resolve(ts_value_make_array(TsArray::Create(0)));
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
        TsValue* p = ts_promise_resolve(item);
        
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
        TsValue* p = ts_promise_resolve(item);
        
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
        TsValue* p = ts_promise_resolve(item);
        
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
        TsValue* p = ts_promise_resolve(item);
        
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

} // namespace ts
