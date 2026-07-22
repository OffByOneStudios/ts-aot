#include "TsObject_Internal.h"
#include "TsBoundFunction.h"

// Function call / apply / construct dispatch extracted from TsObject.cpp:
// ts_call_with_arity, the call_closure/funcptr/dispatch helpers, ts_call_with_this[_0..8],
// ts_function_call/ts_call_n, ts_new_from_constructor[_0..8]/ts_construct_apply,
// ts_function_call_with_this, ts_function_apply. The transient call context
// (ts_call_this_value / ts_last_call_argc) is reached via the extern in the header.
extern "C" {


    // Helper to call a function with up to 3 args, respecting the function's declared arity
    // This is critical for Array.map/filter/etc where JS allows callbacks with fewer params
    TsValue* ts_call_with_arity(TsValue* boxedFunc, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsFunction* func = ts_extract_function(boxedFunc);

        // A null func here means the callee is a TsClosure (or other
        // non-TsFunction callable), NOT an uncallable value — callability is
        // validated by the caller. Returning undefined in that case silently
        // dropped the call (e.g. Array iteration methods on the slow path with
        // a closure callback selected nothing). Default to passing all 3 args;
        // tsCall handles closures.
        int arity = func ? func->arity : -1;
        if (arity < 0) {
            // Arity unknown - default to all 3 args (original behavior)
            arity = 3;
        }

        switch (arity) {
            case 0:
                return tsCall(boxedFunc);
            case 1:
                return tsCall(boxedFunc, arg1);
            case 2:
                return tsCall(boxedFunc, arg1, arg2);
            case 3:
            default:
                return tsCall(boxedFunc, arg1, arg2, arg3);
        }
    }

    // Helper to check if a funcPtr inside a TsFunction is actually a TsClosure
    // This happens when ts_value_make_function wraps a closure pointer
    TsClosure* ts_funcptr_as_closure(void* funcPtr) {
        if (!funcPtr) return nullptr;
        // Only check magic if the pointer is in the GC heap (not a code pointer)
        if (!ts_gc_base(funcPtr)) return nullptr;
        TsObject* obj = (TsObject*)funcPtr;
        if (obj->magic == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)obj;
        }
        return nullptr;
    }

    // ECMA-262 rest-parameter dispatch helper. When a TsClosure has
    // rest_param_index >= 0, ts_call_N is invoked with the caller's full
    // positional argv but the underlying function expects (fixed_args...,
    // rest_array, padding...). This packs argv[rest_idx..argc-1] into a
    // single TsArray, places it at position rest_idx, and pads remaining
    // user-arg slots with undefined to fit the 4-arg padded FnPad ABI.
    //
    // Returns false if rest dispatch is N/A (caller falls through to
    // normal direct call). When true, finalArgs[0..3] are populated.
    static TsValue* ts_rest_pack_and_call(TsClosure* closure, int argc, TsValue** argv) {
        int restIdx = closure->rest_param_index;
        TsValue* u = ts_value_make_undefined();
        TsValue* finalArgs[4] = { u, u, u, u };

        // Copy leading literal args 0..min(restIdx, 4).
        int leading = restIdx < 4 ? restIdx : 4;
        for (int i = 0; i < leading; i++) {
            finalArgs[i] = (i < argc) ? argv[i] : u;
        }

        // Build rest TsArray containing argv[restIdx..argc-1]. Empty array
        // if argc <= restIdx (caller passed too few args). Per ECMA-262
        // §10.2.10 IteratorBindingInitialization, a rest binding gets an
        // empty array when no trailing args are supplied.
        if (restIdx >= 0 && restIdx < 4) {
            int restCount = argc > restIdx ? (argc - restIdx) : 0;
            TsArray* restArr = TsArray::Create((size_t)(restCount > 0 ? restCount : 0));
            for (int i = 0; i < restCount; i++) {
                if (argv[restIdx + i]) {
                    restArr->Push((int64_t)argv[restIdx + i]);
                }
            }
            finalArgs[restIdx] = (TsValue*)ts_value_make_object(restArr);
        }

        typedef TsValue* (*FnPad)(void*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((FnPad)closure->func_ptr)(
            closure, finalArgs[0], finalArgs[1], finalArgs[2], finalArgs[3]);
    }

    // Uniform 9-user-arg dispatch through the closure trampoline. All
    // ts_call_N variants funnel through this signature so functions
    // declared with up to 9 user params receive every slot — never
    // reading stack garbage. Extra slots beyond the trampoline's
    // declared arity are dropped by the callee per the Microsoft x64
    // calling convention (caller cleans up).
    static inline TsValue* call_closure_padded9(
        TsClosure* closure,
        TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4,
        TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9) {
        typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*,
                                       TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn9)closure->func_ptr)(closure, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    }
    static inline TsValue* call_funcptr_padded9(
        void* fp, void* ctx,
        TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4,
        TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9) {
        typedef TsValue* (*Fn9)(void*, TsValue*, TsValue*, TsValue*, TsValue*,
                                       TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn9)fp)(ctx, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    }

    // This-first counterpart of call_closure_padded9 for Convention-B (method)
    // closures: trampoline shape (closure, this, arg1..arg9). Extra padding
    // slots beyond the trampoline's declared arity are dropped by the callee
    // (MS x64: caller cleans up).
    static inline TsValue* call_closure_padded9_method(
        TsClosure* closure, TsValue* thisArg,
        TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4,
        TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9) {
        typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*,
                                        TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn10)closure->func_ptr)(closure, thisArg, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    }

    // 10-user-arg padded variants (the canonical dispatchers funnel through these
    // so they cover the full ts_call_0..10 / ts_call_with_this range; extra slots
    // beyond a callee's declared arity are dropped per the MS x64 ABI).
    static inline TsValue* call_closure_padded10(
        TsClosure* closure, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5,
        TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9, TsValue* a10) {
        typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*,
                                        TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn10)closure->func_ptr)(closure, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    }
    static inline TsValue* call_funcptr_padded10(
        void* fp, void* ctx, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5,
        TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9, TsValue* a10) {
        typedef TsValue* (*Fn10)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*,
                                        TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn10)fp)(ctx, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    }
    static inline TsValue* call_closure_padded10_method(
        TsClosure* closure, TsValue* thisArg,
        TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5,
        TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9, TsValue* a10) {
        typedef TsValue* (*Fn11)(void*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*, TsValue*,
                                        TsValue*, TsValue*, TsValue*, TsValue*, TsValue*);
        return ((Fn11)closure->func_ptr)(closure, thisArg, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    }

    // Method (this-first) counterpart of ts_rest_pack_and_call. A method closure's
    // physical params are (closure, this, a1..aN); the rest element occupies the
    // user-arg slot at rest_param_index. Build the rest TsArray from
    // argv[restIdx..argc-1], place it at that slot, pad the remaining user slots
    // with undefined, and dispatch through the this-first trampoline. Without
    // this, with-receiver dispatch (`obj.m(...)`, `obj[k](...)`) dropped every
    // arg past the first for a rest-param method — ECMA-262 §8.6.3
    // IteratorBindingInitialization requires the rest binding to receive ALL
    // trailing arguments packed into an Array.
    static TsValue* ts_rest_pack_and_call_method(TsClosure* closure, TsValue* thisArg,
                                                 int argc, TsValue** argv) {
        int restIdx = closure->rest_param_index;
        TsValue* u = ts_value_make_undefined();
        TsValue* finalArgs[10] = { u, u, u, u, u, u, u, u, u, u };
        int leading = restIdx < 10 ? restIdx : 10;
        for (int i = 0; i < leading; i++)
            finalArgs[i] = (i < argc && argv) ? argv[i] : u;
        if (restIdx >= 0 && restIdx < 10) {
            int restCount = argc > restIdx ? (argc - restIdx) : 0;
            TsArray* restArr = TsArray::Create((size_t)(restCount > 0 ? restCount : 0));
            for (int i = 0; i < restCount; i++)
                if (argv[restIdx + i]) restArr->Push((int64_t)argv[restIdx + i]);
            finalArgs[restIdx] = (TsValue*)ts_value_make_object(restArr);
        }
        return call_closure_padded10_method(closure, thisArg,
            finalArgs[0], finalArgs[1], finalArgs[2], finalArgs[3], finalArgs[4],
            finalArgs[5], finalArgs[6], finalArgs[7], finalArgs[8], finalArgs[9]);
    }
    // Exact-arity dispatch for functions declared with 11..16 params: call
    // Fn<arity> with EXACTLY arity args so the call matches the compiled
    // function's LLVM signature (closure + arity params). The <=10 path is
    // unchanged (padded10). >16 falls back to padded10 (drops 11+, rare).
    static TsValue* call_closure_exact(TsClosure* closure, int argc, TsValue** argv) {
        TsValue* u = ts_value_make_undefined();
        #define A(i) (((i) < argc && argv) ? argv[i] : u)
        void* fp = closure->func_ptr;
        switch (closure->num_params > 0 ? closure->num_params : closure->arity) {
        case 11: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10)); }
        case 12: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11)); }
        case 13: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12)); }
        case 14: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13)); }
        case 15: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14)); }
        case 16: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14),A(15)); }
        }
        return call_closure_padded10(closure, A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9));
        #undef A
    }
    static TsValue* call_funcptr_exact(void* fp, void* ctx, int arity, int argc, TsValue** argv) {
        TsValue* u = ts_value_make_undefined();
        #define A(i) (((i) < argc && argv) ? argv[i] : u)
        switch (arity) {
        case 11: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10)); }
        case 12: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11)); }
        case 13: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12)); }
        case 14: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13)); }
        case 15: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14)); }
        case 16: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(ctx,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14),A(15)); }
        }
        return call_funcptr_padded10(fp, ctx, A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9));
        #undef A
    }
    static TsValue* call_closure_method_exact(TsClosure* closure, TsValue* thisArg, int argc, TsValue** argv) {
        TsValue* u = ts_value_make_undefined();
        #define A(i) (((i) < argc && argv) ? argv[i] : u)
        void* fp = closure->func_ptr;
        switch (closure->num_params > 0 ? closure->num_params : closure->arity) {
        case 11: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10)); }
        case 12: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11)); }
        case 13: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12)); }
        case 14: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13)); }
        case 15: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14)); }
        case 16: { typedef TsValue* (*F)(void*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*,TsValue*); return ((F)fp)(closure,thisArg,A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10),A(11),A(12),A(13),A(14),A(15)); }
        }
        return call_closure_padded10_method(closure, thisArg, A(0),A(1),A(2),A(3),A(4),A(5),A(6),A(7),A(8),A(9));
        #undef A
    }


    // Canonical call dispatchers — the SINGLE place the closure/proxy/native/
    // inner-closure + is_method + rest dispatch logic lives. The ts_call_N /
    // ts_call_with_this_N families forward to these (each was previously a
    // hand-unrolled, subtly-divergent copy — e.g. the ts_gc_base guard appeared
    // in some N and not others). Defined after maybe_override_context below.
    static TsValue* call_dispatch_n(TsValue* boxedFunc, int argc, TsValue** argv);
    static TsValue* call_dispatch_with_this(TsValue* boxedFunc, TsValue* thisArg,
                                            int argc, TsValue** argv);

    // Helper to extract TsClosure from a boxed or raw value
    TsClosure* ts_extract_closure(TsValue* boxedFunc) {
        if (!boxedFunc) return nullptr;

        uint64_t nb = nanbox_from_tsvalue_ptr(boxedFunc);
        // Non-pointer values can't be closures
        if (!nanbox_is_ptr(nb)) return nullptr;
        void* ptr = nanbox_to_ptr(nb);
        if (!ptr) return nullptr;

        // Check if this object has the closure magic at offset 16
        uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
        if (magic16 == 0x434C5352) {  // 'CLSR'
            return (TsClosure*)ptr;
        }

        return nullptr;
    }

    // Canonical IsCallable (ECMA-262 7.2.3) — the single source of truth shared
    // by every builtin that guards a callback (Array/Map/Set/String/Promise/
    // iterator helpers). Defined to match EXACTLY what call_dispatch_n can
    // invoke: a closure (CLSR), a TsFunction (FUNC — covers native + bound
    // functions, whose magic is FUNC@16), or a Proxy whose [[ProxyTarget]] is
    // callable. It replaces 8 hand-rolled copies that disagreed (some also
    // checked a dead CLSR/FUNC@offset-0; none accepted proxies/bound fns), so
    // `arr.forEach.call(arr, new Proxy(fn, {}))` wrongly threw "not callable".
    // Takes void* because callers hold the value as a TsValue* or a raw nanbox
    // — bit-identical, since nanbox_from_tsvalue_ptr is a reinterpret cast.
    extern "C" bool ts_is_callable(void* val) {
        if (!val) return false;
        TsValue* boxed = (TsValue*)val;
        if (ts_extract_closure(boxed)) return true;
        if (ts_extract_function(boxed)) return true;
        TsProxy* proxy = ts_extract_proxy(boxed);
        if (proxy) return ts_is_callable(proxy->target);  // IsCallable([[ProxyTarget]])
        return false;
    }

    // (The arity-suffixed ts_call_0..10 are gone — internal C++ callers use the
    // `tsCall(...)` variadic template in TsObject.h, which funnels through
    // call_dispatch_n; the compiler emits ts_call / ts_call_n directly. The
    // ts_call_with_this_0..8 family below stays as thin forwarders because the
    // compiler still emits them by name from ASTToHIR.)

    // Forward decl: TsFlatObject.cpp's bound-method trampoline. We need
    // its address to detect bound methods so ts_call_with_this_N doesn't
    // overwrite the BoundMethodCtx* with thisArg.
    extern "C" TsValue* flat_bound_method_trampoline(void*, int, TsValue**);

    // Helper: override func->context = thisArg unless `func` is a bound-
    // method TsFunction (whose context IS its BoundMethodCtx — overriding
    // would corrupt the trampoline's read of the method pointer).
    extern "C" TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv);

    // Expose the bound-function TARGET for IsConstructor recursion
    // (TsGlobals.cpp) without leaking the TU-private struct layout.
    void* ts_bound_function_target(void* boundCtx) {
        if (!boundCtx) return nullptr;
        return ((TsBoundFunction*)boundCtx)->targetFunction;
    }

    static inline void* maybe_override_context(TsFunction* func, TsValue* thisArg) {
        void* savedCtx = func->context;
        // A BOUND function's context is its TsBoundFunction record — a
        // thisArg override corrupts it (bound fns IGNORE thisArg per spec;
        // flatMap passing an explicit undefined this AV'd on the deref).
        if (func->funcPtr != (void*)flat_bound_method_trampoline &&
            func->funcPtr != (void*)ts_bound_function_call &&
            !func->keep_context) {
            func->context = thisArg;
        }
        return savedCtx;
    }

    // Canonical NO-receiver call dispatch (the single implementation behind the
    // ts_call_0..10 family). Replicates the union of their behavior: the
    // corruption guard (ts_gc_base) that some N had and others didn't; rest-param
    // packing on the closure path; the proxy / native / inner-closure paths.
    // OrdinaryCallBindThis (ECMA-262 10.2.1.2) for receiver-less dispatch:
    // bind this = undefined for the callee's duration so strict callees see
    // undefined (sloppy callees re-coerce at their this-read). Arrows are
    // exempt — compiled arrows implement lexical `this` by reading this same
    // slot. Normal returns restore via the destructor; exceptional unwind
    // restores via the try-handler slot snapshot (Core.cpp), so a skipped
    // destructor after longjmp is harmless.
    struct PlainCallThisScope {
        void* saved = nullptr;
        bool active;
        explicit PlainCallThisScope(bool a) : active(a) {
            if (active) {
                saved = ts_call_this_value;
                ts_call_this_value = (void*)ts_value_make_undefined();
            }
        }
        ~PlainCallThisScope() { if (active) ts_call_this_value = saved; }
    };

    static TsValue* call_dispatch_n(TsValue* boxedFunc, int argc, TsValue** argv) {
        ts_last_call_argc = argc;
        TsValue* u = ts_value_make_undefined();
        auto A = [&](int i) -> TsValue* { return (i < argc && argv) ? argv[i] : u; };
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            void* fp = closure->func_ptr;
            if (fp && ts_gc_base(fp)) return u;  // func_ptr in GC heap => corrupt
            PlainCallThisScope pts(!closure->is_arrow);
            if (closure->rest_param_index >= 0)
                return ts_rest_pack_and_call(closure, argc, argv);
            if (closure->is_method) {
                // Bare call (no receiver) to a method closure: ECMA-262 invokes it
                // with `this` = undefined. A method's physical params are
                // (closure, this, a1..aN), so we MUST inject an undefined `this`
                // slot here — otherwise arg0 is swallowed by the `this` parameter
                // and every argument shifts by one. This broke detached
                // `C.prototype.m(a, b)` calls and the entire class/dstr
                // generator-method family (142 dstr tests call methods detached).
                int np = closure->num_params > 0 ? closure->num_params : closure->arity;
                if (np >= 11 && np <= 16)
                    return call_closure_method_exact(closure, u, argc, argv);
                return call_closure_padded10_method(closure, u,
                    A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9));
            }
            if ((closure->num_params > 0 ? closure->num_params : closure->arity) >= 11 && (closure->num_params > 0 ? closure->num_params : closure->arity) <= 16)
                return call_closure_exact(closure, argc, argv);
            return call_closure_padded10(closure, A(0), A(1), A(2), A(3), A(4),
                                                  A(5), A(6), A(7), A(8), A(9));
        }
        TsProxy* proxy = ts_extract_proxy(boxedFunc);
        if (proxy) {
            TsArray* argsArr = TsArray::Create((size_t)(argc > 0 ? argc : 0));
            for (int i = 0; i < argc; i++) argsArr->Push((int64_t)A(i));
            return proxy->apply(nullptr, (TsValue*)argsArr, argc);
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) return u;
        if (func->type == FunctionType::NATIVE) {
            void* fp = func->funcPtr;
            if (fp && ts_gc_base(fp)) return u;
            return ((TsFunctionPtr)fp)(func->context, argc, argv);
        }
        TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
        if (innerClosure) {
            void* fp = innerClosure->func_ptr;
            if (fp && ts_gc_base(fp)) return u;
            PlainCallThisScope pts(!innerClosure->is_arrow);
            if ((innerClosure->num_params > 0 ? innerClosure->num_params : innerClosure->arity) >= 11 && (innerClosure->num_params > 0 ? innerClosure->num_params : innerClosure->arity) <= 16)
                return call_closure_exact(innerClosure, argc, argv);
            return call_closure_padded10(innerClosure, A(0), A(1), A(2), A(3), A(4),
                                                       A(5), A(6), A(7), A(8), A(9));
        }
        void* fp = func->funcPtr;
        if (fp && ts_gc_base(fp)) return u;
        if (func->arity >= 11 && func->arity <= 16)
            return call_funcptr_exact(fp, func->context, func->arity, argc, argv);
        return call_funcptr_padded10(fp, func->context, A(0), A(1), A(2), A(3), A(4),
                                                        A(5), A(6), A(7), A(8), A(9));
    }

    // Canonical WITH-receiver call dispatch (the single implementation behind the
    // ts_call_with_this_0..8 family). Sets/restores the ts_call_this_value global,
    // honors closure->is_method (this-first trampoline), and routes plain
    // TsFunctions through maybe_override_context + the no-receiver dispatch —
    // exactly as the unrolled functions did. (Matches their no-rest-on-the-
    // closure-path behavior.)
    static TsValue* call_dispatch_with_this(TsValue* boxedFunc, TsValue* thisArg,
                                            int argc, TsValue** argv) {
        ts_last_call_argc = argc;
        void* savedThis = ts_call_this_value;
        TsValue* u = ts_value_make_undefined();
        auto A = [&](int i) -> TsValue* { return (i < argc && argv) ? argv[i] : u; };
        TsClosure* closure = ts_extract_closure(boxedFunc);
        // Arrow functions have a lexical `this`; an explicit receiver
        // (`fn.call(x)`, or a thisArg threaded through Array/Set/Map forEach/map)
        // must NOT override it (ES 10.2.1.1 arrows have no [[ThisMode]] rebind).
        // The no-receiver path already guards this via PlainCallThisScope(
        // !is_arrow); mirror it here so the arrow keeps the caller's `this`
        // instead of seeing thisArg. Without this, `set.forEach(_ => this, obj)`
        // saw obj as `this`.
        // NOTE: an arrow's lexical `this` should come from its capture, but the
        // runtime approximates. Using savedThis for arrows fixes
        // `set.forEach(_=>this, obj)` but breaks static-field-init arrow IIFEs
        // where the correct lexical `this` (the class) is threaded AS thisArg —
        // net-negative in the full sweep. Keep the plain thisArg path.
        ts_call_this_value = thisArg;
        if (closure) {
            void* fp = closure->func_ptr;
            if (!fp || ts_gc_base(fp)) { ts_call_this_value = savedThis; return u; }
            // A rest-param closure needs argument PACKING — the padded10 call
            // below passes raw args and the rest array never materializes
            // (`obj.m = (...args) => {}; obj.m(1)` saw args undefined). Method
            // closures (this-first trampoline: `obj.m(...args)`,
            // `obj[k](...args)`, object-literal concise methods) previously fell
            // through this guard and lost every arg past the first — pack them
            // via the this-first packer. Non-method closures use the Convention-A
            // packer; thisArg propagates via ts_call_this_value (set above).
            if (closure->rest_param_index >= 0) {
                TsValue* result = closure->is_method
                    ? ts_rest_pack_and_call_method(closure, thisArg, argc, argv)
                    : ts_rest_pack_and_call(closure, argc, argv);
                ts_call_this_value = savedThis;
                return result;
            }
            TsValue* result;
            if (closure->is_method) {
                if ((closure->num_params > 0 ? closure->num_params : closure->arity) >= 11 && (closure->num_params > 0 ? closure->num_params : closure->arity) <= 16)
                    result = call_closure_method_exact(closure, thisArg, argc, argv);
                else
                result = call_closure_padded10_method(closure, thisArg,
                    A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9));
            } else {
                if ((closure->num_params > 0 ? closure->num_params : closure->arity) >= 11 && (closure->num_params > 0 ? closure->num_params : closure->arity) <= 16)
                    result = call_closure_exact(closure, argc, argv);
                else
                result = call_closure_padded10(closure,
                    A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9));
            }
            ts_call_this_value = savedThis;
            return result;
        }
        // Proxy callee (ES 10.5.12 [[Call]]): the compiler pattern-matches
        // `X.call(thisArg, ...)` straight into this dispatcher with X as the
        // callee — a trap-less proxy must forward to its target with the
        // caller's thisArg (apply trap fires when present).
        {
            TsProxy* proxy = ts_extract_proxy(boxedFunc);
            if (proxy) {
                TsArray* argsArr = TsArray::Create((size_t)(argc > 0 ? argc : 0));
                for (int i = 0; i < argc; i++) argsArr->Push((int64_t)A(i));
                TsValue* result = proxy->apply(thisArg, (TsValue*)argsArr, argc);
                ts_call_this_value = savedThis;
                return result ? result : u;
            }
        }
        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) { ts_call_this_value = savedThis; return u; }
        void* savedCtx = maybe_override_context(func, thisArg);
        TsValue* result = call_dispatch_n(boxedFunc, argc, argv);
        func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    // Unified call entry points. The compiler emits these (instead of selecting
    // one of ts_call_0..10 / ts_call_with_this_0..8 by arity) with the arg count
    // passed EXPLICITLY and the unused arg slots padded with undefined. This
    // collapses the ~20 hand-unrolled call functions to a single dispatch each
    // and turns ts_last_call_argc from a global the caller had to set per-site
    // into a plain parameter. For >9 args the compiler still uses the array
    // forms (ts_call_n / ts_function_call_with_this).
    TsValue* ts_call(TsValue* boxedFunc, int64_t argc,
                     TsValue* a0, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4,
                     TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8) {
        TsValue* argv[9] = { a0, a1, a2, a3, a4, a5, a6, a7, a8 };
        return call_dispatch_n(boxedFunc, (int)argc, argv);
    }
    TsValue* ts_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int64_t argc,
                               TsValue* a0, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4,
                               TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8) {
        TsValue* argv[9] = { a0, a1, a2, a3, a4, a5, a6, a7, a8 };
        return call_dispatch_with_this(boxedFunc, thisArg, (int)argc, argv);
    }

    // ts_call_with_this_X functions: call a function with a specific 'this' binding
    // These temporarily patch the function's context before calling
    TsValue* ts_call_with_this_0(TsValue* boxedFunc, TsValue* thisArg) {
        return call_dispatch_with_this(boxedFunc, thisArg, 0, nullptr);
    }

    TsValue* ts_call_with_this_1(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1) {
        TsValue* argv[1] = { arg1 };
        return call_dispatch_with_this(boxedFunc, thisArg, 1, argv);
    }

    TsValue* ts_call_with_this_2(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2) {
        TsValue* argv[2] = { arg1, arg2 };
        return call_dispatch_with_this(boxedFunc, thisArg, 2, argv);
    }

    TsValue* ts_call_with_this_3(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsValue* argv[3] = { arg1, arg2, arg3 };
        return call_dispatch_with_this(boxedFunc, thisArg, 3, argv);
    }

    TsValue* ts_call_with_this_4(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        TsValue* argv[4] = { arg1, arg2, arg3, arg4 };
        return call_dispatch_with_this(boxedFunc, thisArg, 4, argv);
    }

    TsValue* ts_call_with_this_5(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        TsValue* argv[5] = { arg1, arg2, arg3, arg4, arg5 };
        return call_dispatch_with_this(boxedFunc, thisArg, 5, argv);
    }

    TsValue* ts_call_with_this_6(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        TsValue* argv[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
        return call_dispatch_with_this(boxedFunc, thisArg, 6, argv);
    }

    TsValue* ts_call_with_this_7(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        TsValue* argv[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
        return call_dispatch_with_this(boxedFunc, thisArg, 7, argv);
    }

    TsValue* ts_call_with_this_8(TsValue* boxedFunc, TsValue* thisArg, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        TsValue* argv[8] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
        return call_dispatch_with_this(boxedFunc, thisArg, 8, argv);
    }

    TsValue* ts_function_call(TsValue* boxedFunc, int argc, TsValue** argv) {
        // Array-form entry; the canonical dispatcher pads to 10 physical slots
        // (args beyond 10 are dropped, as the old arity switch did).
        return call_dispatch_n(boxedFunc, argc, argv);
    }

    TsValue* ts_call_n(TsValue* boxedFunc, int64_t argc, TsValue** argv) {
        return ts_function_call(boxedFunc, static_cast<int>(argc), argv);
    }

    // Forward decl: the canonical Array() constructor (defined later in this
    // TU). Used by the dynamic-`new` slow path so `new array.constructor(n)`
    // — where the constructor was obtained dynamically and happens to be the
    // Array built-in — produces a real TsArray instead of a plain object.

    // Typed-array create-on-buffer entry points (defined in TsGlobals.cpp via
    // DEFINE_TYPED_ARRAY_NEW). File-scope for the indirect-new dispatch below.
    extern "C" {
        void* ts_typed_array_new_i8(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_u8(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_clamped(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_i16(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_u16(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_i32(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_u32(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_f32(TsValue*, int64_t, int64_t);
        void* ts_typed_array_new_f64(TsValue*, int64_t, int64_t);
    }

    // Forward decl: the NewTarget-setting variadic entry (defined below); the
    // per-arity forwarders route through it so the ambient new.target register
    // is set for the constructor body (TypedArraySpeciesCreate via
    // ts_new_from_constructor_1 previously reached the core with it UNSET, so a
    // TypedArray constructor's "requires new" guard misfired).
    TsValue* ts_new_from_constructor(TsValue* constructorFn, int argc, TsValue** argv);

    // --- Builtin-subclass branding hooks (extends the proven Temporal/Promise
    // pattern to Array/Date/DataView/ArrayBuffer/Number/Boolean/String). ---

    // Build a genuinely-branded base instance for builtin `name` from the
    // constructor arguments. Each branch mirrors the direct-identity dispatch
    // for `new <Builtin>(...)` earlier in this TU so subclass construction and
    // direct construction share semantics. Returns nullptr for names outside
    // the table.
    static TsValue* ts_construct_builtin_branded(const char* name, int argc, TsValue** argv) {
        TsValue* a0 = (argc >= 1 && argv) ? argv[0] : nullptr;
        if (strcmp(name, "Array") == 0) {
            // ES 23.1.1.1: single-number arg -> length-n array, else [items].
            return ts_array_constructor_native(nullptr, argc, argv);
        }
        if (strcmp(name, "Date") == 0) {
            extern void* ts_date_create();
            extern void* ts_date_create_ms(int64_t ms);
            extern void* ts_date_create_str(void* str);
            extern void* ts_date_create_parts(double, double, double,
                                               double, double, double, double);
            if (argc == 0) return ts_value_make_object(ts_date_create());
            if (argc == 1 && a0) {
                uint64_t anb = nanbox_from_tsvalue_ptr(a0);
                // ES 21.4.2.1: a single String arg is parsed; anything else is
                // coerced via ToNumber (a Date arg uses valueOf -> ms).
                if (nanbox_is_string_ptr(anb))
                    return ts_value_make_object(ts_date_create_str(a0));
                return ts_value_make_object(ts_date_create_ms((int64_t)ts_to_number(a0)));
            }
            double p[7] = {0, 0, 1, 0, 0, 0, 0};
            for (int i = 0; i < 7 && i < argc; ++i)
                if (argv && argv[i]) p[i] = ts_to_number(argv[i]);
            return ts_value_make_object(
                ts_date_create_parts(p[0], p[1], p[2], p[3], p[4], p[5], p[6]));
        }
        if (strcmp(name, "DataView") == 0) {
            // ES 25.3.2.1 DataView(buffer, byteOffset, byteLength); the
            // create_full runtime performs the buffer/offset/length checks.
            extern void* ts_dataview_create_full(void*, int64_t, int64_t);
            int64_t off = 0, len = -1;  // -1 = "rest of buffer"
            if (argc >= 2 && argv && argv[1] && !ts_value_is_undefined(argv[1]))
                off = (int64_t)ts_to_number(argv[1]);
            if (argc >= 3 && argv && argv[2] && !ts_value_is_undefined(argv[2]))
                len = (int64_t)ts_to_number(argv[2]);
            void* dv = ts_dataview_create_full((void*)a0, off, len);
            return dv ? ts_value_make_object(dv) : ts_value_make_undefined();
        }
        if (strcmp(name, "ArrayBuffer") == 0) {
            // ES 25.1.4.1 ArrayBuffer(length) with ToIndex semantics (mirrors
            // the indirect-new ArrayBuffer branch: NaN -> +0; negative or
            // > 2^53-1 -> RangeError).
            extern void* ts_arraybuffer_create(int64_t length);
            double dlen = a0 ? ts_to_number(a0) : 0;
            if (dlen != dlen) dlen = 0;
            if (dlen < 0 || dlen > 9007199254740991.0) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "Invalid array buffer length"));
                return ts_value_make_undefined();  // unreachable
            }
            void* ab = ts_arraybuffer_create((int64_t)dlen);
            return ab ? ts_value_make_object(ab) : ts_value_make_undefined();
        }
        // The primitive wrappers are represented as TsMaps with a hidden
        // __NumberData / __BooleanData / __StringData slot whose prototype is
        // Builtin.prototype (the shape ts_get_global_Number/Boolean/String's
        // ctor functions produce for a direct `new Builtin(x)`); the
        // prototype-reading thisXxxValue helpers and the wrapper-aware
        // property paths key off exactly this shape.
        auto makeWrapper = [](void* (*globalGetter)(), const char* dataKey,
                              TsValue dataVal) -> TsValue* {
            TsMap* obj = TsMap::Create();
            TsValue k; k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::GetInterned(dataKey);
            obj->Set(k, dataVal);
            TsValue* boxed = ts_value_make_object(obj);
            void* g = globalGetter();
            if (g) {
                TsValue* proto = ts_object_get_dynamic((TsValue*)g,
                    ts_value_make_string(TsString::Create("prototype")));
                if (proto && !ts_value_is_undefined(proto) &&
                    !ts_value_is_null(proto))
                    ts_object_setPrototypeOf(boxed, proto);
            }
            return boxed;
        };
        if (strcmp(name, "Number") == 0) {
            // ES 21.1.1.1: [[NumberData]] = ToNumeric(value); +0 when absent.
            extern void* ts_get_global_Number();
            TsValue v; v.type = ValueType::NUMBER_DBL;
            v.d_val = (argc >= 1 && a0 ? ts_to_number(a0) : 0.0);
            return makeWrapper(ts_get_global_Number, "__NumberData", v);
        }
        if (strcmp(name, "Boolean") == 0) {
            // ES 20.3.1.1: [[BooleanData]] = ToBoolean(value).
            extern void* ts_get_global_Boolean();
            extern bool ts_value_to_bool(TsValue* v);
            TsValue v; v.type = ValueType::BOOLEAN;
            v.i_val = (a0 && ts_value_to_bool(a0)) ? 1 : 0;
            return makeWrapper(ts_get_global_Boolean, "__BooleanData", v);
        }
        if (strcmp(name, "String") == 0) {
            // ES 22.1.1.1: [[StringData]] = ToString(value); "" when absent.
            // ts_string_ctor runs spec ToString (ToPrimitive hooks) and may
            // throw, matching the direct `new String(x)` path.
            extern void* ts_get_global_String();
            extern void* ts_string_ctor(TsValue* v);
            void* s = (argc >= 1 && a0) ? ts_string_ctor(a0)
                                        : (void*)TsString::Create("");
            if (!s) return ts_value_make_undefined();
            TsValue v; v.type = ValueType::STRING_PTR;
            v.ptr_val = (TsString*)s;
            return makeWrapper(ts_get_global_String, "__StringData", v);
        }
        return nullptr;
    }

    // Relink a branded subclass instance's [[Prototype]] to Sub.prototype.
    // Wrapper instances (Number/Boolean/String data TsMaps) dispatch methods
    // through the REAL prototype chain — set it directly. Exotic natives
    // (TsArray/TsDate/TsDataView/TsBuffer) keep their magic-based dispatch and
    // use the native-object side proto consumed by ts_proto_chain_has /
    // ts_object_getPrototypeOf's native branch.
    static void ts_subclass_relink_proto(TsValue* inst, void* instRaw, TsValue* protoVal) {
        if (!inst || !instRaw || !protoVal) return;
        extern void ts_native_object_set_proto(void* obj, TsValue* proto);
        uint32_t m16 = ((uintptr_t)instRaw > 0x1000)
            ? *(uint32_t*)((char*)instRaw + 16) : 0;
        if (m16 == 0x4D415053) {  // TsMap (primitive wrapper)
            ts_object_setPrototypeOf(inst, protoVal);
        } else {
            ts_native_object_set_proto(instRaw, protoVal);
        }
    }

    // Walk `ctor`'s [[Prototype]] chain for one of the brandable builtin ctor
    // globals (`Sub.__proto__ === Builtin` for `class Sub extends Builtin`,
    // linked by ts_class_link_builtin_base / ts_class_link_dynamic_base).
    // `rawCtor` (the unboxed ctor, skipped during the walk so a DIRECT
    // `new Builtin()` stays on its dedicated dispatch) may be null to walk
    // without the skip. Returns the builtin's name or nullptr.
    static const char* ts_builtin_subclass_base_name(TsValue* ctor, void* rawCtor) {
        extern TsValue* ts_object_getPrototypeOf(TsValue* obj);
        extern void* ts_get_global_Array();
        extern void* ts_get_global_Date();
        extern void* ts_get_global_DataView();
        extern void* ts_get_global_ArrayBuffer();
        extern void* ts_get_global_Number();
        extern void* ts_get_global_Boolean();
        extern void* ts_get_global_String();
        struct BuiltinCtor { void* (*getter)(); const char* name; };
        static const BuiltinCtor kBuiltinCtors[] = {
            { ts_get_global_Array,       "Array" },
            { ts_get_global_Date,        "Date" },
            { ts_get_global_DataView,    "DataView" },
            { ts_get_global_ArrayBuffer, "ArrayBuffer" },
            { ts_get_global_Number,      "Number" },
            { ts_get_global_Boolean,     "Boolean" },
            { ts_get_global_String,      "String" },
        };
        const char* chosen = nullptr;
        TsValue* cur = ts_object_getPrototypeOf(ctor);
        for (int hops = 0; hops < 64 && cur && !chosen &&
             !ts_value_is_undefined(cur) && !ts_value_is_null(cur); hops++) {
            void* curRaw = ts_value_get_object(cur);
            if (curRaw && curRaw != rawCtor) {
                for (const auto& e : kBuiltinCtors) {
                    void* g = e.getter(); if (!g) continue;
                    void* gRaw = ts_value_get_object((TsValue*)g);
                    if (!gRaw) gRaw = g;
                    if (curRaw == g || curRaw == gRaw) { chosen = e.name; break; }
                }
            }
            cur = ts_object_getPrototypeOf(cur);
        }
        return chosen;
    }

    // Helper for "new ConstructorFunction(...args)" in the slow path.
    // Creates a new object, sets its prototype from constructor.prototype,
    // calls the constructor with this=newObject, and returns the new object.
    TsValue* ts_new_from_constructor_impl(TsValue* constructorFn, int argc, TsValue** argv) {
        // ES 13.3.5.1: `new` on a non-constructor (undefined/null included)
        // throws TypeError. The old guard silently returned a plain object,
        // so `new C.someUndefinedStatic()` succeeded (Iterator helper
        // non-constructible family asserts the throw).
        if (!constructorFn || ts_value_is_undefined(constructorFn) || ts_value_is_null(constructorFn)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "not a constructor (undefined or null)"));
            return ts_value_make_undefined();  // unreachable
        }

        // Proxy [[Construct]] (ES 10.5.13): route through the construct trap
        // or forward to the target. `new proxyOfCtor(...)` previously fell to
        // the generic path and returned undefined.
        {
            TsProxy* px = ts_extract_proxy(constructorFn);
            if (px) {
                TsArray* argArr = TsArray::Create((size_t)(argc > 0 ? argc : 1));
                for (int i = 0; i < argc; ++i)
                    ts_array_push(argArr, argv[i] ? argv[i] : ts_value_make_undefined());
                TsValue* r = px->construct(ts_value_make_object(argArr), argc, nullptr);
                return r ? r : ts_value_make_undefined();
            }
        }

        // A BOUND function's [[Construct]] (ECMA-262 10.4.1.2 BoundFunctionCreate):
        // `new boundFn(a)` constructs the TARGET with (boundArgs ++ callArgs); when
        // newTarget is the bound function itself it folds to the target. bind() makes
        // the bound function a native TsFunction whose funcPtr is
        // ts_bound_function_call and whose context is the TsBoundFunction, so recurse
        // on the target — it allocates `this` from target.prototype and runs the real
        // constructor. The generic path below would instead override the bound context
        // with `this` and merely [[Call]] the bound function (returning undefined).
        {
            void* raw = ts_value_get_object(constructorFn);
            if (raw && (uintptr_t)raw > 0x1000 &&
                *(uint32_t*)((char*)raw + 16) == TsFunction::MAGIC) {
                TsFunction* tf = (TsFunction*)raw;
                if (tf->funcPtr == (void*)ts_bound_function_call && tf->context) {
                    TsBoundFunction* bound = (TsBoundFunction*)tf->context;
                    if (bound->targetFunction &&
                        bound->boundArgCount >= 0 && bound->boundArgCount <= 65535) {
                        if (argc < 0) argc = 0;
                        int total = bound->boundArgCount + argc;
                        std::vector<TsValue*> combined((size_t)total, nullptr);
                        for (int i = 0; i < bound->boundArgCount; i++)
                            combined[i] = bound->boundArgs[i];
                        for (int i = 0; i < argc; i++)
                            combined[(size_t)bound->boundArgCount + i] = argv[i];
                        return ts_new_from_constructor_impl(bound->targetFunction, total,
                                                            total ? combined.data() : nullptr);
                    }
                }
            }
        }

        // Per ES spec, built-in prototype methods (Array.prototype.X etc.)
        // have no [[Construct]] — `new fn()` must throw TypeError. Check the
        // is_constructor flag set by makeNamedNativeFunction / addMethod.
        {
            void* raw = ts_value_get_object(constructorFn);
            if (raw) {
                uint32_t magic16 = *(uint32_t*)((char*)raw + 16);
                if (magic16 == TsFunction::MAGIC) {
                    TsFunction* tf = (TsFunction*)raw;
                    if (!tf->is_constructor) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "is not a constructor"));
                        return ts_value_make_undefined();  // unreachable
                    }
                }
            }
        }

        // Built-in constructor GLOBALS (Map/Set/Date/RegExp/Array/...) are
        // represented as makeSimpleConstructorGlobal TsMap objects, NOT
        // TsFunctions, so the name-based dispatch below (which requires
        // TsFunction::MAGIC) misses them entirely when reached via a runtime
        // value (`new object.constructor(...)` in lodash baseClone, or
        // `var M = root.Map; new M`). Dispatch by POINTER IDENTITY against the
        // known globals first. Without this, `new <Map global>()` fell through
        // to the generic path -> plain object with no [[MapData]] brand.
        {
            extern void* ts_get_global_Map();
            extern void* ts_get_global_Set();
            extern void* ts_get_global_WeakMap();
            extern void* ts_get_global_WeakSet();
            extern void* ts_get_global_Array();
            extern void* ts_get_global_Date();
            extern void* ts_get_global_RegExp();
            extern void* ts_map_create_from_iterable(TsValue* iterable);
            extern void* ts_set_create_from_iterable(TsValue* iterable);
            extern void* ts_weakmap_create();
            extern void* ts_weakset_create();
            void* rawCtor = ts_value_get_object(constructorFn);
            if (!rawCtor) rawCtor = nanbox_is_ptr(nanbox_from_tsvalue_ptr(constructorFn))
                ? nanbox_to_ptr(nanbox_from_tsvalue_ptr(constructorFn)) : nullptr;
            if (rawCtor) {
                auto isGlobal = [&](void*(*getter)()) -> bool {
                    void* g = getter(); if (!g) return false;
                    void* gr = ts_value_get_object((TsValue*)g);
                    if (!gr) gr = g;
                    return gr == rawCtor;
                };
                // ES 20.4.1.1: Symbol is not `new`-able — constructing it
                // (NewTarget !== undefined, incl. Reflect.construct(Symbol))
                // throws TypeError. Without this branch the generic path
                // minted a plain object carrying Symbol.prototype.
                {
                    extern void* ts_get_global_Symbol();
                    if (isGlobal(ts_get_global_Symbol)) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "Symbol is not a constructor"));
                        return ts_value_make_undefined();  // unreachable
                    }
                }
                // new Proxy(target, handler) -> a real TsProxy (otherwise the
                // generic construct produced a plain object whose traps never fire).
                {
                    extern void* ts_get_global_Proxy();
                    extern TsValue* ts_proxy_create(void* target, void* handler);
                    if (isGlobal(ts_get_global_Proxy)) {
                        void* target  = (argc > 0 && argv) ? (void*)argv[0] : nullptr;
                        void* handler = (argc > 1 && argv) ? (void*)argv[1] : nullptr;
                        return ts_proxy_create(target, handler);
                    }
                }
                TsValue* it = (argc >= 1 && argv) ? argv[0] : nullptr;
                if (isGlobal(ts_get_global_Map)) {
                    void* m = ts_map_create_from_iterable(it);
                    return m ? ts_value_make_object(m) : ts_value_make_undefined();
                }
                if (isGlobal(ts_get_global_Set)) {
                    void* s = ts_set_create_from_iterable(it);
                    return s ? ts_value_make_object(s) : ts_value_make_undefined();
                }
                if (isGlobal(ts_get_global_WeakMap)) {
                    void* m = ts_weakmap_create();
                    return m ? ts_value_make_object(m) : ts_value_make_undefined();
                }
                if (isGlobal(ts_get_global_WeakSet)) {
                    void* s = ts_weakset_create();
                    return s ? ts_value_make_object(s) : ts_value_make_undefined();
                }
                if (isGlobal(ts_get_global_Array)) {
                    return ts_array_constructor_native(nullptr, argc, argv);
                }
                if (isGlobal(ts_get_global_RegExp)) {
                    extern void* ts_regexp_create(void* pattern, void* flags);
                    TsValue* pat = (argc >= 1 && argv) ? argv[0] : nullptr;
                    TsValue* fl  = (argc >= 2 && argv) ? argv[1] : nullptr;
                    void* re = ts_regexp_create(pat, fl);
                    return re ? ts_value_make_object(re) : ts_value_make_undefined();
                }
                {
                    // new Temporal.PlainTime(...) — a namespace sub-constructor,
                    // matched via its own cached-ctor getter.
                    extern void* ts_temporal_get_plaintime_ctor();
                    extern TsValue* ts_temporal_plaintime_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_plaintime_ctor)) {
                        return ts_temporal_plaintime_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.Duration(...)
                    extern void* ts_temporal_get_duration_ctor();
                    extern TsValue* ts_temporal_duration_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_duration_ctor)) {
                        return ts_temporal_duration_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.PlainDate(...)
                    extern void* ts_temporal_get_plaindate_ctor();
                    extern TsValue* ts_temporal_plaindate_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_plaindate_ctor)) {
                        return ts_temporal_plaindate_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.PlainYearMonth(...)
                    extern void* ts_temporal_get_plainyearmonth_ctor();
                    extern TsValue* ts_temporal_plainyearmonth_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_plainyearmonth_ctor)) {
                        return ts_temporal_plainyearmonth_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.PlainMonthDay(...)
                    extern void* ts_temporal_get_plainmonthday_ctor();
                    extern TsValue* ts_temporal_plainmonthday_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_plainmonthday_ctor)) {
                        return ts_temporal_plainmonthday_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.PlainDateTime(...)
                    extern void* ts_temporal_get_plaindatetime_ctor();
                    extern TsValue* ts_temporal_plaindatetime_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_plaindatetime_ctor)) {
                        return ts_temporal_plaindatetime_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.Instant(...)
                    extern void* ts_temporal_get_instant_ctor();
                    extern TsValue* ts_temporal_instant_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_instant_ctor)) {
                        return ts_temporal_instant_construct(argc, argv);
                    }
                }
                {
                    // new Temporal.ZonedDateTime(...)
                    extern void* ts_temporal_get_zoneddatetime_ctor();
                    extern TsValue* ts_temporal_zoneddatetime_construct(int argc, TsValue** argv);
                    if (isGlobal(ts_temporal_get_zoneddatetime_ctor)) {
                        return ts_temporal_zoneddatetime_construct(argc, argv);
                    }
                }
                {
                    // `new Promise(executor)` reached through a runtime
                    // constructor value (`var P = Promise; new P(fn)` — and
                    // the literal form also lowers through this dispatcher).
                    // Without this branch it fell through to the generic
                    // path -> plain TsMap, executor never invoked.
                    extern void* ts_get_global_Promise();
                    extern TsValue* ts_promise_new(TsValue* executor);
                    if (isGlobal(ts_get_global_Promise)) {
                        return ts_promise_new(it);
                    }
                }
                if (isGlobal(ts_get_global_Date)) {
                    extern void* ts_date_create();
                    extern void* ts_date_create_ms(int64_t ms);
                    extern void* ts_date_create_str(void* str);
                    extern void* ts_date_create_parts(double, double, double,
                                                       double, double, double, double);
                    if (argc == 0) return ts_value_make_object(ts_date_create());
                    if (argc == 1 && it) {
                        uint64_t anb = nanbox_from_tsvalue_ptr(it);
                        if (nanbox_is_string_ptr(anb))
                            return ts_value_make_object(ts_date_create_str(it));
                        return ts_value_make_object(ts_date_create_ms((int64_t)ts_to_number(it)));
                    }
                    double p[7] = {0, 0, 1, 0, 0, 0, 0};
                    for (int i = 0; i < 7 && i < argc; ++i)
                        if (argv && argv[i]) p[i] = ts_to_number(argv[i]);
                    return ts_value_make_object(
                        ts_date_create_parts(p[0], p[1], p[2], p[3], p[4], p[5], p[6]));
                }
                {
                    // Indirect `new <TypedArray global>(buffer, byteOffset, length)`
                    // (lodash cloneTypedArray). ONLY intercept the BUFFER form
                    // (arg0 is a TsBuffer) — the length/array forms already work
                    // via the generic path and were regressed by a broader gate.
                    TsValue* a0 = (argc >= 1 && argv) ? argv[0] : nullptr;
                    void* a0raw = a0 ? ts_value_get_object(a0) : nullptr;
                    bool a0IsBuffer = false;
                    if (a0raw && (uintptr_t)a0raw > 0x1000)
                        a0IsBuffer = (*(uint32_t*)((char*)a0raw + 16) == 0x42554646);
                    if (a0IsBuffer) {
                        extern void* ts_get_global_Int8Array(); extern void* ts_get_global_Uint8Array();
                        extern void* ts_get_global_Uint8ClampedArray(); extern void* ts_get_global_Int16Array();
                        extern void* ts_get_global_Uint16Array(); extern void* ts_get_global_Int32Array();
                        extern void* ts_get_global_Uint32Array(); extern void* ts_get_global_Float32Array();
                        extern void* ts_get_global_Float64Array();
                        extern void* ts_get_global_BigInt64Array();
                        extern void* ts_get_global_BigUint64Array();
                        extern void* ts_typed_array_new_i64(TsValue*, int64_t, int64_t);
                        extern void* ts_typed_array_new_u64(TsValue*, int64_t, int64_t);
                        struct TAEntry { void*(*g)(); void*(*n)(TsValue*, int64_t, int64_t); };
                        const TAEntry taTable[] = {
                            { ts_get_global_Int8Array, ts_typed_array_new_i8 },
                            { ts_get_global_Uint8Array, ts_typed_array_new_u8 },
                            { ts_get_global_Uint8ClampedArray, ts_typed_array_new_clamped },
                            { ts_get_global_Int16Array, ts_typed_array_new_i16 },
                            { ts_get_global_Uint16Array, ts_typed_array_new_u16 },
                            { ts_get_global_Int32Array, ts_typed_array_new_i32 },
                            { ts_get_global_Uint32Array, ts_typed_array_new_u32 },
                            { ts_get_global_Float32Array, ts_typed_array_new_f32 },
                            { ts_get_global_Float64Array, ts_typed_array_new_f64 },
                            { ts_get_global_BigInt64Array, ts_typed_array_new_i64 },
                            { ts_get_global_BigUint64Array, ts_typed_array_new_u64 },
                        };
                        for (const auto& e : taTable) {
                            if (isGlobal(e.g)) {
                                // ES 23.2.5.1 InitializeTypedArrayFromArrayBuffer:
                                // offset = ToIndex(byteOffset) (offset<0 or non-
                                // integer-overflow → RangeError); then when a
                                // length is present, ToIndex(length) (negative /
                                // -Infinity → RangeError). An omitted/undefined
                                // length keeps the -1 "rest of buffer" sentinel.
                                // ToIndex(ToIntegerOrInfinity(x)): truncate toward
                                // zero, THEN reject a negative (< 0, i.e. <= -1
                                // after trunc) or > 2^53-1 integer. -0.5 truncates
                                // to 0 and must NOT throw (toindex-byteoffset).
                                int64_t bo = 0, bl = -1;
                                if (argc >= 2 && argv && argv[1] &&
                                    !ts_value_is_undefined(argv[1])) {
                                    double od = ts_to_number(argv[1]);
                                    if (od != od) od = 0;  // NaN -> 0
                                    if (od <= -1.0 || od > 9007199254740991.0) {
                                        ts_throw((TsValue*)ts_error_create_typed(
                                            "RangeError", "Invalid typed array byteOffset"));
                                        return ts_value_make_undefined();
                                    }
                                    bo = (od > 0.0) ? (int64_t)od : 0;  // trunc toward zero
                                }
                                if (argc >= 3 && argv && argv[2] &&
                                    !ts_value_is_undefined(argv[2])) {
                                    double ld = ts_to_number(argv[2]);
                                    if (ld != ld) ld = 0;  // NaN -> 0
                                    if (ld <= -1.0 || ld > 9007199254740991.0) {
                                        ts_throw((TsValue*)ts_error_create_typed(
                                            "RangeError", "Invalid typed array length"));
                                        return ts_value_make_undefined();
                                    }
                                    bl = (ld > 0.0) ? (int64_t)ld : 0;
                                }
                                void* r = e.n(a0, bo, bl);
                                return r ? ts_value_make_object(r) : ts_value_make_undefined();
                            }
                        }
                    }
                    extern void* ts_get_global_ArrayBuffer();
                    if (isGlobal(ts_get_global_ArrayBuffer)) {
                        // `new <ArrayBuffer global>(byteLength)` reached via a
                        // runtime value (lodash cloneArrayBuffer:
                        // `new arrayBuffer.constructor(byteLength)`). The
                        // wrapAsCallable body produces a non-buffer, so build a
                        // real TsBuffer here.
                        extern void* ts_arraybuffer_create(int64_t length);
                        // ToIndex semantics: NaN coerces to +0; negative or
                        // non-finite-positive lengths throw RangeError (the
                        // old `len < 0 -> 0` clamp silently swallowed
                        // `new ArrayBuffer(Infinity)` via INT64_MIN).
                        double dlen = (argc >= 1 && it) ? ts_to_number(it) : 0;
                        if (dlen != dlen) dlen = 0;
                        if (dlen < 0 || dlen > 9007199254740991.0) {
                            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                                "Invalid array buffer length"));
                            return ts_value_make_undefined();
                        }
                        void* ab = ts_arraybuffer_create((int64_t)dlen);
                        return ab ? ts_value_make_object(ab) : ts_value_make_undefined();
                    }
                }
            }
        }

        // Built-in collection constructors reached through an aliased/dynamic
        // reference (e.g. lodash's `var Map = getNative(root,'Map'); new Map`)
        // arrive here instead of the compiler's `new Map()` fast path. Their
        // wrapAsCallable body returns undefined, so the generic path below
        // would yield a plain object WITHOUT the [[MapData]]/[[SetData]] brand
        // — then `.get`/`.set`/`.add` throw "incompatible receiver". Detect
        // the constructor by name and build the properly branded instance
        // (these creators set the brand; Map/Set also accept an iterable).
        {
            extern void* ts_map_create_from_iterable(TsValue* iterable);
            extern void* ts_set_create_from_iterable(TsValue* iterable);
            void* raw = ts_value_get_object(constructorFn);
            if (raw && *(uint32_t*)((char*)raw + 16) == TsFunction::MAGIC) {
                TsFunction* tf = (TsFunction*)raw;
                const char* nm = tf->name ? tf->name->ToUtf8() : nullptr;
                if (nm) {
                    TsValue* it = (argc >= 1 && argv) ? argv[0] : nullptr;
                    if (strcmp(nm, "Map") == 0) {
                        void* m = ts_map_create_from_iterable(it);
                        return m ? ts_value_make_object(m) : ts_value_make_undefined();
                    }
                    if (strcmp(nm, "Set") == 0) {
                        void* s = ts_set_create_from_iterable(it);
                        return s ? ts_value_make_object(s) : ts_value_make_undefined();
                    }
                    if (strcmp(nm, "WeakMap") == 0) {
                        void* m = ts_weakmap_create();
                        return m ? ts_value_make_object(m) : ts_value_make_undefined();
                    }
                    if (strcmp(nm, "WeakSet") == 0) {
                        void* s = ts_weakset_create();
                        return s ? ts_value_make_object(s) : ts_value_make_undefined();
                    }
                    if (strcmp(nm, "Array") == 0) {
                        // ECMA-262 §23.1.1.1 — `new Array(n)` (single number)
                        // yields a length-n sparse array; `new Array(...items)`
                        // yields [items]. lodash initCloneArray relies on this:
                        // `new array.constructor(array.length)`.
                        return ts_array_constructor_native(nullptr, argc, argv);
                    }
                    // RegExp/Date reached through an aliased constructor (e.g.
                    // lodash baseClone: `new object.constructor(...)` for
                    // initCloneByTag / cloneRegExp). The generic path builds a
                    // plain TsMap without the REGX/DATE brand, so source/flags
                    // and valueOf break. Dispatch to the native factory.
                    if (strcmp(nm, "RegExp") == 0) {
                        extern void* ts_regexp_create(void* pattern, void* flags);
                        TsValue* pat = (argc >= 1 && argv) ? argv[0] : nullptr;
                        TsValue* fl  = (argc >= 2 && argv) ? argv[1] : nullptr;
                        void* re = ts_regexp_create(pat, fl);
                        return re ? ts_value_make_object(re) : ts_value_make_undefined();
                    }
                    if (strcmp(nm, "Date") == 0) {
                        extern void* ts_date_create();
                        extern void* ts_date_create_ms(int64_t ms);
                        extern void* ts_date_create_str(void* str);
                        extern void* ts_date_create_parts(double, double, double,
                                                           double, double, double, double);
                        if (argc == 0) return ts_value_make_object(ts_date_create());
                        if (argc == 1 && it) {
                            uint64_t anb = nanbox_from_tsvalue_ptr(it);
                            // ECMA-262 §21.4.2.1: a single String arg is parsed;
                            // anything else is coerced via ToNumber (a Date arg
                            // uses its [[DateValue]] via valueOf -> ms).
                            if (nanbox_is_string_ptr(anb)) {
                                return ts_value_make_object(ts_date_create_str(it));
                            }
                            return ts_value_make_object(
                                ts_date_create_ms((int64_t)ts_to_number(it)));
                        }
                        // argc >= 2: (year, month, day=1, h=0, mi=0, s=0, ms=0)
                        double p[7] = {0, 0, 1, 0, 0, 0, 0};
                        for (int i = 0; i < 7 && i < argc; ++i) {
                            if (argv && argv[i]) p[i] = ts_to_number(argv[i]);
                        }
                        return ts_value_make_object(
                            ts_date_create_parts(p[0], p[1], p[2], p[3], p[4], p[5], p[6]));
                    }
                }
            }
        }

        // `class X extends Array/Date/DataView/ArrayBuffer/Number/Boolean/
        // String` reached via the runtime [[Construct]] path (the compiler
        // routes FIELDLESS synthetic-ctor subclasses of these here, mirroring
        // the Temporal shortcut; dynamic/eval subclasses arrive naturally):
        // ES 9.1.13 OrdinaryCreateFromConstructor must yield an object
        // carrying the base type's internal slots — [[DateValue]] 21.4.2.1,
        // [[ViewedArrayBuffer]] 25.3.2.1, [[ArrayBufferData]] 25.1.4.1,
        // [[NumberData]] 21.1.1.1, [[BooleanData]] 20.3.1.1, [[StringData]]
        // 22.1.1.1, Array exotic length 10.4.2. Walk C's [[Prototype]] chain
        // for the builtin ctor global; on a match build a genuinely BRANDED
        // base instance from the args, relink its [[Prototype]] to
        // C.prototype, and run C's ctor body for side effects (mirrors the
        // Temporal subclass block below).
        {
            extern void ts_native_object_set_proto(void* obj, TsValue* proto);
            extern TsValue* ts_object_getPrototypeOf(TsValue* obj);
            void* rawCtor = ts_value_get_object(constructorFn);
            if (rawCtor) {
                const char* chosen = ts_builtin_subclass_base_name(constructorFn, rawCtor);
                if (chosen) {
                    TsValue* inst = ts_construct_builtin_branded(chosen, argc, argv);
                    void* instRaw = inst ? ts_value_get_object(inst) : nullptr;
                    if (inst && instRaw) {
                        // Relink instance -> C.prototype (whose own
                        // [[Prototype]] is Builtin.prototype, so the branded
                        // methods stay reachable up the chain).
                        TsValue* protoVal = ts_object_get_dynamic(constructorFn,
                            ts_value_make_string(TsString::Create("prototype")));
                        if (protoVal && !ts_value_is_undefined(protoVal) &&
                            !ts_value_is_null(protoVal))
                            ts_subclass_relink_proto(inst, instRaw, protoVal);
                        // Run C's own constructor body for its side effects
                        // (field inits etc.); the class is fieldless and
                        // super() to a builtin base no-ops here, so this does
                        // not re-brand.
                        if (ts_is_callable((void*)constructorFn))
                            ts_function_call_with_this(constructorFn, inst, argc, argv);
                        return inst;
                    }
                }
            }
        }

        // A user class `extends Promise`: build a genuine promise instance
        // whose executor (the effect of `super(executor)`) settles it, so
        // NewPromiseCapability(C) — reached here for `Promise.all.call(Sub, …)`,
        // the *-ctx-ctor tests, and `new Sub(fn)` — yields a working subclass
        // promise instead of a plain object. Returns nullptr for non-Promise
        // classes, leaving the generic [[Construct]] path below unchanged.
        {
            extern TsValue* ts_promise_construct_subclass(TsValue*, int, TsValue**);
            if (TsValue* pinst = ts_promise_construct_subclass(constructorFn, argc, argv))
                return pinst;
        }

        // A user class `extends Temporal.PlainDate` (or any of the 8 Temporal
        // types): the direct-identity dispatch above only fires when the
        // constructor IS the Temporal global itself, so a subclass falls to the
        // generic path -> a plain TsMap with no Temporal magic (+16) / internal
        // ISO slots. Every branded Temporal method then throws (subclassing is
        // "ignored"). Mirror ts_promise_construct_subclass: walk C's
        // [[Prototype]] chain for one of the Temporal ctor globals; on a match,
        // build a genuine BRANDED instance via the type's *_construct, then
        // relink its [[Prototype]] to C.prototype so instanceof / getPrototypeOf
        // see the subclass. ES 2024 Temporal: OrdinaryCreateFromConstructor must
        // yield an object carrying the base type's internal slots.
        {
            extern void ts_native_object_set_proto(void* obj, TsValue* proto);
            extern TsValue* ts_object_getPrototypeOf(TsValue* obj);
            void* rawCtor = ts_value_get_object(constructorFn);
            if (rawCtor) {
                extern void* ts_temporal_get_plaintime_ctor();
                extern TsValue* ts_temporal_plaintime_construct(int, TsValue**);
                extern void* ts_temporal_get_duration_ctor();
                extern TsValue* ts_temporal_duration_construct(int, TsValue**);
                extern void* ts_temporal_get_plaindate_ctor();
                extern TsValue* ts_temporal_plaindate_construct(int, TsValue**);
                extern void* ts_temporal_get_plainyearmonth_ctor();
                extern TsValue* ts_temporal_plainyearmonth_construct(int, TsValue**);
                extern void* ts_temporal_get_plainmonthday_ctor();
                extern TsValue* ts_temporal_plainmonthday_construct(int, TsValue**);
                extern void* ts_temporal_get_plaindatetime_ctor();
                extern TsValue* ts_temporal_plaindatetime_construct(int, TsValue**);
                extern void* ts_temporal_get_instant_ctor();
                extern TsValue* ts_temporal_instant_construct(int, TsValue**);
                extern void* ts_temporal_get_zoneddatetime_ctor();
                extern TsValue* ts_temporal_zoneddatetime_construct(int, TsValue**);
                struct TemporalCtor { void* (*getter)(); TsValue* (*construct)(int, TsValue**); };
                const TemporalCtor kTemporalCtors[] = {
                    { ts_temporal_get_plaintime_ctor,      ts_temporal_plaintime_construct },
                    { ts_temporal_get_duration_ctor,       ts_temporal_duration_construct },
                    { ts_temporal_get_plaindate_ctor,      ts_temporal_plaindate_construct },
                    { ts_temporal_get_plainyearmonth_ctor, ts_temporal_plainyearmonth_construct },
                    { ts_temporal_get_plainmonthday_ctor,  ts_temporal_plainmonthday_construct },
                    { ts_temporal_get_plaindatetime_ctor,  ts_temporal_plaindatetime_construct },
                    { ts_temporal_get_instant_ctor,        ts_temporal_instant_construct },
                    { ts_temporal_get_zoneddatetime_ctor,  ts_temporal_zoneddatetime_construct },
                };
                TsValue* (*chosen)(int, TsValue**) = nullptr;
                // Walk C's [[Prototype]] chain (C.__proto__ === Temporal.X for a
                // `class C extends Temporal.X`, set by ts_class_link_builtin_base).
                TsValue* cur = ts_object_getPrototypeOf(constructorFn);
                for (int hops = 0; hops < 64 && cur && !chosen &&
                     !ts_value_is_undefined(cur) && !ts_value_is_null(cur); hops++) {
                    void* curRaw = ts_value_get_object(cur);
                    if (curRaw && curRaw != rawCtor) {  // skip C itself (direct new handled above)
                        for (const auto& e : kTemporalCtors) {
                            void* g = e.getter(); if (!g) continue;
                            void* gRaw = ts_value_get_object((TsValue*)g);
                            if (!gRaw) gRaw = g;
                            if (curRaw == g || curRaw == gRaw) { chosen = e.construct; break; }
                        }
                    }
                    cur = ts_object_getPrototypeOf(cur);
                }
                if (chosen) {
                    TsValue* inst = chosen(argc, argv);
                    void* instRaw = inst ? ts_value_get_object(inst) : nullptr;
                    if (inst && instRaw) {
                        // Relink instance -> C.prototype (the subclass prototype
                        // whose own [[Prototype]] is Temporal.X.prototype, so the
                        // branded methods remain reachable up the chain).
                        TsValue* protoVal = ts_object_get_dynamic(constructorFn,
                            ts_value_make_string(TsString::Create("prototype")));
                        if (protoVal && !ts_value_is_undefined(protoVal) &&
                            !ts_value_is_null(protoVal))
                            ts_native_object_set_proto(instRaw, protoVal);
                        // Run C's own constructor body for its side effects (field
                        // inits etc.); super() to a Temporal base is a no-op in our
                        // lowering, so this does not re-brand.
                        if (ts_is_callable((void*)constructorFn))
                            ts_function_call_with_this(constructorFn, inst, argc, argv);
                        return inst;
                    }
                }
            }
        }

        // 1. Create a new TsMap object
        TsMap* newObj = TsMap::Create();

        // 2. Box the new object as 'this'
        TsValue* thisArg = ts_value_make_object(newObj);

        // 3. Set [[Prototype]] from constructor.prototype. Reuse
        //    ts_object_setPrototypeOf, which handles BOTH TsMap prototypes and
        //    FLAT-OBJECT prototypes (object literals, e.g. `Foo.prototype = {a:1}`
        //    or `Foo.prototype = obj`) — it converts a flat proto to a map and
        //    does cycle detection. The old code only linked prototypes whose
        //    magic was MAPS, so a flat-object prototype was silently dropped and
        //    `new Foo()` instances inherited nothing (lodash `_.omit` inherited-
        //    keyed-properties test: `Foo.prototype = object; _.omit(new Foo, ...)`).
        TsString* protoKey = TsString::Create("prototype");
        TsValue* protoVal = ts_object_get_dynamic(constructorFn, ts_value_make_string(protoKey));
        if (protoVal && !ts_value_is_undefined(protoVal) && !ts_value_is_null(protoVal)) {
            ts_object_setPrototypeOf(thisArg, protoVal);
        }

        // 4. Call the constructor with this = new object
        // Guard: if constructor is not callable (e.g., TsMap stub), store args as .message
        TsClosure* asClosure = ts_extract_closure(constructorFn);
        TsFunction* asFunc = ts_extract_function(constructorFn);
        if (!asClosure && !asFunc) {
            if (argc >= 1 && argv && argv[0]) {
                TsValue msgKey; msgKey.type = ValueType::STRING_PTR;
                msgKey.ptr_val = TsString::GetInterned("message");
                newObj->Set(msgKey, nanbox_to_tagged(argv[0]));
            }
            return thisArg;
        }
        TsValue* result = ts_function_call_with_this(constructorFn, thisArg, argc, argv);

        // 5. If the constructor returned an object, use that instead (JS spec)
        // Per ECMAScript: if the constructor returns ANY object (not a primitive),
        // that object is the result of `new`. This includes functions, arrays, etc.
        if (result && !ts_value_is_undefined(result) && !ts_value_is_null(result)) {
            uint64_t rNb = nanbox_from_tsvalue_ptr(result);
            if (nanbox_is_ptr(rNb)) {
                void* rPtr = nanbox_to_ptr(rNb);
                if (rPtr && (uintptr_t)rPtr > 0x10000) {
                    return result;
                }
            }
        }

        // 6. Return the new object
        return thisArg;
    }

    // Select the result of `new`: if a constructor returned an OBJECT use it,
    // else (undefined/null/primitive) use the freshly-allocated `this`.
    TsValue* ts_construct_select(TsValue* ctorResult, TsValue* thisObj) {
        // ECMA-262 [[Construct]] for a DERIVED class (this helper is emitted only
        // when hirClass->baseClass is set): an Object return replaces `this`;
        // `undefined` keeps `this`; ANY other value — null, boolean, number,
        // string, symbol, bigint — is a TypeError. (The old check accepted strings
        // because they're heap pointers, and silently kept `this` for primitives.)
        if (!ctorResult || ts_value_is_undefined(ctorResult)) return thisObj;
        TsValue tv = nanbox_to_tagged(ctorResult);
        switch (tv.type) {
            case ValueType::OBJECT_PTR:
                if (tv.ptr_val) return ctorResult;  // real object (null maps here with ptr_val==null)
                break;
            case ValueType::ARRAY_PTR:
            case ValueType::PROMISE_PTR:
            case ValueType::FUNCTION_PTR:
                return ctorResult;
            default:
                break;  // STRING/SYMBOL/BIGINT/NUMBER/BOOLEAN — fall through to throw
        }
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Derived constructors may only return object or undefined"));
        return thisObj;  // unreachable
    }

    // Milestone C — dynamic/exotic base derived-class `super(...)`.
    //
    // A class `class Sub extends <runtime-value> { constructor(){ super(...a); } }`
    // whose base is a Temporal-family constructor needs a GENUINE branded base
    // instance created from the SUPER-call arguments (which may differ from the
    // `new Sub()` arguments), then bound as the derived `this`. The eager flat
    // instance the compiler pre-allocates is NOT a branded TsDuration/TsInstant/
    // …, so every branded method throws "not a Temporal.X" (subclassing
    // "ignored"). ES 15.7.14 / 10.2.2: in a derived constructor `this` is bound
    // by SuperCall to base.[[Construct]](args, newTarget).
    //
    // Uses the ambient new.target (set by the `new` site) to find the base by
    // walking new.target's [[Prototype]] chain — mirrors the same walk in
    // ts_new_from_constructor_impl's Temporal-subclass block, but keyed on the
    // SUPER args. On a Temporal match: build the branded instance, relink its
    // [[Prototype]] to newTarget.prototype (so `instanceof Sub` and the branded
    // proto chain both hold), and return it. For any NON-Temporal base returns
    // `oldThis` unchanged so ordinary/flat super semantics are preserved.
    TsValue* ts_super_dynamic_construct(TsValue* oldThis, TsValue* argsArray) {
        extern void* ts_get_new_target();
        extern void ts_native_object_set_proto(void* obj, TsValue* proto);
        extern TsValue* ts_object_getPrototypeOf(TsValue* obj);

        void* ntBoxed = ts_get_new_target();
        TsValue* newTarget = (TsValue*)ntBoxed;
        if (!newTarget || ts_value_is_undefined(newTarget)) return oldThis;
        void* rawCtor = ts_value_get_object(newTarget);
        if (!rawCtor) return oldThis;

        extern void* ts_temporal_get_plaintime_ctor();
        extern TsValue* ts_temporal_plaintime_construct(int, TsValue**);
        extern void* ts_temporal_get_duration_ctor();
        extern TsValue* ts_temporal_duration_construct(int, TsValue**);
        extern void* ts_temporal_get_plaindate_ctor();
        extern TsValue* ts_temporal_plaindate_construct(int, TsValue**);
        extern void* ts_temporal_get_plainyearmonth_ctor();
        extern TsValue* ts_temporal_plainyearmonth_construct(int, TsValue**);
        extern void* ts_temporal_get_plainmonthday_ctor();
        extern TsValue* ts_temporal_plainmonthday_construct(int, TsValue**);
        extern void* ts_temporal_get_plaindatetime_ctor();
        extern TsValue* ts_temporal_plaindatetime_construct(int, TsValue**);
        extern void* ts_temporal_get_instant_ctor();
        extern TsValue* ts_temporal_instant_construct(int, TsValue**);
        extern void* ts_temporal_get_zoneddatetime_ctor();
        extern TsValue* ts_temporal_zoneddatetime_construct(int, TsValue**);
        struct TemporalCtor { void* (*getter)(); TsValue* (*construct)(int, TsValue**); };
        const TemporalCtor kTemporalCtors[] = {
            { ts_temporal_get_plaintime_ctor,      ts_temporal_plaintime_construct },
            { ts_temporal_get_duration_ctor,       ts_temporal_duration_construct },
            { ts_temporal_get_plaindate_ctor,      ts_temporal_plaindate_construct },
            { ts_temporal_get_plainyearmonth_ctor, ts_temporal_plainyearmonth_construct },
            { ts_temporal_get_plainmonthday_ctor,  ts_temporal_plainmonthday_construct },
            { ts_temporal_get_plaindatetime_ctor,  ts_temporal_plaindatetime_construct },
            { ts_temporal_get_instant_ctor,        ts_temporal_instant_construct },
            { ts_temporal_get_zoneddatetime_ctor,  ts_temporal_zoneddatetime_construct },
        };
        TsValue* (*chosen)(int, TsValue**) = nullptr;
        // Walk newTarget's [[Prototype]] chain (Sub.__proto__ === Temporal.X for
        // `class Sub extends Temporal.X`, linked by ts_class_link_dynamic_base).
        TsValue* cur = ts_object_getPrototypeOf(newTarget);
        for (int hops = 0; hops < 64 && cur && !chosen &&
             !ts_value_is_undefined(cur) && !ts_value_is_null(cur); hops++) {
            void* curRaw = ts_value_get_object(cur);
            if (curRaw) {
                for (const auto& e : kTemporalCtors) {
                    void* g = e.getter(); if (!g) continue;
                    void* gRaw = ts_value_get_object((TsValue*)g);
                    if (!gRaw) gRaw = g;
                    if (curRaw == g || curRaw == gRaw) { chosen = e.construct; break; }
                }
            }
            cur = ts_object_getPrototypeOf(cur);
        }
        const char* builtinName = nullptr;
        if (!chosen) {
            // Non-Temporal base: try the brandable-builtin table (Array/Date/
            // DataView/ArrayBuffer/Number/Boolean/String). ES 13.3.7.1
            // SuperCall binds `this` to base.[[Construct]](superArgs,
            // newTarget), which for these bases must yield an instance
            // carrying the base's internal slots (the eager flat instance the
            // compiler pre-allocates is unbranded, so inherited branded
            // methods would throw / read empty state).
            builtinName = ts_builtin_subclass_base_name(newTarget, nullptr);
            if (!builtinName) return oldThis;  // ordinary base: keep `this`
        }

        // Extract the SUPER-call arguments (packed by the compiler, iterator
        // protocol already applied for `super(...iterable)`).
        extern void* ts_value_get_element(void* param, int64_t index);
        int64_t argc = argsArray ? ts_value_length(argsArray) : 0;
        if (argc < 0) argc = 0;
        if (argc > 1024) argc = 1024;
        std::vector<TsValue*> argv((size_t)argc, nullptr);
        for (int64_t i = 0; i < argc; ++i)
            argv[(size_t)i] = (TsValue*)ts_value_get_element(argsArray, i);
        TsValue* inst = chosen
            ? chosen((int)argc, argc ? argv.data() : nullptr)  // may throw RangeError
            : ts_construct_builtin_branded(builtinName, (int)argc,
                                           argc ? argv.data() : nullptr);
        void* instRaw = inst ? ts_value_get_object(inst) : nullptr;
        if (!inst || !instRaw) return oldThis;
        // Relink instance -> newTarget.prototype (the subclass prototype whose
        // own [[Prototype]] is Base.prototype, so branded methods stay
        // reachable). Wrapper-aware: primitive-wrapper TsMaps get a REAL
        // prototype link, exotic natives the side-map link.
        TsValue* protoVal = ts_object_get_dynamic(newTarget,
            ts_value_make_string(TsString::Create("prototype")));
        if (protoVal && !ts_value_is_undefined(protoVal) && !ts_value_is_null(protoVal))
            ts_subclass_relink_proto(inst, instRaw, protoVal);
        return inst;
    }

    TsValue* ts_new_from_constructor_0(TsValue* constructorFn) {
        return ts_new_from_constructor(constructorFn, 0, nullptr);
    }

    TsValue* ts_new_from_constructor_1(TsValue* constructorFn, TsValue* arg1) {
        TsValue* argv[] = { arg1 };
        return ts_new_from_constructor(constructorFn, 1, argv);
    }

    TsValue* ts_new_from_constructor_2(TsValue* constructorFn, TsValue* arg1, TsValue* arg2) {
        TsValue* argv[] = { arg1, arg2 };
        return ts_new_from_constructor(constructorFn, 2, argv);
    }

    TsValue* ts_new_from_constructor_3(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3) {
        TsValue* argv[] = { arg1, arg2, arg3 };
        return ts_new_from_constructor(constructorFn, 3, argv);
    }

    TsValue* ts_new_from_constructor_4(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4 };
        return ts_new_from_constructor(constructorFn, 4, argv);
    }

    TsValue* ts_new_from_constructor_5(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5 };
        return ts_new_from_constructor(constructorFn, 5, argv);
    }

    TsValue* ts_new_from_constructor_6(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6 };
        return ts_new_from_constructor(constructorFn, 6, argv);
    }

    TsValue* ts_new_from_constructor_7(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
        return ts_new_from_constructor(constructorFn, 7, argv);
    }

    TsValue* ts_new_from_constructor_8(TsValue* constructorFn, TsValue* arg1, TsValue* arg2, TsValue* arg3, TsValue* arg4, TsValue* arg5, TsValue* arg6, TsValue* arg7, TsValue* arg8) {
        TsValue* argv[] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8 };
        return ts_new_from_constructor(constructorFn, 8, argv);
    }

    // Unified variable-arity entry — the compiler emits this (ConstructFromValue)
    // instead of the per-arity ts_new_from_constructor_N forwarders, so `new
    // F(...)` no longer caps/drops arguments past 8.
    TsValue* ts_new_from_constructor(TsValue* constructorFn, int argc, TsValue** argv) {
        // ES NewTarget: the ambient register holds the construct target for
        // the duration of the constructor body (swap/restore for nesting).
        extern void* ts_set_new_target(void* v);
        void* prevNT = ts_set_new_target((void*)constructorFn);
        TsValue* r = ts_new_from_constructor_impl(constructorFn, argc, argv);
        ts_set_new_target(prevNT);
        return r;
    }

    // Reflect.construct(target, args, newTarget): same construct path but the
    // ambient new.target register carries the EXPLICIT newTarget.
    TsValue* ts_new_from_constructor_with_target(TsValue* constructorFn,
                                                 TsValue* newTarget,
                                                 int argc, TsValue** argv) {
        extern void* ts_set_new_target(void* v);
        void* prevNT = ts_set_new_target((void*)(newTarget ? newTarget : constructorFn));
        TsValue* r = ts_new_from_constructor_impl(constructorFn, argc, argv);
        ts_set_new_target(prevNT);
        return r;
    }

    // Construct `new constructorFn(...argsArray)` — the spread-in-new analogue of
    // ts_function_apply. The packed argument array is built by the compiler
    // (iterator protocol) for `new C(...args, x)`.
    TsValue* ts_construct_apply(TsValue* constructorFn, TsValue* argsArray) {
        int64_t argc = ts_value_length(argsArray);
        if (argc < 0) argc = 0;
        if (argc > 1024) argc = 1024;
        std::vector<TsValue*> argv(static_cast<size_t>(argc), nullptr);
        for (int64_t i = 0; i < argc; ++i)
            argv[static_cast<size_t>(i)] = (TsValue*)ts_value_get_element(argsArray, i);
        return ts_new_from_constructor_impl(constructorFn, static_cast<int>(argc), argv.data());
    }

    TsValue* ts_function_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int argc, TsValue** argv) {
        ts_last_call_argc = argc;
        // Save/set/restore the global 'this' context so that functions compiled
        // with ts_get_call_this() (function declarations and function expressions
        // that reference 'this') can find the thisArg. This is essential for:
        // - Constructor functions called via 'new' (ts_new_from_constructor)
        // - Function.prototype.call/apply
        // - Prototype method calls on dynamically-typed objects
        void* savedThis = ts_call_this_value;
        ts_call_this_value = thisArg;

        // Handle closures - delegate to ts_function_call which dispatches
        // through ts_call_N (0..10), each of which respects the closure's
        // rest_param_index and routes through ts_rest_pack_and_call when
        // appropriate. The thisArg is propagated via ts_call_this_value
        // (set above), so the callee can still read `this` if it uses
        // ts_get_call_this(). The previous inline switch capped at 3 and
        // its `default` branch dropped extra args — broke `apply` and
        // spread-into-rest call sites.
        // Proxy callee: route through [[Call]] (ES 10.5.12) so the apply trap
        // fires — or the call forwards to the target with the RIGHT thisArg.
        // Without this branch, fn.call(thisObj)/Reflect.apply on a proxy fell
        // through to the generic paths and lost the receiver.
        {
            TsProxy* px = ts_extract_proxy(boxedFunc);
            if (px) {
                TsArray* argArr = TsArray::Create((size_t)(argc > 0 ? argc : 1));
                for (int i = 0; i < argc; ++i)
                    ts_array_push(argArr, argv[i] ? argv[i] : ts_value_make_undefined());
                TsValue* r = px->apply(thisArg, ts_value_make_object(argArr), argc);
                ts_call_this_value = savedThis;
                return r ? r : ts_value_make_undefined();
            }
        }
        TsClosure* closure = ts_extract_closure(boxedFunc);
        if (closure) {
            TsValue* result;
            // An is_method closure has this-first physical params (closure, this, a1..aN);
            // ts_function_call uses Convention A (closure, a1..aN) and would let the `this`
            // slot swallow the first real argument. ts_call_with_this_0..4 honor is_method,
            // so route fixed-arity method closures through them. A rest param still needs
            // the packing ts_function_call provides, so only take this path with no rest.
            if (closure->rest_param_index >= 0 && closure->is_method) {
                // Rest-param METHODS need the this-first packer: their physical
                // params are (closure, this, a1..aN), so the rest array must be
                // placed in a user-arg slot AFTER the receiver. The Convention-A
                // packer (ts_rest_pack_and_call) would drop the array into the
                // `this` slot. Invoke directly with thisArg so the receiver is
                // preserved (routing through the receiver-less dispatcher would
                // hit OrdinaryCallBindThis and clobber it with undefined).
                result = ts_rest_pack_and_call_method(closure, thisArg, argc, argv);
            } else {
                // Honor is_method (Convention B: this-first) for ALL arities;
                // call_dispatch_with_this also covers plain closures and
                // non-method rest packing. It must be the ONLY route here:
                // the old else-branch delegated through the RECEIVER-LESS
                // dispatcher, whose OrdinaryCallBindThis scope replaced the
                // explicitly-bound this with undefined (broke `new`,
                // .call/.apply on plain function closures).
                result = call_dispatch_with_this(boxedFunc, thisArg, argc, argv);
            }
            ts_call_this_value = savedThis;
            return result;
        }

        TsFunction* func = ts_extract_function(boxedFunc);
        if (!func) {
            ts_call_this_value = savedThis;
            return ts_value_make_undefined();
        }

        // Check if funcPtr wraps a closure (via ts_value_make_function wrapping a TsClosure*)
        // In this case, call the inner closure's function with thisArg directly
        TsClosure* innerClosure = ts_funcptr_as_closure(func->funcPtr);
        if (innerClosure) {
            // A rest-param closure needs argument PACKING — the raw arity
            // switch below passes thisArg in the closure slot and drops the
            // rest array entirely (`Promise.resolve = (...args) => ...`
            // observed args === undefined). ts_function_call routes through
            // ts_call_N which honors rest_param_index; thisArg propagates
            // via ts_call_this_value (set above).
            if (innerClosure->rest_param_index >= 0) {
                TsValue* result = ts_function_call(boxedFunc, argc, argv);
                ts_call_this_value = savedThis;
                return result;
            }
            TsValue* result;
            switch (argc) {
                case 0: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg);
                    break;
                }
                case 1: {
                    typedef TsValue* (*Fn)(void*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0]);
                    break;
                }
                case 2: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0], argv[1]);
                    break;
                }
                case 3: {
                    typedef TsValue* (*Fn)(void*, TsValue*, TsValue*, TsValue*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg, argv[0], argv[1], argv[2]);
                    break;
                }
                default: {
                    typedef TsValue* (*Fn)(void*);
                    result = ((Fn)innerClosure->func_ptr)(thisArg);
                    break;
                }
            }
            ts_call_this_value = savedThis;
            return result;
        }

        // Preserve the captured context and only override when the function has none.
        // .call() overrides the receiver for plain TsFunctions, but NOT
        // for bound-method trampolines whose context IS a BoundMethodCtx*
        // (overriding clobbers the methodPtr → indirect call to data crash).
        void* savedCtx = maybe_override_context(func, thisArg);

        TsValue* result = ts_function_call(boxedFunc, argc, argv);

        func->context = savedCtx;
        ts_call_this_value = savedThis;
        return result;
    }

    TsValue* ts_function_apply(TsValue* boxedFunc, TsValue* thisArg, TsValue* argsArray) {
        int64_t argc = ts_value_length(argsArray);
        if (argc < 0) argc = 0;

        // Pass the FULL argument list. NATIVE callees receive (ctx, argc, argv)
        // verbatim — the old 16-arg cap silently corrupted e.g.
        // String.fromCodePoint.apply(null, [10k points]) (test262 buildString).
        // Closure paths bound their own reads (padded10 / exact / rest-pack),
        // so oversize argc is safe there too.
        std::vector<TsValue*> argv(static_cast<size_t>(argc), ts_value_make_undefined());
        for (int64_t i = 0; i < argc; ++i) {
            argv[static_cast<size_t>(i)] = (TsValue*)ts_value_get_element(argsArray, i);
        }
        return ts_function_call_with_this(boxedFunc, thisArg, static_cast<int>(argc), argv.data());
    }

}  // extern "C"
