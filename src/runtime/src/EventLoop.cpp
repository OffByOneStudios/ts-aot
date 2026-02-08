#include "TsRuntime.h"
#include "TsObject.h"
#include "TsPromise.h"
#include "TsMap.h"
#include "GC.h"
#include <uv.h>
#include <vector>
#include <functional>

using namespace ts;

struct TimerData {
    TsValue* callback;
    uv_timer_t* timer;
    bool isInterval;
};

// For timers/promises - stores promise to resolve
struct PromiseTimerData {
    TsPromise* promise;
    uv_timer_t* timer;
    TsValue* resolveValue;  // Optional value to resolve with
};

static void on_timer_close(uv_handle_t* handle) {
    TimerData* data = (TimerData*)handle->data;
    delete data;
    free(handle);
}

static void on_timer_callback(uv_timer_t* handle) {
    TimerData* data = (TimerData*)handle->data;

    // Call the callback - ts_call_0 handles TsValue*, TsClosure*, and TsFunction*
    if (data->callback) {
        ts_call_0(data->callback);
    }

    if (!data->isInterval) {
        uv_timer_stop(handle);
        uv_close((uv_handle_t*)handle, on_timer_close);
    }
}

extern "C" TsValue* ts_set_timeout(TsValue* callback, int64_t delay) {
    uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    uv_timer_init(uv_default_loop(), timer);

    TimerData* data = new TimerData();
    data->callback = callback;
    data->timer = timer;
    data->isInterval = false;

    timer->data = data;
    uv_timer_start(timer, on_timer_callback, delay, 0);

    return ts_value_make_int((int64_t)timer);
}

extern "C" TsValue* ts_set_interval(TsValue* callback, int64_t delay) {
    uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    uv_timer_init(uv_default_loop(), timer);
    
    TimerData* data = new TimerData();
    data->callback = callback;
    data->timer = timer;
    data->isInterval = true;
    
    timer->data = data;
    uv_timer_start(timer, on_timer_callback, delay, delay);
    
    return ts_value_make_int((int64_t)timer);
}

extern "C" void ts_clear_timer(TsValue* timerId) {
    if (!timerId) return;

    // Handle different value types - timer ID can come as int or boxed pointer
    int64_t timerAddr = 0;
    if (timerId->type == ValueType::NUMBER_INT) {
        timerAddr = timerId->i_val;
    } else if (timerId->type == ValueType::OBJECT_PTR && timerId->ptr_val) {
        // Value might be boxed - try to extract int from nested TsValue
        TsValue* inner = (TsValue*)timerId->ptr_val;
        if (inner->type == ValueType::NUMBER_INT) {
            timerAddr = inner->i_val;
        }
    }

    if (timerAddr == 0) return;

    uv_timer_t* timer = (uv_timer_t*)timerAddr;
    uv_timer_stop(timer);
    uv_close((uv_handle_t*)timer, on_timer_close);
}

extern "C" TsValue* ts_set_immediate(TsValue* callback) {
    // Use a zero-timeout timer instead of uv_check_t
    // uv_check callbacks only fire after I/O poll, which blocks if there's no I/O
    uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    uv_timer_init(uv_default_loop(), timer);
    
    TimerData* data = new TimerData();
    data->callback = callback;
    data->timer = timer;
    data->isInterval = false;
    
    timer->data = data;
    uv_timer_start(timer, on_timer_callback, 0, 0);  // Zero delay = immediate
    
    return ts_value_make_int((int64_t)timer);
}

extern "C" void ts_process_next_tick(TsValue* callback) {
    ts_queue_microtask([](void* data) {
        ts_call_0((TsValue*)data);
    }, callback);
}

#include <windows.h>

static std::vector<std::function<void()>> microtasks;

extern "C" void ts_queue_microtask(void (*callback)(void*), void* data) {
    microtasks.push_back([callback, data]() { callback(data); });
}

void ts_run_microtasks() {
    while (!microtasks.empty()) {
        std::vector<std::function<void()>> current = std::move(microtasks);
        microtasks.clear();
        for (auto& task : current) {
            task();
        }
    }
}

extern "C" void ts_loop_run() {
    uv_loop_t* loop = uv_default_loop();

    while (true) {
        ts_run_microtasks();

        bool alive = uv_loop_alive(loop);
        if (alive) {
            uv_run(loop, UV_RUN_ONCE);
        } else {
            break;
        }
    }
}

extern "C" void ts_loop_init() {
    // uv_default_loop() initializes itself on first call
}

// ============================================================================
// timers/promises API - Promise-based timer functions
// ============================================================================

static void on_promise_timer_close(uv_handle_t* handle) {
    PromiseTimerData* data = (PromiseTimerData*)handle->data;
    delete data;
    free(handle);
}

