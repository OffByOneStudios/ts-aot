#include "TsProxy.h"
#include "TsMap.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsRuntime.h"
#include "TsFlatObject.h"
#include "TsNanBox.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

// TsProxy implementation

extern "C" void ts_throw(TsValue* err);
extern "C" TsValue* ts_new_from_constructor_impl(TsValue* ctor, int argc, TsValue** argv);
extern "C" TsValue* ts_new_from_constructor_with_target(TsValue* ctor, TsValue* newTarget, int argc, TsValue** argv);
extern "C" void* ts_error_create_typed(const char* type, const char* message);

// ECMA-262 10.5.x: every Proxy internal method whose [[ProxyHandler]] is null
// (i.e. the proxy has been revoked) must throw a TypeError. Previously the
// revoked traps returned a benign value (undefined/false/empty), so the ~163
// built-ins/*/proxy-revoked tests that revoke a proxy and expect a TypeError
// silently "succeeded".
static void throw_revoked(const char* op) {
    char msg[96];
    snprintf(msg, sizeof(msg),
             "Cannot perform '%s' on a proxy that has been revoked", op);
    ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
}

TsProxy::TsProxy(void* t, TsMap* h) : target(t), handler(h), revoked(false) {
    proxyMagic = PROXY_MAGIC;
}

// Captured once on first proxy creation: the TsProxy vtable pointer (at object
// offset 0). Lets hot paths detect a proxy with a single pointer compare instead
// of dynamic_cast. Reading offset 0 of any polymorphic heap object is safe.
extern "C" { void* g_ts_proxy_vtable = nullptr; }
TsProxy* TsProxy::Create(void* target, TsMap* handler) {
    void* mem = ts_alloc(sizeof(TsProxy));
    TsProxy* p = new (mem) TsProxy(target, handler);
    if (!g_ts_proxy_vtable) g_ts_proxy_vtable = *(void**)p;
    return p;
}

// Public guard for builtins (Object.keys/getPrototypeOf/defineProperty/...) that
// operate on an object directly rather than through the proxy's internal methods:
// if the receiver is a REVOKED proxy they must throw a TypeError (ECMA-262 10.5.x)
// instead of silently reading the dead proxy as a plain map. No-op for anything
// else. Detects the proxy via the captured vtable pointer (offset 0) — safe on any
// heap object and avoids dynamic_cast UB on non-polymorphic types.
extern "C" void ts_proxy_throw_if_revoked(void* boxed) {
    void* raw = ts_value_get_object((TsValue*)boxed);
    if (!raw) raw = boxed;
    if (!raw || (uintptr_t)raw <= 0x1000) return;
    if (!g_ts_proxy_vtable || *(void**)raw != g_ts_proxy_vtable) return;
    if (((TsProxy*)raw)->revoked) throw_revoked("get");
}

TsValue* TsProxy::getTrap(const char* trapName) {
    if (revoked || !handler) return nullptr;

    // Use the generic property read so the handler resolves whether it is a flat
    // object (shorthand-method traps like `{ get(){} }` live in the shape) or a
    // TsMap. The trap value may be a TsFunction OR a TsClosure (object-literal
    // shorthand methods compile to closures), so accept any callable and return
    // the boxed value for tsCall to dispatch.
    extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
    extern bool ts_is_callable(void* val);
    TsValue* boxed = ts_object_get_property((void*)handler, trapName);
    if (!boxed) return nullptr;
    uint64_t nb = nanbox_from_tsvalue_ptr(boxed);
    if (nanbox_is_undefined(nb) || nanbox_is_null(nb)) return nullptr;
    if (!ts_is_callable((void*)boxed)) {
        // ES GetMethod: a present, non-callable trap is a TypeError.
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "proxy handler trap is not a function"));
    }
    return boxed;
}

