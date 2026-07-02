#include "TsPromise.h"
#include "TsRuntime.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsGC.h"
#include "TsError.h"
#include <uv.h>
#include <vector>
#include <iostream>
#include <cstdio>

namespace ts {

extern "C" {
    TsValue* Generator_next_internal(void* context, TsValue* value);
    TsValue* AsyncGenerator_next_internal(void* context, TsValue* value);
    TsValue* ts_map_get_property(void* obj, void* propName);
    void ts_set_call_this(void* thisArg);
    void* ts_get_call_this();
    void* getIteratorPrototypeBoxed();  // TsMap.cpp — boxed %IteratorPrototype%
    void* ts_get_global_Promise();      // TsGlobals.cpp — the Promise constructor
}

// Async iterator wrapper for arrays - used by for await...of
struct AsyncArrayIterator : public TsMap {
    TsArray* array;
    int64_t index;

    AsyncArrayIterator(TsArray* arr) : array(arr), index(0) {
        vtable = nullptr;
    }
};

extern void* TsPromise_VTable[];

TsPromise::TsPromise() {
    vtable = TsPromise_VTable;
    magic = MAGIC;  // Set magic for identification
    state = PromiseState::Pending;
    handled = false;
}

AsyncContext::AsyncContext() {
    state = 0;
    error = false;
    yielded = false;
    promise = ts_promise_create();
    pendingNextPromise = nullptr;
    resumeFn = nullptr;
    data = nullptr;
    resumedValue = nullptr;
    execContext = nullptr;
}

TsValue* create_generator_result(TsValue value, bool done) {
    TsMap* map = TsMap::Create();
    map->Set(TsString::Create("value"), value);
    map->Set(TsString::Create("done"), TsValue(done));
    return ts_value_make_object(map);
}

TsGenerator::TsGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    static_cast<TsObject*>(this)->magic = MAGIC; // Set magic for type detection

    TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)Generator_next_internal, this));
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this));
    this->Set(TsString::Create("[Symbol.iterator]"), iterFunc);

    // Link the generator's [[Prototype]] to %IteratorPrototype% so it inherits
    // the iterator helpers (map/filter/take/drop/flatMap/toArray/...). The own
    // next/@@iterator above still shadow the inherited ones. Was null before,
    // so `gen.map(...)` / `Object.getPrototypeOf(gen)` returned undefined/null.
    if (void* ip = ts_value_get_object((TsValue*)getIteratorPrototypeBoxed()))
        this->SetPrototype((TsMap*)ip);
}

TsValue* TsGenerator::next(TsValue* value) {
    if (done) {
        return create_generator_result(TsValue(), true);
    }

    ctx->yielded = false;
    ctx->resumedValue = value;

    // ECMA-262: `this` inside a generator body must be the receiver at the
    // time the generator was created, not the receiver of the .next() call.
    // The wrapper captured it via ts_async_context_set_this; restore it here
    // so that `ts_get_call_this()` inside the impl returns the correct value.
    // Save/restore so we don't permanently clobber the caller's `this`.
    void* savedThis = ts_get_call_this();
    if (ctx->thisValue) {
        ts_set_call_this(ctx->thisValue);
    }
    ctx->resumeFn(ctx);
    ts_set_call_this(savedThis);

    if (ctx->yielded) {
        return create_generator_result(ctx->yieldedValue, false);
    } else {
        done = true;
        return create_generator_result(ctx->yieldedValue, true);
    }
}

TsAsyncGenerator::TsAsyncGenerator(AsyncContext* ctx) : ctx(ctx) {
    vtable = nullptr;
    done = false;
    static_cast<TsObject*>(this)->magic = MAGIC; // Set magic for type detection

    TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)AsyncGenerator_next_internal, this));
    this->Set(TsString::Create("next"), nextFunc);
    
    TsValue iterFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
        return ts_value_make_object(ctx);
    }, this));
    this->Set(TsString::Create("[Symbol.asyncIterator]"), iterFunc);
}

// ---------------------------------------------------------------------------
// Suspendable async-generator request machinery (GEN-001 Stage 2).
// Dead code until the Stage-3 lowering creates generators via
// ts_async_generator_create_suspendable and emits a resumeFn.
// ---------------------------------------------------------------------------

// Resume completion kinds carried in AsyncContext::resumeMode.
enum AgenResumeMode { AGEN_MODE_NEXT = 0, AGEN_MODE_THROW = 1, AGEN_MODE_RETURN = 2 };

// Drive ONE request: set the resume registers and invoke the impl. The impl
// settles `p` through ts_agen_suspend_yield / ts_agen_complete[_reject]
// before returning (awaits are blocking pumps, not suspension points).
static void agen_drive_request(TsAsyncGenerator* gen, TsPromise* p,
                               TsValue* value, int mode) {
    AsyncContext* ctx = gen->ctx;
    if (gen->done || !ctx || !ctx->resumeFn) {
        // Completed generator (or no impl): settle per spec without driving.
        if (mode == AGEN_MODE_THROW) {
            ts_promise_reject_internal(p, value ? value : ts_value_make_undefined());
        } else if (mode == AGEN_MODE_RETURN) {
            ts_promise_resolve_internal(p,
                create_generator_result(value ? nanbox_to_tagged(value) : TsValue(), true));
        } else {
            ts_promise_resolve_internal(p, create_generator_result(TsValue(), true));
        }
        return;
    }
    ctx->pendingNextPromise = p;
    ctx->yielded = false;
    ctx->resumedValue = value;
    ctx->resumeMode = mode;
    // Same `this` save/restore pattern as TsGenerator::next: the body must see
    // the receiver captured at generator creation, not the .next() caller's.
    void* savedThis = ts_get_call_this();
    if (ctx->thisValue) {
        ts_set_call_this(ctx->thisValue);
    }
    ctx->resumeFn(ctx);
    ts_set_call_this(savedThis);
    // Safety net: if the impl returned without settling this request (it
    // always should once the Stage-3 lowering lands), complete as done so the
    // caller never hangs on a forever-pending promise.
    if (ctx->pendingNextPromise == p) {
        ctx->pendingNextPromise = nullptr;
        gen->done = true;
        ts_promise_resolve_internal(p, create_generator_result(TsValue(), true));
    }
}

// AsyncGeneratorEnqueue (ECMA-262 27.6.3.3, simplified): if the impl frame is
// live (an await inside it pumped microtasks that re-entered next/throw/
// return), queue the request; otherwise drive it now and then drain anything
// queued meanwhile, in order.
static TsPromise* agen_enqueue_request(TsAsyncGenerator* gen, TsValue* value, int mode) {
    TsPromise* p = ts_promise_create();
    if (gen->executing) {
        if (!gen->requestQueue) {
            gen->requestQueue = TsArray::Create();
            ts_gc_write_barrier(&gen->requestQueue, gen->requestQueue);
        }
        // Flattened [promise, value, mode] triple; the TsArray element scan
        // keeps the boxed values alive and forwarded across GCs.
        gen->requestQueue->Push((int64_t)ts_value_make_promise(p));
        gen->requestQueue->Push((int64_t)(value ? value : ts_value_make_undefined()));
        gen->requestQueue->Push((int64_t)ts_value_make_int(mode));
        return p;
    }
    gen->executing = true;
    agen_drive_request(gen, p, value, mode);
    // Drain re-entrant requests in arrival order. The loop re-reads Length()
    // every iteration because driving a request can enqueue more.
    if (gen->requestQueue) {
        TsArray* q = gen->requestQueue;
        for (size_t i = 0; i + 2 < (size_t)q->Length(); i += 3) {
            TsValue pv = nanbox_to_tagged((TsValue*)q->Get(i));
            TsValue* qval = (TsValue*)q->Get(i + 1);
            int qmode = (int)ts_value_get_int((TsValue*)q->Get(i + 2));
            if (pv.ptr_val) {
                agen_drive_request(gen, (TsPromise*)pv.ptr_val, qval, qmode);
            }
        }
        gen->requestQueue = nullptr;
    }
    gen->executing = false;
    return p;
}

TsPromise* TsAsyncGenerator::next(TsValue* value) {
    // Suspendable model: every request goes through the enqueue mechanism.
    if (suspendable) {
        return agen_enqueue_request(this, value, AGEN_MODE_NEXT);
    }

    TsPromise* p = ts_promise_create();

    // Eager-body model: the lowered async-generator body ran to completion
    // inside the initial call, queueing every yielded value (see
    // ts_async_generator_yield). Drain the queue first.
    if (pendingYields && yieldCursor < (size_t)pendingYields->Length()) {
        TsValue* boxed = (TsValue*)pendingYields->Get(yieldCursor++);
        ts_promise_resolve_internal(
            p, create_generator_result(nanbox_to_tagged(boxed), false));
        return p;
    }
    // An uncaught throw from the eager body rejects the first next() after
    // the queued yields drain, then the generator is done (ECMA-262
    // AsyncGeneratorReject + completed state).
    if (hasException) {
        hasException = false;
        done = true;
        ts_promise_reject_internal(p, nanbox_from_tagged(pendingException));
        pendingException = TsValue();
        return p;
    }
    if (done) {
        // Spec: the body's return value surfaces on the FIRST done-result
        // only; later next() calls produce {undefined, done: true}.
        TsValue rv = returnValue;
        returnValue = TsValue();
        ts_promise_resolve_internal(p, create_generator_result(rv, true));
        return p;
    }

    // Legacy state-machine path (no current lowering emits a resumeFn for
    // async generators, but keep it for a future suspendable implementation).
    if (ctx && ctx->resumeFn) {
        ctx->pendingNextPromise = p;
        ctx->yielded = false;
        ctx->resumedValue = value;
        ctx->resumeFn(ctx);
        return p;
    }

    done = true;
    ts_promise_resolve_internal(p, create_generator_result(TsValue(), true));
    return p;
}