static void on_promise_timer_callback(uv_timer_t* handle) {
    PromiseTimerData* data = (PromiseTimerData*)handle->data;

    if (data->promise) {
        // Resolve the promise with the provided value (or undefined)
        if (data->resolveValue) {
            ts_promise_resolve_internal(data->promise, data->resolveValue);
        } else {
            TsValue undefinedVal;
            undefinedVal.type = ValueType::UNDEFINED;
            ts_promise_resolve_internal(data->promise, &undefinedVal);
        }
    }

    uv_timer_stop(handle);
    uv_close((uv_handle_t*)handle, on_promise_timer_close);
}

// timers/promises.setTimeout(delay, value?) -> Promise<value>
extern "C" TsValue* ts_timers_promises_setTimeout(int64_t delay, TsValue* value) {
    TsPromise* promise = ts_promise_create();

    uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    uv_timer_init(uv_default_loop(), timer);

    PromiseTimerData* data = new PromiseTimerData();
    data->promise = promise;
    data->timer = timer;
    data->resolveValue = value;

    timer->data = data;
    uv_timer_start(timer, on_promise_timer_callback, delay, 0);

    return ts_value_make_promise(promise);  // Use PROMISE_PTR type for await
}

// timers/promises.setImmediate(value?) -> Promise<value>
extern "C" TsValue* ts_timers_promises_setImmediate(TsValue* value) {
    TsPromise* promise = ts_promise_create();

    uv_timer_t* timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
    uv_timer_init(uv_default_loop(), timer);

    PromiseTimerData* data = new PromiseTimerData();
    data->promise = promise;
    data->timer = timer;
    data->resolveValue = value;

    timer->data = data;
    uv_timer_start(timer, on_promise_timer_callback, 0, 0);  // Zero delay = immediate

    return ts_value_make_promise(promise);  // Use PROMISE_PTR type for await
}

// ============================================================================
// timers/promises.setInterval - Returns an AsyncIterable
// ============================================================================

// IntervalState - holds the mutable state for an interval iterator
// This is stored separately from the TsMap to avoid inheritance issues
struct IntervalState {
    int64_t delay;
    TsValue* resolveValue;
    bool stopped;
    uv_timer_t* timer;
    TsPromise* pendingPromise;

    IntervalState(int64_t delayMs, TsValue* value)
        : delay(delayMs), resolveValue(value), stopped(false),
          timer(nullptr), pendingPromise(nullptr) {}

    void stop() {
        stopped = true;
        if (timer) {
            uv_timer_stop(timer);
            uv_close((uv_handle_t*)timer, [](uv_handle_t* handle) {
                free(handle);
            });
            timer = nullptr;
        }
        // If there's a pending promise, resolve it as done
        if (pendingPromise) {
            TsMap* result = TsMap::Create();
            TsValue undefinedVal;
            undefinedVal.type = ValueType::UNDEFINED;
            TsValue k1, k2;
            k1.type = ValueType::STRING_PTR;
            k1.ptr_val = TsString::Create("value");
            k2.type = ValueType::STRING_PTR;
            k2.ptr_val = TsString::Create("done");
            result->Set(k1, undefinedVal);
            TsValue doneVal;
            doneVal.type = ValueType::BOOLEAN;
            doneVal.b_val = true;
            result->Set(k2, doneVal);
            TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
            res->type = ValueType::OBJECT_PTR;
            res->ptr_val = result;
            ts_promise_resolve_internal(pendingPromise, res);
            pendingPromise = nullptr;
        }
    }
};

static void on_interval_timer_callback(uv_timer_t* handle) {
    IntervalState* state = (IntervalState*)handle->data;
    if (!state || state->stopped) return;

    if (state->pendingPromise) {
        // Create the result { value: <value>, done: false }
        TsMap* result = TsMap::Create();
        TsValue k1, k2;
        k1.type = ValueType::STRING_PTR;
        k1.ptr_val = TsString::Create("value");
        k2.type = ValueType::STRING_PTR;
        k2.ptr_val = TsString::Create("done");

        if (state->resolveValue) {
            result->Set(k1, *state->resolveValue);
        } else {
            TsValue undefinedVal;
            undefinedVal.type = ValueType::UNDEFINED;
            result->Set(k1, undefinedVal);
        }
        TsValue doneVal;
        doneVal.type = ValueType::BOOLEAN;
        doneVal.b_val = false;
        result->Set(k2, doneVal);

        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        res->type = ValueType::OBJECT_PTR;
        res->ptr_val = result;

        ts_promise_resolve_internal(state->pendingPromise, res);
        state->pendingPromise = nullptr;
    }

    // Stop the timer after each iteration - next() will restart it
    uv_timer_stop(handle);
}