TsValue* TsProxy::get(TsValue* prop, void* receiver) {
    if (revoked) {
        throw_revoked("get");
        return ts_value_make_undefined();
    }

    TsValue* trap = getTrap("get");
    if (trap) {
        // ECMAScript [[Get]]: Call(trap, handler, «target, P, Receiver»). The trap
        // must run with `this` = the handler — call it through the this-passing entry
        // so an is_method trap (one that captures an outer variable compiles to a
        // this-first closure) receives target/P/Receiver in the right slots instead of
        // having them shifted by one.
        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* receiverVal = receiver ? ts_value_box_any(receiver) : ts_value_box_any(this);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[3] = { targetVal, prop, receiverVal };
        return ts_function_call_with_this(trap, handlerVal, 3, argv);
    }

    // No trap - forward to target
    if (!target) return ts_value_make_undefined();

    // Get property from target
    return ts_object_get_dynamic(ts_value_box_any(target), prop);
}

bool TsProxy::set(TsValue* prop, TsValue* value, void* receiver) {
    if (revoked) {
        throw_revoked("set");
        return false;
    }

    TsValue* trap = getTrap("set");
    if (trap) {
        // Call trap(target, prop, value, receiver)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* receiverVal = receiver ? ts_value_box_any(receiver) : ts_value_box_any(this);
        // ES 10.5.9: Call(trap, HANDLER, «target, P, V, Receiver») — the trap
        // runs with this = the handler (call-parameters family).
        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[4] = { targetVal, prop, value, receiverVal };
        TsValue* result = ts_function_call_with_this(trap, handlerVal, 4, argv);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    ts_object_set_dynamic(ts_value_box_any(target), prop, value);
    return true;
}

bool TsProxy::has(TsValue* prop) {
    if (revoked) {
        throw_revoked("has");
        return false;
    }

    TsValue* trap = getTrap("has");
    if (trap) {
        // Call trap(target, prop)
        TsValue* targetVal = ts_value_box_any(target);


        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[2] = { targetVal, prop };
        TsValue* result = ts_function_call_with_this(trap, handlerVal, 2, argv);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    return ts_object_has_prop(ts_value_box_any(target), prop);
}

bool TsProxy::deleteProperty(TsValue* prop) {
    if (revoked) {
        throw_revoked("deleteProperty");
        return false;
    }

    TsValue* trap = getTrap("deleteProperty");
    if (trap) {
        // Call trap(target, prop)
        TsValue* targetVal = ts_value_box_any(target);


        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[2] = { targetVal, prop };
        TsValue* result = ts_function_call_with_this(trap, handlerVal, 2, argv);
        return result && ts_value_get_bool(result);
    }

    // No trap - forward to target
    if (!target) return false;

    return ts_object_delete_prop(ts_value_box_any(target), prop);
}

TsValue* TsProxy::apply(void* thisArg, TsValue* args, int argCount) {
    if (revoked) {
        throw_revoked("apply");
        return ts_value_make_undefined();
    }

    TsValue* trap = getTrap("apply");
    if (trap) {
        // Call trap(target, thisArg, argumentsList)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* thisArgVal = thisArg ? ts_value_box_any(thisArg) : ts_value_make_undefined();
        TsValue* argsVal = ts_value_box_any(args);


        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[3] = { targetVal, thisArgVal, argsVal };
        return ts_function_call_with_this(trap, handlerVal, 3, argv);
    }

    // No trap - forward call to target
    if (!target) return ts_value_make_undefined();

    TsValue* targetVal = ts_value_box_any(target);
    TsValue* thisVal = thisArg ? ts_value_box_any(thisArg) : ts_value_make_undefined();
    TsValue* argsArray = ts_value_box_any(args);

    return ts_function_apply(targetVal, thisVal, argsArray);
}

TsValue* TsProxy::construct(TsValue* args, int argCount, void* newTarget) {
    if (revoked) {
        throw_revoked("construct");
        return ts_value_make_undefined();
    }

    TsValue* trap = getTrap("construct");
    if (trap) {
        // Call trap(target, argumentsList, newTarget)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* argsVal = ts_value_box_any(args);
        TsValue* newTargetVal = newTarget ? ts_value_box_any(newTarget) : targetVal;


        extern TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
        TsValue* handlerVal = handler ? ts_value_box_any(handler) : ts_value_make_undefined();
        TsValue* argv[3] = { targetVal, argsVal, newTargetVal };
        TsValue* r = ts_function_call_with_this(trap, handlerVal, 3, argv);
        // ES 10.5.13 step 10: a non-Object trap result is a TypeError
        // (construct/return-not-object-throws-* family). Strings are heap
        // pointers but NOT Objects.
        {
            uint64_t rnb = r ? (uint64_t)(uintptr_t)r : 0;
            void* rraw = (r && nanbox_is_ptr(rnb)) ? ts_value_get_object(r) : nullptr;
            bool isObj = rraw && (uintptr_t)rraw >= 4096 &&
                         *(uint32_t*)rraw != 0x53545247 /* not a bare STRG */;
            if (!isObj) {
                extern void ts_throw(TsValue* err);
                extern void* ts_error_create_typed(const char* type, const char* msg);
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "proxy [[Construct]] trap must return an object"));
            }
        }
        return r;
    }

    // No trap — ES 10.5.13 step 5: return ? Construct(target, args, newTarget).
    // newTarget defaults to the PROXY itself (new proxyOfCtor(...) runs the
    // target ctor with new.target === proxy).
    if (!target) return ts_value_make_undefined();
    {
        void* argsRaw = args ? ts_value_get_object(args) : nullptr;
        TsArray* argArr = (argsRaw && *(uint32_t*)argsRaw == 0x41525259 /*ARRY*/)
                              ? (TsArray*)argsRaw : nullptr;
        int n = argArr ? (int)argArr->Length() : 0;
        // argv holds GC pointers, but each element stays rooted through
        // argArr (GC-visible array) for the duration of the call.
        std::vector<TsValue*> argv((size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; ++i) argv[(size_t)i] = (TsValue*)argArr->GetElementBoxed((size_t)i);
        TsValue* nt = newTarget ? (TsValue*)newTarget : ts_value_box_any(this);
        return ts_new_from_constructor_with_target(ts_value_box_any(target), nt, n, argv.data());
    }
}

extern "C" int64_t ts_reflect_isExtensible(void* targetArg);
extern "C" TsValue* ts_object_getOwnPropertyNames(TsValue* obj);
extern "C" TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop);
static TsValue* proxy_call_trap(TsProxy* px, TsValue* trap, int argc, TsValue** argv);