extern "C" {

void ts_async_yield(TsValue* value, AsyncContext* ctx) {
    ctx->yielded = true;
    ctx->yieldedValue = nanbox_to_tagged(value);

    if (ctx->pendingNextPromise) {
        ts_promise_resolve_internal(ctx->pendingNextPromise, create_generator_result(nanbox_to_tagged(value), false));
        ctx->pendingNextPromise = nullptr;
    }
}

TsGenerator* ts_generator_create(AsyncContext* ctx) {
    void* mem = ts_alloc(sizeof(TsGenerator));
    TsGenerator* gen = new (mem) TsGenerator(ctx);
    ctx->syncGenerator = gen;  // Store back-pointer for impl function access
    return gen;
}

TsValue* Generator_next_internal(void* context, TsValue* value) {
    TsGenerator* gen = (TsGenerator*)context;
    if (!gen) return nullptr;
    return gen->next(value);
}

TsValue* AsyncGenerator_next_internal(void* context, TsValue* value); // defined below

TsValue* Generator_next(TsValue* genVal, TsValue* value) {
    void* raw = ts_value_get_object(genVal);
    if (!raw) return ts_value_make_undefined();

    // Async generator: next() must return a PROMISE of the iteration result.
    // Without this branch the AGEN object fell through to the sync
    // TsGenerator path below (layout-compatible prefix) and returned the
    // {value, done} result object directly — the source of the 1,594-test
    // "Cannot read properties of undefined (reading 'then')" cluster.
    if (ts_is_unchecked<TsAsyncGenerator>(raw)) {
        return AsyncGenerator_next_internal(raw, value);
    }

    // Check if this is a TsMap-based iterator (has "next" property)
    // rather than a real TsGenerator
    if (ts_is_unchecked<TsMap>(raw)) { // TsMap-based iterator
        // It's a Map-based iterator — look up "next" via prototype chain
        // (ts_map_get_property only checks own properties, which misses
        // shared ArrayIteratorPrototype). ts_object_get_dynamic walks the
        // prototype chain and accepts NaN-boxed TsValue*s.
        TsValue* nextKey = ts_value_make_string(TsString::Create("next"));
        TsValue* nextFn = ts_object_get_dynamic(genVal, nextKey);
        if (nextFn && !ts_value_is_undefined(nextFn)) {
            // Preserve `this` so native methods that read ts_get_call_this()
            // (e.g., ArrayIteratorPrototype.next) find the receiver.
            return ts_call_with_this_0(nextFn, genVal);
        }
        return ts_value_make_undefined();
    }

    return Generator_next_internal(raw, value);
}

void ts_generator_return(TsGenerator* gen, TsValue* value) {
    if (!gen) return;
    gen->done = true;
    if (value) {
        gen->ctx->yieldedValue = nanbox_to_tagged(value);
    } else {
        gen->ctx->yieldedValue = TsValue(); // undefined
    }
}

void ts_generator_return_via_ctx(AsyncContext* ctx, TsValue* value) {
    if (!ctx || !ctx->syncGenerator) return;
    ts_generator_return((TsGenerator*)ctx->syncGenerator, value);
}

TsValue* ts_generator_yield(TsValue* value) {
    // This is a simplified yield implementation.
    // In a proper generator state machine, this would:
    // 1. Store the yielded value
    // 2. Suspend the generator
    // 3. Return the value passed to next() when resumed
    //
    // For now, we just return undefined (value passed to next() is typically undefined)
    return ts_value_make_undefined();
}

// yield* delegation stubs. The proper implementation requires
// generator state-machine support that isn't wired up at this level
// (the codegen handles the common case inline). These exist to
// satisfy the linker for paths that emit a fallback runtime call;
// returning undefined causes the delegating expression to evaluate
// to undefined, which converts compile_error → runtime-fail.
TsValue* ts_generator_yield_star(TsValue* iterable) {
    (void)iterable;
    return ts_value_make_undefined();
}

// Distinguishes async-iteration protocol throws (yield* GetIterator
// TypeErrors, awaited next()-result rejections) from other throws escaping
// the eager async-generator body. Protocol throws must REJECT the first
// next() promise; parameter-binding errors (lowered as a body prologue in
// the eager model) must keep throwing SYNCHRONOUSLY out of gen() — the
// dstr/dflt-params test262 family asserts that with assert.throws. The
// compiler's agen.reject landing pad consults-and-clears this flag.
static bool g_agen_protocol_throw = false;

// agen.reject landing-pad decision: reject (1) for yield*-protocol throws
// and for any throw after the body-started marker; re-throw synchronously
// (0) for parameter-prologue errors. Consumes the protocol flag.
int ts_agen_should_reject(TsAsyncGenerator* gen) {
    int v = (g_agen_protocol_throw || (gen && gen->bodyStarted)) ? 1 : 0;
    g_agen_protocol_throw = false;
    return v;
}

static void agen_protocol_throw(TsValue* exc) {
    g_agen_protocol_throw = true;
    ts_throw(exc);
}

// IsCallable for yield* protocol checks: TsFunction/TsClosure carry their
// magic at offset 0 (native fns) or 16 (canonical TsObject slot).
static bool agen_is_callable(TsValue* val) {
    return ts_is_callable((void*)val);  // canonical IsCallable (TsObject.cpp)
}

static bool agen_is_undef_or_null(TsValue* val) {
    if (!val) return true;
    uint64_t nb = nanbox_from_tsvalue_ptr(val);
    return nanbox_is_undefined(nb) || nanbox_is_null(nb);
}

// `yield* iterable` inside an async generator (eager-body model).
// ECMA-262 §27.6.3.7 YieldExpression : yield* — GetIterator(value, async)
// with full protocol checks (the dominant test262 "abrupt completion closes
// iter" cluster is these TypeErrors), then an eager drain that re-yields
// every value. Throws via ts_throw; the async-gen prologue barrier turns
// that into a rejected first-next() promise.
TsValue* ts_async_generator_yield_star(TsValue* iterable) {
    extern TsValue* ts_object_get_property(void* o, const char* k);

    // Everything thrown during yield* evaluation — our protocol TypeErrors,
    // user getters/methods invoked by GetIterator, next() calls, awaited
    // rejections — is an abrupt completion of the yield* per spec and must
    // REJECT the first next() promise (not escape gen() synchronously).
    // Flag the whole evaluation; cleared on the normal-return paths below.
    // The compiler's agen.reject landing pad consumes the flag.
    g_agen_protocol_throw = true;

    void* raw = iterable ? ts_value_get_object(iterable) : nullptr;
    if (!raw) {
        agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
            "value is not async iterable"));
        return ts_value_make_undefined();
    }

    // GetIterator(value, async): method = GetMethod(obj, @@asyncIterator).
    // The property read runs getters, so an abrupt get propagates here.
    TsValue* method = ts_object_get_property(raw, "[Symbol.asyncIterator]");
    bool isAsyncIter = !agen_is_undef_or_null(method);
    if (isAsyncIter && !agen_is_callable(method)) {
        agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
            "[Symbol.asyncIterator] is not callable"));
        return ts_value_make_undefined();
    }
    if (!isAsyncIter) {
        // Fall back to the sync iterator (spec: CreateAsyncFromSyncIterator).
        method = ts_object_get_property(raw, "[Symbol.iterator]");
        if (agen_is_undef_or_null(method)) {
            agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
                "value is not async iterable"));
            return ts_value_make_undefined();
        }
        if (!agen_is_callable(method)) {
            agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
                "[Symbol.iterator] is not callable"));
            return ts_value_make_undefined();
        }
    }

    // iterator = ? Call(method, obj); must be an Object.
    TsValue* iter = ts_call_with_this_0(method, iterable);
    void* iterRaw = iter ? ts_value_get_object(iter) : nullptr;
    if (!iterRaw) {
        agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator method returned a non-object"));
        return ts_value_make_undefined();
    }

    TsValue* nextFn = ts_object_get_property(iterRaw, "next");
    if (!agen_is_callable(nextFn)) {
        agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator.next is not callable"));
        return ts_value_make_undefined();
    }

    // Eager drain: pull every value, awaiting promise-shaped results
    // (ts_promise_await pumps the loop and re-throws rejections), and
    // re-yield through the generator's queue. Bounded as a hang guard —
    // an infinite iterator can't be partially consumed in the eager model.
    extern TsValue* ts_promise_await(TsValue* promise);
    for (int64_t guard = 0; guard < 1000000; guard++) {
        TsValue* res = ts_call_with_this_0(nextFn, iter);
        TsValue rv = res ? nanbox_to_tagged(res) : TsValue();
        if (rv.type == ValueType::PROMISE_PTR) {
            res = ts_promise_await(res);
        }
        void* resRaw = res ? ts_value_get_object(res) : nullptr;
        if (!resRaw) {
            agen_protocol_throw((TsValue*)ts_error_create_typed("TypeError",
                "iterator result is not an object"));
            return ts_value_make_undefined();
        }
        extern bool ts_iterator_result_done(TsValue* result);
        extern TsValue* ts_iterator_result_value(TsValue* result);
        bool isDone = ts_iterator_result_done(res);
        TsValue* value = ts_iterator_result_value(res);
        TsValue vv = value ? nanbox_to_tagged(value) : TsValue();
        if (vv.type == ValueType::PROMISE_PTR) {
            value = ts_promise_await(value);
        }
        if (isDone) {
            g_agen_protocol_throw = false;
            return value ? value : ts_value_make_undefined();
        }
        ts_async_generator_yield(value);
    }
    g_agen_protocol_throw = false;
    return ts_value_make_undefined();
}

// gen.return(value) per spec §27.5.1.4: completes the generator, optionally
// running pending finally clauses with `value` as the return value. We
// don't yet support the finally-clause unwinding (would require compiler
// state-machine support), so this is the simplified semantics: mark done
// and produce {value, done:true}. For TsMap-based iterators (custom
// iterables), forward to their .return method if present.
TsValue* Generator_return(TsValue* genVal, TsValue* value) {
    if (!genVal) return create_generator_result(TsValue(), true);
    void* raw = ts_value_get_object(genVal);
    if (!raw) return create_generator_result(TsValue(), true);

    // Async generator: return() must produce a PROMISE of the iteration
    // result (same AGEN-magic forward Generator_next has — without it an
    // untyped agen.return() fell into the own-property path below and
    // synthesized a plain {value, done:true} object).
    if (ts_is_unchecked<TsAsyncGenerator>(raw)) {
        return AsyncGenerator_return(genVal, value);
    }

    if (ts_is_unchecked<TsGenerator>(raw)) {
        TsGenerator* gen = (TsGenerator*)raw;
        gen->done = true;
        TsValue v = value ? nanbox_to_tagged(value) : TsValue();
        return create_generator_result(v, true);
    }

    // Any non-generator receiver (a TsMap-based custom iterator OR a flat
    // inline-slot object) delegates to its own .return method. The compiler
    // routes every `x.return()` call here when x is untyped (GeneratorHandler
    // claims next/return/throw for className.empty()), so this must behave like
    // a normal method call for plain objects — use the flat-object/prototype
    // aware getter (ts_object_get_dynamic missed flat-object slots, so
    // `({ return(){...} }).return()` silently produced {value,done:true} and
    // never called the method).
    {
        extern TsValue* ts_object_get_property(void* o, const char* k);
        TsValue* retFn = ts_object_get_property(raw, "return");
        if (retFn) {
            TsValue rf = nanbox_to_tagged(retFn);
            if ((rf.type == ValueType::OBJECT_PTR || rf.type == ValueType::FUNCTION_PTR)
                && rf.ptr_val) {
                return ts_call_with_this_1(retFn, genVal,
                    value ? value : ts_value_make_undefined());
            }
        }
    }

    TsValue v = value ? nanbox_to_tagged(value) : TsValue();
    return create_generator_result(v, true);
}

// gen.throw(exception) per spec §27.5.1.3. For our simplified state
// machine, we don't yet support resuming into a try/catch that handles
// the injected exception — that needs compiler-side cooperation. So:
// mark the generator done and re-throw the exception, matching the
// behavior when no try/catch in the generator catches it.
TsValue* Generator_throw(TsValue* genVal, TsValue* exception) {
    if (!genVal) {
        ts_throw(exception);
        return ts_value_make_undefined();
    }
    void* raw = ts_value_get_object(genVal);
    if (!raw) {
        ts_throw(exception);
        return ts_value_make_undefined();
    }

    // Async generator: throw() must produce a (rejected) PROMISE, not a
    // synchronous re-throw (same AGEN-magic forward Generator_next has).
    if (ts_is_unchecked<TsAsyncGenerator>(raw)) {
        return AsyncGenerator_throw(genVal, exception);
    }

    if (ts_is_unchecked<TsGenerator>(raw)) {
        TsGenerator* gen = (TsGenerator*)raw;
        gen->done = true;
        ts_throw(exception);
        return ts_value_make_undefined();
    }

    // Any non-generator receiver (TsMap-based custom iterator OR flat
    // inline-slot object): forward to its own .throw method if present (same
    // flat-object-aware lookup as Generator_return), else re-throw.
    {
        extern TsValue* ts_object_get_property(void* o, const char* k);
        TsValue* throwFn = ts_object_get_property(raw, "throw");
        if (throwFn) {
            TsValue tf = nanbox_to_tagged(throwFn);
            if ((tf.type == ValueType::OBJECT_PTR || tf.type == ValueType::FUNCTION_PTR)
                && tf.ptr_val) {
                return ts_call_with_this_1(throwFn, genVal,
                    exception ? exception : ts_value_make_undefined());
            }
        }
    }

    ts_throw(exception);
    return ts_value_make_undefined();
}

// The generator currently executing its (eager) body. The compiler's
// async-generator prologue calls ts_async_generator_create(), the body's
// yields call ts_async_generator_yield(value), and the epilogue calls
// ts_async_generator_return(gen, v) — create pushes, return pops. A body
// that throws leaves its entry until the next return pops it (one-shot
// test processes make this acceptable; revisit with suspendable gens).
// Malloc-backed vector of GC pointers => scanner + minor fixup REQUIRED
// (.claude/rules/runtime-safety.md).
static std::vector<TsAsyncGenerator*> g_asyncgen_stack;

static void asyncgen_stack_gc_scan(void*) {
    for (TsAsyncGenerator* g : g_asyncgen_stack) {
        if (g) ts_gc_mark_object(g);
    }
}

static void asyncgen_stack_gc_fixup(void*) {
    for (TsAsyncGenerator*& g : g_asyncgen_stack) {
        if (g) {
            void* f = ts_gc_minor_lookup_forward(g);
            if (f) g = (TsAsyncGenerator*)f;
        }
    }
}

TsAsyncGenerator* ts_async_generator_create() {
    static bool registered = false;
    if (!registered) {
        registered = true;
        ts_gc_register_scanner(asyncgen_stack_gc_scan, nullptr);
        ts_gc_register_minor_fixup(asyncgen_stack_gc_fixup, nullptr);
    }
    AsyncContext* ctx = ts_async_context_create();
    void* mem = ts_alloc(sizeof(TsAsyncGenerator));
    TsAsyncGenerator* gen = new (mem) TsAsyncGenerator(ctx);
    ctx->generator = gen;
    g_asyncgen_stack.push_back(gen);
    return gen;
}

// Suspendable-model creation (GEN-001 Stage 2): like ts_async_generator_create
// but the body does NOT run eagerly, so there is no g_asyncgen_stack entry —
// yields reach the generator through ctx (ts_agen_suspend_yield), never the
// ambient stack. The generator is rooted by whoever holds the returned value.
// Takes the WRAPPER's AsyncContext (the one carrying resumeFn/data/this —
// identical convention to ts_generator_create(ctx)) so the impl and the
// generator's next() drive the SAME context. Null-tolerant for safety.
TsAsyncGenerator* ts_async_generator_create_suspendable(AsyncContext* ctx) {
    if (!ctx) ctx = ts_async_context_create();
    void* mem = ts_alloc(sizeof(TsAsyncGenerator));
    TsAsyncGenerator* gen = new (mem) TsAsyncGenerator(ctx);
    ctx->generator = gen;
    gen->suspendable = true;
    return gen;
}

// Suspend-at-yield (factor of ts_async_yield, ctx-routed): record the yielded
// value and resolve the current request's promise with {value, done:false}.
void ts_agen_suspend_yield(AsyncContext* ctx, TsValue* v) {
    if (!ctx) return;
    ctx->yielded = true;
    ctx->yieldedValue = v ? nanbox_to_tagged(v) : TsValue();

    if (ctx->pendingNextPromise) {
        ts_promise_resolve_internal(ctx->pendingNextPromise,
            create_generator_result(ctx->yieldedValue, false));
        ctx->pendingNextPromise = nullptr;
    }
}

