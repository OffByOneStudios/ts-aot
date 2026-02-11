// events extension for ts-aot
// C API wrappers for the events module (EventEmitter)

#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsPromise.h"
#include "TsRuntime.h"

#include <new>

using namespace ts;

extern "C" {
    void* ts_event_emitter_create() {
        void* mem = ts_alloc(sizeof(TsEventEmitter));
        return new (mem) TsEventEmitter();
    }

    void ts_event_emitter_on(void* emitter, void* event, void* callback) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        // Check for boxed TsValue by seeing if type is a valid ValueType (0-10)
        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            // Assume it's a raw pointer
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        // Use dynamic_cast to handle virtual inheritance correctly
        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;

        TsString* s = (TsString*)event;
        if (!s) return;
        e->On(s->ToUtf8(), callback);
    }

    void ts_event_emitter_once(void* emitter, void* event, void* callback) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->Once(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_listener(void* emitter, void* event, void* callback) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->PrependListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_once_listener(void* emitter, void* event, void* callback) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->PrependOnceListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_remove_listener(void* emitter, void* event, void* callback) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->RemoveListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_remove_all_listeners(void* emitter, void* event) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        TsString* s = (TsString*)event;
        e->RemoveAllListeners(s ? s->ToUtf8() : nullptr);
    }

    void ts_event_emitter_set_max_listeners(void* emitter, int n) {
        if (!emitter) return;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return;
        e->SetMaxListeners(n);
    }

    int ts_event_emitter_get_max_listeners(void* emitter) {
        if (!emitter) return 0;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return 0;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return 0;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return 0;
        return e->GetMaxListeners();
    }

    int ts_event_emitter_listener_count(void* emitter, void* event) {
        if (!emitter) return 0;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return 0;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return 0;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return 0;
        TsString* s = (TsString*)event;
        if (!s) return 0;
        return e->ListenerCount(s->ToUtf8());
    }

    void* ts_event_emitter_listeners(void* emitter, void* event) {
        if (!emitter) return TsArray::Create();

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return TsArray::Create();
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return TsArray::Create();

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return TsArray::Create();
        TsString* s = (TsString*)event;
        if (!s) return TsArray::Create();
        return e->Listeners(s->ToUtf8());
    }

    void* ts_event_emitter_raw_listeners(void* emitter, void* event) {
        if (!emitter) return TsArray::Create();

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return TsArray::Create();
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return TsArray::Create();

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return TsArray::Create();
        TsString* s = (TsString*)event;
        if (!s) return TsArray::Create();
        return e->RawListeners(s->ToUtf8());
    }

    bool ts_event_emitter_emit(void* emitter, void* event, int argc, void** argv) {
        if (!emitter) return false;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        // Check for boxed TsValue by seeing if type is a valid ValueType (0-10)
        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return false;
            }
        } else {
            // Assume it's a raw pointer
            rawPtr = emitter;
        }

        if (!rawPtr) return false;

        // Use dynamic_cast to handle virtual inheritance correctly
        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return false;
        TsString* s = (TsString*)event;
        if (!s) return false;
        return e->Emit(s->ToUtf8(), argc, argv);
    }

    struct StaticOnceContext {
        TsPromise* promise;
    };

    static TsValue* static_once_callback(void* context, int argc, TsValue** argv) {
        StaticOnceContext* ctx = (StaticOnceContext*)context;
        void* args = ts_array_create();
        for (int i = 0; i < argc; i++) {
            ts_array_push(args, argv[i]);
        }
        TsValue* boxed = ts_value_make_array(args);
        ts_promise_resolve_internal(ctx->promise, boxed);
        return ts_value_make_undefined();
    }

    TsValue* ts_event_emitter_static_once(void* emitter, void* event) {
        if (!emitter) return ts_value_make_undefined();

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return ts_value_make_undefined();
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return ts_value_make_undefined();

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return ts_value_make_undefined();
        TsString* s = (TsString*)event;
        if (!s) return ts_value_make_undefined();

        TsPromise* promise = ts_promise_create();
        StaticOnceContext* ctx = (StaticOnceContext*)ts_alloc(sizeof(StaticOnceContext));
        ctx->promise = promise;

        TsValue* callback = ts_value_make_native_function((void*)static_once_callback, ctx);
        e->Once(s->ToUtf8(), callback);

        return ts_value_make_promise(promise);
    }

    void* ts_event_emitter_event_names(void* emitter) {
        if (!emitter) return nullptr;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return nullptr;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return nullptr;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return nullptr;
        return e->EventNames();
    }

    // ============================================================================
    // events.on(emitter, event) -> AsyncIterator
    // Returns an AsyncIterator that yields arrays of event arguments
    // ============================================================================

    struct StaticOnState {
        TsEventEmitter* emitter;
        TsString* eventName;
        TsPromise* pendingPromise;  // Currently awaited promise (for next())
        bool closed;
        TsValue* listenerCallback;  // Stored to enable removal
    };

    // Forward declaration
    static TsValue* static_on_listener_callback(void* context, int argc, TsValue** argv);

    static TsValue* StaticOn_next_internal(void* context) {
        StaticOnState* state = (StaticOnState*)context;
        if (!state) {
            return ts_value_make_undefined();
        }

        // Create a promise that will be resolved when the next event fires
        TsPromise* promise = ts_promise_create();

        if (state->closed) {
            // Already closed - return done
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

        // Store the promise to be resolved when an event fires
        state->pendingPromise = promise;

        return ts_value_make_promise(promise);
    }

    static TsValue* StaticOn_return_internal(void* context) {
        StaticOnState* state = (StaticOnState*)context;
        if (!state) return ts_value_make_undefined();

        state->closed = true;

        // Remove the listener from the emitter
        if (state->emitter && state->listenerCallback) {
            state->emitter->RemoveListener(state->eventName->ToUtf8(), state->listenerCallback);
        }

        // Resolve pending promise if any
        if (state->pendingPromise) {
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
            ts_promise_resolve_internal(state->pendingPromise, res);
            state->pendingPromise = nullptr;
        }

        // Return a promise that resolves to { value: undefined, done: true }
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

    // This is the callback that fires when the event is emitted
    static TsValue* static_on_listener_callback(void* context, int argc, TsValue** argv) {
        StaticOnState* state = (StaticOnState*)context;
        if (!state || state->closed || !state->pendingPromise) {
            return ts_value_make_undefined();
        }

        // Create array of event arguments
        void* argsArray = ts_array_create();
        for (int i = 0; i < argc; i++) {
            ts_array_push(argsArray, argv[i]);
        }

        // Create the result { value: argsArray, done: false }
        TsMap* result = TsMap::Create();
        TsValue k1, k2;
        k1.type = ValueType::STRING_PTR;
        k1.ptr_val = TsString::Create("value");
        k2.type = ValueType::STRING_PTR;
        k2.ptr_val = TsString::Create("done");

        TsValue arrayVal;
        arrayVal.type = ValueType::ARRAY_PTR;
        arrayVal.ptr_val = argsArray;
        result->Set(k1, arrayVal);

        TsValue doneVal;
        doneVal.type = ValueType::BOOLEAN;
        doneVal.b_val = false;
        result->Set(k2, doneVal);

        TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
        res->type = ValueType::OBJECT_PTR;
        res->ptr_val = result;

        ts_promise_resolve_internal(state->pendingPromise, res);
        state->pendingPromise = nullptr;

        return ts_value_make_undefined();
    }

    TsValue* ts_event_emitter_static_on(void* emitter, void* event) {
        if (!emitter) return ts_value_make_undefined();

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return ts_value_make_undefined();
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return ts_value_make_undefined();

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return ts_value_make_undefined();
        TsString* s = (TsString*)event;
        if (!s) return ts_value_make_undefined();

        // Create the state for this async iterator
        void* stateMem = ts_alloc(sizeof(StaticOnState));
        StaticOnState* state = new (stateMem) StaticOnState();
        state->emitter = e;
        state->eventName = s;
        state->pendingPromise = nullptr;
        state->closed = false;

        // Create and register the listener callback
        TsValue* callback = ts_value_make_native_function((void*)static_on_listener_callback, state);
        state->listenerCallback = callback;
        e->On(s->ToUtf8(), callback);

        // Create a TsMap to hold the iterator methods
        TsMap* iteratorMap = TsMap::Create();

        // Set up the next() method
        TsValue* nextFunc = ts_value_make_function((void*)StaticOn_next_internal, state);
        TsValue nextKey;
        nextKey.type = ValueType::STRING_PTR;
        nextKey.ptr_val = TsString::Create("next");
        iteratorMap->Set(nextKey, *nextFunc);

        // Set up the return() method (for early termination via break)
        TsValue* returnFunc = ts_value_make_function((void*)StaticOn_return_internal, state);
        TsValue returnKey;
        returnKey.type = ValueType::STRING_PTR;
        returnKey.ptr_val = TsString::Create("return");
        iteratorMap->Set(returnKey, *returnFunc);

        // Set up [Symbol.asyncIterator] to return itself
        TsValue* iterFunc = ts_value_make_function((void*)[](void* ctx) -> TsValue* {
            return ts_value_make_object(ctx);
        }, iteratorMap);
        TsValue iterKey;
        iterKey.type = ValueType::STRING_PTR;
        iterKey.ptr_val = TsString::Create("[Symbol.asyncIterator]");
        iteratorMap->Set(iterKey, *iterFunc);

        return ts_value_make_object(iteratorMap);
    }

    int64_t ts_event_emitter_static_listener_count(void* emitter, void* event) {
        // EventEmitter.listenerCount(emitter, eventName) - deprecated static method
        // Simply delegates to the instance method
        if (!emitter) return 0;

        // Check if emitter is a boxed TsValue* and unbox it
        TsValue* val = (TsValue*)emitter;
        void* rawPtr = nullptr;

        if ((uint8_t)val->type <= 10) {
            if (val->type == ValueType::OBJECT_PTR && val->ptr_val) {
                rawPtr = val->ptr_val;
            } else {
                return 0;
            }
        } else {
            rawPtr = emitter;
        }

        if (!rawPtr) return 0;

        TsObject* obj = (TsObject*)rawPtr;
        TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
        if (!e) e = obj->AsEventEmitter();
        if (!e) return 0;
        TsString* s = (TsString*)event;
        if (!s) return 0;
        return (int64_t)e->ListenerCount(s->ToUtf8());
    }
}