TsValue* TsProxy::ownKeys() {
    if (revoked) {
        throw_revoked("ownKeys");
        return ts_value_make_array(TsArray::Create());
    }

    TsValue* trap = getTrap("ownKeys");
    if (trap) {
        // Call trap(target)
        TsValue* targetVal = ts_value_box_any(target);
        TsValue* trapArgs[1] = { targetVal };
        TsValue* r = proxy_call_trap(this, trap, 1, trapArgs);
        // ES 10.5.11 invariants over the trap result:
        //  - result keys must be unique (step 9: List of String/Symbol with
        //    no duplicates -> TypeError)
        //  - every non-configurable own key of the target must appear
        //  - a NON-EXTENSIBLE target's key set must match exactly (no new
        //    keys, none missing)
        {
            void* rRaw = r ? ts_value_get_object(r) : nullptr;
            TsArray* keys = (rRaw && *(uint32_t*)rRaw == 0x41525259 /*ARRY*/)
                ? (TsArray*)rRaw : nullptr;
            if (!rRaw) {
                // CreateListFromArrayLike: non-object trap result -> TypeError
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "'ownKeys' on proxy: trap returned neither array nor array-like object"));
            }
            if (keys) {
                int64_t n = keys->Length();
                // Element types: every entry must be a String or a Symbol.
                for (int64_t i = 0; i < n; i++) {
                    TsValue* ki = (TsValue*)keys->Get(i);
                    uint64_t nki = ki ? nanbox_from_tsvalue_ptr(ki) : 0;
                    void* kp = (ki && nanbox_is_ptr(nki)) ? nanbox_to_ptr(nki) : nullptr;
                    uint32_t km = kp ? *(uint32_t*)kp : 0;
                    if (km != 0x53545247 /*STRG*/ && km != 0x53594D42 /*SYMB*/) {
                        ts_throw((TsValue*)ts_error_create_typed("TypeError",
                            "'ownKeys' on proxy: trap result element is neither a string nor a symbol"));
                    }
                }
                // Duplicate check (string compare via ts_value_strict_eq
                // would be ideal; string keys interned so pointer-compare of
                // ToString is unreliable — use utf8 compare for strings,
                // pointer identity for symbols).
                for (int64_t i = 0; i < n; i++) {
                    TsValue* ki = (TsValue*)keys->Get(i);
                    for (int64_t j = i + 1; j < n; j++) {
                        TsValue* kj = (TsValue*)keys->Get(j);
                        bool dup = false;
                        if (ki == kj) dup = true;
                        else if (ki && kj) {
                            uint64_t ni = nanbox_from_tsvalue_ptr(ki);
                            uint64_t nj = nanbox_from_tsvalue_ptr(kj);
                            if (nanbox_is_ptr(ni) && nanbox_is_ptr(nj)) {
                                void* pi = nanbox_to_ptr(ni);
                                void* pj = nanbox_to_ptr(nj);
                                if (pi && pj &&
                                    *(uint32_t*)pi == 0x53545247 /*STRG*/ &&
                                    *(uint32_t*)pj == 0x53545247) {
                                    const char* ci = ((TsString*)pi)->ToUtf8();
                                    const char* cj = ((TsString*)pj)->ToUtf8();
                                    if (ci && cj && strcmp(ci, cj) == 0) dup = true;
                                } else if (pi == pj) dup = true;
                            }
                        }
                        if (dup) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "'ownKeys' on proxy: trap returned duplicate entries"));
                        }
                    }
                }
                // Extensibility invariants.
                bool ext = target &&
                    ts_reflect_isExtensible(ts_value_box_any(target)) != 0;
                auto keyInResult = [&](TsValue* tkv) -> bool {
                    for (int64_t i = 0; i < n; i++) {
                        TsValue* kv = (TsValue*)keys->Get(i);
                        if (tkv == kv) return true;
                        if (!tkv || !kv) continue;
                        uint64_t na = nanbox_from_tsvalue_ptr(tkv);
                        uint64_t nb2 = nanbox_from_tsvalue_ptr(kv);
                        if (!nanbox_is_ptr(na) || !nanbox_is_ptr(nb2)) continue;
                        void* pa = nanbox_to_ptr(na);
                        void* pb = nanbox_to_ptr(nb2);
                        if (!pa || !pb) continue;
                        if (*(uint32_t*)pa == 0x53545247 && *(uint32_t*)pb == 0x53545247) {
                            const char* ca = ((TsString*)pa)->ToUtf8();
                            const char* cb = ((TsString*)pb)->ToUtf8();
                            if (ca && cb && strcmp(ca, cb) == 0) return true;
                        } else if (pa == pb) return true;
                    }
                    return false;
                };
                // ES 10.5.11 step 17: every NON-CONFIGURABLE own key of the
                // target must appear in the trap result (even when the
                // target is extensible).
                if (target) {
                    TsValue* tka = ts_object_getOwnPropertyNames(ts_value_box_any(target));
                    void* tkaRaw = tka ? ts_value_get_object(tka) : nullptr;
                    TsArray* tkArr = (tkaRaw && *(uint32_t*)tkaRaw == 0x41525259)
                        ? (TsArray*)tkaRaw : nullptr;
                    int64_t tkn = tkArr ? tkArr->Length() : 0;
                    for (int64_t ti = 0; ti < tkn; ti++) {
                        TsValue* tkv = (TsValue*)tkArr->Get(ti);
                        if (keyInResult(tkv)) continue;
                        TsValue* d = ts_object_getOwnPropertyDescriptor(
                            ts_value_box_any(target), tkv);
                        if (!d || ts_value_is_undefined(d)) continue;
                        void* dRaw3 = ts_value_get_object(d);
                        TsValue* cfg = dRaw3
                            ? ts_object_get_property(dRaw3, "configurable") : nullptr;
                        if (!cfg || !ts_value_to_bool(cfg)) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "'ownKeys' on proxy: trap result did not include all non-configurable keys of the proxy target"));
                        }
                    }
                }
                if (!ext && target) {
                    // Target's own keys must ALL appear, and no extras.
                    // ALL own keys (incl. non-enumerable) — Object.keys'
                    // enumerable filter false-fired the extra-keys check.
                    TsValue* tk = ts_object_getOwnPropertyNames(ts_value_box_any(target));
                    void* tkRaw = tk ? ts_value_get_object(tk) : nullptr;
                    TsArray* tKeys = (tkRaw && *(uint32_t*)tkRaw == 0x41525259)
                        ? (TsArray*)tkRaw : nullptr;
                    int64_t tn = tKeys ? tKeys->Length() : 0;
                    auto keyEq = [](TsValue* a, TsValue* b) -> bool {
                        if (a == b) return true;
                        if (!a || !b) return false;
                        uint64_t na = nanbox_from_tsvalue_ptr(a);
                        uint64_t nb2 = nanbox_from_tsvalue_ptr(b);
                        if (!nanbox_is_ptr(na) || !nanbox_is_ptr(nb2)) return false;
                        void* pa = nanbox_to_ptr(na);
                        void* pb = nanbox_to_ptr(nb2);
                        if (!pa || !pb) return false;
                        if (*(uint32_t*)pa == 0x53545247 && *(uint32_t*)pb == 0x53545247) {
                            const char* ca = ((TsString*)pa)->ToUtf8();
                            const char* cb = ((TsString*)pb)->ToUtf8();
                            return ca && cb && strcmp(ca, cb) == 0;
                        }
                        return pa == pb;
                    };
                    for (int64_t ti = 0; ti < tn; ti++) {
                        TsValue* tkv = (TsValue*)tKeys->Get(ti);
                        bool found = false;
                        for (int64_t i = 0; i < n && !found; i++)
                            if (keyEq(tkv, (TsValue*)keys->Get(i))) found = true;
                        if (!found) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "'ownKeys' on proxy: trap result did not include all keys of the non-extensible proxy target"));
                        }
                    }
                    for (int64_t i = 0; i < n; i++) {
                        TsValue* kv = (TsValue*)keys->Get(i);
                        bool found = false;
                        for (int64_t ti = 0; ti < tn && !found; ti++)
                            if (keyEq(kv, (TsValue*)tKeys->Get(ti))) found = true;
                        if (!found) {
                            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                                "'ownKeys' on proxy: trap returned extra keys but proxy target is non-extensible"));
                        }
                    }
                }
            }
        }
        return r;
    }

    // No trap - return Object.keys(target)
    if (!target) {
        return ts_value_make_array(TsArray::Create());
    }

    // Get keys from target using ts_object_keys
    return ts_object_keys(ts_value_box_any(target));
}

