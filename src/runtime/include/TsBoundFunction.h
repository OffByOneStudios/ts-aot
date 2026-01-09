#pragma once
#include "TsObject.h"

// Structure to hold bound function data
struct TsBoundFunction {
    TsValue* targetFunction;  // The original function
    TsValue* boundThis;       // The bound this value
    TsValue** boundArgs;      // Array of bound arguments
    int boundArgCount;        // Number of bound arguments
};

// Forward declarations
extern "C" {
    TsValue* ts_function_bind_native(void* ctx, int argc, TsValue** argv);
    TsValue* ts_bound_function_call(void* ctx, int argc, TsValue** argv);
}
