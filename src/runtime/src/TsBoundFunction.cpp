#include "TsBoundFunction.h"
#include "TsRuntime.h"
#include "GC.h"

extern "C" {

// Called when a bound function is invoked
// ctx is a TsBoundFunction* containing the original function and bound args
TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv) {
    TsBoundFunction* bound = (TsBoundFunction*)ctx;
    if (!bound || !bound->targetFunction) {
        return ts_value_make_undefined();
    }

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
TsValue* ts_function_bind_native(void* ctx, int argc, TsValue** argv) {
    TsValue* targetFunc = (TsValue*)ctx;

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
    return ts_value_make_native_function((void*)ts_bound_function_call, (void*)bound);
}

} // extern "C"
