#include "BoxingPolicy.h"
#include <stdexcept>

namespace ts {

// =============================================================================
// RUNTIME API REGISTRY - THE SINGLE SOURCE OF TRUTH
// =============================================================================
// 
// This registry defines exactly what each runtime C function expects.
// When adding a new runtime function, ADD IT HERE.
//
// Format: {"function_name", {arg0_boxed, arg1_boxed, ...}}
//   - true  = argument must be TsValue* (boxed)
//   - false = argument is raw (int64_t, double, pointer, etc.)
//

const std::unordered_map<std::string, std::vector<bool>> BoxingPolicy::RUNTIME_ARG_BOXING = {
    // =========================================================================
    // Map operations - keys and values are ALWAYS boxed for type-aware hashing
    // =========================================================================
    {"ts_map_create",  {}},                        // () -> TsMap*
    {"ts_map_set",     {false, true, true}},       // (map*, key, value)
    {"ts_map_get",     {false, true}},             // (map*, key) -> TsValue*
    {"ts_map_has",     {false, true}},             // (map*, key) -> bool
    {"ts_map_delete",  {false, true}},             // (map*, key) -> bool
    {"ts_map_size",    {false}},                   // (map*) -> int
    {"ts_map_clear",   {false}},                   // (map*) -> void

    // =========================================================================
    // Set operations - values are ALWAYS boxed
    // =========================================================================
    {"ts_set_create",  {}},                        // () -> TsSet*
    {"ts_set_add",     {false, true}},             // (set*, value)
    {"ts_set_has",     {false, true}},             // (set*, value) -> bool
    {"ts_set_delete",  {false, true}},             // (set*, value) -> bool
    {"ts_set_size",    {false}},                   // (set*) -> int
    {"ts_set_clear",   {false}},                   // (set*) -> void

    // =========================================================================
    // Dynamic function calls - function AND all args are boxed
    // The callee uses ts_value_get_* to extract what it needs
    // =========================================================================
    {"ts_call_0",      {true}},                    // (func)
    {"ts_call_1",      {true, true}},              // (func, arg0)
    {"ts_call_2",      {true, true, true}},        // (func, arg0, arg1)
    {"ts_call_3",      {true, true, true, true}},  // (func, arg0, arg1, arg2)
    {"ts_call_4",      {true, true, true, true, true}},
    {"ts_call_n",      {true, false, true}},       // (func, argc, argv[])

    // =========================================================================
    // Array operations - specialized arrays use RAW values
    // =========================================================================
    {"ts_array_create",             {}},                       // () -> TsArray*
    {"ts_array_create_specialized", {false, false, false}},    // (elemPtr, count, elemSize)
    {"ts_array_push",               {false, false}},           // (arr*, value) - RAW
    {"ts_array_pop",                {false}},                  // (arr*) -> raw value
    {"ts_array_get",                {false, false}},           // (arr*, index) -> raw
    {"ts_array_get_unchecked",      {false, false}},           // (arr*, index) -> raw
    {"ts_array_set",                {false, false, false}},    // (arr*, index, value)
    {"ts_array_length",             {false}},                  // (arr*) -> int
    {"ts_array_get_elements_ptr",   {false}},                  // (arr*) -> void*
    {"ts_array_sort",               {false}},                  // (arr*)
    {"ts_array_sort_with_comparator", {false, true}},          // (arr*, comparator) - comparator is boxed

    // =========================================================================
    // Console - optimized paths use raw values
    // =========================================================================
    {"ts_console_log",        {false}},            // (TsString*) - raw pointer
    {"ts_console_log_int",    {false}},            // (int64_t) - raw
    {"ts_console_log_double", {false}},            // (double) - raw
    {"ts_console_log_bool",   {false}},            // (bool) - raw
    {"ts_console_log_value",  {true}},             // (TsValue*) - boxed for generic

    // =========================================================================
    // Value boxing functions - these CREATE boxed values from raw
    // Input is raw, output is boxed
    // =========================================================================
    {"ts_value_make_int",       {false}},          // (int64_t) -> TsValue*
    {"ts_value_make_double",    {false}},          // (double) -> TsValue*
    {"ts_value_make_bool",      {false}},          // (bool) -> TsValue*
    {"ts_value_make_string",    {false}},          // (TsString*) -> TsValue*
    {"ts_value_make_object",    {false}},          // (void*) -> TsValue*
    {"ts_value_make_function",  {false, false}},   // (fn_ptr, closure) -> TsValue*
    {"ts_value_make_undefined", {}},               // () -> TsValue*
    {"ts_value_make_null",      {}},               // () -> TsValue*

    // =========================================================================
    // Value unboxing functions - these EXTRACT raw values from boxed
    // Input is boxed, output is raw
    // =========================================================================
    {"ts_value_get_int",    {true}},               // (TsValue*) -> int64_t
    {"ts_value_get_double", {true}},               // (TsValue*) -> double
    {"ts_value_get_bool",   {true}},               // (TsValue*) -> bool
    {"ts_value_get_string", {true}},               // (TsValue*) -> TsString*
    {"ts_value_get_object", {true}},               // (TsValue*) -> void*

    // =========================================================================
    // String operations - work with raw TsString* pointers
    // =========================================================================
    {"ts_string_create",    {false}},              // (const char*) -> TsString*
    {"ts_string_length",    {false}},              // (TsString*) -> int
    {"ts_string_concat",    {false, false}},       // (TsString*, TsString*) -> TsString*
    {"ts_string_equals",    {false, false}},       // (TsString*, TsString*) -> bool

    // =========================================================================
    // Math operations - raw primitives
    // =========================================================================
    {"ts_math_floor",   {false}},                  // (double) -> int64_t
    {"ts_math_ceil",    {false}},                  // (double) -> int64_t
    {"ts_math_round",   {false}},                  // (double) -> int64_t
    {"ts_math_abs",     {false}},                  // (double) -> double
    {"ts_math_sqrt",    {false}},                  // (double) -> double
    {"ts_math_pow",     {false, false}},           // (double, double) -> double
    {"ts_math_min",     {false, false}},           // (double, double) -> double
    {"ts_math_max",     {false, false}},           // (double, double) -> double

    // =========================================================================
    // Memory allocation - raw pointers
    // =========================================================================
    {"ts_alloc",        {false}},                  // (size_t) -> void*
    {"ts_gc_init",      {}},                       // ()
};

const std::unordered_set<std::string> BoxingPolicy::RUNTIME_RETURNS_BOXED = {
    // Functions that return TsValue* (boxed values)
    "ts_map_get",
    "ts_value_make_int",
    "ts_value_make_double",
    "ts_value_make_bool",
    "ts_value_make_string",
    "ts_value_make_object",
    "ts_value_make_function",
    "ts_value_make_undefined",
    "ts_value_make_null",
    "ts_call_0",
    "ts_call_1",
    "ts_call_2",
    "ts_call_3",
    "ts_call_4",
    "ts_call_n",
};

// =============================================================================
// DECISION LOGIC
// =============================================================================

BoxingPolicy::Decision BoxingPolicy::decide(
    Type* valueType,
    BoxingContext context,
    const std::string& runtimeFunc,
    int argIndex,
    Type* targetType,
    bool isAlreadyBoxed
) const {
    Decision d;

    switch (context) {
        // ---------------------------------------------------------------------
        // RuntimeApiArg: Look up what the C function expects
        // ---------------------------------------------------------------------
        case BoxingContext::RuntimeApiArg: {
            bool expectsBoxed = runtimeExpectsBoxed(runtimeFunc, argIndex);
            if (expectsBoxed && !isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }

        // ---------------------------------------------------------------------
        // RuntimeApiReturn: Unbox if we need a specific type
        // ---------------------------------------------------------------------
        case BoxingContext::RuntimeApiReturn: {
            bool returnsBoxed = runtimeReturnsBoxed(runtimeFunc);
            if (returnsBoxed && targetType && targetType->kind != TypeKind::Any) {
                d.needsUnboxing = true;
                d.unboxingFunction = getUnboxingFunction(targetType);
            }
            break;
        }

        // ---------------------------------------------------------------------
        // UserFunctionArg: Box only for untyped params or callbacks
        // ---------------------------------------------------------------------
        case BoxingContext::UserFunctionArg: {
            // Case 1: No target type or Any type = needs runtime type info
            if (!targetType || targetType->kind == TypeKind::Any) {
                if (!isAlreadyBoxed) {
                    d.needsBoxing = true;
                    d.boxingFunction = getBoxingFunction(valueType);
                }
            }
            // Case 2: Callback function = must box for ts_call_N
            else if (valueType && valueType->kind == TypeKind::Function) {
                if (!isAlreadyBoxed) {
                    d.needsBoxing = true;
                    d.boxingFunction = "ts_value_make_function";
                }
            }
            // Case 3: Typed parameter = pass raw for optimization
            // (d remains with needsBoxing = false)
            break;
        }

        // ---------------------------------------------------------------------
        // UserFunctionReturn: Usually raw for typed functions
        // ---------------------------------------------------------------------
        case BoxingContext::UserFunctionReturn: {
            // If returning from a function with Any return type, box it
            if (targetType && targetType->kind == TypeKind::Any && !isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }

        // ---------------------------------------------------------------------
        // MapKey/MapValue: ALWAYS boxed - Map stores generic TsValue*
        // ---------------------------------------------------------------------
        case BoxingContext::MapKey:
        case BoxingContext::MapValue: {
            if (!isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }

        // ---------------------------------------------------------------------
        // ArrayStore: Depends on whether array is specialized
        // ---------------------------------------------------------------------
        case BoxingContext::ArrayStore: {
            // Specialized arrays (int[], double[], string[]) = raw
            // Generic arrays (any[]) = boxed
            // The caller should use shouldBoxForSpecializedArray() first
            // Here we assume the caller knows the array type
            if (targetType && shouldBoxForSpecializedArray(targetType)) {
                // Generic array element type = needs boxing
                if (!isAlreadyBoxed) {
                    d.needsBoxing = true;
                    d.boxingFunction = getBoxingFunction(valueType);
                }
            }
            // else: specialized array, keep raw
            break;
        }

        // ---------------------------------------------------------------------
        // ArrayLoad: Unbox if loading from generic array
        // ---------------------------------------------------------------------
        case BoxingContext::ArrayLoad: {
            if (targetType && shouldBoxForSpecializedArray(targetType)) {
                // Loading from generic array = result is boxed
                if (valueType && valueType->kind != TypeKind::Any) {
                    d.needsUnboxing = true;
                    d.unboxingFunction = getUnboxingFunction(valueType);
                }
            }
            break;
        }

        // ---------------------------------------------------------------------
        // ClosureCapture: Box for uniform storage, EXCEPT already-boxed funcs
        // ---------------------------------------------------------------------
        case BoxingContext::ClosureCapture: {
            // Functions that are already boxed (from call site) = don't double-box
            if (valueType && valueType->kind == TypeKind::Function && isAlreadyBoxed) {
                // Already boxed - store directly, no additional boxing
                break;
            }
            // Everything else: box for uniform storage in closure environment
            if (!isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }

        // ---------------------------------------------------------------------
        // ClosureExtract: Unbox based on expected type
        // ---------------------------------------------------------------------
        case BoxingContext::ClosureExtract: {
            // Functions stay boxed (they were stored boxed)
            if (valueType && valueType->kind == TypeKind::Function) {
                // Functions remain as boxed TsValue*
                break;
            }
            // Other types: unbox to get raw value
            if (targetType) {
                d.needsUnboxing = true;
                d.unboxingFunction = getUnboxingFunction(targetType);
            }
            break;
        }
    }

    return d;
}

bool BoxingPolicy::runtimeExpectsBoxed(const std::string& funcName, int argIndex, bool strictMode) const {
    auto it = RUNTIME_ARG_BOXING.find(funcName);
    if (it == RUNTIME_ARG_BOXING.end()) {
        if (strictMode) {
            throw std::runtime_error(
                "BoxingPolicy: Runtime function '" + funcName + "' is not registered!\n"
                "Add it to RUNTIME_ARG_BOXING in BoxingPolicy.cpp.\n"
                "Format: {\"" + funcName + "\", {arg0_boxed, arg1_boxed, ...}}"
            );
        }
        // Non-strict mode: assume raw (conservative for optimization)
        return false;
    }
    if (argIndex < 0 || static_cast<size_t>(argIndex) >= it->second.size()) {
        if (strictMode) {
            throw std::runtime_error(
                "BoxingPolicy: Runtime function '" + funcName + "' has " + 
                std::to_string(it->second.size()) + " registered args, but arg " + 
                std::to_string(argIndex) + " was requested."
            );
        }
        return false;
    }
    return it->second[argIndex];
}

bool BoxingPolicy::hasRuntimeApiRegistered(const std::string& funcName) const {
    return RUNTIME_ARG_BOXING.find(funcName) != RUNTIME_ARG_BOXING.end();
}

void BoxingPolicy::assertRuntimeApiRegistered(const std::string& funcName) const {
    if (!hasRuntimeApiRegistered(funcName)) {
        throw std::runtime_error(
            "\n==========================================================\n"
            "BOXING POLICY ERROR: Unregistered runtime function!\n"
            "==========================================================\n"
            "Function: " + funcName + "\n\n"
            "When adding new runtime C functions, you MUST register them\n"
            "in BoxingPolicy.cpp RUNTIME_ARG_BOXING.\n\n"
            "Add this entry:\n"
            "    {\"" + funcName + "\", {arg0_boxed, arg1_boxed, ...}},\n\n"
            "Where true = expects TsValue* (boxed), false = expects raw.\n"
            "==========================================================\n"
        );
    }
}

bool BoxingPolicy::runtimeReturnsBoxed(const std::string& funcName) const {
    return RUNTIME_RETURNS_BOXED.count(funcName) > 0;
}

std::string BoxingPolicy::getBoxingFunction(Type* type) {
    if (!type) return "ts_value_make_object";
    
    switch (type->kind) {
        case TypeKind::Int:
            return "ts_value_make_int";
        case TypeKind::Double:
            return "ts_value_make_double";
        case TypeKind::Boolean:
            return "ts_value_make_bool";
        case TypeKind::String:
            return "ts_value_make_string";
        case TypeKind::Function:
            return "ts_value_make_function";
        case TypeKind::Null:
            return "ts_value_make_null";
        case TypeKind::Undefined:
            return "ts_value_make_undefined";
        default:
            // Arrays, objects, classes, etc.
            return "ts_value_make_object";
    }
}

std::string BoxingPolicy::getUnboxingFunction(Type* type) {
    if (!type) return "ts_value_get_object";
    
    switch (type->kind) {
        case TypeKind::Int:
            return "ts_value_get_int";
        case TypeKind::Double:
            return "ts_value_get_double";
        case TypeKind::Boolean:
            return "ts_value_get_bool";
        case TypeKind::String:
            return "ts_value_get_string";
        default:
            // Arrays, objects, classes, etc.
            return "ts_value_get_object";
    }
}

bool BoxingPolicy::shouldBoxForSpecializedArray(Type* elementType) {
    if (!elementType) return true;  // Unknown = box to be safe
    
    // Specialized array element types (store raw)
    switch (elementType->kind) {
        case TypeKind::Int:
        case TypeKind::Double:
        case TypeKind::Boolean:
        case TypeKind::String:
            return false;  // Specialized = raw storage
        default:
            return true;   // Generic = boxed storage
    }
}

} // namespace ts