// Normal completion: resolve the current request's promise with
// {value, done:true} and mark the generator completed.
void ts_agen_complete(AsyncContext* ctx, TsValue* v) {
    if (!ctx) return;
    ctx->yielded = false;
    if (ctx->generator) {
        ctx->generator->done = true;
    }
    if (ctx->pendingNextPromise) {
        ts_promise_resolve_internal(ctx->pendingNextPromise,
            create_generator_result(v ? nanbox_to_tagged(v) : TsValue(), true));
        ctx->pendingNextPromise = nullptr;
    }
}

// Abrupt (throw) completion: reject the current request's promise and mark
// the generator completed (ECMA-262 AsyncGeneratorReject + completed state).
void ts_agen_complete_reject(AsyncContext* ctx, TsValue* exc) {
    if (!ctx) return;
    ctx->yielded = false;
    if (ctx->generator) {
        ctx->generator->done = true;
    }
    if (ctx->pendingNextPromise) {
        ts_promise_reject_internal(ctx->pendingNextPromise,
            exc ? exc : ts_value_make_undefined());
        ctx->pendingNextPromise = nullptr;
    }
}

// Spec AsyncGeneratorYield step 1: Await(value). Pump-await iff the operand is
// promise-shaped; a rejection ts_throws INSIDE the caller's frame (so a user
// try/catch around the yield catches it). Non-promises pass through untouched.
TsValue* ts_agen_await_operand(TsValue* v) {
    if (!v) return v;
    TsValue tv = nanbox_to_tagged(v);
    if (tv.type == ValueType::PROMISE_PTR) {
        extern TsValue* ts_promise_await(TsValue* promise);
        return ts_promise_await(v);
    }
    return v;
}

// GetIterator(value, async) for the suspendable yield* lowering — the same
// protocol sequence as the eager ts_async_generator_yield_star prologue
// (@@asyncIterator lookup, sync-@@iterator fallback per
// CreateAsyncFromSyncIterator, callable/object checks), but throwing via plain
// ts_throw: in the suspendable model the impl's own barrier converts the throw
// into a rejected request promise, so no g_agen_protocol_throw flag is needed.
// The eager path is untouched.
TsValue* ts_agen_get_async_iterator(TsValue* iterable) {
    extern TsValue* ts_object_get_property(void* o, const char* k);

    void* raw = iterable ? ts_value_get_object(iterable) : nullptr;
    if (!raw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "value is not async iterable"));
        return ts_value_make_undefined();
    }

    // GetIterator(value, async): method = GetMethod(obj, @@asyncIterator).
    // The property read runs getters, so an abrupt get propagates here.
    TsValue* method = ts_object_get_property(raw, "[Symbol.asyncIterator]");
    bool isAsyncIter = !agen_is_undef_or_null(method);
    if (isAsyncIter && !agen_is_callable(method)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "[Symbol.asyncIterator] is not callable"));
        return ts_value_make_undefined();
    }
    if (!isAsyncIter) {
        // Fall back to the sync iterator (spec: CreateAsyncFromSyncIterator).
        method = ts_object_get_property(raw, "[Symbol.iterator]");
        if (agen_is_undef_or_null(method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "value is not async iterable"));
            return ts_value_make_undefined();
        }
        if (!agen_is_callable(method)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "[Symbol.iterator] is not callable"));
            return ts_value_make_undefined();
        }
    }

    // iterator = ? Call(method, obj); must be an Object.
    TsValue* iter = ts_call_with_this_0(method, iterable);
    void* iterRaw = iter ? ts_value_get_object(iter) : nullptr;
    if (!iterRaw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator method returned a non-object"));
        return ts_value_make_undefined();
    }

    // Legacy iterator shape: several runtime @@iterator implementations
    // return a plain ARRAY ("iterator-like" per the conformance notes)
    // rather than a {next} object. ts_agen_delegate_step walks these by
    // index (the same tolerance iterator_concat_next and the eager drain's
    // callers rely on), so don't reject them for a missing .next here.
    if (*(uint32_t*)iterRaw == 0x41525259) { // TsArray "ARRY"
        return iter;
    }

    TsValue* nextFn = ts_object_get_property(iterRaw, "next");
    if (!agen_is_callable(nextFn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator.next is not callable"));
        return ts_value_make_undefined();
    }

    return iter;
}

// ONE step of suspendable yield* delegation (GEN-001 Stage 4b): performs
// IteratorNext with the shape tolerances the EAGER drain has:
// - plain-TsArray "iterator-likes" are walked by index (cursor persisted in
//   ctx->delegateIndex, which ts_async_context_set_delegate_iterator resets),
//   returning a FRESH {value, done} result per element;
// - promise-shaped step results are pump-awaited (rejections ts_throw into
//   the impl frame, caught by the impl barrier / user try);
// - a non-object step result is the protocol TypeError.
// Returns the iteration-result object.
TsValue* ts_agen_delegate_step(AsyncContext* ctx, TsValue* iterator, TsValue* sentArg) {
    extern TsValue* ts_object_get_property(void* o, const char* k);
    extern TsValue* ts_promise_await(TsValue* promise);

    void* raw = iterator ? ts_value_get_object(iterator) : nullptr;
    if (!raw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator method returned a non-object"));
        return ts_value_make_undefined();
    }

    // Legacy array-shaped iterator: walk by index with fresh result objects
    // (proven pattern: iterator_concat_next in TsGlobals.cpp).
    if (*(uint32_t*)raw == 0x41525259) { // TsArray "ARRY"
        TsArray* arr = (TsArray*)raw;
        int64_t idx = ctx ? ctx->delegateIndex : 0;
        if (idx >= (int64_t)arr->Length()) {
            if (ctx) ctx->delegateIndex = 0;
            return create_generator_result(TsValue(), true);
        }
        if (ctx) ctx->delegateIndex = idx + 1;
        TsValue* elem = arr->GetElementBoxed((size_t)idx);
        return create_generator_result(
            elem ? nanbox_to_tagged(elem) : TsValue(), false);
    }

    TsValue* nextFn = ts_object_get_property(raw, "next");
    // Only reject definitively-absent next; some runtime iterator shapes
    // carry callables that evade the magic check, and a non-callable still
    // surfaces as TypeError via the result-not-object check below.
    if (!nextFn || agen_is_undef_or_null(nextFn)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator.next is not callable"));
        return ts_value_make_undefined();
    }

    TsValue* res = sentArg
        ? ts_call_with_this_1(nextFn, iterator, sentArg)
        : ts_call_with_this_0(nextFn, iterator);

    // Async iterators return a promise of the result object: await it. A
    // rejection ts_throws inside the caller's impl frame (matching the eager
    // drain's ts_promise_await points).
    TsValue rv = res ? nanbox_to_tagged(res) : TsValue();
    if (rv.type == ValueType::PROMISE_PTR) {
        res = ts_promise_await(res);
    }

    void* resRaw = res ? ts_value_get_object(res) : nullptr;
    if (!resRaw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator result is not an object"));
        return ts_value_make_undefined();
    }
    return res;
}

// GEN-001 Stage 7: forward a throw/return resume completion arriving while the
// generator is suspended inside yield* to the DELEGATE iterator, per
// ECMA-262 27.6.3.7 (yield* runtime semantics, throw/return paths).
// mode: 1 = throw (gen.throw(arg)), 2 = return (gen.return(arg)).
//
// Return convention (chosen to need the fewest IR changes in the lowered
// delegation loop): returns the inner step-RESULT OBJECT whenever delegation
// continues — the compiler-side loop feeds it through the same done-check as
// a ts_agen_delegate_step result (not done -> yield the value, stay suspended
// in the yield*; done -> the yield* completes with the result's value and the
// outer body CONTINUES after it). Returns NULL when the GENERATOR itself
// completes (return-completion cases) — the helper has already settled the
// current request via ts_agen_complete; the lowered code branches to a
// pop-handlers + ret-void suspend path. Protocol violations ts_throw (the
// caller re-armed the enclosing user try handlers before calling, so the
// throw is catchable; otherwise the impl barrier rejects the request).
TsValue* ts_agen_delegate_resume(AsyncContext* ctx, TsValue* iterator,
                                 int mode, TsValue* arg) {
    extern TsValue* ts_object_get_property(void* o, const char* k);
    extern TsValue* ts_promise_await(TsValue* promise);

    void* raw = iterator ? ts_value_get_object(iterator) : nullptr;
    // Legacy plain-TsArray "iterator-likes" (see ts_agen_delegate_step) carry
    // no throw/return methods.
    bool isLegacyArray = raw && *(uint32_t*)raw == 0x41525259; // "ARRY"

    TsValue* method = nullptr;
    if (raw && !isLegacyArray) {
        method = ts_object_get_property(
            raw, mode == AGEN_MODE_THROW ? "throw" : "return");
    }
    bool hasMethod = method && agen_is_callable(method);

    // Generator-object delegates (yield* over a sync or async generator)
    // surface NO "throw"/"return" own properties — their semantics live in
    // the Generator_*/AsyncGenerator_* built-ins (GeneratorHandler dispatch).
    // They DO have the methods per spec, so don't take the missing-method
    // paths; forward to the built-ins instead (a sync Generator_throw
    // ts_throws the uncaught exception, propagating out of the yield* to the
    // re-armed user handlers / impl barrier, matching spec propagation).
    bool isGenObject = raw && !isLegacyArray &&
        (ts_is_unchecked<TsGenerator>(raw) ||
         ts_is_unchecked<TsAsyncGenerator>(raw));

    if (!hasMethod && !isGenObject) {
        if (mode == AGEN_MODE_RETURN) {
            // 27.6.3.7 return path, return method undefined: Await the sent
            // value (async generator kind), then the generator completes with
            // it — no iterator close required.
            if (ctx) ctx->delegateIndex = 0;
            ts_agen_complete(ctx, ts_agen_await_operand(arg));
            return nullptr;
        }
        // THROW with no throw method: AsyncIteratorClose(iterator, normal) —
        // call return() if present (awaited; a rejection/throw propagates,
        // replacing the TypeError), result absorbed — then throw TypeError
        // (spec NOTE: the iterator protocol was violated).
        if (raw && !isLegacyArray) {
            TsValue* retFn = ts_object_get_property(raw, "return");
            if (retFn && agen_is_callable(retFn)) {
                TsValue* closeRes = ts_call_with_this_0(retFn, iterator);
                TsValue crv = closeRes ? nanbox_to_tagged(closeRes) : TsValue();
                if (crv.type == ValueType::PROMISE_PTR) {
                    ts_promise_await(closeRes);
                }
            }
        }
        if (ctx) ctx->delegateIndex = 0;
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "The iterator does not provide a 'throw' method"));
        return ts_value_make_undefined();
    }

    // innerResult = ? Call(method, iterator, [arg]); Await it (async kind).
    TsValue* res;
    if (hasMethod) {
        res = arg ? ts_call_with_this_1(method, iterator, arg)
                  : ts_call_with_this_0(method, iterator);
    } else if (ts_is_unchecked<TsGenerator>(raw)) {
        // Sync generator delegate built-ins: Generator_throw ts_throws an
        // uncaught exception (never returns); Generator_return produces the
        // {value, done:true} result handled below.
        res = (mode == AGEN_MODE_THROW) ? Generator_throw(iterator, arg)
                                        : Generator_return(iterator, arg);
    } else {
        // Async generator delegate built-ins: both return a PROMISE of the
        // iteration result — the await below unwraps it (a rejection
        // ts_throws, propagating the abrupt completion out of the yield*).
        res = (mode == AGEN_MODE_THROW) ? AsyncGenerator_throw(iterator, arg)
                                        : AsyncGenerator_return(iterator, arg);
    }
    TsValue rv = res ? nanbox_to_tagged(res) : TsValue();
    if (rv.type == ValueType::PROMISE_PTR) {
        res = ts_promise_await(res);
    }
    void* resRaw = res ? ts_value_get_object(res) : nullptr;
    if (!resRaw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "iterator result is not an object"));
        return ts_value_make_undefined();
    }

    if (mode == AGEN_MODE_RETURN && ts_iterator_result_done(res)) {
        // Return-completion with done: the GENERATOR completes with the
        // result's value (finally unwinding is a future stage, matching the
        // forced-return path).
        TsValue* value = ts_iterator_result_value(res);
        TsValue vv = value ? nanbox_to_tagged(value) : TsValue();
        if (vv.type == ValueType::PROMISE_PTR) {
            value = ts_promise_await(value);
        }
        if (ctx) ctx->delegateIndex = 0;
        ts_agen_complete(ctx, value);
        return nullptr;
    }

    // THROW results (done or not) and RETURN-not-done results flow back into
    // the lowered done-check: done -> the yield* completes with the value and
    // the body continues; not done -> the value is yielded (awaited by the
    // yield path) and the generator stays suspended inside the yield*.
    return res;
}

