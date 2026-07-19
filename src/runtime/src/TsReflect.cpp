#include "TsReflect.h"
#include "TsProxy.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsRuntime.h"
#include "TsFlatObject.h"
#include "TsError.h"
#include "TsClosure.h"
#include "TsObject.h"
#include "TsTyped.h"
#include "GC.h"
#include <csetjmp>
// Type tags (TsFunction/TsClosure/TsArray) are enrolled in their headers.

extern "C" {
    void ts_throw(TsValue* err);
    void* ts_error_create_typed(const char* type, const char* message);
    TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor);
    void* ts_push_exception_handler();
    void ts_pop_exception_handler();
}

// Reflect.defineProperty on an ORDINARY object must run the full
// [[DefineOwnProperty]] machinery (ToPropertyDescriptor coercions/validation,
// accessor storage, ValidateAndApply invariants) but SURFACE a boolean rather
// than throwing (ES 28.1.3). ts_object_defineProperty is the single source of
// truth for that machinery; it throws on refusal, so wrap it in an exception
// handler and translate a throw into `false`. POD-only frame (no non-POD
// locals) so the longjmp out of the throwing callee is safe on MSVC.
static int reflect_define_ordinary(void* targetArg, void* propArg, void* descArg) {
    void* handler = ts_push_exception_handler();
    jmp_buf* env = (jmp_buf*)handler;
    if (setjmp(*env) == 0) {
        ts_object_defineProperty((TsValue*)targetArg, (TsValue*)propArg,
                                 (TsValue*)descArg);
        ts_pop_exception_handler();
        return 1;
    }
    // Threw (ToPropertyDescriptor error or a ValidateAndApply refusal): the
    // throw path already popped the handler; clear the pending exception and
    // report failure to the Reflect caller.
    ts_set_exception((TsValue*)nullptr);
    return 0;
}

// Reflect provides static methods for interceptable JavaScript operations
// These methods directly access targets without going through Proxy traps

// ECMA-262: Reflect.{get,set,has,deleteProperty,ownKeys,getPrototypeOf,
// setPrototypeOf,isExtensible,preventExtensions,getOwnPropertyDescriptor,
// defineProperty} step 1 require Type(target) is Object, else a TypeError.
// A NaN-boxed primitive (number/bool/null/undefined), or a Symbol/String
// primitive, is NOT an Object. Throws on a non-object; returns the unboxed
// object pointer otherwise. (Forward-declared here, defined below.)
static void* reflect_require_object(void* targetArg, const char* msg);

extern "C" TsValue* ts_reflect_get(void* targetArg, void* propArg, void* receiverArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.get called on non-object");

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Direct property access on target (bypasses Proxy)
    return ts_object_get_dynamic(ts_value_box_any(target), (TsValue*)propArg);
}

extern "C" TsValue* ts_reflect_getOwnPropertyDescriptor(void* targetArg, void* propArg);

