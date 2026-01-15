#include "TsRuntime.h"
#include "TsObject.h"
#include "TsPromise.h"
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
    
    if (data->callback && data->callback->type == ValueType::OBJECT_PTR && data->callback->ptr_val) {
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
    if (!timerId || timerId->type != ValueType::NUMBER_INT) return;
    
    uv_timer_t* timer = (uv_timer_t*)timerId->i_val;
    if (!timer) return;

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