TsValue* ts_async_generator_yield(TsValue* value) {
    if (!g_asyncgen_stack.empty()) {
        TsAsyncGenerator* gen = g_asyncgen_stack.back();
        if (!gen->pendingYields) {
            gen->pendingYields = TsArray::Create();
            ts_gc_write_barrier(&gen->pendingYields, gen->pendingYields);
        }
        gen->pendingYields->Push(
            (int64_t)(value ? value : ts_value_make_undefined()));
    }
    // Eager model: the value a future next(v) would send back is unknowable
    // here; the yield expression evaluates to undefined.
    return ts_value_make_undefined();
}

TsValue* AsyncGenerator_next_internal(void* context, TsValue* value) {
    TsAsyncGenerator* gen = (TsAsyncGenerator*)context;
    if (!gen) return nullptr;
    TsPromise* p = gen->next(value);
    return ts_value_make_promise(p);
}

TsValue* AsyncGenerator_next(TsValue* genVal, TsValue* value) {
    return AsyncGenerator_next_internal(ts_value_get_object(genVal), value);
}

// agen.return(value), ECMA-262 27.6.1.3 (simplified): always returns a
// PROMISE. Eager model: complete the generator and resolve {value, done:true}
// (finally-block unwinding needs the suspendable machinery, Stage 6).
// Suspendable model: enqueue a RETURN-mode request.
// These symbols were emitted by GeneratorHandler for typed AsyncGenerator
// receivers but never defined anywhere until GEN-001 Stage 2.
TsValue* AsyncGenerator_return(TsValue* genVal, TsValue* value) {
    void* raw = genVal ? ts_value_get_object(genVal) : nullptr;
    TsAsyncGenerator* gen =
        (raw && ts_is_unchecked<TsAsyncGenerator>(raw)) ? (TsAsyncGenerator*)raw
                                                        : nullptr;
    if (gen && gen->suspendable) {
        return ts_value_make_promise(
            agen_enqueue_request(gen, value, AGEN_MODE_RETURN));
    }
    if (gen) {
        gen->done = true;
    }
    TsPromise* p = ts_promise_create();
    ts_promise_resolve_internal(p,
        create_generator_result(value ? nanbox_to_tagged(value) : TsValue(), true));
    return ts_value_make_promise(p);
}

// agen.throw(exc), ECMA-262 27.6.1.4 (simplified): always returns a PROMISE.
// Eager model: complete the generator and reject with exc (resuming into a
// body try/catch needs the suspendable machinery, Stage 6). Suspendable
// model: enqueue a THROW-mode request.
TsValue* AsyncGenerator_throw(TsValue* genVal, TsValue* exc) {
    void* raw = genVal ? ts_value_get_object(genVal) : nullptr;
    TsAsyncGenerator* gen =
        (raw && ts_is_unchecked<TsAsyncGenerator>(raw)) ? (TsAsyncGenerator*)raw
                                                        : nullptr;
    if (gen && gen->suspendable) {
        return ts_value_make_promise(
            agen_enqueue_request(gen, exc, AGEN_MODE_THROW));
    }
    if (gen) {
        gen->done = true;
    }
    TsPromise* p = ts_promise_create();
    ts_promise_reject_internal(p, exc ? exc : ts_value_make_undefined());
    return ts_value_make_promise(p);
}

void ts_async_generator_return(TsAsyncGenerator* gen, TsValue* value) {
    if (!gen) return;
    gen->done = true;
    gen->returnValue = value ? nanbox_to_tagged(value) : TsValue();
    if (gen->ctx) {
        gen->ctx->yieldedValue = gen->returnValue;
    }
    // Pop the eager-body stack entry pushed by ts_async_generator_create.
    if (!g_asyncgen_stack.empty() && g_asyncgen_stack.back() == gen) {
        g_asyncgen_stack.pop_back();
    }
}

// Uncaught throw escaping the eager async-generator body: the compiler's
// async-gen prologue barrier (HIRToLLVM) lands here instead of letting the
// throw escape gen() synchronously. Recorded; the first next() after any
// queued yields drain returns a REJECTED promise (ECMA-262: the throw
// completes the generator).
void ts_async_generator_set_exception(TsAsyncGenerator* gen, TsValue* exc) {
    if (!gen) return;
    gen->hasException = true;
    gen->pendingException = exc ? nanbox_to_tagged(exc) : TsValue();
    // Body ended abruptly — ts_async_generator_return never ran, so pop here.
    if (!g_asyncgen_stack.empty() && g_asyncgen_stack.back() == gen) {
        g_asyncgen_stack.pop_back();
    }
}

// Compiler marker emitted after the parameter prologue of an async-generator
// body: from here on an uncaught throw REJECTS the first next() promise
// (ECMA-262 body semantics); parameter-binding errors before the marker
// keep escaping gen() synchronously.
void ts_async_generator_body_started() {
    if (!g_asyncgen_stack.empty()) {
        g_asyncgen_stack.back()->bodyStarted = true;
    }
}

// Sync-generator parameter-prologue marker. HIRToLLVM lowers this as the
// SuspendedStart suspension (state 0 -> 1) and never emits a real call, so
// this is only a defensive no-op for the (unreached) generic-call fallback.
void ts_generator_body_started() {}

// Non-protocol throw escaping the eager body synchronously (e.g. a
// parameter-binding error): unwind the eager-body stack entry before the
// compiler's agen.rethrow landing pad re-throws to the outer handler.
void ts_async_generator_abort(TsAsyncGenerator* gen) {
    if (!gen) return;
    gen->done = true;
    if (!g_asyncgen_stack.empty() && g_asyncgen_stack.back() == gen) {
        g_asyncgen_stack.pop_back();
    }
}

// Magic number for AsyncArrayIterator
static constexpr uint32_t ASYNC_ARRAY_ITER_MAGIC = 0x41414954; // "AAIT"

extern "C" TsValue* ts_async_iterator_get(TsValue* iterable) {
    if (!iterable) {
        return nullptr;
    }

    TsValue iterVal = nanbox_to_tagged(iterable);

    // Check if it's an array (ARRAY_PTR type = 7) - wrap it in an async iterator
    if (iterVal.type == ValueType::ARRAY_PTR && iterVal.ptr_val) {
        if (ts_is_unchecked<TsArray>(iterVal.ptr_val)) {
            // Create AsyncArrayIterator wrapper
            void* mem = ts_alloc(sizeof(AsyncArrayIterator));
            AsyncArrayIterator* iter = new (mem) AsyncArrayIterator((TsArray*)iterVal.ptr_val);
            // Set magic for identification
            *(uint32_t*)mem = ASYNC_ARRAY_ITER_MAGIC;
            return ts_value_make_object(iter);
        }
    }

    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        TsString* key = TsString::Create("[Symbol.asyncIterator]");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterVal.ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterVal.ptr_val, bucket, &method_type, &method_val);
            // Check for both OBJECT_PTR and FUNCTION_PTR since ts_value_make_function uses FUNCTION_PTR
            if (method_type == (uint8_t)ValueType::OBJECT_PTR || method_type == (uint8_t)ValueType::FUNCTION_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*AsyncIterFunc)(void*);
                return ((AsyncIterFunc)func->funcPtr)(func->context);
            }
        }
    }

    return iterable;
}

extern "C" TsValue* ts_async_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) {
        return nullptr;
    }

    TsValue iterVal = nanbox_to_tagged(iterator);

    // Check if it's our AsyncArrayIterator
    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        uint32_t magic = *(uint32_t*)iterVal.ptr_val;
        if (magic == ASYNC_ARRAY_ITER_MAGIC) {
            AsyncArrayIterator* iter = (AsyncArrayIterator*)iterVal.ptr_val;
            int64_t len = iter->array->Length();

            if (iter->index >= len) {
                // Done - return resolved promise with { value: undefined, done: true }
                TsValue undef;
                undef.type = ValueType::UNDEFINED;
                undef.i_val = 0;
                TsValue* result = create_generator_result(undef, true);

                // Wrap in resolved promise
                TsPromise* p = ts_promise_create();
                ts_promise_resolve_internal(p, result);
                return ts_value_make_promise(p);
            }

            // Get current element - stored as NaN-boxed TsValue* in array
            TsValue* elemBoxed = (TsValue*)iter->array->Get(iter->index);
            iter->index++;

            // Create a new promise that resolves with { value, done: false } when elem resolves
            TsPromise* resultPromise = ts_promise_create();

            // Decode the element using nanbox_to_tagged
            TsValue elemDecoded = nanbox_to_tagged(elemBoxed);

            // Check for Promise
            if ((elemDecoded.type == ValueType::OBJECT_PTR || elemDecoded.type == ValueType::PROMISE_PTR)
                && elemDecoded.ptr_val) {
                void* elemPtr = elemDecoded.ptr_val;
                // Check if it's a TsPromise (validated, offset-derived tag).
                if (ts_is_unchecked<TsPromise>(elemPtr)) {
                    TsPromise* elemPromise = (TsPromise*)elemPtr;
                        // When elemPromise resolves, resolve resultPromise with { value, done: false }
                        TsValue onFulfilled;
                        onFulfilled.type = ValueType::OBJECT_PTR;

                        // Create a function that wraps the resolved value
                        struct WrapContext {
                            TsPromise* resultPromise;
                        };
                        WrapContext* ctx = (WrapContext*)ts_alloc(sizeof(WrapContext));
                        ctx->resultPromise = resultPromise;

                        void* wrapFuncAddr = (void*)(+[](void* context, TsValue* resolvedValue) -> TsValue* {
                            WrapContext* ctx = (WrapContext*)context;
                            TsValue* iterResult = create_generator_result(nanbox_to_tagged(resolvedValue), false);
                            ts_promise_resolve_internal(ctx->resultPromise, iterResult);
                            return nullptr;
                        });
                        // Use placement new with constructor to properly set up vtable
                        TsFunction* wrapFunc = new (ts_alloc(sizeof(TsFunction))) TsFunction(wrapFuncAddr, ctx, FunctionType::COMPILED);

                        onFulfilled.ptr_val = wrapFunc;
                        elemPromise->then(onFulfilled);
                        return ts_value_make_promise(resultPromise);
                    }
                }

            // Non-promise value - resolve immediately
            TsValue* iterResult = create_generator_result(elemDecoded, false);
            ts_promise_resolve_internal(resultPromise, iterResult);
            return ts_value_make_promise(resultPromise);
        }
    }

    if (iterVal.type == ValueType::OBJECT_PTR && iterVal.ptr_val) {
        TsString* key = TsString::Create("next");
        // Use scalar helpers directly
        uint64_t hash = (uint64_t)key; // Use pointer as hash
        int64_t bucket = __ts_map_find_bucket(iterVal.ptr_val, hash, (uint8_t)ValueType::STRING_PTR, (int64_t)key);
        if (bucket >= 0) {
            uint8_t method_type;
            int64_t method_val;
            __ts_map_get_value_at(iterVal.ptr_val, bucket, &method_type, &method_val);
            // Check for both OBJECT_PTR and FUNCTION_PTR since ts_value_make_function uses FUNCTION_PTR
            if (method_type == (uint8_t)ValueType::OBJECT_PTR || method_type == (uint8_t)ValueType::FUNCTION_PTR) {
                TsFunction* func = (TsFunction*)method_val;
                typedef TsValue* (*NextFunc)(void*, TsValue*);
                return ((NextFunc)func->funcPtr)(func->context, value);
            }
        }
    }

    return AsyncGenerator_next(iterator, value);
}

void ts_async_generator_resolve(AsyncContext* ctx, TsValue* value, bool done) {
    if (!ctx->generator) return;
    ctx->generator->done = done;
    if (ctx->pendingNextPromise) {
        TsValue* res = create_generator_result(nanbox_to_tagged(value), done);
        ts_promise_resolve_internal(ctx->pendingNextPromise, res);
        ctx->pendingNextPromise = nullptr;
    }
}

// Array.prototype[@@iterator] mutation tracking (defined in TsArray.cpp).
// Declared at file scope so the references in ts_iterator_get link with C
// linkage (block-scope `extern "C"` is illegal).
extern "C" uint64_t g_array_prototype_version;
extern "C" bool g_array_default_iterator_deleted;
extern "C" TsMap* g_array_prototype_map;

