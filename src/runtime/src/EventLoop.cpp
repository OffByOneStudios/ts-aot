#include "TsRuntime.h"
#include "TsObject.h"
#include <uv.h>
#include <vector>
#include <functional>

struct TimerData {
    TsValue* callback;
    uv_timer_t* timer;
    bool isInterval;
};

static void on_timer_close(uv_handle_t* handle) {
    TimerData* data = (TimerData*)handle->data;
    delete data;
    free(handle);
}

static void on_timer_callback(uv_timer_t* handle) {
    TimerData* data = (TimerData*)handle->data;
    
    if (data->callback && data->callback->type == ValueType::OBJECT_PTR && data->callback->ptr_val) {
        typedef void* (*TsFunction)();
        TsFunction func = (TsFunction)data->callback->ptr_val;
        func();
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
        
        if (uv_loop_alive(loop)) {
            uv_run(loop, UV_RUN_ONCE);
        } else {
            break;
        }
    }
}

extern "C" void ts_loop_init() {
    // uv_default_loop() initializes itself on first call
}