// ---- ES 10.5.1-10.5.6: remaining internal-method traps (stage A5) ----
extern "C" TsValue* ts_object_getPrototypeOf(TsValue* obj);
extern "C" TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
extern "C" int64_t ts_reflect_isExtensible(void* targetArg);
extern "C" int64_t ts_reflect_preventExtensions(void* targetArg);
extern "C" int64_t ts_reflect_defineProperty(void* t, void* p, void* d);
extern "C" TsValue* ts_reflect_getOwnPropertyDescriptor(void* t, void* p);
extern "C" TsValue* ts_function_call_with_this(TsValue* fn, TsValue* thisArg, int argc, TsValue** argv);
extern "C" TsValue* ts_object_get_property2(void* o, const char* k);

static TsValue* proxy_call_trap(TsProxy* px, TsValue* trap, int argc, TsValue** argv) {
    TsValue* handlerVal = px->handler ? ts_value_box_any(px->handler) : ts_value_make_undefined();
    return ts_function_call_with_this(trap, handlerVal, argc, argv);
}

TsValue* TsProxy::getPrototypeOfTrap() {
    if (revoked) { throw_revoked("getPrototypeOf"); return ts_value_make_undefined(); }
    TsValue* trap = getTrap("getPrototypeOf");
    if (trap) {
        TsValue* argv[1] = { ts_value_box_any(target) };
        TsValue* r = proxy_call_trap(this, trap, 1, argv);
        // ES 10.5.1 step 8: trap result must be Object or null.
        uint64_t nb = r ? nanbox_from_tsvalue_ptr(r) : 0;
        bool isNull = r && nanbox_is_null(nb);
        bool isObj = r && nanbox_is_ptr(nb) && nanbox_to_ptr(nb);
        if (!isNull && !isObj) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "'getPrototypeOf' on proxy: trap returned neither object nor null"));
        }
        return r;
    }
    if (!target) return ts_value_make_null();
    return ts_object_getPrototypeOf(ts_value_box_any(target));
}