// yield* delegation support - get an iterator from an iterable
TsValue* ts_iterator_get(TsValue* iterable) {
    if (!iterable) {
        return nullptr;
    }

    // First, try to extract the raw object pointer using ts_value_get_object
    // This handles both boxed TsValue* and raw object pointers
    void* rawObj = ts_value_get_object(iterable);
    // ts_value_get_object returns nullptr for primitive strings (which are
    // valid iterables per spec). Fall back to extracting the string pointer
    // directly so the TsString branch below can fire.
    if (!rawObj) {
        void* maybeStr = ts_value_get_string(iterable);
        if (maybeStr) {
            uint32_t m = *(uint32_t*)maybeStr;
            if (m == 0x53545247) rawObj = maybeStr;  // TsString::MAGIC
        }
    }

    // Check if we have a TsMap-based object (TsMap, TsGenerator, TsAsyncGenerator)
    if (rawObj) {
        // Set/Map COLLECTIONS: their [Symbol.iterator] is exposed through the
        // Set/Map property getter (ts_set/map_get_property), NOT the raw
        // TsMap::Get lookups below, so the old code returned the collection
        // itself -> next() reported done immediately -> spread `[...set]` /
        // `[...map]`, Array.from(set/map), and Map.groupBy came out empty.
        // Build the proper iterator directly (Set -> values; Map -> entries).
        // Exclude objects that are already iterators (they carry __iter_items).
        if ((uintptr_t)rawObj >= 0x1000) {
            uint32_t m16 = *(uint32_t*)((char*)rawObj + 16);
            bool isIterAlready = false;
            if (m16 == 0x4D415053) {  // TsMap "MAPS"
                TsValue ik; ik.type = ValueType::STRING_PTR;
                ik.ptr_val = TsString::GetInterned("__iter_items");
                isIterAlready = ((TsMap*)rawObj)->Has(ik);
            }
            if (!isIterAlready) {
                if (m16 == 0x53455453) {  // TsSet "SETS"
                    extern void* ts_set_values(void* set);
                    extern void* ts_create_set_iterator(void* items);
                    return (TsValue*)ts_create_set_iterator(ts_set_values(rawObj));
                }
                if (m16 == 0x4D415053 && ((TsMap*)rawObj)->IsExplicitMap()) {
                    extern void* ts_map_entries(void* map);
                    extern void* ts_create_map_iterator(void* items);
                    return (TsValue*)ts_create_map_iterator(ts_map_entries(rawObj));
                }
            }
        }

        // Map, Generator, or AsyncGenerator (all TsMap-based, tag at offset 16).
        if (ts_is_unchecked<TsMap>(rawObj) ||
            ts_is_unchecked<TsGenerator>(rawObj) ||
            ts_is_unchecked<TsAsyncGenerator>(rawObj)) {
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                // Call via the proper ts_call_0 dispatcher so user-written
                // [Symbol.iterator]() native functions (which expect
                // (ctx, argc, argv)) are invoked correctly. Direct
                // `funcPtr(context)` only worked for functions that ignore
                // argc/argv like TsGenerator's identity [Symbol.iterator].
                TsValue* boxedFn = (TsValue*)iterMethod.ptr_val;
                return tsCall(boxedFn);
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }

        // Also try magic at offset 8 for some TsMap layouts
        uint32_t magic8 = *(uint32_t*)((char*)rawObj + 8);
        if (magic8 == 0x4D415053) { // TsMap::MAGIC
            TsMap* obj = (TsMap*)rawObj;

            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                // Call via the proper ts_call_0 dispatcher so user-written
                // [Symbol.iterator]() native functions (which expect
                // (ctx, argc, argv)) are invoked correctly. Direct
                // `funcPtr(context)` only worked for functions that ignore
                // argc/argv like TsGenerator's identity [Symbol.iterator].
                TsValue* boxedFn = (TsValue*)iterMethod.ptr_val;
                return tsCall(boxedFn);
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                return iterable;
            }
        }
    }

    // Check for raw TsArray* pointer (TsArray has no vtable, magic at offset 0)
    TsValue iterDecoded = nanbox_to_tagged(iterable);
    if (rawObj) {
        if (ts_is_unchecked<TsArray>(rawObj)) { // TsArray (tag at offset 0)
            TsArray* arr = (TsArray*)rawObj;
            // ECMA-262 GetIterator(obj): honor an OWN overridden @@iterator
            // (`arr[Symbol.iterator] = fn`) instead of the built-in array
            // iterator. The default lives on Array.prototype (inherited), so an
            // entry in the instance's own `properties` side-map is necessarily
            // a user override — call it with `this` = the array. Without this,
            // `var [a,b] = arrWithCustomIter` read the raw elements, ignoring
            // the override (and never ran a custom next()).
            extern TsValue* ts_call_with_this_0(TsValue*, TsValue*);
            if (arr->properties) {
                TsValue ik; ik.type = ValueType::STRING_PTR;
                ik.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                if (arr->properties->Has(ik)) {
                    TsValue m = arr->properties->Get(ik);
                    if ((m.type == ValueType::OBJECT_PTR ||
                         m.type == ValueType::FUNCTION_PTR) && m.ptr_val) {
                        return ts_call_with_this_0((TsValue*)m.ptr_val, iterable);
                    }
                    // Own @@iterator present but undefined/non-callable:
                    // GetIterator throws TypeError (no fall-back to the
                    // built-in array iterator).
                    ts_throw((TsValue*)ts_error_create_typed(
                        "TypeError", "(intermediate value)[Symbol.iterator] is not a function"));
                    return nullptr;  // unreachable (ts_throw longjmps)
                }
            }
            // ECMA-262 GetIterator also consults Array.prototype[@@iterator].
            // ts-aot serves the default iterator from a built-in fast path, so
            // a user mutation of Array.prototype[Symbol.iterator] (override or
            // `delete`) is only observable via the prototype map / deleted
            // flag. Gate on the version counter so the unmutated path stays hot.
            {
                if (g_array_prototype_version != 0 && g_array_prototype_map) {
                    TsValue pk; pk.type = ValueType::STRING_PTR;
                    pk.ptr_val = TsString::GetInterned("[Symbol.iterator]");
                    if (g_array_prototype_map->Has(pk)) {
                        TsValue pm = g_array_prototype_map->Get(pk);
                        if ((pm.type == ValueType::OBJECT_PTR ||
                             pm.type == ValueType::FUNCTION_PTR) && pm.ptr_val) {
                            // Overridden Array.prototype[@@iterator]. A re-assigned
                            // default reads back as the array `values` native, so
                            // this also restores normal behavior after a restore.
                            return ts_call_with_this_0((TsValue*)pm.ptr_val, iterable);
                        }
                        ts_throw((TsValue*)ts_error_create_typed(
                            "TypeError", "Array.prototype[Symbol.iterator] is not a function"));
                        return nullptr;  // unreachable
                    }
                    if (g_array_default_iterator_deleted) {
                        // `delete Array.prototype[Symbol.iterator]` -> no iterator.
                        ts_throw((TsValue*)ts_error_create_typed(
                            "TypeError", "Array.prototype[Symbol.iterator] is not a function"));
                        return nullptr;  // unreachable
                    }
                }
            }
            // Update decoded value for array iterator path below
            iterDecoded.type = ValueType::ARRAY_PTR;
            iterDecoded.ptr_val = arr;
            // Fall through to ARRAY_PTR check below
        }
        // TsString: per spec, String.prototype[@@iterator] yields each
        // Unicode code point as a one-character string. We decode the
        // string into a TsArray of one-codepoint strings up-front, then
        // reuse the array-iterator path below. This is O(n) memory but
        // correct for surrogate pairs (e.g. "a😀b" yields "a","😀","b").
        // Without this branch, for-of on a string previously fell through
        // to `return iterable` and the resulting non-iterator caused an
        // infinite alloc loop in the for-of next() polling.
        if (ts_is_unchecked<TsString>(rawObj)) { // TsString (tag at offset 0)
            TsString* s = (TsString*)rawObj;
            int64_t len = s->Length();  // code-unit length
            TsArray* arr = (TsArray*)ts_array_create();
            int64_t i = 0;
            while (i < len) {
                int64_t cp = s->CodePointAt(i);
                int64_t cps[1] = {cp};
                TsString* part = TsString::FromCodePoint(cps, 1);
                TsValue v; v.type = ValueType::STRING_PTR; v.ptr_val = part;
                arr->Push((int64_t)(uintptr_t)nanbox_from_tagged(v));
                // Advance past surrogate pair if needed.
                i += (cp > 0xFFFF) ? 2 : 1;
            }
            iterDecoded.type = ValueType::ARRAY_PTR;
            iterDecoded.ptr_val = arr;
        }
    }

    // Fall back to type-based check for explicit OBJECT_PTR values
    if (iterDecoded.type == ValueType::OBJECT_PTR) {
        TsMap* obj = (TsMap*)iterDecoded.ptr_val;
        if (obj) {
            // Check for [Symbol.iterator] method
            TsString* iterKey = TsString::Create("[Symbol.iterator]");
            TsValue keyVal;
            keyVal.type = ValueType::STRING_PTR;
            keyVal.ptr_val = iterKey;
            TsValue iterMethod = obj->Get(keyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((iterMethod.type == ValueType::OBJECT_PTR || iterMethod.type == ValueType::FUNCTION_PTR) && iterMethod.ptr_val) {
                TsFunction* func = (TsFunction*)iterMethod.ptr_val;
                if (func->funcPtr) {
                    typedef TsValue* (*IterFunc)(void*);
                    return ((IterFunc)func->funcPtr)(func->context);
                }
            }

            // Check if it already has a next method (is already an iterator)
            TsString* nextKey = TsString::Create("next");
            TsValue nextKeyVal;
            nextKeyVal.type = ValueType::STRING_PTR;
            nextKeyVal.ptr_val = nextKey;
            TsValue nextMethod = obj->Get(nextKeyVal);
            // Check for both OBJECT_PTR and FUNCTION_PTR since functions can be stored with either type
            if ((nextMethod.type == ValueType::OBJECT_PTR || nextMethod.type == ValueType::FUNCTION_PTR) && nextMethod.ptr_val) {
                // Already an iterator, return as-is
                return iterable;
            }
        }
    }

    // For arrays, create an array iterator
    if (iterDecoded.type == ValueType::ARRAY_PTR) {
        TsArray* arr = (TsArray*)iterDecoded.ptr_val;
        if (arr) {
            // Create a simple array iterator object
            TsMap* iterator = TsMap::Create();

            // Create properly typed keys for internal properties
            TsValue arrKey, idxKey;
            arrKey.type = ValueType::STRING_PTR;
            arrKey.ptr_val = TsString::Create("__arr");
            idxKey.type = ValueType::STRING_PTR;
            idxKey.ptr_val = TsString::Create("__idx");

            iterator->Set(arrKey, iterDecoded);
            iterator->Set(idxKey, TsValue((int64_t)0));

            // Create the next function that iterates over the array
            TsValue nextFunc = nanbox_to_tagged(ts_value_make_function((void*)(TsValue*(*)(void*, TsValue*))[](void* ctx, TsValue* arg) -> TsValue* {
                TsMap* self = (TsMap*)ctx;
                if (!self) return create_generator_result(TsValue(), true);

                TsValue arrKey, idxKey;
                arrKey.type = ValueType::STRING_PTR;
                arrKey.ptr_val = TsString::Create("__arr");
                idxKey.type = ValueType::STRING_PTR;
                idxKey.ptr_val = TsString::Create("__idx");

                TsValue arrVal = self->Get(arrKey);
                TsValue idxVal = self->Get(idxKey);

                TsArray* arr = (TsArray*)arrVal.ptr_val;
                int64_t idx = idxVal.i_val;

                if (!arr) {
                    return create_generator_result(TsValue(), true);
                }

                size_t len = arr->Length();

                if (idx >= (int64_t)len) {
                    return create_generator_result(TsValue(), true);
                }

                // Get the value and increment index
                // Use GetElementBoxed which returns a proper TsValue*, not Get() which returns raw int64_t
                TsValue* elem = arr->GetElementBoxed(idx);
                self->Set(idxKey, TsValue(idx + 1));

                if (elem) {
                    return create_generator_result(nanbox_to_tagged(elem), false);
                } else {
                    TsValue undef;
                    undef.type = ValueType::UNDEFINED;
                    return create_generator_result(undef, false);
                }
            }, iterator));

            TsValue nextKey;
            nextKey.type = ValueType::STRING_PTR;
            nextKey.ptr_val = TsString::Create("next");
            iterator->Set(nextKey, nextFunc);
            return ts_value_make_object(iterator);
        }
    }

    return iterable;
}

// Call next on an iterator
TsValue* ts_iterator_next(TsValue* iterator, TsValue* value) {
    if (!iterator) return nullptr;

    void* rawObj = ts_value_get_object(iterator);
    if (!rawObj) rawObj = iterator;
    if (rawObj) {
        // Look up `next` via the PROTOTYPE-WALKING property getter. Built-in
        // Map/Set/Array iterators keep next() on their prototype, which the old
        // raw TsMap::Get did not traverse -> it found nothing and returned done
        // immediately (spread/Array.from over those iterators came out empty).
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* nextFn = ts_object_get_property(rawObj, "next");
        if (nextFn) {
            TsValue nm = nanbox_to_tagged(nextFn);
            if ((nm.type == ValueType::OBJECT_PTR || nm.type == ValueType::FUNCTION_PTR) && nm.ptr_val) {
                // ECMA-262 IteratorNext: call next() with the ITERATOR as `this`
                // (these iterators read their state from ts_get_call_this()).
                if (value) return ts_call_with_this_1(nextFn, iterator, value);
                return ts_call_with_this_0(nextFn, iterator);
            }
        }
    }

    // Fallback: return done
    return create_generator_result(TsValue(), true);
}

// Check if an iterator result is done.
// ECMA-262 IteratorComplete: ToBoolean(Get(result, "done")). The result object
// may be a TsMap (generator/Map/Set iterators) OR a flat inline-slot object
// returned from a user-written `next()`. The raw TsMap::Get used previously did
// not see flat-object slots (or prototype-walked), so a `{value, done:false}`
// returned by a user iterator read no BOOLEAN -> fell back to done=true ->
// spread/destructuring over user iterables came out empty. Use the general
// property getter (the same one ts_iterator_next uses to find `next`).
bool ts_iterator_result_done(TsValue* result) {
    if (!result) return true;

    void* rawObj = ts_value_get_object(result);
    if (!rawObj) rawObj = result;
    if ((uintptr_t)rawObj >= 0x1000) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* doneV = ts_object_get_property(rawObj, "done");
        if (doneV) return ts_value_to_bool(doneV);  // ToBoolean, undefined -> false
    }

    return true;
}