extern "C" int64_t ts_reflect_set(void* targetArg, void* propArg, void* valueArg, void* receiverArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.set called on non-object");

    // ES 10.4.6.9: a module namespace exotic's [[Set]] returns false for
    // every key, regardless of receiver.
    if ((uintptr_t)target >= 4096 &&
        *(uint32_t*)((char*)target + 16) == 0x4D415053 /*MAPS*/ &&
        ((TsMap*)target)->IsModuleNamespace()) {
        return 0;
    }

    // ES 10.4.5.5 [[Set]] with an explicit DIFFERENT receiver: the exotic
    // element-set (and its ToNumber coercion — "valueOf is not called") is
    // bypassed. An invalid canonical index returns true untouched; a valid
    // one ordinary-sets on the Receiver (false when it isn't an object).
    if ((uintptr_t)target >= 4096 &&
        *(uint32_t*)((char*)target + 16) == 0x54415252 /* TARR */ &&
        receiverArg) {
        void* recvRaw = ts_nanbox_safe_unbox(receiverArg);
        if (recvRaw != target) {
            extern int ts_ta_classify_index_c(void* taRaw, TsValue* prop);
            int cls = ts_ta_classify_index_c(target, (TsValue*)propArg);
            if (cls == 2) return 1;   // invalid index: true, nothing written
            bool recvIsObj = recvRaw && (uintptr_t)recvRaw >= 4096 &&
                             (uintptr_t)recvRaw < 0x0000800000000000ULL &&
                             *(uint32_t*)recvRaw != 0x53545247 /* STRG */ &&
                             *(uint32_t*)recvRaw != 0x53594D42 /* SYMB */;
            if (cls == 1) {
                if (!recvIsObj) return 0;   // CreateDataProperty on non-object
                ts_object_set_dynamic(ts_value_box_any(recvRaw),
                                      (TsValue*)propArg, (TsValue*)valueArg);
                return 1;
            }
            // cls == 0 (ordinary key): OrdinarySet lands on the receiver.
            if (recvIsObj) {
                ts_object_set_dynamic(ts_value_box_any(recvRaw),
                                      (TsValue*)propArg, (TsValue*)valueArg);
                return 1;
            }
            return 0;
        }
    }

    // ES 10.4.5.5 [[Set]] with SameValue(O, Receiver) (no explicit receiver, or
    // receiver === target): a canonical numeric index runs IntegerIndexedElementSet
    // (always "true"); an ORDINARY named key runs OrdinarySet, which returns FALSE
    // when the own property is a non-writable data property or a setter-less
    // accessor. The direct ts_object_set_dynamic path below always reported true
    // (internals/Set/key-is-{symbol,not-numeric-index,not-canonical-index}).
    if ((uintptr_t)target >= 4096 &&
        *(uint32_t*)((char*)target + 16) == 0x54415252 /* TARR */) {
        extern int ts_ta_classify_index_c(void* taRaw, TsValue* prop);
        if (ts_ta_classify_index_c(target, (TsValue*)propArg) == 0) {  // ordinary key
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            TsValue* desc = ts_reflect_getOwnPropertyDescriptor(target, propArg);
            if (desc && !ts_value_is_undefined(desc)) {
                void* dRaw = ts_value_get_object(desc);
                if (dRaw) {
                    TsValue* getF = ts_object_get_property(dRaw, "get");
                    TsValue* setF = ts_object_get_property(dRaw, "set");
                    bool isAccessor =
                        (getF && !ts_value_is_undefined(getF)) ||
                        (setF && !ts_value_is_undefined(setF));
                    if (isAccessor) {
                        if (!setF || !ts_is_callable((void*)setF)) return 0;  // no setter
                    } else {
                        TsValue* wF = ts_object_get_property(dRaw, "writable");
                        if (wF && !ts_value_to_bool(wF)) return 0;  // non-writable data
                    }
                }
            }
        }
    }

    // If receiver is null, use target
    if (!receiverArg) receiverArg = target;

    // Proxy: [[Set]] runs the set trap and the trap's boolean IS the answer
    // (ES 10.5.9) — the direct set below bypassed the trap and always
    // reported true (Reflect.set(proxy,...) === false family).
    if ((uintptr_t)target >= 4096 &&
        *(uint32_t*)((char*)target + 16) == 0x4D415053 /*MAPS*/) {
        TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)target);
        if (proxy) {
            void* recv = receiverArg ? ts_nanbox_safe_unbox(receiverArg) : target;
            return proxy->set((TsValue*)propArg, (TsValue*)valueArg,
                              recv ? recv : target) ? 1 : 0;
        }
    }

    // Direct property set on target (bypasses Proxy)
    ts_object_set_dynamic(ts_value_box_any(target), (TsValue*)propArg, (TsValue*)valueArg);
    return 1;  // Return true - simplified, real impl checks writable
}

