#include "TsPromise.h"
#include "TsRuntime.h"
#include <iostream>

namespace ts {

TsPromise::TsPromise() {
    state = PromiseState::Pending;
}

extern "C" {

TsPromise* ts_promise_create() {
    void* mem = ts_alloc(sizeof(TsPromise));
    return new (mem) TsPromise();
}

struct PromiseResolveTask {
    TsPromise* promise;
    TsValue value;
};

typedef TsValue* (*CallbackFn)(TsValue*);

void ts_promise_resolve_microtask(void* data) {
    auto task = static_cast<PromiseResolveTask*>(data);
    TsPromise* promise = task->promise;
    TsValue value = task->value;

    if (promise->state != PromiseState::Pending) return;

    promise->state = PromiseState::Fulfilled;
    promise->value = value;

    for (auto& cb : promise->callbacks) {
        if (cb.onFulfilled.type == ValueType::OBJECT_PTR && cb.onFulfilled.ptr_val) {
            CallbackFn fn = (CallbackFn)cb.onFulfilled.ptr_val;
            TsValue* arg = (TsValue*)ts_alloc(sizeof(TsValue));
            *arg = value;
            TsValue* result = fn(arg);
            if (cb.nextPromise && result) {
                ts_promise_resolve_internal(cb.nextPromise, *result);
            } else if (cb.nextPromise) {
                TsValue undefined;
                undefined.type = ValueType::UNDEFINED;
                ts_promise_resolve_internal(cb.nextPromise, undefined);
            }
        }
    }
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue value) {
    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = value;
    ts_queue_microtask(ts_promise_resolve_microtask, task);
}

void ts_promise_reject_internal(TsPromise* promise, TsValue reason) {
    // Similar to resolve but for rejection
    if (promise->state != PromiseState::Pending) return;
    promise->state = PromiseState::Rejected;
    promise->value = reason;
}

TsValue* ts_promise_resolve(TsValue* value) {
    TsPromise* p = ts_promise_create();
    ts_promise_resolve_internal(p, *value);
    return (TsValue*)p;
}

TsValue* ts_promise_then(TsValue* promise, TsValue* callback) {
    TsPromise* p = (TsPromise*)promise;
    p->then(*callback);
    return promise;
}

TsValue* ts_promise_await(TsValue* promise) {
    TsPromise* p = (TsPromise*)promise;
    
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

} // extern "C"

void TsPromise::then(TsValue onFulfilled, TsValue onRejected) {
    Callback cb;
    cb.onFulfilled = onFulfilled;
    cb.onRejected = onRejected;
    cb.nextPromise = ts_promise_create();
    
    if (state != PromiseState::Pending) {
        struct CallbackTask {
            TsPromise* promise;
            Callback cb;
        };
        auto task = static_cast<CallbackTask*>(ts_alloc(sizeof(CallbackTask)));
        task->promise = this;
        task->cb = cb;
        
        ts_queue_microtask([](void* data) {
            auto t = static_cast<CallbackTask*>(data);
            if (t->promise->state == PromiseState::Fulfilled) {
                if (t->cb.onFulfilled.type == ValueType::OBJECT_PTR && t->cb.onFulfilled.ptr_val) {
                    CallbackFn fn = (CallbackFn)t->cb.onFulfilled.ptr_val;
                    TsValue* arg = (TsValue*)ts_alloc(sizeof(TsValue));
                    *arg = t->promise->value;
                    TsValue* result = fn(arg);
                    if (t->cb.nextPromise && result) {
                        ts_promise_resolve_internal(t->cb.nextPromise, *result);
                    } else if (t->cb.nextPromise) {
                        TsValue undefined;
                        undefined.type = ValueType::UNDEFINED;
                        ts_promise_resolve_internal(t->cb.nextPromise, undefined);
                    }
                }
            }
        }, task);
    } else {
        callbacks.push_back(cb);
    }
}

} // namespace ts