// Get value from an iterator result (ECMA-262 IteratorValue: Get(result,"value")).
// Same flat-object/prototype concern as ts_iterator_result_done above.
TsValue* ts_iterator_result_value(TsValue* result) {
    if (!result) {
        return ts_value_make_undefined();
    }

    void* rawObj = ts_value_get_object(result);
    if (!rawObj) rawObj = result;
    if ((uintptr_t)rawObj >= 0x1000) {
        extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
        TsValue* val = ts_object_get_property(rawObj, "value");
        if (val) return val;
    }

    return ts_value_make_undefined();
}

void ts_async_resume(AsyncContext* ctx, TsValue* value) {
    // With NaN boxing, the value IS the NaN-boxed uint64_t encoded as TsValue*.
    // No heap copy needed - the value is self-contained in the pointer.
    ctx->resumedValue = value;

    if (ctx->resumeFn) {
        ctx->resumeFn(ctx);
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
    // Convert stored TsValue struct to NaN-boxed for outgoing calls
    TsValue* nbValue = nanbox_from_tagged(value);

    if (cb.asyncCtx) {
        cb.asyncCtx->error = (promise->state == PromiseState::Rejected);
        ts_async_resume(cb.asyncCtx, nbValue);
        return;
    }

    if ((cb.onFinally.type == ValueType::OBJECT_PTR ||
         cb.onFinally.type == ValueType::FUNCTION_PTR) && cb.onFinally.ptr_val) {
        tsCall(nanbox_from_tagged(cb.onFinally));
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, nbValue);
            } else {
                ts_promise_reject_internal(cb.nextPromise, nbValue);
            }
        }
        return;
    }

    TsValue handler = (promise->state == PromiseState::Fulfilled) ? cb.onFulfilled : cb.onRejected;

    if ((handler.type == ValueType::OBJECT_PTR || handler.type == ValueType::FUNCTION_PTR) && handler.ptr_val) {
        // ES 27.2.2.1 PromiseReactionJob: an abrupt completion from the
        // handler must REJECT cb.nextPromise — it must NOT unwind out of
        // the microtask drain (which silently killed every later queued
        // reaction and exited the process). Runtime-side catch, same
        // pattern as promise_iterable_to_array below. Locals mutated
        // after setjmp and read later must be volatile per C semantics.
        TsValue* volatile result = nullptr;
        void* hbuf = ts_push_exception_handler();
        jmp_buf* env = (jmp_buf*)hbuf;
        if (setjmp(*env) == 0) {
#ifdef _WIN64
            // Disable unwinding longjmp (RtlUnwindEx) for this buffer: the
            // throw crosses compiled-JS frames and trampolines; an unwinding
            // longjmp dies with STATUS_BAD_FUNCTION_TABLE (0xc00000ff).
            ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
            result = tsCall(nanbox_from_tagged(handler), nbValue);
            ts_pop_exception_handler();
        } else {
            // ts_throw already popped our handler.
            TsValue* exc = ts_get_exception();
            ts_set_exception(nullptr);
            if (cb.nextPromise) {
                ts_promise_reject_internal(cb.nextPromise,
                    exc ? exc : ts_value_make_undefined());
            }
            return;
        }
        if (cb.nextPromise) {
            if (result) {
                ts_promise_resolve_internal(cb.nextPromise, (TsValue*)result);
            } else {
                ts_promise_resolve_internal(cb.nextPromise, ts_value_make_undefined());
            }
        }
    } else {
        // No handler, propagate
        if (cb.nextPromise) {
            if (promise->state == PromiseState::Fulfilled) {
                ts_promise_resolve_internal(cb.nextPromise, nbValue);
            } else {
                ts_promise_reject_internal(cb.nextPromise, nbValue);
            }
        }
    }
}

void ts_promise_settle_microtask(void* data) {
    auto task = static_cast<PromiseResolveTask*>(data);
    TsPromise* promise = task->promise;
    if (promise->state == PromiseState::Rejected && !promise->handled) {
        ts_console_log_value(nanbox_from_tagged(promise->value));
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

// Native function wrappers for Promise constructor (variadic calling convention)
TsValue* ts_promise_resolve_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* value = (argc > 0) ? argv[0] : nullptr;
    ts_promise_resolve_internal((TsPromise*)context, value);
    // ES 27.2.1.3.2 step 8: a resolve function returns undefined.
    return ts_value_make_undefined();
}

TsValue* ts_promise_reject_wrapper(void* context, int argc, TsValue** argv) {
    TsValue* reason = (argc > 0) ? argv[0] : nullptr;
    ts_promise_reject_internal((TsPromise*)context, reason);
    return ts_value_make_undefined();
}

// Make a resolve/reject function whose context (the [[Promise]] internal
// slot) SURVIVES bare calls. Without keep_context, maybe_override_context
// replaces the context with the ambient `this` on every plain invocation —
// so a resolve function captured OUT of the executor (`let r; new
// Promise(res => r = res); r(v)`) resolved globalThis instead of the
// promise and the settlement was silently lost.
static TsValue* make_promise_settle_fn(void* fnPtr, TsPromise* promise) {
    TsValue* fn = ts_value_make_native_function(fnPtr, promise);
    void* raw = ts_value_get_object(fn);
    if (raw) {
        TsFunction* f = ts_cast<TsFunction>(raw);
        if (f) f->keep_context = true;
    }
    return fn;
}

// ES 27.2.1.3.2 + 27.2.2.2 PromiseResolveThenableJob: when a promise is
// resolved with a thenable (non-promise object with a callable `then`), the
// promise must NOT fulfill with the thenable itself — a microtask calls
// then.call(thenable, resolveFn, rejectFn). Once-semantics come from the
// Pending-state check in resolve/reject_internal. Queuing (not calling
// synchronously) also bounds a thenable that resolves with itself: it
// becomes a microtask loop (matching real engines), not stack recursion.
struct PromiseThenableJob {
    TsPromise* promise;
    TsValue thenable;  // tagged copy (job struct lives on the GC heap)
    TsValue thenFn;    // tagged copy
};

static void ts_promise_thenable_microtask(void* data) {
    auto job = static_cast<PromiseThenableJob*>(data);
    TsValue* resolveFn = make_promise_settle_fn(
        (void*)ts_promise_resolve_wrapper, job->promise);
    TsValue* rejectFn = make_promise_settle_fn(
        (void*)ts_promise_reject_wrapper, job->promise);
    void* hbuf = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)hbuf;
    if (setjmp(*env) == 0) {
#ifdef _WIN64
        // See promise_iterable_to_array: register-restore longjmp only.
        ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
        ts_call_with_this_2(nanbox_from_tagged(job->thenFn),
                            nanbox_from_tagged(job->thenable),
                            resolveFn, rejectFn);
        ts_pop_exception_handler();
    } else {
        // then() threw: reject (unless resolveFn/rejectFn already settled).
        // ts_throw already popped our handler.
        TsValue* exc = ts_get_exception();
        ts_set_exception(nullptr);
        ts_promise_reject_internal(job->promise,
            exc ? exc : ts_value_make_undefined());
    }
}

void ts_promise_resolve_internal(TsPromise* promise, TsValue* value) {
    if (!promise) {
        return;
    }
    if (promise->state != PromiseState::Pending) {
        return;
    }

    TsValue val = value ? nanbox_to_tagged(value) : TsValue();

    if (val.type == ValueType::PROMISE_PTR && val.ptr_val) {
        TsPromise* other = (TsPromise*)val.ptr_val;
        // ES 27.2.1.3.2 step 6: SameValue(resolution, promise) -> reject with
        // TypeError (self-resolution chain cycle).
        if (other == promise) {
            ts_promise_reject_internal(promise,
                (TsValue*)ts_error_create_typed("TypeError",
                    "Chaining cycle detected for promise"));
            return;
        }
        TsValue onFulfilled;
        onFulfilled.type = ValueType::OBJECT_PTR;
        TsFunction* f1 = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_resolve_internal_helper, promise, FunctionType::COMPILED);
        onFulfilled.ptr_val = f1;

        TsValue onRejected;
        onRejected.type = ValueType::OBJECT_PTR;
        TsFunction* f2 = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_reject_internal_helper, promise, FunctionType::COMPILED);
        onRejected.ptr_val = f2;

        other->then(onFulfilled, onRejected);
        return;
    }

    // Thenable assimilation (spec step 9-12). This runs on EVERY async-
    // function return, so the non-thenable fast path comes first and stays
    // cheap: a magic sniff (plain object = TsMap "MAPS" at the canonical
    // offset-16 slot; class instance = FLAT at offset 0) gates the property
    // lookup; a TsMap miss on "then" is one hash probe.
    if (val.type == ValueType::OBJECT_PTR && val.ptr_val &&
        (uintptr_t)val.ptr_val > 0x1000) {
        void* raw = val.ptr_val;
        uint32_t magic0 = *(uint32_t*)raw;
        uint32_t magic16 = *(uint32_t*)((char*)raw + 16);
        if (magic0 == 0x464C4154 /* FLAT */ || magic16 == TsMap::MAGIC) {
            extern TsValue* ts_object_get_property(void* o, const char* k);
            // ES 27.2.1.3.2 step 9: Get(resolution, "then") is inside the
            // resolve function — an abrupt GET (poisoned accessor) REJECTS
            // the promise with the thrown value, it must not propagate.
            TsValue* thenFn = nullptr;
            {
                void* hbuf = ts_push_exception_handler();
                jmp_buf* env = (jmp_buf*)hbuf;
                if (setjmp(*env) == 0) {
#ifdef _WIN64
                    ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
                    thenFn = ts_object_get_property(raw, "then");
                    ts_pop_exception_handler();
                } else {
                    TsValue* exc = ts_get_exception();
                    ts_set_exception(nullptr);
                    ts_promise_reject_internal(promise,
                        exc ? exc : ts_value_make_undefined());
                    return;
                }
            }
            if (thenFn && agen_is_callable(thenFn)) {
                auto job = static_cast<PromiseThenableJob*>(
                    ts_alloc(sizeof(PromiseThenableJob)));
                job->promise = promise;
                job->thenable = val;
                job->thenFn = nanbox_to_tagged(thenFn);
                ts_queue_microtask(ts_promise_thenable_microtask, job);
                return;  // stays Pending until resolveFn/rejectFn fire
            }
        }
    }

    promise->state = PromiseState::Fulfilled;
    promise->value = val;

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

void ts_promise_reject_internal(TsPromise* promise, TsValue* reason) {
    if (promise->state != PromiseState::Pending) {
        return;
    }
    promise->state = PromiseState::Rejected;
    promise->value = reason ? nanbox_to_tagged(reason) : TsValue();

    auto task = static_cast<PromiseResolveTask*>(ts_alloc(sizeof(PromiseResolveTask)));
    task->promise = promise;
    task->value = promise->value;
    ts_queue_microtask(ts_promise_settle_microtask, task);
}

TsValue* ts_promise_resolve(void* context, TsValue* value) {
    TsValue val = value ? nanbox_to_tagged(value) : TsValue();
    if (val.type == ValueType::PROMISE_PTR) {
        return value;
    }
    TsPromise* p = ts_promise_create();
    ts_promise_resolve_internal(p, value);
    return ts_value_make_promise(p);
}

TsValue* ts_promise_reject(void* context, TsValue* reason) {
    TsPromise* p = ts_promise_create();
    ts_promise_reject_internal(p, reason);
    return ts_value_make_promise(p);
}

TsValue* ts_promise_then(TsValue* promise, TsValue* onFulfilled, TsValue* onRejected) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->then(onFulfilled ? nanbox_to_tagged(onFulfilled) : TsValue(), onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_catch(TsValue* promise, TsValue* onRejected) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->catch_error(onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_finally(TsValue* promise, TsValue* onFinally) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return nullptr;
    TsPromise* p = (TsPromise*)pVal.ptr_val;
    TsPromise* next = p->finally(onFinally ? nanbox_to_tagged(onFinally) : TsValue());
    return ts_value_make_promise(next);
}

TsValue* ts_promise_new(TsValue* executor) {
    // ES 27.2.3.1 step 2: executor must be callable, else TypeError.
    if (!executor || !agen_is_callable(executor)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Promise resolver is not a function"));
        return nullptr;  // unreachable
    }

    // Create a new promise
    TsPromise* promise = ts_promise_create();

    // Create resolve and reject functions using the variadic wrappers
    TsValue* resolveArg = make_promise_settle_fn(
        (void*)ts_promise_resolve_wrapper, promise);

    TsValue* rejectArg = make_promise_settle_fn(
        (void*)ts_promise_reject_wrapper, promise);

    // Call the executor with (resolve, reject). ES 27.2.3.1 step 10: an
    // executor throw rejects the promise (no-op if already settled —
    // reject_internal's Pending check provides the once-semantics).
    void* hbuf = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)hbuf;
    if (setjmp(*env) == 0) {
#ifdef _WIN64
        // See promise_iterable_to_array: register-restore longjmp only.
        ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
        tsCall(executor, resolveArg, rejectArg);
        ts_pop_exception_handler();
    } else {
        // ts_throw already popped our handler.
        TsValue* exc = ts_get_exception();
        ts_set_exception(nullptr);
        ts_promise_reject_internal(promise,
            exc ? exc : ts_value_make_undefined());
    }

    // Return the promise
    return ts_value_make_promise(promise);
}