extern "C" int64_t ts_reflect_has(void* targetArg, void* propArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.has called on non-object");

    return ts_object_has_prop(ts_value_box_any(target), (TsValue*)propArg) ? 1 : 0;
}

extern "C" int64_t ts_reflect_deleteProperty(void* targetArg, void* propArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.deleteProperty called on non-object");

    return ts_object_delete_prop(ts_value_box_any(target), (TsValue*)propArg) ? 1 : 0;
}

extern "C" TsValue* ts_reflect_apply(void* targetArg, void* thisArgArg, void* argsArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) return ts_value_make_undefined();

    TsValue* funcVal = ts_value_box_any(target);
    TsValue* thisVal = thisArgArg ? (TsValue*)thisArgArg : ts_value_make_undefined();
    TsValue* argsVal = (TsValue*)argsArg;

    return ts_function_apply(funcVal, thisVal, argsVal);
}

extern "C" TsValue* ts_reflect_construct(void* targetArg, void* argsArg, void* newTargetArg) {
    void* target = ts_nanbox_safe_unbox(targetArg);
    if (!target) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError", "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // If newTarget is provided, validate it's a constructor (function/closure).
    // If not provided, default to target (per ES spec).
    // Use ts_value_get_object to safely extract an object pointer — returns
    // null for primitives (numbers, booleans, strings, undefined, null).
    if (newTargetArg) {
        void* rawNt = ts_value_get_object((TsValue*)newTargetArg);
        if (!rawNt) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
        TsFunction* ntf = ts_cast<TsFunction>(rawNt);
        if (!ntf && !ts_is<TsClosure>(rawNt)) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
        // Per ES spec, newTarget must also have [[Construct]]. Built-in
        // prototype methods (Array.prototype.X) have is_constructor=false.
        if (ntf && !ntf->is_constructor) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Reflect.construct: newTarget is not a constructor"));
            return ts_value_make_undefined();
        }
    }

    // Proxy target: [[Construct]] exists when the proxy target has one
    // (ES 10.5.13) — route through the construct trap / target forwarding.
    // reflect_as_proxy is declared below; forward-declare its shape here.
    {
        void* rawT = ts_value_get_object((TsValue*)targetArg);
        if (!rawT) rawT = target;
        if (rawT && (uintptr_t)rawT > 0x1000 &&
            *(uint32_t*)((char*)rawT + 16) == 0x4D415053 /*MAPS*/) {
            if (TsProxy* px = dynamic_cast<TsProxy*>((TsMap*)rawT)) {
                return px->construct((TsValue*)argsArg, 0,
                                     newTargetArg ? newTargetArg : nullptr);
            }
        }
    }

    // Check if target is a callable function or closure (tag at offsetof(magic)).
    TsFunction* tf = ts_cast<TsFunction>(target);
    if (!tf && !ts_is<TsClosure>(target)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // Per ES spec, built-in prototype methods (Array.prototype.X etc.) have
    // no [[Construct]] — Reflect.construct must throw TypeError. The
    // is_constructor flag is set on TsFunction at registration time.
    if (tf && !tf->is_constructor) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Reflect.construct: target is not a constructor"));
        return ts_value_make_undefined();
    }

    // Get arguments array — TsArray is NOT a TsObject subclass,
    // so check magic at offset 0 directly instead of dynamic_cast.
    void* argsRaw = ts_nanbox_safe_unbox(argsArg);
    int64_t len = 0;
    TsArray* argsArray = nullptr;

    if (argsRaw) {
        if (TsArray* a = ts_cast<TsArray>(argsRaw)) {
            argsArray = a;
            len = a->Length();
        }
    }

    // Construct through the unified construct path — it allocates `this` from
    // the constructor's prototype, runs the real constructor body, and handles
    // user functions, BOUND functions, and builtin constructor globals (Array,
    // Map, ...). The old stub just tsCall'd the target (dropping args past the
    // first and never setting `this`), so Reflect.construct(F,[1,2]) returned
    // undefined.
    int argc = (int)(len > 0 ? (len > 1024 ? 1024 : len) : 0);
    TsValue* argv[1024];
    for (int i = 0; i < argc && argsArray; i++)
        argv[i] = (TsValue*)argsArray->Get(i);
    // ES 28.1.2 step 6: the constructor runs with [[newTarget]] = newTarget
    // (defaults to target).
    extern TsValue* ts_new_from_constructor_with_target(TsValue*, TsValue*, int, TsValue**);
    TsValue* result = ts_new_from_constructor_with_target((TsValue*)targetArg,
        (TsValue*)(newTargetArg ? newTargetArg : targetArg), argc, argc ? argv : nullptr);

    // ECMA-262 OrdinaryCreateFromConstructor: the new object's [[Prototype]]
    // comes from newTarget.prototype, not target.prototype. Re-link when a
    // distinct newTarget was supplied.
    if (newTargetArg && newTargetArg != targetArg && result &&
        !ts_value_is_undefined(result) && !ts_value_is_null(result)) {
        void* rnt = ts_value_get_object((TsValue*)newTargetArg);
        if (rnt) {
            extern TsValue* ts_object_get_property(void* obj, const char* keyStr);
            extern TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);
            TsValue* ntProto = ts_object_get_property(rnt, "prototype");
            if (ntProto && !ts_value_is_undefined(ntProto) && !ts_value_is_null(ntProto))
                ts_object_setPrototypeOf(result, ntProto);
        }
    }
    return result;
}