bool TsProxy::setPrototypeOfTrap(TsValue* proto) {
    if (revoked) { throw_revoked("setPrototypeOf"); return false; }
    TsValue* trap = getTrap("setPrototypeOf");
    if (trap) {
        TsValue* argv[2] = { ts_value_box_any(target),
                             proto ? proto : ts_value_make_null() };
        TsValue* r = proxy_call_trap(this, trap, 2, argv);
        return r && ts_value_get_bool(r);
    }
    if (!target) return false;
    ts_object_setPrototypeOf(ts_value_box_any(target), proto);
    return true;
}

bool TsProxy::isExtensibleTrap() {
    if (revoked) { throw_revoked("isExtensible"); return false; }
    TsValue* trap = getTrap("isExtensible");
    if (trap) {
        TsValue* argv[1] = { ts_value_box_any(target) };
        TsValue* r = proxy_call_trap(this, trap, 1, argv);
        bool booleanTrapResult = r && ts_value_get_bool(r);
        // ES 10.5.3 step 8: must match IsExtensible(target).
        bool targetResult = target &&
            ts_reflect_isExtensible(ts_value_box_any(target)) != 0;
        if (booleanTrapResult != targetResult) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "'isExtensible' on proxy: trap result does not reflect extensibility of proxy target"));
        }
        return booleanTrapResult;
    }
    return target && ts_reflect_isExtensible(ts_value_box_any(target)) != 0;
}

