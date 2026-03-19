// events extension for ts-aot
// C API wrappers for the events module (EventEmitter)

#include "TsEventEmitter.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsNanBox.h"

#include <new>

using namespace ts;

// Runtime helper: check if a vtable belongs to a registered EventEmitter subclass
extern "C" bool ts_is_registered_event_emitter(uint64_t vtable);

// Helper: extract TsEventEmitter* from a possibly NaN-boxed emitter pointer.
// On Linux/Itanium ABI, virtual inheritance means TsObject is NOT at offset 0
// for stream classes (TsReadable, TsWritable, etc.). For these, dynamic_cast
// from (TsObject*) fails because the pointer doesn't point to a TsObject subobject.
// We handle this by:
//   1. Try regular dynamic_cast (works for non-virtual inheritance like TsServer)
//   2. If that fails on Linux, check the vtable dispatch table and use the
//      Itanium ABI vbase offset to find the TsEventEmitter subobject.
static TsEventEmitter* getEmitter(void* emitter) {
    if (!emitter) return nullptr;
    // Guard against NaN-boxed non-pointer values (numbers, bools, undefined, null)
    uint64_t nb = (uint64_t)(uintptr_t)emitter;
    if (nanbox_is_number(nb) || nanbox_is_special(nb)) return nullptr;
    void* rawPtr = ts_nanbox_safe_unbox(emitter);
    if (!rawPtr) return nullptr;

    // Try standard dynamic_cast (works for non-virtual inheritance like TsServer)
    TsEventEmitter* e = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
    if (e) return e;

#ifdef __linux__
    // On Linux/Itanium ABI, for virtual-inheritance classes (TsReadable, TsSocket, etc.),
    // (TsObject*)rawPtr is wrong because TsObject is inside the virtual base at a
    // non-zero offset. Check the vtable dispatch table and use the vbase offset.
    {
        uint64_t vt = *(uint64_t*)rawPtr;
        if (ts_is_registered_event_emitter(vt)) {
            // This IS an EventEmitter subclass with virtual inheritance.
            // In Itanium ABI, the vbase offset for the first virtual base
            // is stored at vtable[-3] (before offset-to-top at [-2] and typeinfo at [-1]).
            void** vtbl = (void**)vt;
            ptrdiff_t vbase_offset = (ptrdiff_t)vtbl[-3];
            if (vbase_offset > 0 && vbase_offset < 4096) {
                return reinterpret_cast<TsEventEmitter*>((char*)rawPtr + vbase_offset);
            }
        }
    }
#endif

    return nullptr;
}

