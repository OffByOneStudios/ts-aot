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

const std::unordered_map<std::string, std::vector<bool>> BoxingPolicy::CORE_RUNTIME_ARG_BOXING = {
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
    {"ts_new",          {false}},                  // (size_t) -> void*
    {"ts_panic",        {false}},                  // (const char*) -> void
    {"ts_cfi_fail",     {}},                       // () -> void

    // =========================================================================
    // Array operations - additional
    // =========================================================================
    {"ts_array_at",       {false, false}},         // (arr*, index) -> raw
    {"ts_array_concat",   {false, false}},         // (arr*, arr*) -> arr*
    {"ts_array_includes", {false, true}},          // (arr*, value) -> bool

    // =========================================================================
    // Map operations - additional
    // =========================================================================
    {"ts_map_keys",              {false}},         // (map*) -> iterator
    {"ts_map_copy_excluding_v2", {false, true}},   // (map*, key) -> map*

    // =========================================================================
    // Set operations - additional
    // =========================================================================
    {"ts_set_size", {false}},                      // (set*) -> int

    // =========================================================================
    // String operations - additional
    // =========================================================================
    {"ts_string_eq",          {false, false}},     // (TsString*, TsString*) -> bool
    {"ts_string_slice",       {false, false, false}}, // (str*, start, end) -> str*
    {"ts_string_from_int",    {false}},            // (int64_t) -> TsString*
    {"ts_string_from_double", {false}},            // (double) -> TsString*
    {"ts_string_from_bool",   {false}},            // (bool) -> TsString*
    {"ts_string_from_value",  {true}},             // (TsValue*) -> TsString*

    // =========================================================================
    // Value operations - additional
    // =========================================================================
    {"ts_value_get_element",  {true, false}},      // (arr, index) -> TsValue*
    {"ts_value_get_property", {true, false}},      // (obj, prop) -> TsValue*
    {"ts_value_is_nullish",   {true}},             // (TsValue*) -> bool
    {"ts_value_length",       {true}},             // (TsValue*) -> int
    {"ts_value_to_bool",      {true}},             // (TsValue*) -> bool
    {"ts_value_make_promise", {false}},            // (promise*) -> TsValue*
    {"ts_typeof",             {true}},             // (TsValue*) -> TsString*

    // =========================================================================
    // Date operations
    // =========================================================================
    {"ts_date_create",     {}},                    // () -> Date*
    {"ts_date_create_ms",  {false}},               // (ms) -> Date*
    {"ts_date_create_str", {false}},               // (str*) -> Date*

    // =========================================================================
    // Error operations
    // =========================================================================
    {"ts_error_create",    {false}},               // (msg) -> Error*
    {"ts_set_exception",   {false}},               // (exception) -> void

    // =========================================================================
    // RegExp operations
    // =========================================================================
    {"ts_regexp_create",       {false, false}},    // (pattern, flags) -> RegExp*
    {"ts_regexp_from_literal", {false, false}},    // (pattern, flags) -> RegExp*

    // =========================================================================
    // Promise operations
    // =========================================================================
    {"ts_promise_then",             {false, true}},           // (promise*, callback) -> promise*
    {"ts_promise_catch",            {false, true}},           // (promise*, callback) -> promise*
    {"ts_promise_finally",          {false, true}},           // (promise*, callback) -> promise*
    {"ts_promise_resolve_internal", {false, true}},           // (promise*, value)
    {"ts_promise_reject_internal",  {false, true}},           // (promise*, reason)

    // =========================================================================
    // Async/Generator operations
    // =========================================================================
    {"ts_async_await",             {false}},                  // (promise*) -> value
    {"ts_async_context_create",    {}},                       // () -> context*
    {"ts_async_yield",             {false, true}},            // (gen*, value)
    {"ts_generator_create",        {false, false}},           // (fn, closure) -> gen*
    {"ts_async_generator_create",  {false, false}},           // (fn, closure) -> gen*
    {"ts_async_generator_resolve", {false, true, false}},     // (gen*, value, done)
    {"ts_async_iterator_get",      {true}},                   // (obj) -> iterator*
    {"ts_async_iterator_next",     {false}},                  // (iterator*) -> result

    // =========================================================================
    // Timer operations
    // =========================================================================
    {"ts_set_immediate", {true}},                  // (callback) -> timer_id
    {"ts_clear_timer",   {false}},                 // (timer_id) -> void

    // =========================================================================
    // BigInt operations
    // =========================================================================
    {"ts_bigint_create_str", {false}},             // (str*) -> BigInt*
    {"ts_bigint_from_value", {true}},              // (TsValue*) -> BigInt*

    // =========================================================================
    // Symbol operations
    // =========================================================================
    {"ts_symbol_create", {false}},                 // (description) -> Symbol*

    // =========================================================================
    // Buffer operations
    // =========================================================================
    {"ts_buffer_get",              {false, false}},      // (buf*, index) -> int
    {"ts_buffer_set",              {false, false, false}}, // (buf*, index, value)
    {"ts_buffer_byte_length",      {false}},             // (buf*) -> int
    {"ts_buffer_byte_offset",      {false}},             // (buf*) -> int
    {"ts_buffer_get_array_buffer", {false}},             // (buf*) -> ArrayBuffer*
    {"ts_data_view_create",        {false, false, false}}, // (buf, offset, len) -> DataView*

    // =========================================================================
    // TextEncoder/TextDecoder
    // =========================================================================
    {"ts_text_encoder_create",       {}},                // () -> TextEncoder*
    {"ts_text_encoder_get_encoding", {false}},           // (enc*) -> str*
    {"ts_text_decoder_create",       {false, false}},    // (encoding, options) -> TextDecoder*
    {"ts_text_decoder_get_encoding", {false}},           // (dec*) -> str*
    {"ts_text_decoder_is_fatal",     {false}},           // (dec*) -> bool
    {"ts_text_decoder_ignore_bom",   {false}},           // (dec*) -> bool

    // =========================================================================
    // URL operations
    // =========================================================================
    {"ts_url_create",               {false, false}},     // (url, base) -> URL*
    {"ts_url_search_params_create", {false}},            // (init) -> URLSearchParams*
    {"ts_url_search_params_size",   {false}},            // (params*) -> int

    // =========================================================================
    // EventEmitter
    // =========================================================================
    {"ts_event_emitter_create", {}},                     // () -> EventEmitter*

    // =========================================================================
    // HTTP operations
    // =========================================================================
    {"ts_http_agent_create",  {false}},                  // (options) -> Agent*
    {"ts_https_agent_create", {false}},                  // (options) -> Agent*
    {"ts_fetch",              {false, true}},            // (url, options) -> Promise*

    // =========================================================================
    // Net operations
    // =========================================================================
    {"ts_net_block_list_create",    {}},                 // () -> BlockList*
    {"ts_net_socket_address_create", {false}},           // (options) -> SocketAddress*
    {"ts_websocket_create",         {false, false}},     // (url, protocols) -> WebSocket*

    // =========================================================================
    // Process property getters
    // =========================================================================
    {"ts_get_process_argv",               {}},           // () -> array
    {"ts_get_process_env",                {}},           // () -> object
    {"ts_process_get_arch",               {}},           // () -> str*
    {"ts_process_get_argv0",              {}},           // () -> str*
    {"ts_process_get_config",             {}},           // () -> object
    {"ts_process_get_debug_port",         {}},           // () -> int
    {"ts_process_get_exec_argv",          {}},           // () -> array
    {"ts_process_get_exec_path",          {}},           // () -> str*
    {"ts_process_get_exit_code",          {}},           // () -> int
    {"ts_process_get_features",           {}},           // () -> object
    {"ts_process_get_pid",                {}},           // () -> int
    {"ts_process_get_ppid",               {}},           // () -> int
    {"ts_process_get_release",            {}},           // () -> object
    {"ts_process_get_report",             {}},           // () -> object
    {"ts_process_get_stderr",             {}},           // () -> Writable*
    {"ts_process_get_stdin",              {}},           // () -> Readable*
    {"ts_process_get_stdout",             {}},           // () -> Writable*
    {"ts_process_get_title",              {}},           // () -> str*
    {"ts_process_get_version",            {}},           // () -> str*
    {"ts_process_get_versions",           {}},           // () -> object
    {"ts_process_set_env",                {false, false}}, // (key, value) -> void
    {"ts_process_set_exit_code",          {false}},      // (code) -> void

    // =========================================================================
    // Math constants
    // =========================================================================
    {"ts_math_PI",        {}},                     // () -> double

    // =========================================================================
    // Global functions
    // =========================================================================
    {"ts_parseInt",           {false, false}},     // (str*, radix) -> int
    {"ts_double_to_int32",    {false}},            // (double) -> int32
    {"ts_double_to_uint32",   {false}},            // (double) -> uint32
};