bool TsProxy::preventExtensionsTrap() {
    if (revoked) { throw_revoked("preventExtensions"); return false; }
    TsValue* trap = getTrap("preventExtensions");
    if (trap) {
        TsValue* argv[1] = { ts_value_box_any(target) };
        TsValue* r = proxy_call_trap(this, trap, 1, argv);
        bool booleanTrapResult = r && ts_value_get_bool(r);
        // ES 10.5.4 step 7: if trap returned true, target must be non-extensible.
        if (booleanTrapResult && target) {
                if (ts_reflect_isExtensible(ts_value_box_any(target)) != 0) {
                ts_throw((TsValue*)ts_error_create_typed("TypeError",
                    "'preventExtensions' on proxy: trap returned truthy but the proxy target is extensible"));
            }
        }
        return booleanTrapResult;
    }
    if (!target) return false;
    return ts_reflect_preventExtensions(ts_value_box_any(target)) != 0;
}

bool TsProxy::definePropertyTrap(TsValue* prop, TsValue* descriptor) {
    if (revoked) { throw_revoked("defineProperty"); return false; }
    TsValue* trap = getTrap("defineProperty");
    if (trap) {
        TsValue* argv[3] = { ts_value_box_any(target), prop, descriptor };
        TsValue* r = proxy_call_trap(this, trap, 3, argv);
        return r && ts_value_get_bool(r);
    }
    if (!target) return false;
    return ts_reflect_defineProperty(ts_value_box_any(target), (void*)prop, (void*)descriptor) != 0;
}