extern "C" {
    void* ts_event_emitter_create() {
        void* mem = ts_alloc(sizeof(TsEventEmitter));
        return new (mem) TsEventEmitter();
    }

    void ts_event_emitter_on(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->On(s->ToUtf8(), callback);
    }

    void ts_event_emitter_once(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->Once(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_listener(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->PrependListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_prepend_once_listener(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->PrependOnceListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_remove_listener(void* emitter, void* event, void* callback) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        if (!s) return;
        e->RemoveListener(s->ToUtf8(), callback);
    }

    void ts_event_emitter_remove_all_listeners(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        TsString* s = (TsString*)event;
        e->RemoveAllListeners(s ? s->ToUtf8() : nullptr);
    }

    void ts_event_emitter_set_max_listeners(void* emitter, int n) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return;
        e->SetMaxListeners(n);
    }

    int ts_event_emitter_get_max_listeners(void* emitter) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return 0;
        return e->GetMaxListeners();
    }

    int ts_event_emitter_listener_count(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return 0;
        TsString* s = (TsString*)event;
        if (!s) return 0;
        return e->ListenerCount(s->ToUtf8());
    }

    void* ts_event_emitter_listeners(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return TsArray::Create();
        TsString* s = (TsString*)event;
        if (!s) return TsArray::Create();
        return e->Listeners(s->ToUtf8());
    }

    void* ts_event_emitter_raw_listeners(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return TsArray::Create();
        TsString* s = (TsString*)event;
        if (!s) return TsArray::Create();
        return e->RawListeners(s->ToUtf8());
    }

    bool ts_event_emitter_emit(void* emitter, void* event, int argc, void** argv) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return false;
        TsString* s = (TsString*)event;
        if (!s) return false;
        return e->Emit(s->ToUtf8(), argc, argv);
    }

    bool ts_event_emitter_emit_ext(void* emitter, void* event, void* arg1) {
        if (!arg1 || ts_value_is_undefined((TsValue*)arg1) || ts_value_is_null((TsValue*)arg1)) {
            return ts_event_emitter_emit(emitter, event, 0, nullptr);
        }
        void* argv[] = { arg1 };
        return ts_event_emitter_emit(emitter, event, 1, argv);
    }

    bool ts_event_emitter_emit_packed(void* emitter, void* event, void* argsArray) {
        if (!argsArray) {
            return ts_event_emitter_emit(emitter, event, 0, nullptr);
        }
        TsArray* arr = (TsArray*)argsArray;
        int64_t len = arr->Length();
        if (len == 0) {
            return ts_event_emitter_emit(emitter, event, 0, nullptr);
        }
        void** argv = (void**)alloca(len * sizeof(void*));
        for (int64_t i = 0; i < len; i++) {
            argv[i] = (void*)arr->Get(i);
        }
        return ts_event_emitter_emit(emitter, event, (int)len, argv);
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
        TsEventEmitter* e = getEmitter(emitter);
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
        TsEventEmitter* e = getEmitter(emitter);
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
            ts_promise_resolve_internal(promise, ts_value_make_object(result));
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
            ts_promise_resolve_internal(state->pendingPromise, ts_value_make_object(result));
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
        ts_promise_resolve_internal(promise, ts_value_make_object(result));
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

        ts_promise_resolve_internal(state->pendingPromise, ts_value_make_object(result));
        state->pendingPromise = nullptr;

        return ts_value_make_undefined();
    }

    TsValue* ts_event_emitter_static_on(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
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
        iteratorMap->Set(nextKey, nanbox_to_tagged(nextFunc));

        // Set up the return() method (for early termination via break)
        TsValue* returnFunc = ts_value_make_function((void*)StaticOn_return_internal, state);
        TsValue returnKey;
        returnKey.type = ValueType::STRING_PTR;
        returnKey.ptr_val = TsString::Create("return");
        iteratorMap->Set(returnKey, nanbox_to_tagged(returnFunc));

        // Set up [Symbol.asyncIterator] to return itself
        TsValue* iterFunc = ts_value_make_function((void*)+[](void* ctx) -> TsValue* {
            return ts_value_make_object(ctx);
        }, iteratorMap);
        TsValue iterKey;
        iterKey.type = ValueType::STRING_PTR;
        iterKey.ptr_val = TsString::Create("[Symbol.asyncIterator]");
        iteratorMap->Set(iterKey, nanbox_to_tagged(iterFunc));

        return ts_value_make_object(iteratorMap);
    }

    int64_t ts_event_emitter_static_listener_count(void* emitter, void* event) {
        TsEventEmitter* e = getEmitter(emitter);
        if (!e) return 0;
        TsString* s = (TsString*)event;
        if (!s) return 0;
        return (int64_t)e->ListenerCount(s->ToUtf8());
    }
}

// Native thunk wrappers for EventEmitter instance methods.
// These are called as (void* context, int argc, TsValue** argv) where
// context is the EventEmitter (bound as 'this' via native function).
// However, in the untyped JS path, 'this' isn't passed through context —
// it's the first argument from ts_call_N.

static TsValue* ee_on_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 3) return argv ? argv[0] : ts_value_make_undefined();
    ts_event_emitter_on(argv[0], argv[1], argv[2]);
    return argv[0]; // Return this for chaining
}

static TsValue* ee_once_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 3) return argv ? argv[0] : ts_value_make_undefined();
    ts_event_emitter_once(argv[0], argv[1], argv[2]);
    return argv[0];
}

static TsValue* ee_emit_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_bool(false);
    // Collect remaining args
    int extraArgc = argc - 2;
    void** extraArgv = extraArgc > 0 ? (void**)alloca(extraArgc * sizeof(void*)) : nullptr;
    for (int i = 0; i < extraArgc; i++) {
        extraArgv[i] = argv[i + 2];
    }
    bool result = ts_event_emitter_emit(argv[0], argv[1], extraArgc, extraArgv);
    return ts_value_make_bool(result);
}

static TsValue* ee_removeListener_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 3) return argv ? argv[0] : ts_value_make_undefined();
    ts_event_emitter_remove_listener(argv[0], argv[1], argv[2]);
    return argv[0];
}