extern "C" TsValue* ts_object_getPrototypeOf(TsValue* obj);
extern "C" TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto);

// Proxy detection for the object-operation dispatch below: unbox and
// dynamic_cast only when the offset-16 magic says TsMap-family (see
// runtime-safety rules — dynamic_cast on magic-at-0 types is UB).
static TsProxy* reflect_as_proxy(void* arg) {
    void* raw = ts_nanbox_safe_unbox(arg);
    if (!raw) return nullptr;
    uint32_t m16 = *(uint32_t*)((char*)raw + 16);
    if (m16 != 0x4D415053 /*MAPS*/) return nullptr;
    return dynamic_cast<TsProxy*>((TsMap*)raw);
}

extern "C" TsValue* ts_reflect_getPrototypeOf(void* targetArg) {
    // ECMA-262 step 1: Type(target) must be Object.
    reflect_require_object(targetArg,
        "Reflect.getPrototypeOf called on non-object");
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->getPrototypeOfTrap();
    return ts_object_getPrototypeOf((TsValue*)targetArg);
}

// ECMA-262 10.1.2.1 OrdinarySetPrototypeOf status test (performs NO mutation).
// Returns 1 if setting O.[[Prototype]] = V would succeed — including the
// SameValue(V, current) no-op — and 0 if it must return false: O is
// non-extensible with V != current, or V's [[Prototype]] chain already contains
// O (a cycle). O must be an ordinary object; proxies/primitives are handled by
// callers. The chain is walked with the generic ts_object_getPrototypeOf so
// exotic prototypes (Array.prototype, %TypedArray%.prototype, ...) are
// traversed — a TsMap-magic-only walk broke at the first exotic link and so
// missed real cycles (e.g. Object.prototype <- Array.prototype).
extern "C" int64_t ts_ordinary_set_proto_status(void* targetArg, void* protoArg) {
    void* tRaw = ts_nanbox_safe_unbox(targetArg);
    if (!tRaw) return 1;  // not an object — no OrdinarySetPrototypeOf failure
    TsValue* cur = ts_object_getPrototypeOf((TsValue*)targetArg);
    uint64_t vNb = protoArg ? nanbox_from_tsvalue_ptr((TsValue*)protoArg) : 0;
    uint64_t cNb = cur ? nanbox_from_tsvalue_ptr(cur) : 0;
    void* vRaw = (protoArg && nanbox_is_ptr(vNb)) ? nanbox_to_ptr(vNb) : nullptr;
    void* cRaw = (cur && nanbox_is_ptr(cNb)) ? nanbox_to_ptr(cNb) : nullptr;
    bool vNull = !protoArg || nanbox_is_null(vNb) || nanbox_is_undefined(vNb);
    bool cNull = !cur || nanbox_is_null(cNb) || nanbox_is_undefined(cNb);
    if ((vNull && cNull) || (vRaw && vRaw == cRaw)) return 1;   // SameValue -> no-op success
    if (!ts_reflect_isExtensible(targetArg)) return 0;          // non-extensible -> false
    // Cycle check (ES 10.1.2.1 step 8): walk V's [[Prototype]] chain looking for
    // the target O. Per step 8.d the walk STOPS at a node whose [[GetPrototypeOf]]
    // is not the ordinary internal method — i.e. a Proxy — WITHOUT reporting a
    // cycle even if O lies beyond it (a proxy "shadows" the chain).
    void* walk = vRaw;
    for (int depth = 0; walk && depth < 1000000; depth++) {
        if (walk == tRaw) return 0;                             // 8.c: cycle
        // 8.d: non-ordinary [[GetPrototypeOf]] (Proxy) -> stop, no cycle.
        if ((uintptr_t)walk >= 0x10000 &&
            *(uint32_t*)((char*)walk + 16) == 0x4D415053 /*MAPS*/ &&
            dynamic_cast<TsProxy*>((TsMap*)walk) != nullptr) break;
        TsValue* nxt = ts_object_getPrototypeOf((TsValue*)walk);
        uint64_t nNb = nxt ? nanbox_from_tsvalue_ptr(nxt) : 0;
        if (!nxt || !nanbox_is_ptr(nNb)) break;                 // reached null / non-object
        void* nRaw = nanbox_to_ptr(nNb);
        if (!nRaw || nRaw == walk) break;                       // defensive
        walk = nRaw;
    }
    return 1;
}