TsValue* TsProxy::getOwnPropertyDescriptorTrap(TsValue* prop) {
    if (revoked) { throw_revoked("getOwnPropertyDescriptor"); return ts_value_make_undefined(); }
    TsValue* trap = getTrap("getOwnPropertyDescriptor");
    if (trap) {
        TsValue* argv[2] = { ts_value_box_any(target), prop };
        TsValue* r = proxy_call_trap(this, trap, 2, argv);
        // ES 10.5.5 step 9: result must be Object or undefined.
        uint64_t nb = r ? nanbox_from_tsvalue_ptr(r) : 0;
        bool isUndef = !r || nanbox_is_undefined(nb);
        bool isObj = r && nanbox_is_ptr(nb) && nanbox_to_ptr(nb);
        if (!isUndef && !isObj) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "'getOwnPropertyDescriptor' on proxy: trap returned neither object nor undefined"));
        }
        return r ? r : ts_value_make_undefined();
    }
    if (!target) return ts_value_make_undefined();
    // Trap-less [[GetOwnProperty]] = target.[[GetOwnProperty]] — must report
    // real attributes (Reflect's variant hardcodes enumerable:true).
    return ts_object_getOwnPropertyDescriptor(ts_value_box_any(target), prop);
}


// C API implementations

extern "C" TsValue* ts_proxy_create(void* targetArg, void* handlerArg) {
    // ECMA-262 ProxyCreate steps 1-2: target and handler must both be Objects.
    // Without this, `new Proxy({}, null)` / `(null, {})` / `({}, 5)` built a
    // malformed proxy that later crashed (VectoredException) on first trap use.
    auto isObject = [](void* arg) -> bool {
        uint64_t nb = (uint64_t)(uintptr_t)arg;
        if (nb <= NANBOX_UNDEFINED) return false;                                   // null/undefined/bool
        if (!nanbox_is_ptr(nb) && (nb & 0xFFFF000000000000ULL) != 0) return false;   // number primitive
        return ts_nanbox_safe_unbox(arg) != nullptr;
    };
    if (!isObject(targetArg) || !isObject(handlerArg)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Cannot create proxy with a non-object as target or handler"));
        return ts_value_make_undefined();
    }
    // Unbox arguments
    void* target = ts_nanbox_safe_unbox(targetArg);

    void* handlerRaw = ts_nanbox_safe_unbox(handlerArg);

    // Keep the handler as-is (flat object or TsMap). getTrap reads it via the
    // generic ts_object_get_property, so converting to a TsMap is unnecessary and
    // the conversion previously dropped shorthand-method traps. Store the raw
    // pointer (dynamic_cast<TsMap*> can spuriously return null here).
    TsMap* handler = (TsMap*)handlerRaw;

    // Create proxy
    TsProxy* proxy = TsProxy::Create(target, handler);

    return ts_value_make_object(proxy);
}

extern "C" TsValue* ts_proxy_revocable(void* targetArg, void* handlerArg) {
    // Create proxy
    TsValue* proxyVal = ts_proxy_create(targetArg, handlerArg);
    // proxyVal is NaN-boxed — extract proxy pointer
    TsProxy* proxy = (TsProxy*)ts_value_get_object(proxyVal);

    // Create revoke function
    struct RevokeContext {
        TsProxy* proxy;
    };
    RevokeContext* ctx = (RevokeContext*)ts_alloc(sizeof(RevokeContext));
    ctx->proxy = proxy;

    void* revokeFuncAddr = (void*)(+[](void* context) -> TsValue* {
        RevokeContext* ctx = (RevokeContext*)context;
        if (ctx->proxy) {
            ctx->proxy->revoked = true;
        }
        return ts_value_make_undefined();
    });
    TsFunction* revokeFunc = new (ts_alloc(sizeof(TsFunction))) TsFunction(revokeFuncAddr, ctx, FunctionType::COMPILED);
    // The revoke trampoline reads its [[RevocableProxy]] from `context`; a
    // method-style `r.revoke()` must NOT override that with `this`.
    revokeFunc->keep_context = true;

    TsValue revokeFuncVal;
    revokeFuncVal.type = ValueType::FUNCTION_PTR;
    revokeFuncVal.ptr_val = revokeFunc;

    // Create result object { proxy, revoke }
    TsMap* result = TsMap::Create();
    // proxyVal is a NaN-boxed value (ts_value_make_object), NOT a pointer to a
    // TsValue struct — `*proxyVal` would misread it (it read back as undefined).
    // Build a struct TsValue from the already-extracted raw proxy pointer,
    // mirroring how revokeFuncVal is constructed above.
    TsValue proxyTsVal;
    proxyTsVal.type = ValueType::OBJECT_PTR;
    proxyTsVal.ptr_val = proxy;
    result->Set(TsString::Create("proxy"), proxyTsVal);
    result->Set(TsString::Create("revoke"), revokeFuncVal);

    return ts_value_make_object(result);
}

