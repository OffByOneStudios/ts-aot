#include "TsRuntime.h"
#include "TsObject.h"
#include "TsPromise.h"
#include "TsMap.h"
#include "GC.h"
#include "TsGC.h"
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

// ── GC rooting for pending timers ──────────────────────────────────────────
// TimerData/PromiseTimerData are malloc'd and stashed in libuv handle->data,
// so their GC pointers (the boxed callback / promise / resolve value) are
// INVISIBLE to the collector. A GC between scheduling a timer and its callback
// firing would move/collect the callback, leaving data->callback stale ->
// ts_call_0(stale) invokes a moved/freed closure -> stale captured key ->
// crash in find_slot (seen in lodash's qunit setTimeout-driven test harness via
// uv__run_timers). Same unscanned-container class as the microtask queue. Fix:
// a registry of live timers + a GC mark-scanner (keep callbacks alive) and a
// minor-GC fixup (forward the field after promotion). See gc-unscanned-cpp-
// containers memory.
static std::vector<TimerData*> g_live_timers;
static std::vector<PromiseTimerData*> g_live_promise_timers;
static bool g_timers_gc_registered = false;

static inline bool timer_ptr_is_heap(void* p) {
    uintptr_t v = (uintptr_t)p;
    return v >= 4096 && v <= 0x00007FFFFFFFFFFFULL;
}

static void timers_gc_scan(void* /*ctx*/) {
    for (TimerData* t : g_live_timers) {
        if (t && timer_ptr_is_heap(t->callback)) ts_gc_mark_object(t->callback);
    }
    for (PromiseTimerData* t : g_live_promise_timers) {
        if (!t) continue;
        if (timer_ptr_is_heap(t->promise)) ts_gc_mark_object(t->promise);
        if (timer_ptr_is_heap(t->resolveValue)) ts_gc_mark_object(t->resolveValue);
    }
}

static void timers_gc_fixup(void* /*ctx*/) {
    for (TimerData* t : g_live_timers) {
        if (t && timer_ptr_is_heap(t->callback)) {
            void* fwd = ts_gc_minor_lookup_forward(t->callback);
            if (fwd) t->callback = (TsValue*)fwd;
        }
    }
    for (PromiseTimerData* t : g_live_promise_timers) {
        if (!t) continue;
        if (timer_ptr_is_heap(t->promise)) {
            void* fwd = ts_gc_minor_lookup_forward(t->promise);
            if (fwd) t->promise = (TsPromise*)fwd;
        }
        if (timer_ptr_is_heap(t->resolveValue)) {
            void* fwd = ts_gc_minor_lookup_forward(t->resolveValue);
            if (fwd) t->resolveValue = (TsValue*)fwd;
        }
    }
}

static void timers_gc_ensure_registered() {
    if (g_timers_gc_registered) return;
    g_timers_gc_registered = true;
    ts_gc_register_scanner(timers_gc_scan, nullptr);
    ts_gc_register_minor_fixup(timers_gc_fixup, nullptr);
}

static void timers_track(TimerData* t) {
    timers_gc_ensure_registered();
    g_live_timers.push_back(t);
}
static void timers_untrack(TimerData* t) {
    for (size_t i = 0; i < g_live_timers.size(); i++) {
        if (g_live_timers[i] == t) {
            g_live_timers[i] = g_live_timers.back();
            g_live_timers.pop_back();
            return;
        }
    }
}
static void promise_timers_track(PromiseTimerData* t) {
    timers_gc_ensure_registered();
    g_live_promise_timers.push_back(t);
}
static void promise_timers_untrack(PromiseTimerData* t) {
    for (size_t i = 0; i < g_live_promise_timers.size(); i++) {
        if (g_live_promise_timers[i] == t) {
            g_live_promise_timers[i] = g_live_promise_timers.back();
            g_live_promise_timers.pop_back();
            return;
        }
    }
}

static void on_timer_close(uv_handle_t* handle) {
    TimerData* data = (TimerData*)handle->data;
    timers_untrack(data);
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
    timers_track(data);
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
    timers_track(data);
    uv_timer_start(timer, on_timer_callback, delay, delay);
    
    return ts_value_make_int((int64_t)timer);
}

extern "C" void ts_clear_timer(TsValue* timerId) {
    if (!timerId) return;

    // Timer ID is a NaN-boxed integer (the raw timer address)
    int64_t timerAddr = ts_value_get_int(timerId);
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
    timers_track(data);
    uv_timer_start(timer, on_timer_callback, 0, 0);  // Zero delay = immediate
    
    return ts_value_make_int((int64_t)timer);
}

extern "C" void ts_process_next_tick(TsValue* callback) {
    ts_queue_microtask([](void* data) {
        ts_call_0((TsValue*)data);
    }, callback);
}