extern "C" int64_t ts_reflect_setPrototypeOf(void* targetArg, void* protoArg) {
    // ECMA-262 step 1: Type(target) must be Object.
    reflect_require_object(targetArg,
        "Reflect.setPrototypeOf called on non-object");
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->setPrototypeOfTrap((TsValue*)protoArg) ? 1 : 0;
    // OrdinarySetPrototypeOf (ES 10.1.2): SameValue(V, current) -> true;
    // non-extensible target -> false; V's prototype chain containing the
    // target (cycle) -> false; else set and return true.
    if (!ts_ordinary_set_proto_status(targetArg, protoArg)) return 0;
    ts_object_setPrototypeOf((TsValue*)targetArg, (TsValue*)protoArg);
    return 1;
}

// ECMA-262: Reflect.{isExtensible,preventExtensions,getOwnPropertyDescriptor,
// defineProperty,...} step 1 require Type(target) is Object, else a TypeError.
// A NaN-boxed primitive (number/bool/null/undefined), or a Symbol/String
// primitive, is NOT an Object; dynamic_cast<TsMap*> on such a value was UB and
// crashed in _RTDynamicCast (e.g. Reflect.isExtensible(Symbol())). Throws on a
// non-object; returns the unboxed object pointer otherwise.
static void* reflect_require_object(void* targetArg, const char* msg) {
    void* t = ts_nanbox_safe_unbox(targetArg);
    if (!t || *(uint32_t*)t == 0x53594D42 /*SYMB*/ || *(uint32_t*)t == 0x53545247 /*STRG*/) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg));
        return nullptr;  // unreachable
    }
    return t;
}

extern "C" uint8_t ts_integrity_get(void* raw);