// The next() function for interval iterator
// NOTE: For ts_call_0, COMPILED functions expect TsValue* (*)(void*)
static TsValue* IntervalIterator_next_internal(void* context) {
    IntervalState* state = (IntervalState*)context;
    if (!state) {
        return ts_value_make_undefined();
    }

    TsPromise* promise = ts_promise_create();

    if (state->stopped) {
        // Already stopped - return done
        TsMap* result = TsMap::Create();
        TsValue k1, k2;
        k1.type = ValueType::STRING_PTR;
        k1.ptr_val = TsString::Create("value");
        k2.type = ValueType::STRING_PTR;
        k2.ptr_val = TsString::Create("done");
        TsValue undefinedVal;
        undefinedVal.type = ValueType::UNDEFINED;
        result->Set(k1, undefinedVal);
        TsValue doneVal;
        doneVal.type = ValueType::BOOLEAN;
        doneVal.b_val = true;
        result->Set(k2, doneVal);
        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        res->type = ValueType::OBJECT_PTR;
        res->ptr_val = result;
        ts_promise_resolve_internal(promise, res);
        return ts_value_make_promise(promise);
    }

    state->pendingPromise = promise;

    // Create or restart the timer
    if (!state->timer) {
        state->timer = (uv_timer_t*)malloc(sizeof(uv_timer_t));
        uv_timer_init(uv_default_loop(), state->timer);
        state->timer->data = state;
    }

    // Start the timer with the interval delay
    uv_timer_start(state->timer, on_interval_timer_callback, state->delay, 0);

    return ts_value_make_promise(promise);
}

// The return() function for interval iterator (for breaking out of for await)
// NOTE: For ts_call_0, COMPILED functions expect TsValue* (*)(void*)
static TsValue* IntervalIterator_return_internal(void* context) {
    IntervalState* state = (IntervalState*)context;
    if (!state) return ts_value_make_undefined();

    state->stop();

    TsPromise* promise = ts_promise_create();
    TsMap* result = TsMap::Create();
    TsValue k1, k2;
    k1.type = ValueType::STRING_PTR;
    k1.ptr_val = TsString::Create("value");
    k2.type = ValueType::STRING_PTR;
    k2.ptr_val = TsString::Create("done");
    TsValue undefinedVal;
    undefinedVal.type = ValueType::UNDEFINED;
    result->Set(k1, undefinedVal);
    TsValue doneVal;
    doneVal.type = ValueType::BOOLEAN;
    doneVal.b_val = true;
    result->Set(k2, doneVal);
    TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
    res->type = ValueType::OBJECT_PTR;
    res->ptr_val = result;
    ts_promise_resolve_internal(promise, res);
    return ts_value_make_promise(promise);
}

// timers/promises.setInterval(delay, value?) -> AsyncIterable
extern "C" TsValue* ts_timers_promises_setInterval(int64_t delay, TsValue* value) {
    // Create the interval state - holds mutable state for the interval
    void* stateMem = ts_alloc(sizeof(IntervalState));
    IntervalState* state = new (stateMem) IntervalState(delay, value);

    // Create a TsMap to hold the iterator methods
    TsMap* iteratorMap = TsMap::Create();

    // Set up the next() method - context is the IntervalState
    TsValue* nextFunc = ts_value_make_function((void*)IntervalIterator_next_internal, state);
    TsValue nextKey;
    nextKey.type = ValueType::STRING_PTR;
    nextKey.ptr_val = TsString::Create("next");
    iteratorMap->Set(nextKey, *nextFunc);

    // Set up the return() method (for early termination)
    TsValue* returnFunc = ts_value_make_function((void*)IntervalIterator_return_internal, state);
    TsValue returnKey;
    returnKey.type = ValueType::STRING_PTR;
    returnKey.ptr_val = TsString::Create("return");
    iteratorMap->Set(returnKey, *returnFunc);

    // Set up [Symbol.asyncIterator] to return itself
    // NOTE: For ts_call_0, COMPILED functions expect TsValue* (*)(void*)
    TsValue* iterFunc = ts_value_make_function((void*)[](void* ctx) -> TsValue* {
        return ts_value_make_object(ctx);
    }, iteratorMap);
    TsValue iterKey;
    iterKey.type = ValueType::STRING_PTR;
    iterKey.ptr_val = TsString::Create("[Symbol.asyncIterator]");
    iteratorMap->Set(iterKey, *iterFunc);

    return ts_value_make_object(iteratorMap);
}

// ============================================================================
// timers/promises.scheduler - Scheduler API
// ============================================================================

// scheduler.wait(delay, options?) -> Promise<void>
// Similar to setTimeout but more explicit about scheduling semantics
extern "C" TsValue* ts_timers_scheduler_wait(int64_t delay) {
    return ts_timers_promises_setTimeout(delay, nullptr);
}

// scheduler.yield() -> Promise<void>
// Yields to the event loop, similar to setImmediate but scheduler-aware
extern "C" TsValue* ts_timers_scheduler_yield() {
    return ts_timers_promises_setImmediate(nullptr);
}
