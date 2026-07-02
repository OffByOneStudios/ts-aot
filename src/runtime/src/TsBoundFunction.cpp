#include "TsBoundFunction.h"
#include "TsMap.h"
#include "TsString.h"
#include "TsHashTable.h"
#include "TsNanBox.h"
#include <cstdio>
#include "TsRuntime.h"
#include "GC.h"

extern "C" {

// Declared in TsObject.cpp
void* ts_get_call_this();

// Called when a bound function is invoked
// ctx is a TsBoundFunction* containing the original function and bound args
TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv) {
    TsBoundFunction* bound = (TsBoundFunction*)ctx;
    if (!bound || !bound->targetFunction) {
        return ts_value_make_undefined();
    }

    // Defensive: if ctx is not a real TsBoundFunction (e.g. `new boundFn()`
    // misroutes the constructor's `this` here instead of the bound context),
    // boundArgCount is garbage and `ts_alloc(8 * totalArgc)` aborts the
    // process. Bail gracefully on an implausible count rather than crash.
    if (bound->boundArgCount < 0 || bound->boundArgCount > 65535) {
        return ts_value_make_undefined();
    }
    if (argc < 0 || argc > 65535) argc = 0;

    // Calculate total argument count: bound args + call args
    int totalArgc = bound->boundArgCount + argc;

    // Build combined argument array
    TsValue** combinedArgs = nullptr;
    if (totalArgc > 0) {
        combinedArgs = (TsValue**)ts_alloc(sizeof(TsValue*) * totalArgc);

        // Copy bound arguments first
        for (int i = 0; i < bound->boundArgCount; i++) {
            combinedArgs[i] = bound->boundArgs[i];
        }

        // Copy call arguments after
        for (int i = 0; i < argc; i++) {
            combinedArgs[bound->boundArgCount + i] = argv[i];
        }
    }

    // Call the original function with bound this
    return ts_function_call_with_this(bound->targetFunction, bound->boundThis, totalArgc, combinedArgs);
}

// Creates a bound function
// ctx is a TsValue* containing the target function
// argv[0] is thisArg, argv[1...] are bound arguments
extern "C" void* ts_error_create_typed(const char* type, const char* message);

TsValue* ts_function_bind_native(void* ctx, int argc, TsValue** argv) {
    TsValue* targetFunc = (TsValue*)ctx;
    if (!targetFunc) targetFunc = (TsValue*)ts_get_call_this();

    // Function.prototype.bind: step 2 requires IsCallable(this) -> TypeError.
    if (!ts_is_callable(targetFunc)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Function.prototype.bind called on incompatible (non-callable) receiver"));
        return ts_value_make_undefined();
    }

    // Get thisArg (first argument) or undefined
    TsValue* thisArg = (argc >= 1 && argv) ? argv[0] : ts_value_make_undefined();

    // Count bound arguments (all args after thisArg)
    int boundArgCount = (argc > 1) ? (argc - 1) : 0;

    // Allocate bound function structure
    TsBoundFunction* bound = (TsBoundFunction*)ts_alloc(sizeof(TsBoundFunction));
    bound->targetFunction = targetFunc;
    bound->boundThis = thisArg;
    bound->boundArgCount = boundArgCount;

    // Copy bound arguments if any
    if (boundArgCount > 0) {
        bound->boundArgs = (TsValue**)ts_alloc(sizeof(TsValue*) * boundArgCount);
        for (int i = 0; i < boundArgCount; i++) {
            bound->boundArgs[i] = argv[i + 1];  // Skip thisArg
        }
    } else {
        bound->boundArgs = nullptr;
    }

    // Return a native function that will call ts_bound_function_call with bound context
    TsValue* fnVal = ts_value_make_native_function((void*)ts_bound_function_call, (void*)bound);

    // ES 20.2.3.2 steps 8-11: the bound function's own `length` is
    // max(0, ToIntegerOrInfinity(target.length) - boundArgCount) when the
    // target's length is a Number (0 otherwise), and its `name` is
    // "bound " + target.name (empty when the target's name isn't a string).
    // Both are {writable:false, enumerable:false, configurable:true}.
    {
        TsFunction* f = (TsFunction*)ts_value_get_object(fnVal);
        if (!f) f = (TsFunction*)fnVal;
        extern TsValue* ts_object_get_property(void* o, const char* k);
        void* rawT = ts_value_get_object(targetFunc);
        if (!rawT) rawT = (void*)targetFunc;

        double lenD = 0;
        bool lenIsNum = false;
        TsValue* tl = ts_object_get_property(rawT, "length");
        if (tl) {
            TsValue tv = nanbox_to_tagged(tl);
            if (tv.type == ValueType::NUMBER_INT) { lenD = (double)tv.i_val; lenIsNum = true; }
            else if (tv.type == ValueType::NUMBER_DBL) { lenD = tv.d_val; lenIsNum = true; }
        }
        double outLen = 0;
        if (lenIsNum) {
            if (lenD == lenD) {  // not NaN
                double t = (lenD > 0 && lenD != lenD + 1) ? lenD : lenD;  // keep +Inf
                t = t - (double)boundArgCount;
                if (t < 0) t = 0;
                if (!(t != t)) outLen = t;
            }
        }

        const char* tname = nullptr;
        TsValue* tn = ts_object_get_property(rawT, "name");
        if (tn) {
            TsValue nv = nanbox_to_tagged(tn);
            if (nv.type == ValueType::STRING_PTR && nv.ptr_val)
                tname = ((TsString*)nv.ptr_val)->ToUtf8();
        }
        char nameBuf[512];
        snprintf(nameBuf, sizeof(nameBuf), "bound %s", tname ? tname : "");
        // spec: non-string target name -> just "bound " (with trailing space)
        f->name = TsString::Create(nameBuf);
        f->arity = (int)outLen;
        f->is_constructor = true;  // bound fns are constructors when target is

        if (!f->properties) f->properties = TsMap::Create();
        TsValue lk; lk.type = ValueType::STRING_PTR; lk.ptr_val = TsString::GetInterned("length");
        TsValue lv;
        if (outLen == (double)(int64_t)outLen &&
            outLen >= -9007199254740992.0 && outLen <= 9007199254740992.0) {
            lv.type = ValueType::NUMBER_INT; lv.i_val = (int64_t)outLen;
        } else {
            lv.type = ValueType::NUMBER_DBL; lv.d_val = outLen;
        }
        f->properties->SetWithAttrs(lk, lv, TsHashTable::ATTR_CONFIGURABLE);
        TsValue nk; nk.type = ValueType::STRING_PTR; nk.ptr_val = TsString::GetInterned("name");
        TsValue nvv; nvv.type = ValueType::STRING_PTR; nvv.ptr_val = f->name;
        f->properties->SetWithAttrs(nk, nvv, TsHashTable::ATTR_CONFIGURABLE);
    }
    return fnVal;
}

} // extern "C"