extern "C" int64_t ts_reflect_isExtensible(void* targetArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.isExtensible called on non-object");
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->isExtensibleTrap() ? 1 : 0;

    if (is_flat_object(target)) return flat_object_is_extensible(target) ? 1 : 0;

    TsMap* obj = ts_cast<TsMap>(target);
    if (obj) {
        return obj->IsExtensible() ? 1 : 0;
    }
    // Exotic objects: consult the weak integrity side-table.
    return ts_integrity_get(target) == 0 ? 1 : 0;
}

extern "C" int64_t ts_reflect_preventExtensions(void* targetArg) {
    void* target = reflect_require_object(targetArg,
        "Reflect.preventExtensions called on non-object");
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->preventExtensionsTrap() ? 1 : 0;

    if (is_flat_object(target)) {
        flat_object_set_non_extensible(target);
        return 1;
    }

    TsMap* obj = ts_cast<TsMap>(target);
    if (obj) {
        obj->PreventExtensions();
        return 1;
    }
    return 0;
}

extern "C" TsValue* ts_reflect_getOwnPropertyDescriptor(void* targetArg, void* propArg) {
    reflect_require_object(targetArg,
        "Reflect.getOwnPropertyDescriptor called on non-object");
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->getOwnPropertyDescriptorTrap((TsValue*)propArg);

    // Delegate to the full [[GetOwnProperty]] engine so function/array/native
    // receivers, accessor descriptors, symbol keys, and the ACTUAL property
    // attributes (writable/enumerable/configurable) are reported correctly.
    // (The prior shallow reimplementation cast the target to TsMap — returning
    // undefined for every function/array receiver — and hardcoded all three
    // attribute flags to true; Reflect.gOPD(fn, k) round-tripped nothing.)
    return ts_object_getOwnPropertyDescriptor((TsValue*)targetArg, (TsValue*)propArg);
}

extern "C" int64_t ts_reflect_defineProperty(void* targetArg, void* propArg, void* descriptorArg) {
    {
        TsProxy* px = reflect_as_proxy(targetArg);
        if (px) return px->definePropertyTrap((TsValue*)propArg, (TsValue*)descriptorArg) ? 1 : 0;
    }
    void* target = reflect_require_object(targetArg,
        "Reflect.defineProperty called on non-object");

    // ES 10.4.5.3: integer-indexed exotic [[DefineOwnProperty]] — Reflect
    // surfaces the boolean instead of throwing.
    if ((uintptr_t)target >= 4096 &&
        *(uint32_t*)((char*)target + 16) == 0x54415252 /* TARR */) {
        extern int ts_ta_define_own_property(void* taRaw, TsValue* prop,
                                             TsValue* descriptor);
        void* dRaw = ts_nanbox_safe_unbox(descriptorArg);
        if (dRaw && is_flat_object(dRaw)) {
            descriptorArg = (void*)ts_value_box_any(ts_flat_object_to_map(dRaw));
        }
        int r = ts_ta_define_own_property(target, (TsValue*)propArg,
                                          (TsValue*)descriptorArg);
        if (r >= 0) return r;
        // r == -1: ordinary NAMED key — side-map define (data + accessors).
        extern int ts_ta_define_named_property(void* taRaw, TsValue* prop,
                                               TsValue* descriptor);
        return ts_ta_define_named_property(target, (TsValue*)propArg,
                                           (TsValue*)descriptorArg);
    }

    // Ordinary object [[DefineOwnProperty]]: delegate to the full engine
    // (ts_object_defineProperty) so descriptor coercions, accessor storage,
    // symbol keys, and the ValidateAndApply invariants all behave exactly as
    // Object.defineProperty — but return the boolean result instead of
    // throwing on refusal. (The shallow prior implementation only handled a
    // bare `value` field on TsMaps, so accessor/symbol/config descriptors and
    // the freeze/seal refusal-boolean were all wrong.)
    return reflect_define_ordinary(targetArg, propArg, descriptorArg);
}