const std::unordered_set<std::string> BoxingPolicy::CORE_RUNTIME_RETURNS_BOXED = {
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
    "ts_value_make_promise",
    "ts_value_get_element",
    "ts_value_get_property",
    "ts_call_0",
    "ts_call_1",
    "ts_call_2",
    "ts_call_3",
    "ts_call_4",
    "ts_call_n",
    "ts_async_await",
    "ts_async_iterator_next",
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
    auto it = runtimeArgBoxing_.find(funcName);
    if (it == runtimeArgBoxing_.end()) {
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
    return runtimeArgBoxing_.find(funcName) != runtimeArgBoxing_.end();
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
    return runtimeReturnsBoxed_.count(funcName) > 0;
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

void BoxingPolicy::initFromCoreRegistry() {
    // Copy the static seed data into instance maps
    runtimeArgBoxing_ = CORE_RUNTIME_ARG_BOXING;
    runtimeReturnsBoxed_ = CORE_RUNTIME_RETURNS_BOXED;
}

void BoxingPolicy::registerRuntimeApi(const std::string& funcName, std::vector<bool> argBoxing, bool returnsBoxed) {
    runtimeArgBoxing_[funcName] = std::move(argBoxing);
    if (returnsBoxed) {
        runtimeReturnsBoxed_.insert(funcName);
    }
}

} // namespace ts
