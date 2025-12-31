#pragma once

#include "../analysis/Type.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ts {

/**
 * BoxingContext - Where is this value being used?
 * 
 * This enum drives the boxing decision. The context tells us whether
 * the value needs to be boxed (wrapped in TsValue*) or can remain raw.
 */
enum class BoxingContext {
    RuntimeApiArg,       // Passing to ts_* C function
    RuntimeApiReturn,    // Receiving from ts_* C function
    UserFunctionArg,     // Passing to user-defined function
    UserFunctionReturn,  // Returning from user-defined function
    ArrayStore,          // Storing into array
    ArrayLoad,           // Loading from array
    MapKey,              // Key for Map operation
    MapValue,            // Value for Map operation
    ClosureCapture,      // Capturing into closure environment
    ClosureExtract,      // Extracting from closure environment
};

/**
 * BoxingPolicy - THE SINGLE SOURCE OF TRUTH for boxing decisions
 * 
 * This class centralizes all boxing/unboxing logic. Instead of scattered
 * ad-hoc decisions throughout the codebase, all boxing questions go through
 * this class.
 * 
 * Usage:
 *   BoxingPolicy policy;
 *   auto decision = policy.decide(valueType, BoxingContext::MapKey);
 *   if (decision.needsBoxing) {
 *       value = createCall(decision.boxingFunction, {value});
 *   }
 */
class BoxingPolicy {
public:
    /**
     * Decision - The result of a boxing policy query
     */
    struct Decision {
        bool needsBoxing = false;      // Should we box this value?
        bool needsUnboxing = false;    // Should we unbox this value?
        std::string boxingFunction;    // e.g., "ts_value_make_int"
        std::string unboxingFunction;  // e.g., "ts_value_get_int"
    };

    BoxingPolicy() { initFromCoreRegistry(); }

    /**
     * decide - The main entry point for boxing decisions
     * 
     * @param valueType     The type of the value being processed
     * @param context       Where the value is being used
     * @param runtimeFunc   For RuntimeApiArg/Return: which ts_* function
     * @param argIndex      For RuntimeApiArg: which argument (0-based)
     * @param targetType    For UserFunctionArg: the declared parameter type
     * @param isAlreadyBoxed Whether the value is already boxed
     * @return Decision with boxing/unboxing instructions
     */
    Decision decide(
        Type* valueType,
        BoxingContext context,
        const std::string& runtimeFunc = "",
        int argIndex = -1,
        Type* targetType = nullptr,
        bool isAlreadyBoxed = false
    ) const;

    /**
     * runtimeExpectsBoxed - Does this runtime function expect a boxed arg?
     * 
     * This is the authoritative registry of what each ts_* function expects.
     * Throws if function is not registered and strictMode is true.
     */
    bool runtimeExpectsBoxed(const std::string& funcName, int argIndex, bool strictMode = false) const;

    /**
     * hasRuntimeApiRegistered - Check if a function is in the registry
     * 
     * Use this to verify a function exists before calling runtimeExpectsBoxed.
     */
    bool hasRuntimeApiRegistered(const std::string& funcName) const;

    /**
     * assertRuntimeApiRegistered - Fail compilation if function is not registered
     * 
     * Call this when emitting code for a ts_* function. If the function is not
     * in the registry, this throws a std::runtime_error with a helpful message
     * about how to add it.
     */
    void assertRuntimeApiRegistered(const std::string& funcName) const;

    /**
     * runtimeReturnsBoxed - Does this runtime function return a boxed value?
     */
    bool runtimeReturnsBoxed(const std::string& funcName) const;

    /**
     * getBoxingFunction - Get the appropriate ts_value_make_* function
     */
    static std::string getBoxingFunction(Type* type);

    /**
     * getUnboxingFunction - Get the appropriate ts_value_get_* function
     */
    static std::string getUnboxingFunction(Type* type);

    /**
     * shouldBoxForSpecializedArray - Check if value should be boxed for array storage
     * 
     * Specialized arrays (int[], double[], string[]) store raw values.
     * Generic arrays (any[]) store boxed values.
     */
    static bool shouldBoxForSpecializedArray(Type* elementType);

    /**
     * registerRuntimeApi - Register a runtime function's boxing expectations
     * 
     * Call this from each IRGenerator_*.cpp file to register the functions
     * that file uses. This allows incremental migration and self-documentation.
     * 
     * @param funcName  The function name (e.g., "ts_fs_read_file_sync")
     * @param argBoxing Vector of booleans: true = arg expects boxed, false = raw
     * @param returnsBoxed Whether the function returns a boxed TsValue*
     * 
     * Example:
     *   boxingPolicy.registerRuntimeApi("ts_fs_open", {false, false, false}, false);
     *   boxingPolicy.registerRuntimeApi("ts_map_get", {false, true}, true);
     */
    void registerRuntimeApi(const std::string& funcName, std::vector<bool> argBoxing, bool returnsBoxed = false);

private:
    // Registry of runtime function argument expectations
    // Function name -> [arg0 expects boxed?, arg1 expects boxed?, ...]
    // Mutable so each codegen file can register its functions
    std::unordered_map<std::string, std::vector<bool>> runtimeArgBoxing_;
    
    // Set of runtime functions that return boxed values
    std::unordered_set<std::string> runtimeReturnsBoxed_;
    
    // Static seed data for core runtime functions (Map, Set, Array, Call, Value, Console)
    static const std::unordered_map<std::string, std::vector<bool>> CORE_RUNTIME_ARG_BOXING;
    static const std::unordered_set<std::string> CORE_RUNTIME_RETURNS_BOXED;
    
    // Initialize instance maps from static seed data
    void initFromCoreRegistry();
};

} // namespace ts