extern "C" TsValue* ts_reflect_ownKeys(void* targetArg) {
    if (TsProxy* px = reflect_as_proxy(targetArg))
        return px->ownKeys();
    void* target = reflect_require_object(targetArg,
        "Reflect.ownKeys called on non-object");

    // ES 28.1.11: [[OwnPropertyKeys]] = ALL own string keys (incl.
    // non-enumerable — getOwnPropertyNames, NOT the enumerable-only
    // Object.keys) followed by own SYMBOL keys (module namespace
    // @@toStringTag; own-property-keys-sort asserts >= strings+symbols).
    extern TsValue* ts_object_getOwnPropertyNames(TsValue* obj);
    extern TsValue* ts_object_getOwnPropertySymbols_native(void* context,
                                                           int argc,
                                                           TsValue** argv);
    TsValue* boxed = ts_value_box_any(target);
    TsValue* namesV = ts_object_getOwnPropertyNames(boxed);
    TsValue* symsV = ts_object_getOwnPropertySymbols_native(nullptr, 1, &boxed);
    void* nraw = namesV ? ts_value_get_object(namesV) : nullptr;
    void* sraw = symsV ? ts_value_get_object(symsV) : nullptr;
    TsArray* names = (nraw && *(uint32_t*)nraw == 0x41525259)
                         ? (TsArray*)nraw : nullptr;
    TsArray* syms = (sraw && *(uint32_t*)sraw == 0x41525259)
                        ? (TsArray*)sraw : nullptr;
    if (!syms || syms->Length() == 0)
        return namesV ? namesV : ts_object_keys(boxed);
    TsArray* out = TsArray::Create(0);
    if (names)
        for (int64_t i = 0; i < names->Length(); i++)
            out->Push(names->Get((size_t)i));
    for (int64_t i = 0; i < syms->Length(); i++)
        out->Push(syms->Get((size_t)i));
    return ts_value_make_array(out);
}

// ============================================================================
// Mangled-name aliases for `Reflect.X(...)` call expressions in untyped JS.
// The analyzer mangles `Reflect.construct(target, args)` to the symbol
// `Reflect_construct_any_any`. Rather than teaching the analyzer to route
// these to ts_reflect_X, we expose the exact mangled symbols that forward
// to the underlying helpers. Each variant matches one or more arg-count
// shapes (e.g., construct/3 supports the optional newTarget parameter).
// ============================================================================

extern "C" TsValue* Reflect_construct_any_any(void* target, void* args) {
    return ts_reflect_construct(target, args, /*newTarget=*/target);
}

extern "C" TsValue* Reflect_construct_any_any_any(void* target, void* args, void* newTarget) {
    return ts_reflect_construct(target, args, newTarget);
}

extern "C" TsValue* Reflect_apply_any_any_any(void* target, void* thisArg, void* args) {
    return ts_reflect_apply(target, thisArg, args);
}

extern "C" TsValue* Reflect_get_any_any(void* target, void* prop) {
    return ts_reflect_get(target, prop, /*receiver=*/target);
}

extern "C" TsValue* Reflect_get_any_any_any(void* target, void* prop, void* receiver) {
    return ts_reflect_get(target, prop, receiver);
}

extern "C" TsValue* Reflect_has_any_any(void* target, void* prop) {
    return ts_value_make_bool(ts_reflect_has(target, prop) != 0);
}

extern "C" TsValue* Reflect_ownKeys_any(void* target) {
    return ts_reflect_ownKeys(target);
}

extern "C" TsValue* Reflect_getPrototypeOf_any(void* target) {
    return ts_reflect_getPrototypeOf(target);
}

extern "C" TsValue* Reflect_isExtensible_any(void* target) {
    return ts_value_make_bool(ts_reflect_isExtensible(target) != 0);
}

extern "C" TsValue* Reflect_preventExtensions_any(void* target) {
    return ts_value_make_bool(ts_reflect_preventExtensions(target) != 0);
}
