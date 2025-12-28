#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsPromise.h"
#include "TsRuntime.h"
#include <gc/gc.h>
#include <new>

using namespace ts;

TsEventEmitter::TsEventEmitter() {
    magic = MAGIC;
    listeners = TsMap::Create();
}

TsEventEmitter::~TsEventEmitter() {
}

struct OnceContext {
    TsEventEmitter* emitter;
    TsString* event;
    TsValue* originalCallback;
};

TsValue* once_wrapper_func(void* context, int argc, TsValue** argv) {
    OnceContext* ctx = (OnceContext*)context;
    ctx->emitter->RemoveListener(ctx->event->ToUtf8(), ctx->originalCallback);
    return ts_function_call(ctx->originalCallback, argc, argv);
}

void TsEventEmitter::On(const char* event, void* callback) {
    // Emit 'newListener' BEFORE adding the listener
    if (strcmp(event, "newListener") != 0 && strcmp(event, "removeListener") != 0) {
        TsValue eventNameVal = TsValue(TsString::Create(event));
        TsValue* args[2] = { &eventNameVal, (TsValue*)callback };
        this->Emit("newListener", 2, (void**)args);
    }

    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    TsArray* arr;
    if (listenersVal.type == ValueType::UNDEFINED) {
        arr = TsArray::Create();
        this->listeners->Set(eventVal, TsValue(arr));
    } else {
        arr = (TsArray*)listenersVal.ptr_val;
    }
    arr->Push((int64_t)callback);

    if (maxListeners > 0 && arr->Length() > maxListeners) {
        char buf[256];
        snprintf(buf, sizeof(buf), "MaxListenersExceededWarning: Possible EventEmitter memory leak detected. %d %s listeners added. Use emitter.setMaxListeners() to increase limit", (int)arr->Length(), event);
        ts_console_log(TsString::Create(buf));
    }
}

void TsEventEmitter::Once(const char* event, void* callback) {
    OnceContext* ctx = (OnceContext*)ts_alloc(sizeof(OnceContext));
    ctx->emitter = this;
    ctx->event = TsString::Create(event);
    ctx->originalCallback = (TsValue*)callback;

    TsValue* wrapper = ts_value_make_native_function((void*)once_wrapper_func, ctx);
    this->On(event, wrapper);
}

void TsEventEmitter::PrependOnceListener(const char* event, void* callback) {
    OnceContext* ctx = (OnceContext*)ts_alloc(sizeof(OnceContext));
    ctx->emitter = this;
    ctx->event = TsString::Create(event);
    ctx->originalCallback = (TsValue*)callback;

    TsValue* wrapper = ts_value_make_native_function((void*)once_wrapper_func, ctx);
    this->PrependListener(event, wrapper);
}

void TsEventEmitter::PrependListener(const char* event, void* callback) {
    // Emit 'newListener' BEFORE adding the listener
    if (strcmp(event, "newListener") != 0 && strcmp(event, "removeListener") != 0) {
        TsValue eventNameVal = TsValue(TsString::Create(event));
        TsValue* args[2] = { &eventNameVal, (TsValue*)callback };
        this->Emit("newListener", 2, (void**)args);
    }

    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    TsArray* arr;
    if (listenersVal.type == ValueType::UNDEFINED) {
        arr = TsArray::Create();
        this->listeners->Set(eventVal, TsValue(arr));
    } else {
        arr = (TsArray*)listenersVal.ptr_val;
    }
    arr->Unshift((int64_t)callback);

    if (maxListeners > 0 && arr->Length() > maxListeners) {
        char buf[256];
        snprintf(buf, sizeof(buf), "MaxListenersExceededWarning: Possible EventEmitter memory leak detected. %d %s listeners added. Use emitter.setMaxListeners() to increase limit", (int)arr->Length(), event);
        ts_console_log(TsString::Create(buf));
    }
}

void TsEventEmitter::RemoveListener(const char* event, void* callback) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        return;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    TsValue* target = (TsValue*)callback;
    
    void* targetPtr = nullptr;
    void* targetCtx = nullptr;
    if (target && target->type == ValueType::OBJECT_PTR) {
        TsFunction* f = (TsFunction*)target->ptr_val;
        targetPtr = f->funcPtr;
        targetCtx = f->context;
    }

    for (int i = 0; i < arr->Length(); i++) {
        TsValue* existing = (TsValue*)arr->Get(i);
        bool match = false;
        TsValue* actualCallback = existing;

        if (existing == target) {
            match = true;
        } else if (existing && existing->type == ValueType::OBJECT_PTR) {
            TsFunction* f = (TsFunction*)existing->ptr_val;
            
            // Check if it's a once wrapper
            if (f->funcPtr == (void*)once_wrapper_func) {
                OnceContext* ctx = (OnceContext*)f->context;
                if (ctx->originalCallback == target) {
                    match = true;
                    actualCallback = ctx->originalCallback;
                } else if (targetPtr && ctx->originalCallback->type == ValueType::OBJECT_PTR) {
                    TsFunction* origF = (TsFunction*)ctx->originalCallback->ptr_val;
                    if (origF->funcPtr == targetPtr && origF->context == targetCtx) {
                        match = true;
                        actualCallback = ctx->originalCallback;
                    }
                }
            } else if (targetPtr && f->funcPtr == targetPtr && f->context == targetCtx) {
                match = true;
            }
        }

        if (match) {
            arr->Remove((int64_t)existing);
            
            // Emit 'removeListener' AFTER removing the listener
            if (strcmp(event, "newListener") != 0 && strcmp(event, "removeListener") != 0) {
                TsValue eventNameVal = TsValue(TsString::Create(event));
                TsValue* args[2] = { &eventNameVal, actualCallback };
                this->Emit("removeListener", 2, (void**)args);
            }
            return;
        }
    }
}