TsValue* ts_promise_await(TsValue* promise) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) return promise;
    TsPromise* p = (TsPromise*)pVal.ptr_val;

    // An `await` is intent to consume the result. If the promise is (or
    // becomes) rejected, we'll throw the rejection value to the caller —
    // whose try/catch or async-prologue handler is the actual rejection
    // handler. Either way the rejection is handled, so don't fire the
    // unhandled-rejection warning.
    p->handled = true;

    // Run microtasks + libuv until settled. Pre-settled promises skip the
    // loop. The promise may depend on I/O (fetch, setTimeout, etc.).
    if (p->state == PromiseState::Pending) {
        uv_loop_t* loop = uv_default_loop();
        while (p->state == PromiseState::Pending) {
            ts_run_microtasks();
            if (p->state != PromiseState::Pending) break;
            if (uv_loop_alive(loop)) {
                uv_run(loop, UV_RUN_ONCE);
            }
        }
    }

    TsValue* result = nanbox_from_tagged(p->value);
    if (p->state == PromiseState::Rejected) {
        // ts_throw longjmps to the topmost exception handler — the user's
        // try/catch when available, otherwise the async function's prologue
        // handler (installed by HIRToLLVM::lowerFunction) which converts the
        // throw into a rejection on the outer promise. ts_throw does not
        // return; control resumes at the matching setjmp landing pad. The
        // post-call IR is dead at runtime but doesn't need a `noreturn`
        // marker — setjmp's `returns_twice` attribute already tells LLVM
        // not to reason linearly across this frame.
        ts_throw(result);
    }
    return result;
}

void ts_async_await(TsValue* promise, AsyncContext* ctx) {
    TsValue pVal = promise ? nanbox_to_tagged(promise) : TsValue();
    if (pVal.type != ValueType::PROMISE_PTR || !pVal.ptr_val) {
        ts_async_resume(ctx, promise);
        return;
    }
    TsPromise* p = (TsPromise*)pVal.ptr_val;
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

// Shared by all/race/allSettled: convert a CUSTOM (non-TsArray) iterable
// into a TsArray by walking the real iterator protocol, with the runtime-
// side catch (X8 pattern). They all blind-cast the argument to TsArray
// before this — a custom iterable's garbage ->Length() drove runaway
// loops/OOM exactly like the fixed Promise.any. On abrupt completion the
// iterator is closed (return(), absorbed) and mainPromise is rejected;
// the caller returns it immediately.
static bool promise_iterable_to_array(TsValue* iterableVal, void* raw,
                                      ts::TsPromise* mainPromise,
                                      TsArray** out) {
    extern TsValue* ts_object_get_property(void* o, const char* k);
    TsValue* method = ts_object_get_property(raw, "[Symbol.iterator]");
    if (!method || ts_value_is_nullish(method)) {
        ts_promise_reject_internal(mainPromise,
            (TsValue*)ts_error_create_typed("TypeError",
                "argument is not iterable"));
        return false;
    }
    TsArray* acc = TsArray::Create();
    TsValue* volatile iterSave = nullptr;
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
#ifdef _WIN64
        // Disable unwinding longjmp (RtlUnwindEx) for this buffer: the throw
        // crosses compiled-JS frames and trampolines; an unwinding longjmp
        // dies with STATUS_BAD_FUNCTION_TABLE (0xc00000ff). Register-restore
        // longjmp is what the compiled-code handlers rely on too.
        ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
        TsValue* iter = ts_call_with_this_0(method, iterableVal);
        void* iterRaw = iter ? ts_value_get_object(iter) : nullptr;
        if (!iterRaw) {
            ts_pop_exception_handler();
            ts_promise_reject_internal(mainPromise,
                (TsValue*)ts_error_create_typed("TypeError",
                    "iterator is not an object"));
            return false;
        }
        iterSave = iter;
        TsValue* nextFn = ts_object_get_property(iterRaw, "next");
        for (int64_t guard = 0; ; guard++) {
            if (guard >= 100000) {
                ts_pop_exception_handler();
                ts_promise_reject_internal(mainPromise,
                    (TsValue*)ts_error_create_typed("TypeError",
                        "iterator did not complete"));
                return false;
            }
            TsValue* res = ts_call_with_this_0(nextFn, iter);
            void* resRaw = res ? ts_value_get_object(res) : nullptr;
            if (!resRaw) {
                ts_pop_exception_handler();
                ts_promise_reject_internal(mainPromise,
                    (TsValue*)ts_error_create_typed("TypeError",
                        "iterator result is not an object"));
                return false;
            }
            if (ts_iterator_result_done(res)) break;
            acc->Push((int64_t)(intptr_t)ts_iterator_result_value(res));
        }
        ts_pop_exception_handler();
        *out = acc;
        return true;
    } else {
        TsValue* exc = ts_get_exception();
        ts_set_exception(nullptr);
        TsValue* iterDone = iterSave;
        void* iterDoneRaw = iterDone ? ts_value_get_object(iterDone) : nullptr;
        if (iterDoneRaw) {
            TsValue* retFn = ts_object_get_property(iterDoneRaw, "return");
            if (retFn && !ts_value_is_nullish(retFn)) {
                void* h2 = ts_push_exception_handler();
                jmp_buf* env2 = (jmp_buf*)h2;
                if (setjmp(*env2) == 0) {
#ifdef _WIN64
                    ((_JUMP_BUFFER*)env2)->Frame = 0;
#endif
                    ts_call_with_this_0(retFn, iterDone);
                    ts_pop_exception_handler();
                } else {
                    ts_set_exception(nullptr);  // absorb return() throw
                }
            }
        }
        ts_promise_reject_internal(mainPromise, exc);
        return false;
    }
}

TsValue* ts_promise_all(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        return ts_promise_resolve(nullptr, ts_value_make_array(TsArray::Create(0)));
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    if (!ts_is_unchecked<TsArray>(iterVal.ptr_val)) {
        ts::TsPromise* earlyPromise = ts_promise_create();
        TsArray* converted = nullptr;
        if (!promise_iterable_to_array(iterableVal, iterVal.ptr_val,
                                       earlyPromise, &converted)) {
            return ts_value_make_promise(earlyPromise);
        }
        iterable = converted;
    }
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
        TsValue* p = ts_promise_resolve(nullptr, item);

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
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    if (!ts_is_unchecked<TsArray>(iterVal.ptr_val)) {
        ts::TsPromise* earlyPromise = ts_promise_create();
        TsArray* converted = nullptr;
        if (!promise_iterable_to_array(iterableVal, iterVal.ptr_val,
                                       earlyPromise, &converted)) {
            return ts_value_make_promise(earlyPromise);
        }
        iterable = converted;
    }
    size_t total = iterable->Length();
    ts::TsPromise* mainPromise = ts_promise_create();

    for (size_t i = 0; i < total; ++i) {
        TsValue* item = (TsValue*)iterable->Get(i);
        TsValue* p = ts_promise_resolve(nullptr, item);

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
    obj->Set((TsString*)ts_string_create("value"), val ? nanbox_to_tagged(val) : TsValue());
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
    obj->Set((TsString*)ts_string_create("reason"), reason ? nanbox_to_tagged(reason) : TsValue());
    e->ctx->results->Set(e->index, (int64_t)ts_value_make_object(obj));
    e->ctx->remaining--;
    if (e->ctx->remaining == 0) {
        ts_promise_resolve_internal(e->ctx->mainPromise, ts_value_make_array(e->ctx->results));
    }
    return nullptr;
}

extern "C" TsValue* ts_promise_allSettled(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_resolve_internal(p, ts_value_make_array(TsArray::Create(0)));
        return ts_value_make_promise(p);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
    if (!ts_is_unchecked<TsArray>(iterVal.ptr_val)) {
        ts::TsPromise* earlyPromise = ts_promise_create();
        TsArray* converted = nullptr;
        if (!promise_iterable_to_array(iterableVal, iterVal.ptr_val,
                                       earlyPromise, &converted)) {
            return ts_value_make_promise(earlyPromise);
        }
        iterable = converted;
    }
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
        TsValue* p = ts_promise_resolve(nullptr, item);
        
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

// ES2024 Promise.withResolvers()
// Returns an object with { promise, resolve, reject } properties
extern "C" TsValue* ts_promise_withResolvers() {
    // Create a new promise
    TsPromise* promise = ts_promise_create();

    // Create resolve and reject functions using the variadic wrappers
    TsValue* resolveFunc = make_promise_settle_fn(
        (void*)ts_promise_resolve_wrapper, promise);

    TsValue* rejectFunc = make_promise_settle_fn(
        (void*)ts_promise_reject_wrapper, promise);

    // Create the result object with { promise, resolve, reject }
    TsMap* result = TsMap::Create();

    // Set promise property
    TsValue promiseVal;
    promiseVal.type = ValueType::PROMISE_PTR;
    promiseVal.ptr_val = promise;
    result->Set(TsString::Create("promise"), promiseVal);

    // Set resolve property
    result->Set(TsString::Create("resolve"), nanbox_to_tagged(resolveFunc));

    // Set reject property
    result->Set(TsString::Create("reject"), nanbox_to_tagged(rejectFunc));

    return ts_value_make_object(result);
}

extern "C" TsValue* ts_promise_any(TsValue* iterableVal) {
    TsValue iterVal = iterableVal ? nanbox_to_tagged(iterableVal) : TsValue();
    if (iterVal.type != ValueType::OBJECT_PTR && iterVal.type != ValueType::ARRAY_PTR) {
        ts::TsPromise* p = ts_promise_create();
        ts_promise_reject_internal(p, iterableVal);
        return ts_value_make_promise(p);
    }
    // CUSTOM ITERABLES (non-TsArray objects): the old code blind-cast them
    // to TsArray and read a garbage Length() — the 27-test Promise.any OOM
    // cluster. Walk the real iterator protocol lazily per spec
    // PerformPromiseAny: per element resolve + Invoke(p, "then", ...); an
    // abrupt completion (user .then override that throws, bad results)
    // closes the iterator (return(), once) and rejects the main promise.
    if (!ts_is_unchecked<TsArray>(iterVal.ptr_val)) {
        void* raw = iterVal.ptr_val;
        ts::TsPromise* mainPromise = ts_promise_create();
        extern TsValue* ts_object_get_property(void* o, const char* k);
        TsValue* method = ts_object_get_property(raw, "[Symbol.iterator]");
        if (!method || ts_value_is_nullish(method)) {
            ts_promise_reject_internal(mainPromise,
                (TsValue*)ts_error_create_typed("TypeError",
                    "Promise.any: argument is not iterable"));
            return ts_value_make_promise(mainPromise);
        }
        AnyContext* ctx = (AnyContext*)ts_alloc(sizeof(AnyContext));
        ctx->mainPromise = mainPromise;
        ctx->errors = TsArray::Create();
        // Spec: remainingElementsCount starts at 1 (the iteration's own
        // hold) so completion can't fire while elements are still arriving.
        ctx->remaining = 1;

        // Runtime-side catch (invoke_and_absorb pattern, TsObject.cpp):
        // user code runs inside (iterator next(), promise .then overrides).
        // Locals mutated after setjmp and read in the landing branch must
        // be volatile per C semantics.
        TsValue* volatile iterSave = nullptr;
        void* handler = ts_push_exception_handler();
        jmp_buf* env = (jmp_buf*)handler;
        if (setjmp(*env) == 0) {
#ifdef _WIN64
        // Disable unwinding longjmp (RtlUnwindEx) for this buffer: the throw
        // crosses compiled-JS frames and trampolines; an unwinding longjmp
        // dies with STATUS_BAD_FUNCTION_TABLE (0xc00000ff). Register-restore
        // longjmp is what the compiled-code handlers rely on too.
        ((_JUMP_BUFFER*)env)->Frame = 0;
#endif
            TsValue* iter = ts_call_with_this_0(method, iterableVal);
            void* iterRaw = iter ? ts_value_get_object(iter) : nullptr;
            if (!iterRaw) {
                ts_pop_exception_handler();
                ts_promise_reject_internal(mainPromise,
                    (TsValue*)ts_error_create_typed("TypeError",
                        "Promise.any: iterator is not an object"));
                return ts_value_make_promise(mainPromise);
            }
            iterSave = iter;
            TsValue* nextFn = ts_object_get_property(iterRaw, "next");
            // Hang guard: the spec drains the sync iterator fully, and the
            // OOM-cluster tests use INFINITE iterators stopped only by a
            // throwing `.then` override — which our TsPromise doesn't make
            // observable yet (own-prop writes on promises are dropped).
            // Until then, cap and reject instead of exhausting the heap.
            for (int64_t guard = 0; ; guard++) {
                if (guard >= 100000) {
                    ts_pop_exception_handler();
                    ts_promise_reject_internal(mainPromise,
                        (TsValue*)ts_error_create_typed("TypeError",
                            "Promise.any: iterator did not complete"));
                    return ts_value_make_promise(mainPromise);
                }
                TsValue* res = ts_call_with_this_0(nextFn, iter);
                void* resRaw = res ? ts_value_get_object(res) : nullptr;
                if (!resRaw) {
                    // next() result not an object: abrupt WITHOUT
                    // IteratorClose (the iterator itself misbehaved).
                    ts_pop_exception_handler();
                    ts_promise_reject_internal(mainPromise,
                        (TsValue*)ts_error_create_typed("TypeError",
                            "Promise.any: iterator result is not an object"));
                    return ts_value_make_promise(mainPromise);
                }
                if (ts_iterator_result_done(res)) break;
                TsValue* item = ts_iterator_result_value(res);
                ctx->remaining++;
                // PerformPromiseAny: nextPromise = ? Call(promiseResolve, C, «item»)
                // where promiseResolve = Get(C, "resolve") — the OVERRIDABLE
                // constructor resolve. A user override of Promise.resolve that
                // throws must abort the drain and IteratorClose; the OOM-cluster
                // tests use an INFINITE iterator stopped only by that throw. Fall
                // back to the internal resolve if no callable resolve is present.
                TsValue* p = nullptr;
                {
                    void* pc = ts_get_global_Promise();
                    void* pcRaw = pc ? ts_value_get_object((TsValue*)pc) : nullptr;
                    TsValue* resolveFn = (pcRaw ? pcRaw : pc)
                        ? ts_object_get_property(pcRaw ? pcRaw : pc, "resolve") : nullptr;
                    if (resolveFn && ts_is_callable((void*)resolveFn)) {
                        p = ts_call_with_this_1(resolveFn, (TsValue*)pc, item);
                    } else {
                        p = ts_promise_resolve(nullptr, item);
                    }
                }
                TsValue* onF = ts_value_make_function(
                    (void*)ts_promise_any_fulfilled_helper, ctx);
                TsValue* onR = ts_value_make_function(
                    (void*)ts_promise_any_rejected_helper, ctx);
                // Spec: Invoke(nextPromise, "then", ...) — the OBSERVABLE
                // then (a user override that throws lands in our catch).
                TsValue* thenFn = p ? ts_object_get_property(
                    ts_value_get_object(p) ? ts_value_get_object(p) : p,
                    "then") : nullptr;
                if (thenFn && !ts_value_is_nullish(thenFn)) {
                    ts_call_with_this_2(thenFn, p, onF, onR);
                } else {
                    ts_promise_then(p, onF, onR);
                }
            }
            ts_pop_exception_handler();
            // Release the iteration hold; if nothing was pending, reject
            // with AggregateError-equivalent (empty/settled errors array).
            ctx->remaining--;
            if (ctx->remaining == 0) {
                ts_promise_reject_internal(mainPromise,
                    ts_value_make_array(ctx->errors));
            }
        } else {
            // Abrupt completion from user code: ts_throw already popped our
            // handler. IteratorClose (call return() once, absorbing its own
            // throw), then reject with the original error.
            TsValue* exc = ts_get_exception();
            ts_set_exception(nullptr);
            TsValue* iterDone = iterSave;
            void* iterDoneRaw = iterDone ? ts_value_get_object(iterDone) : nullptr;
            if (iterDoneRaw) {
                TsValue* retFn = ts_object_get_property(iterDoneRaw, "return");
                if (retFn && !ts_value_is_nullish(retFn)) {
                    void* h2 = ts_push_exception_handler();
                    jmp_buf* env2 = (jmp_buf*)h2;
                    if (setjmp(*env2) == 0) {
#ifdef _WIN64
                    ((_JUMP_BUFFER*)env2)->Frame = 0;
#endif
                        ts_call_with_this_0(retFn, iterDone);
                        ts_pop_exception_handler();
                    } else {
                        ts_set_exception(nullptr);  // absorb return() throw
                    }
                }
            }
            ts_promise_reject_internal(mainPromise, exc);
        }
        return ts_value_make_promise(mainPromise);
    }
    TsArray* iterable = (TsArray*)iterVal.ptr_val;
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
        TsValue* p = ts_promise_resolve(nullptr, item);
        
        TsValue* onFulfilled = ts_value_make_function((void*)ts_promise_any_fulfilled_helper, ctx);
        TsValue* onRejected = ts_value_make_function((void*)ts_promise_any_rejected_helper, ctx);
        
        ts_promise_then(p, onFulfilled, onRejected);
    }
    return ts_value_make_promise(mainPromise);
}

} // extern "C"