extern "C" TsValue* ts_proxy_get(void* proxyArg, void* propArg, void* receiverArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    // Flat objects have no vtable - skip dynamic_cast
    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_object_get_dynamic((TsValue*)proxyArg, (TsValue*)propArg);
    }

    // Check if it's actually a proxy
    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Not a proxy, use regular property access
        return ts_object_get_dynamic((TsValue*)proxyArg, (TsValue*)propArg);
    }

    return proxy->get((TsValue*)propArg, receiverArg);
}

extern "C" int64_t ts_proxy_set(void* proxyArg, void* propArg, void* valueArg, void* receiverArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        ts_object_set_dynamic((TsValue*)proxyArg, (TsValue*)propArg, (TsValue*)valueArg);
        return 1;
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        ts_object_set_dynamic((TsValue*)proxyArg, (TsValue*)propArg, (TsValue*)valueArg);
        return 1;
    }

    return proxy->set((TsValue*)propArg, (TsValue*)valueArg, receiverArg) ? 1 : 0;
}

extern "C" int64_t ts_proxy_has(void* proxyArg, void* propArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_object_has_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_object_has_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    return proxy->has((TsValue*)propArg) ? 1 : 0;
}

extern "C" int64_t ts_proxy_delete(void* proxyArg, void* propArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_object_delete_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_object_delete_prop((TsValue*)proxyArg, (TsValue*)propArg) ? 1 : 0;
    }

    return proxy->deleteProperty((TsValue*)propArg) ? 1 : 0;
}

extern "C" TsValue* ts_proxy_apply(void* proxyArg, void* thisArg, void* argsArg, int64_t argCount) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_function_apply((TsValue*)proxyArg, (TsValue*)thisArg, (TsValue*)argsArg);
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Not a proxy, call directly
        return ts_function_apply((TsValue*)proxyArg, (TsValue*)thisArg, (TsValue*)argsArg);
    }

    return proxy->apply(thisArg, (TsValue*)argsArg, (int)argCount);
}

extern "C" TsValue* ts_proxy_construct(void* proxyArg, void* argsArg, int64_t argCount, void* newTargetArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_value_make_undefined();
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        return ts_value_make_undefined();
    }

    return proxy->construct((TsValue*)argsArg, (int)argCount, newTargetArg);
}

extern "C" TsValue* ts_proxy_ownKeys(void* proxyArg) {
    void* rawProxy = ts_nanbox_safe_unbox(proxyArg);

    if (!rawProxy || is_flat_object(rawProxy)) {
        return ts_object_keys((TsValue*)proxyArg);
    }

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawProxy);
    if (!proxy) {
        // Return Object.keys for non-proxy
        return ts_object_keys((TsValue*)proxyArg);
    }

    return proxy->ownKeys();
}

extern "C" int64_t ts_is_proxy(void* objArg) {
    void* raw = ts_nanbox_safe_unbox(objArg);
    if (!raw) return 0;

    // Flat objects have no vtable - never a proxy
    if (is_flat_object(raw)) return 0;

    TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)raw);
    return proxy ? 1 : 0;
}