static TsValue* ee_removeAllListeners_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 1) return ts_value_make_undefined();
    void* event = argc >= 2 ? argv[1] : nullptr;
    ts_event_emitter_remove_all_listeners(argv[0], event);
    return argv[0];
}

static TsValue* ee_setMaxListeners_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return argv ? argv[0] : ts_value_make_undefined();
    // Extract int from NaN-boxed value
    uint64_t nb = (uint64_t)(uintptr_t)argv[1];
    int n = (int)nanbox_to_int64(nb);
    ts_event_emitter_set_max_listeners(argv[0], n);
    return argv[0];
}

static TsValue* ee_listeners_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_undefined();
    void* result = ts_event_emitter_listeners(argv[0], argv[1]);
    return result ? (TsValue*)result : ts_value_make_undefined();
}

static TsValue* ee_listenerCount_native(void* ctx, int argc, TsValue** argv) {
    if (argc < 2) return ts_value_make_int(0);
    int count = ts_event_emitter_listener_count(argv[0], argv[1]);
    return ts_value_make_int(count);
}

static TsValue* ee_create_native(void* ctx, int argc, TsValue** argv) {
    void* emitter = ts_event_emitter_create();
    return ts_value_make_object(emitter);
}

// Create EventEmitter constructor with prototype
// Called lazily when 'events' module is first required
static TsMap* createEventEmitterConstructor() {
    TsMap* ctor = TsMap::Create();

    // Create prototype with instance methods
    TsMap* proto = TsMap::Create();

    auto addMethod = [](TsMap* map, const char* name, void* fn) {
        extern TsValue* ts_value_make_native_function(void*, void*);
        TsValue key;
        key.type = ValueType::STRING_PTR;
        key.ptr_val = TsString::GetInterned(name);
        TsValue val;
        val.type = ValueType::FUNCTION_PTR;
        val.ptr_val = ts_value_make_native_function(fn, nullptr);
        map->Set(key, val);
    };

    addMethod(proto, "on", (void*)ee_on_native);
    addMethod(proto, "addListener", (void*)ee_on_native);
    addMethod(proto, "once", (void*)ee_once_native);
    addMethod(proto, "emit", (void*)ee_emit_native);
    addMethod(proto, "removeListener", (void*)ee_removeListener_native);
    addMethod(proto, "off", (void*)ee_removeListener_native);
    addMethod(proto, "removeAllListeners", (void*)ee_removeAllListeners_native);
    addMethod(proto, "setMaxListeners", (void*)ee_setMaxListeners_native);
    addMethod(proto, "listeners", (void*)ee_listeners_native);
    addMethod(proto, "listenerCount", (void*)ee_listenerCount_native);

    // Set prototype property
    TsValue protoKey;
    protoKey.type = ValueType::STRING_PTR;
    protoKey.ptr_val = TsString::GetInterned("prototype");
    TsValue protoVal;
    protoVal.type = ValueType::OBJECT_PTR;
    protoVal.ptr_val = proto;
    ctor->Set(protoKey, protoVal);

    return ctor;
}

// Hook: called by create_builtin_module after building the events TsMap.
// We add EventEmitter as a constructor object with methods on its prototype.
extern "C" void ts_events_post_init(void* moduleMap) {
    TsMap* mod = (TsMap*)moduleMap;
    TsMap* ee = createEventEmitterConstructor();

    TsValue key;
    key.type = ValueType::STRING_PTR;
    key.ptr_val = TsString::GetInterned("EventEmitter");
    TsValue val;
    val.type = ValueType::OBJECT_PTR;
    val.ptr_val = ee;
    mod->Set(key, val);
}

extern "C" void ts_builtin_register_post_init(const char*, void (*)(void*));

// Register special functions used by tsruntime property access
static struct EventsRegistrar {
    EventsRegistrar() {
        ts_builtin_register_special("event_emitter_on", (void*)ts_event_emitter_on);
        // Register EventEmitter constructor in the builtin registry
        ts_builtin_register("events", "EventEmitter", (void*)ee_create_native, TS_THUNK_FN);
        // Register a post-init callback to replace with full constructor+prototype
        ts_builtin_register_post_init("events", ts_events_post_init);
    }
} g_events_registrar;
