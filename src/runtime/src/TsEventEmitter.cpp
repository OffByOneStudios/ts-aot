// TsEventEmitter class implementation
// Note: extern "C" API wrappers moved to extensions/node/events/

#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsPromise.h"
#include "TsRuntime.h"
#include <new>

using namespace ts;

TsEventEmitter::TsEventEmitter() {
    this->magic = MAGIC;
    listeners = TsMap::Create();
}

TsEventEmitter::~TsEventEmitter() {
}

struct OnceContext {
    TsEventEmitter* emitter;
    TsString* event;
    TsValue* originalCallback;
};

static TsValue* once_wrapper_func(void* context, int argc, TsValue** argv) {
    OnceContext* ctx = (OnceContext*)context;
    ctx->emitter->RemoveListener(ctx->event->ToUtf8(), ctx->originalCallback);
    return ts_function_call(ctx->originalCallback, argc, argv);
}

void TsEventEmitter::On(const char* event, void* callback) {
    // Emit 'newListener' BEFORE adding the listener
    if (strcmp(event, "newListener") != 0 && strcmp(event, "removeListener") != 0) {
        TsValue* nbEventName = ts_value_make_string(TsString::Create(event));
        TsValue* args[2] = { nbEventName, (TsValue*)callback };
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
        TsValue* nbEventName = ts_value_make_string(TsString::Create(event));
        TsValue* args[2] = { nbEventName, (TsValue*)callback };
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

    // Decode the target callback from NaN-boxed pointer
    TsValue targetVal = nanbox_to_tagged((TsValue*)callback);
    void* targetPtr = nullptr;
    void* targetCtx = nullptr;
    if ((targetVal.type == ValueType::OBJECT_PTR || targetVal.type == ValueType::FUNCTION_PTR) && targetVal.ptr_val) {
        TsFunction* f = (TsFunction*)targetVal.ptr_val;
        targetPtr = f->funcPtr;
        targetCtx = f->context;
    }

    for (int i = 0; i < arr->Length(); i++) {
        TsValue* existing = (TsValue*)arr->Get(i);
        bool match = false;
        TsValue* actualCallback = existing;

        // Direct NaN-boxed pointer equality
        if (existing == (TsValue*)callback) {
            match = true;
        } else {
            // Decode existing from NaN-boxed pointer
            TsValue existingVal = nanbox_to_tagged(existing);
            if ((existingVal.type == ValueType::OBJECT_PTR || existingVal.type == ValueType::FUNCTION_PTR) && existingVal.ptr_val) {
                TsFunction* f = (TsFunction*)existingVal.ptr_val;

                // Check if it's a once wrapper
                if (f->funcPtr == (void*)once_wrapper_func) {
                    OnceContext* ctx = (OnceContext*)f->context;
                    if (ctx->originalCallback == (TsValue*)callback) {
                        match = true;
                        actualCallback = ctx->originalCallback;
                    } else if (targetPtr) {
                        TsValue origVal = nanbox_to_tagged(ctx->originalCallback);
                        if ((origVal.type == ValueType::OBJECT_PTR || origVal.type == ValueType::FUNCTION_PTR) && origVal.ptr_val) {
                            TsFunction* origF = (TsFunction*)origVal.ptr_val;
                            if (origF->funcPtr == targetPtr && origF->context == targetCtx) {
                                match = true;
                                actualCallback = ctx->originalCallback;
                            }
                        }
                    }
                } else if (targetPtr && f->funcPtr == targetPtr && f->context == targetCtx) {
                    match = true;
                }
            }
        }

        if (match) {
            arr->Remove((int64_t)existing);

            // Emit 'removeListener' AFTER removing the listener
            if (strcmp(event, "newListener") != 0 && strcmp(event, "removeListener") != 0) {
                TsValue* nbEventName = ts_value_make_string(TsString::Create(event));
                TsValue* args[2] = { nbEventName, actualCallback };
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

bool TsEventEmitter::Emit(const char* event, int argc, void** argv) {
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
        return false;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;

    // We must copy the array before iterating because listeners might remove themselves
    TsArray* copy = TsArray::Create();
    for (int i = 0; i < arr->Length(); i++) {
        copy->Push(arr->Get(i));
    }

    for (int i = 0; i < copy->Length(); i++) {
        TsValue* callback = (TsValue*)(uintptr_t)copy->Get(i);
        if (callback && (uintptr_t)callback > 0x10000) {
            // Debug: validate the callback pointer
            uint64_t nb = (uint64_t)(uintptr_t)callback;
            void* rawCb = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : (void*)callback;
            if (rawCb) {
                uint32_t m16 = *(uint32_t*)((char*)rawCb + 16);
                
                    event, callback, rawCb, m16);
            }
            ts_function_call(callback, argc, (TsValue**)argv);
        }
    }
    return true;
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

void* TsEventEmitter::Listeners(const char* event) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);

    // Return a copy of the listeners array, unwrapping once-wrappers
    TsArray* result = TsArray::Create();

    if (listenersVal.type == ValueType::UNDEFINED) {
        return result;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    for (int i = 0; i < arr->Length(); i++) {
        TsValue* callback = (TsValue*)arr->Get(i);

        // Decode NaN-boxed callback to check if it's a once-wrapper
        TsValue callbackVal = nanbox_to_tagged(callback);
        if ((callbackVal.type == ValueType::OBJECT_PTR || callbackVal.type == ValueType::FUNCTION_PTR) && callbackVal.ptr_val) {
            TsFunction* f = (TsFunction*)callbackVal.ptr_val;
            if (f->funcPtr == (void*)once_wrapper_func) {
                // Unwrap to get original callback
                OnceContext* ctx = (OnceContext*)f->context;
                result->Push((int64_t)ctx->originalCallback);
                continue;
            }
        }
        result->Push(arr->Get(i));
    }
    return result;
}

void* TsEventEmitter::RawListeners(const char* event) {
    TsValue eventVal;
    eventVal.type = ValueType::STRING_PTR;
    eventVal.ptr_val = TsString::Create(event);
    TsValue listenersVal = this->listeners->Get(eventVal);

    // Return a copy of the raw listeners array (including wrappers)
    TsArray* result = TsArray::Create();

    if (listenersVal.type == ValueType::UNDEFINED) {
        return result;
    }

    TsArray* arr = (TsArray*)listenersVal.ptr_val;
    for (int i = 0; i < arr->Length(); i++) {
        result->Push(arr->Get(i));
    }
    return result;
}

void TsEventEmitter::SetMaxListeners(int n) {
    this->maxListeners = n;
}

int TsEventEmitter::GetMaxListeners() {
    return this->maxListeners;
}

void* TsEventEmitter::EventNames() {
    return this->listeners->GetKeys();
}