// Microtask queue. CRITICAL: the `data` pointer is almost always a GC-allocated
// object (PromiseResolveTask / PromiseCallbackTask holding a TsPromise + value,
// or a callback closure). The queue must therefore be a GC ROOT SOURCE — a GC
// running between enqueue and drain would otherwise collect or move the task,
// leaving a dangling/stale `data` and crashing ts_promise_settle_microtask.
// Previously this stored type-erased std::function lambdas in a malloc'd vector
// the collector never scanned -> nondeterministic use-after-free under GC
// pressure (the lodash-harness crashes). Now we store explicit (cb, data)
// entries and register a GC mark-scanner + minor-GC fixup over them.
struct MicrotaskEntry { void (*cb)(void*); void* data; };
static std::vector<MicrotaskEntry> microtasks;
static size_t microtask_drain_pos = 0;   // entries [0, pos) already executed
static bool microtasks_gc_registered = false;

static inline bool microtask_data_is_heap_ptr(void* p) {
    uintptr_t v = (uintptr_t)p;
    return v >= 4096 && v <= 0x00007FFFFFFFFFFFULL;
}

// Mark scanner: keep every pending (and in-flight) task's `data` alive. Marking
// the task object transitively keeps its promise/value/callback alive via the
// normal field trace. Run entries are harmless to mark (over-approximation).
static void microtasks_gc_scan(void* /*ctx*/) {
    for (auto& e : microtasks) {
        if (microtask_data_is_heap_ptr(e.data)) ts_gc_mark_object(e.data);
    }
}

// Minor-GC fixup: after promotion, rewrite each `data` to its new old-gen
// address so the queued pointer stays valid.
static void microtasks_gc_fixup(void* /*ctx*/) {
    for (auto& e : microtasks) {
        if (microtask_data_is_heap_ptr(e.data)) {
            void* fwd = ts_gc_minor_lookup_forward(e.data);
            if (fwd) e.data = fwd;
        }
    }
}

extern "C" void ts_queue_microtask(void (*callback)(void*), void* data) {
    if (!microtasks_gc_registered) {
        microtasks_gc_registered = true;
        ts_gc_register_scanner(microtasks_gc_scan, nullptr);
        ts_gc_register_minor_fixup(microtasks_gc_fixup, nullptr);
    }
    microtasks.push_back({ callback, data });
}

void ts_run_microtasks() {
    // Index-based drain so the global `microtasks` vector remains the live,
    // GC-scanned store for the whole drain (callbacks may enqueue more, and a
    // GC may run inside any callback). Entries stay in the vector until the
    // drain finishes, so the scanner/fixup always see pending entries.
    while (microtask_drain_pos < microtasks.size()) {
        MicrotaskEntry e = microtasks[microtask_drain_pos++];
        e.cb(e.data);
    }
    microtasks.clear();
    microtask_drain_pos = 0;
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
    promise_timers_untrack(data);
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
            ts_promise_resolve_internal(data->promise, ts_value_make_undefined());
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
    promise_timers_track(data);
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
    promise_timers_track(data);
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
            ts_promise_resolve_internal(pendingPromise, ts_value_make_object(result));
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
            result->Set(k1, nanbox_to_tagged(state->resolveValue));
        } else {
            TsValue undefinedVal;
            undefinedVal.type = ValueType::UNDEFINED;
            result->Set(k1, undefinedVal);
        }
        TsValue doneVal;
        doneVal.type = ValueType::BOOLEAN;
        doneVal.b_val = false;
        result->Set(k2, doneVal);

        ts_promise_resolve_internal(state->pendingPromise, ts_value_make_object(result));
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
        ts_promise_resolve_internal(promise, ts_value_make_object(result));
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
    ts_promise_resolve_internal(promise, ts_value_make_object(result));
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
    iteratorMap->Set(nextKey, nanbox_to_tagged(nextFunc));

    // Set up the return() method (for early termination)
    TsValue* returnFunc = ts_value_make_function((void*)IntervalIterator_return_internal, state);
    TsValue returnKey;
    returnKey.type = ValueType::STRING_PTR;
    returnKey.ptr_val = TsString::Create("return");
    iteratorMap->Set(returnKey, nanbox_to_tagged(returnFunc));

    // Set up [Symbol.asyncIterator] to return itself
    // NOTE: For ts_call_0, COMPILED functions expect TsValue* (*)(void*)
    static auto iterLambda = [](void* ctx) -> TsValue* {
        return ts_value_make_object(ctx);
    };
    TsValue* iterFunc = ts_value_make_function((void*)(TsValue*(*)(void*))iterLambda, iteratorMap);
    TsValue iterKey;
    iterKey.type = ValueType::STRING_PTR;
    iterKey.ptr_val = TsString::Create("[Symbol.asyncIterator]");
    iteratorMap->Set(iterKey, nanbox_to_tagged(iterFunc));

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