// --- GC rooting for pending promise callbacks ---------------------------------
// TsPromise::callbacks is a std::vector whose backing is malloc'd C++ memory the
// collector never scans. Each Callback holds GC pointers (onFulfilled/onRejected/
// onFinally TsValues + nextPromise). A GC between then()/finally() and settle
// would collect or move those -> stale -> crash in ts_promise_run_callback.
// Maintain a registry of promises that currently hold pending callbacks; a GC
// mark-scanner marks those pointers (keeping them live) and a minor-GC fixup
// forwards them after promotion. Entries are pruned once a promise's callbacks
// drain (settle clears them). Same pattern as the microtask-queue rooting.
static std::vector<TsPromise*> g_promises_with_cb;
static bool g_promise_cb_gc_registered = false;

static inline bool prom_is_heap_val(const TsValue& v) {
    return v.ptr_val && (v.type == ValueType::OBJECT_PTR ||
                         v.type == ValueType::FUNCTION_PTR ||
                         v.type == ValueType::PROMISE_PTR ||
                         v.type == ValueType::STRING_PTR ||
                         v.type == ValueType::ARRAY_PTR ||
                         v.type == ValueType::SYMBOL_PTR);
}

static void promise_cb_gc_scan(void*) {
    size_t w = 0;
    for (size_t i = 0; i < g_promises_with_cb.size(); i++) {
        TsPromise* p = g_promises_with_cb[i];
        if (!p || p->callbacks.empty()) continue;  // drained -> drop from registry
        g_promises_with_cb[w++] = p;
        ts_gc_mark_object(p);
        for (auto& cb : p->callbacks) {
            if (prom_is_heap_val(cb.onFulfilled)) ts_gc_mark_object(cb.onFulfilled.ptr_val);
            if (prom_is_heap_val(cb.onRejected))  ts_gc_mark_object(cb.onRejected.ptr_val);
            if (prom_is_heap_val(cb.onFinally))   ts_gc_mark_object(cb.onFinally.ptr_val);
            if (cb.nextPromise) ts_gc_mark_object(cb.nextPromise);
        }
    }
    g_promises_with_cb.resize(w);
}

static void promise_cb_gc_fixup(void*) {
    auto fwd_val = [](TsValue& v) {
        if (v.ptr_val) { void* f = ts_gc_minor_lookup_forward(v.ptr_val); if (f) v.ptr_val = f; }
    };
    for (auto*& p : g_promises_with_cb) {
        if (!p) continue;
        p = (TsPromise*)ts_gc_minor_lookup_forward(p);  // forward the registry slot
        for (auto& cb : p->callbacks) {
            fwd_val(cb.onFulfilled); fwd_val(cb.onRejected); fwd_val(cb.onFinally);
            if (cb.nextPromise) cb.nextPromise = (TsPromise*)ts_gc_minor_lookup_forward(cb.nextPromise);
        }
    }
}

// Call right after pushing a Callback onto promise->callbacks.
static inline void ts_promise_track_pending(TsPromise* p) {
    if (!g_promise_cb_gc_registered) {
        g_promise_cb_gc_registered = true;
        ts_gc_register_scanner(promise_cb_gc_scan, nullptr);
        ts_gc_register_minor_fixup(promise_cb_gc_fixup, nullptr);
    }
    // Add on the empty->non-empty transition (callers invoke after push_back,
    // so size==1 means this is the first pending callback).
    if (p->callbacks.size() == 1) g_promises_with_cb.push_back(p);
}

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
        ts_promise_track_pending(this);
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
        ts_promise_track_pending(this);
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
        ts_promise_track_pending(this);
    }
}

static TsValue* ts_promise_then_wrapper(void* context, TsValue* onFulfilled, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->then(onFulfilled ? nanbox_to_tagged(onFulfilled) : TsValue(), onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_catch_wrapper(void* context, TsValue* onRejected) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->catch_error(onRejected ? nanbox_to_tagged(onRejected) : TsValue());
    return ts_value_make_promise(next);
}

static TsValue* ts_promise_finally_wrapper(void* context, TsValue* onFinally) {
    TsPromise* promise = (TsPromise*)context;
    TsPromise* next = promise->finally(onFinally ? nanbox_to_tagged(onFinally) : TsValue());
    return ts_value_make_promise(next);
}

// Built-in prototype methods have no [[Construct]] — `new p.then()` etc.
// must throw TypeError (the dispatcher checks is_constructor).
static TsValue* promise_method_function(void* fp, void* obj) {
    TsFunction* func = new (ts_alloc(sizeof(TsFunction)))
        TsFunction(fp, obj, FunctionType::COMPILED, -1);
    func->is_constructor = false;
    return (TsValue*)func;
}

TsValue* ts_promise_get_property(void* obj, void* propName) {
    TsString* prop = (TsString*)propName;
    const char* name = prop->ToUtf8();

    if (strcmp(name, "then") == 0) {
        return promise_method_function((void*)ts_promise_then_wrapper, obj);
    } else if (strcmp(name, "catch") == 0) {
        return promise_method_function((void*)ts_promise_catch_wrapper, obj);
    } else if (strcmp(name, "finally") == 0) {
        return promise_method_function((void*)ts_promise_finally_wrapper, obj);
    }
    return ts_value_make_undefined();
}

TsValue TsPromise::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "then") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        TsFunction* f = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_then_wrapper, this, FunctionType::COMPILED, 2);
        f->is_constructor = false;  // built-in method, no [[Construct]]
        v.ptr_val = f;
        return v;
    }
    if (strcmp(key, "catch") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        TsFunction* f = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_catch_wrapper, this, FunctionType::COMPILED, 1);
        f->is_constructor = false;  // built-in method, no [[Construct]]
        v.ptr_val = f;
        return v;
    }
    if (strcmp(key, "finally") == 0) {
        TsValue v;
        v.type = ValueType::FUNCTION_PTR;
        TsFunction* f = new (ts_alloc(sizeof(TsFunction))) TsFunction(
            (void*)ts_promise_finally_wrapper, this, FunctionType::COMPILED, 1);
        f->is_constructor = false;  // built-in method, no [[Construct]]
        v.ptr_val = f;
        return v;
    }
    return TsObject::GetPropertyVirtual(key);
}

void* TsPromise_VTable[] = {
    nullptr,
    (void*)ts_promise_get_property
};

// ========================================================================
// AsyncContext helper functions for generator state machine
// ========================================================================

void ts_async_context_set_resume_fn(AsyncContext* ctx, void (*fn)(AsyncContext*)) {
    if (ctx) {
        ctx->resumeFn = fn;
    }
}

int ts_async_context_get_state(AsyncContext* ctx) {
    return ctx ? ctx->state : 0;
}

void ts_async_context_set_state(AsyncContext* ctx, int state) {
    if (ctx) {
        ctx->state = state;
    }
}

void ts_async_context_yield(AsyncContext* ctx, TsValue* value) {
    if (ctx) {
        ctx->yielded = true;
        if (value) {
            ctx->yieldedValue = nanbox_to_tagged(value);
        } else {
            ctx->yieldedValue = TsValue();
        }
    }
}

TsValue* ts_async_context_get_resumed_value(AsyncContext* ctx) {
    if (ctx && ctx->resumedValue) {
        return ctx->resumedValue;
    }
    return ts_value_make_undefined();
}

void ts_async_context_set_delegate_iterator(AsyncContext* ctx, TsValue* iter) {
    if (ctx) {
        ctx->delegateIterator = iter;
        // New delegation: reset the legacy array-shape cursor (GEN-001 Stage
        // 4b, ts_agen_delegate_step). Sync generators never read it.
        ctx->delegateIndex = 0;
    }
}

TsValue* ts_async_context_get_delegate_iterator(AsyncContext* ctx) {
    if (ctx) {
        return ctx->delegateIterator;
    }
    return nullptr;
}

void ts_async_context_set_this(AsyncContext* ctx, TsValue* thisArg) {
    if (ctx) {
        ctx->thisValue = thisArg;
    }
}

TsValue* ts_async_context_get_this(AsyncContext* ctx) {
    if (ctx) {
        return ctx->thisValue;
    }
    return nullptr;
}

void ts_async_context_set_data(AsyncContext* ctx, void* data) {
    if (ctx) {
        ctx->data = data;
    }
}

void* ts_async_context_get_data(AsyncContext* ctx) {
    if (ctx) {
        return ctx->data;
    }
    return nullptr;
}

int ts_async_context_get_resume_mode(AsyncContext* ctx) {
    return ctx ? ctx->resumeMode : 0;
}

void ts_async_context_set_resume_mode(AsyncContext* ctx, int mode) {
    if (ctx) {
        ctx->resumeMode = mode;
    }
}

} // namespace ts