void TsEventEmitter::RemoveAllListeners(const char* event) {
    if (event) {
        TsValue eventVal;
        eventVal.type = ValueType::STRING_PTR;
        eventVal.ptr_val = TsString::Create(event);
        this->listeners->Delete(eventVal);
    } else {
        this->listeners->Clear();
    }
}

void TsEventEmitter::Emit(const char* event, int argc, void** argv) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        if (strcmp(event, "error") == 0) {
            if (argc > 0) {
                ts_throw((TsValue*)argv[0]);
            } else {
                ts_throw(ts_value_make_string(TsString::Create("Unhandled error event")));
            }
        }
        return;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    // We must copy the array before iterating because listeners might remove themselves
    TsArray* copy = TsArray::Create();
    for (int i = 0; i < arr->Length(); i++) {
        copy->Push(arr->Get(i));
    }

    for (int i = 0; i < copy->Length(); i++) {
        TsValue* callback = (TsValue*)copy->Get(i);
        if (callback) {
            ts_function_call(callback, argc, (TsValue**)argv);
        }
    }
}

int TsEventEmitter::ListenerCount(const char* event) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);
    
    if (listenersVal.type == ValueType::UNDEFINED) {
        return 0;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    return (int)arr->Length();
}

void TsEventEmitter::SetMaxListeners(int n) {
    this->maxListeners = n;
}

int TsEventEmitter::GetMaxListeners() {
    return this->maxListeners;
}

extern "C" {
    void* ts_event_emitter_create() {
        void* mem = ts_alloc(sizeof(TsEventEmitter));
        return new (mem) TsEventEmitter();
    }

    void ts_event_emitter_on(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e) {
            fprintf(stderr, "ts_event_emitter_on: emitter is null\n");
            return;
        }
        if (e->magic != TsEventEmitter::MAGIC) {
            fprintf(stderr, "ts_event_emitter_on: magic mismatch! expected %08x, got %08x\n", TsEventEmitter::MAGIC, e->magic);
            return;
        }
        TsString* s = (TsString*)event;
        if (!s) {
            return;
        }
        e->On(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_listener(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return;
        TsString* s = (TsString*)event;
        if (!s) {
            return;
        }
        e->PrependListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_once_listener(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return;
        TsString* s = (TsString*)event;
        if (!s) {
            return;
        }
        e->PrependOnceListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_remove_all_listeners(void* emitter, void* event) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return;
        TsString* s = (TsString*)event;
        e->RemoveAllListeners(s ? s->ToUtf8() : nullptr);
    }

    void ts_event_emitter_set_max_listeners(void* emitter, int n) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return;
        e->SetMaxListeners(n);
    }

    void ts_event_emitter_emit(void* emitter, void* event, int argc, void** argv) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->Emit(s->ToUtf8(), argc, argv);
    }

    struct StaticOnceContext {
        TsPromise* promise;
    };

    TsValue* static_once_callback(void* context, int argc, TsValue** argv) {
        StaticOnceContext* ctx = (StaticOnceContext*)context;
        void* args = ts_array_create();
        for (int i = 0; i < argc; i++) {
            ts_array_push(args, argv[i]);
        }
        ts_promise_resolve_internal(ctx->promise, ts_value_make_object(args));
        return ts_value_make_undefined();
    }

    TsValue* ts_event_emitter_static_once(void* emitter, void* event) {
        TsEventEmitter* e = (TsEventEmitter*)emitter;
        if (!e || e->magic != TsEventEmitter::MAGIC) return ts_value_make_undefined();
        TsString* s = (TsString*)event;
        if (!s) return ts_value_make_undefined();

        TsPromise* promise = ts_promise_create();
        StaticOnceContext* ctx = (StaticOnceContext*)ts_alloc(sizeof(StaticOnceContext));
        ctx->promise = promise;

        TsValue* callback = ts_value_make_native_function((void*)static_once_callback, ctx);
        e->Once(s->ToUtf8(), callback);

        return ts_value_make_promise(promise);
    }
}
