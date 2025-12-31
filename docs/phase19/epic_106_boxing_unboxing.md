# Epic 106: Boxing/Unboxing Contract & Test Infrastructure

**Status:** Proposed
**Priority:** CRITICAL - Blocking all other work
**Parent:** [Phase 19 Meta Epic](./meta_epic.md)

## Problem Statement

We keep breaking things because there's no clear contract for when values should be boxed (wrapped in `TsValue*`) vs unboxed (raw primitives/pointers). Every time we fix one thing, we break another. This is unsustainable.

### Recent Casualties
1. Fixed Map.get → broke sortBy (double-boxing)
2. Fixed function argument boxing → broke closure capture
3. Fixed closure capture → who knows what's next

### Root Causes
1. **No documented contract** - Developers guess whether to box/unbox
2. **No unit tests** - We only discover breakage when example programs crash
3. **Inconsistent patterns** - Some runtime functions expect boxed, some expect raw
4. **Tracking is ad-hoc** - `boxedValues` set is incomplete and cleared incorrectly
5. **Two worlds collide** - TypeScript-optimized code (raw values) meets runtime C API (boxed values)

---

## The Boxing Contract (PROPOSED)

### Core Principle: TypeScript Types Drive Optimization

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         BOXING DECISION TREE                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Is this a call to a RUNTIME C FUNCTION (ts_* API)?                    │
│     │                                                                   │
│     ├── YES: Does the C function signature expect TsValue*?            │
│     │         │                                                         │
│     │         ├── YES: Box the argument                                │
│     │         │         Examples: ts_map_set(map, KEY, VALUE)          │
│     │         │                   ts_call_1(FUNC, ARG)                 │
│     │         │                                                         │
│     │         └── NO: Pass raw value                                   │
│     │                  Examples: ts_array_push(arr, raw_int64)         │
│     │                            ts_string_length(str)                 │
│     │                                                                   │
│     └── NO: Is this a call to a USER-DEFINED function?                 │
│              │                                                          │
│              ├── Is the parameter type known (TypeScript typed)?       │
│              │    │                                                     │
│              │    ├── YES: Pass raw value (optimization!)              │
│              │    │        The callee knows the type at compile time   │
│              │    │                                                     │
│              │    └── NO (type is `any`): Box the value               │
│              │         The callee needs runtime type info              │
│              │                                                          │
│              └── Is the argument a function/callback?                  │
│                   │                                                     │
│                   ├── Will callee invoke via ts_call_N? → Box it       │
│                   └── Will callee invoke directly? → Pass raw pointer  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Rule 1: Runtime C API Boundaries

| C Function | Parameter | Expects | Why |
|------------|-----------|---------|-----|
| `ts_map_set` | key | `TsValue*` | Keys need type info for hashing/equality |
| `ts_map_set` | value | `TsValue*` | Values are stored generically |
| `ts_map_get` | key | `TsValue*` | Lookup needs type-aware comparison |
| `ts_call_1` | func | `TsValue*` | Contains function pointer + closure |
| `ts_call_1` | arg | `TsValue*` | Callee extracts with ts_value_get_* |
| `ts_array_push` | value | `int64_t` or `TsValue*` | Depends on array specialization |
| `ts_console_log_int` | value | `int64_t` | Optimized primitive path |

### Rule 2: Specialized Array Operations

```cpp
// SPECIALIZED arrays (double[], int[], string[]) - store RAW values
ts_array_push(arr, raw_value);           // Raw int64/double/pointer
element = arr->elements[i];               // Raw value out

// GENERIC arrays (any[]) - store BOXED values
ts_array_push(arr, boxed_value);         // TsValue*
element = ts_value_get_*(arr->Get(i));   // Unbox on retrieval
```

### Rule 3: User Function Calls

```cpp
// TYPED parameters - pass RAW for optimization
function add(a: number, b: number): number  // a, b are raw doubles
call add_T_dbl_dbl(rawA, rawB);

// UNTYPED parameters (any) - pass BOXED
function process(x: any): any               // x is TsValue*
call process(boxedX);

// CALLBACK parameters - ALWAYS BOX
function map(arr, fn: (x) => y)             // fn must be boxed
call map(arr, boxedFn);                     // so ts_call_1 can invoke it
```

### Rule 4: Closure Capture

```cpp
// When capturing a variable into a closure environment:

// Already a pointer (TsValue*, TsString*, TsArray*, function ptr)
//   AND the type is TypeKind::Function
//   → The function was ALREADY BOXED at the call site
//   → Do NOT re-box! Store the pointer directly

// Primitive values (int, double, bool)
//   → Box them for storage in the closure environment
```

### Rule 5: Return Values

```cpp
// From SPECIALIZED functions (monomorphized with concrete types)
//   → Return RAW value matching the return type

// From GENERIC functions (any return type)
//   → Return TsValue* (boxed)

// Crossing C API boundary (runtime → generated code callback)
//   → Runtime always returns TsValue*, caller unboxes if needed
```

---

## Implementation Plan

### Milestone 106.1: BoxingPolicy Class Implementation

- [x] **Task 106.1.1:** Create `BoxingPolicy.h` with `BoxingContext` enum and `Decision` struct ✅
- [x] **Task 106.1.2:** Implement runtime API registry (`runtimeExpectsBoxed`) ✅
- [x] **Task 106.1.3:** Implement `prepareForContext()` core logic ✅
- [ ] **Task 106.1.4:** Implement `isAlreadyBoxed()` tracking
- [x] **Task 106.1.5:** Add BoxingPolicy to IRGenerator as member ✅
- [x] **Task 106.1.6:** Add to CMakeLists.txt ✅

### Milestone 106.1b: Compile-Time Enforcement

- [x] **Task 106.1b.1:** Add `hasRuntimeApiRegistered()` to check if a function is in registry ✅
- [x] **Task 106.1b.2:** Add `assertRuntimeApiRegistered()` to throw helpful error for unregistered functions ✅
- [x] **Task 106.1b.3:** Add `strictMode` parameter to `runtimeExpectsBoxed()` ✅
- [x] **Task 106.1b.4:** Add unit tests for enforcement (hasRuntimeApiRegistered, assertRuntimeApiRegistered, strictMode) ✅
- [x] **Task 106.1b.5:** Add `getRuntimeFunction()` helper to IRGenerator that enforces registry ✅
- [x] **Task 106.1b.6:** Per-file registration for all 12 codegen files (371 functions total) ✅
- [x] **Task 106.1b.7:** Enable strict enforcement in `getRuntimeFunction()` ✅
- [ ] **Task 106.1b.8:** Migrate remaining `getOrInsertFunction()` calls to `getRuntimeFunction()` (optional)

### Milestone 106.2: Unit Tests for BoxingPolicy

- [x] **Task 106.2.1:** Create `tests/unit/BoxingPolicyTests.cpp` ✅
- [x] **Task 106.2.2:** Test runtime API expectations (ts_map_set, ts_call_1, etc.) ✅
- [x] **Task 106.2.3:** Test user function arg decisions (typed vs any vs callback) ✅
- [x] **Task 106.2.4:** Test array store decisions (specialized vs generic) ✅
- [x] **Task 106.2.5:** Test closure capture decisions (already boxed vs not) ✅
- [x] **Task 106.2.6:** Test unboxing decisions (RuntimeApiReturn context) ✅

### Milestone 106.3: Integrate BoxingPolicy into Codegen

- [ ] **Task 106.3.1:** Refactor `IRGenerator_Expressions_Calls.cpp` to use BoxingPolicy
- [ ] **Task 106.3.2:** Refactor `IRGenerator_Expressions_Calls_Builtin.cpp` (Map/Set ops)
- [ ] **Task 106.3.3:** Refactor `IRGenerator_Functions.cpp` (closure capture)
- [ ] **Task 106.3.4:** Refactor `IRGenerator_Core.cpp` (boxValue/unboxValue)
- [ ] **Task 106.3.5:** Remove ad-hoc `boxedValues` tracking, use BoxingPolicy

### Milestone 106.4: Integration Tests

- [x] **Task 106.4.1:** Verify all existing examples still work ✅
- [x] **Task 106.4.2:** Verify groupBy_test.ts passes ✅
- [ ] **Task 106.4.3:** Verify sortBy_test.ts passes
- [x] **Task 106.4.4:** Verify map_set_test.ts passes ✅
- [ ] **Task 106.4.5:** Add new edge case tests

---

## BoxingPolicy Design

### Core Architecture

```cpp
// src/compiler/codegen/BoxingPolicy.h

enum class BoxingContext {
    // Where is this value being used?
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

class BoxingPolicy {
public:
    struct Decision {
        bool needsBoxing;
        bool needsUnboxing;
        std::string boxingFunction;    // e.g., "ts_value_make_int"
        std::string unboxingFunction;  // e.g., "ts_value_get_int"
    };
    
    // THE SINGLE SOURCE OF TRUTH
    Decision decide(
        Type* valueType,
        BoxingContext context,
        const std::string& runtimeFunc = "",  // For RuntimeApiArg
        int argIndex = -1,                     // For RuntimeApiArg
        Type* targetType = nullptr,            // For UserFunctionArg
        bool isAlreadyBoxed = false            // Current boxing state
    );
    
    // Query methods
    bool runtimeExpectsBoxed(const std::string& funcName, int argIndex);
    bool runtimeReturnsBoxed(const std::string& funcName);
    
    // Boxing function selection based on type
    static std::string getBoxingFunction(Type* type);
    static std::string getUnboxingFunction(Type* type);
};
```

### Runtime API Registry

```cpp
// EXPLICIT REGISTRY - no guessing
static const std::unordered_map<std::string, std::vector<bool>> RUNTIME_ARG_BOXING = {
    // Function name -> [arg0 expects boxed?, arg1 expects boxed?, ...]
    
    // Map operations - keys and values are ALWAYS boxed
    {"ts_map_set",     {false, true, true}},   // (map, key, value)
    {"ts_map_get",     {false, true}},          // (map, key)
    {"ts_map_has",     {false, true}},          // (map, key)
    {"ts_map_delete",  {false, true}},          // (map, key)
    
    // Set operations - values are ALWAYS boxed
    {"ts_set_add",     {false, true}},          // (set, value)
    {"ts_set_has",     {false, true}},          // (set, value)
    {"ts_set_delete",  {false, true}},          // (set, value)
    
    // Dynamic function calls - ALL args boxed
    {"ts_call_0",      {true}},                 // (func)
    {"ts_call_1",      {true, true}},           // (func, arg0)
    {"ts_call_2",      {true, true, true}},     // (func, arg0, arg1)
    {"ts_call_3",      {true, true, true, true}},
    
    // Array operations - RAW values for specialized arrays
    {"ts_array_push",           {false, false}}, // (arr, value) - raw
    {"ts_array_create_specialized", {false, false, false}}, // all raw
    
    // Console - optimized paths use raw
    {"ts_console_log_int",    {false}},
    {"ts_console_log_double", {false}},
    {"ts_console_log",        {false}},  // TsString* is raw pointer
    
    // Value boxing functions - these CREATE boxed values
    {"ts_value_make_int",     {false}},  // raw int -> boxed
    {"ts_value_make_double",  {false}},  // raw double -> boxed
    {"ts_value_make_bool",    {false}},  // raw bool -> boxed
    {"ts_value_make_string",  {false}},  // raw TsString* -> boxed
    {"ts_value_make_object",  {false}},  // raw pointer -> boxed
    {"ts_value_make_function",{false, false}}, // raw fn ptr, raw closure
    
    // Value unboxing functions - these CONSUME boxed values
    {"ts_value_get_int",      {true}},   // boxed -> raw int
    {"ts_value_get_double",   {true}},   // boxed -> raw double
    {"ts_value_get_bool",     {true}},   // boxed -> raw bool
    {"ts_value_get_string",   {true}},   // boxed -> raw TsString*
    {"ts_value_get_object",   {true}},   // boxed -> raw pointer
};

static const std::unordered_set<std::string> RUNTIME_RETURNS_BOXED = {
    "ts_map_get",
    "ts_value_make_int",
    "ts_value_make_double",
    "ts_value_make_bool",
    "ts_value_make_string",
    "ts_value_make_object",
    "ts_value_make_function",
    "ts_value_make_undefined",
    "ts_call_0", "ts_call_1", "ts_call_2", "ts_call_3",
};
```

### Decision Logic

```cpp
Decision BoxingPolicy::decide(
    Type* valueType,
    BoxingContext context,
    const std::string& runtimeFunc,
    int argIndex,
    Type* targetType,
    bool isAlreadyBoxed
) {
    Decision d = {false, false, "", ""};
    
    switch (context) {
        case BoxingContext::RuntimeApiArg: {
            bool expectsBoxed = runtimeExpectsBoxed(runtimeFunc, argIndex);
            if (expectsBoxed && !isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }
        
        case BoxingContext::RuntimeApiReturn: {
            bool returnsBoxed = runtimeReturnsBoxed(runtimeFunc);
            if (returnsBoxed && targetType && targetType->kind != TypeKind::Any) {
                d.needsUnboxing = true;
                d.unboxingFunction = getUnboxingFunction(targetType);
            }
            break;
        }
        
        case BoxingContext::UserFunctionArg: {
            // Typed parameters = raw (optimization)
            // Any parameters = boxed (needs runtime type info)
            // Function callbacks = boxed (for ts_call_N invocation)
            if (!targetType || targetType->kind == TypeKind::Any) {
                if (!isAlreadyBoxed) {
                    d.needsBoxing = true;
                    d.boxingFunction = getBoxingFunction(valueType);
                }
            } else if (valueType->kind == TypeKind::Function) {
                // Callbacks MUST be boxed so callee can use ts_call_N
                if (!isAlreadyBoxed) {
                    d.needsBoxing = true;
                    d.boxingFunction = "ts_value_make_function";
                }
            }
            // else: typed param, pass raw
            break;
        }
        
        case BoxingContext::MapKey:
        case BoxingContext::MapValue: {
            // Map always stores boxed values
            if (!isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }
        
        case BoxingContext::ArrayStore: {
            // Check if array is specialized (int[], double[], string[])
            // Specialized = raw, Generic (any[]) = boxed
            // This requires knowing the array's element type
            // For now, caller must check and use appropriate context
            break;
        }
        
        case BoxingContext::ClosureCapture: {
            // Functions that are already boxed: don't double-box
            if (valueType->kind == TypeKind::Function && isAlreadyBoxed) {
                // Already boxed - store directly
                break;
            }
            // Everything else: box for uniform storage
            if (!isAlreadyBoxed) {
                d.needsBoxing = true;
                d.boxingFunction = getBoxingFunction(valueType);
            }
            break;
        }
        
        case BoxingContext::ClosureExtract: {
            // Extract and unbox based on expected type
            if (targetType && targetType->kind != TypeKind::Function) {
                d.needsUnboxing = true;
                d.unboxingFunction = getUnboxingFunction(targetType);
            }
            break;
        }
    }
    
    return d;
}

std::string BoxingPolicy::getBoxingFunction(Type* type) {
    switch (type->kind) {
        case TypeKind::Int:     return "ts_value_make_int";
        case TypeKind::Double:  return "ts_value_make_double";
        case TypeKind::Boolean: return "ts_value_make_bool";
        case TypeKind::String:  return "ts_value_make_string";
        case TypeKind::Function: return "ts_value_make_function";
        default:                return "ts_value_make_object";
    }
}

std::string BoxingPolicy::getUnboxingFunction(Type* type) {
    switch (type->kind) {
        case TypeKind::Int:     return "ts_value_get_int";
        case TypeKind::Double:  return "ts_value_get_double";
        case TypeKind::Boolean: return "ts_value_get_bool";
        case TypeKind::String:  return "ts_value_get_string";
        default:                return "ts_value_get_object";
    }
}
```

### Usage in Codegen

```cpp
// BEFORE (scattered, error-prone):
if (argType->kind == TypeKind::Function) {
    llvm::Value* boxedFn = boxValue(argValue, argType);
    boxedValues.insert(boxedFn);
    args.push_back(boxedFn);
} else if (isAnyType(paramType)) {
    // ... more ad-hoc logic
}

// AFTER (centralized, deterministic):
auto decision = boxingPolicy.decide(
    argType,
    BoxingContext::UserFunctionArg,
    "",           // not a runtime func
    -1,           // no arg index
    paramType,    // declared parameter type
    boxedValues.count(argValue) > 0  // is already boxed?
);

llvm::Value* finalArg = argValue;
if (decision.needsBoxing) {
    finalArg = createCall(decision.boxingFunction, {argValue});
    boxedValues.insert(finalArg);
}
args.push_back(finalArg);
```

---

## Quick Reference: Boxing Patterns

### Pattern A: Calling Runtime API with Boxed Args
```cpp
// In IRGenerator when calling ts_map_set:
llvm::Value* boxedKey = boxValue(key, keyType);    // ALWAYS box
llvm::Value* boxedValue = boxValue(val, valType);  // ALWAYS box
builder->CreateCall(ts_map_set, {map, boxedKey, boxedValue});
```

### Pattern B: Retrieving from Runtime API
```cpp
// In IRGenerator when calling ts_map_get:
llvm::Value* boxedKey = boxValue(key, keyType);
llvm::Value* boxedResult = builder->CreateCall(ts_map_get, {map, boxedKey});
// Unbox based on EXPECTED type from TypeScript
llvm::Value* rawResult = unboxValue(boxedResult, expectedType);
```

### Pattern C: Calling User Function with Typed Params
```cpp
// In IRGenerator for call to typed user function:
// DO NOT box - pass raw values for optimization
builder->CreateCall(userFunc, {rawArg1, rawArg2});
```

### Pattern D: Passing Callback to User Function
```cpp
// In IRGenerator when arg is a function type:
llvm::Value* boxedFn = boxValue(fnPtr, fnType);  // MUST box
boxedValues.insert(boxedFn);                      // Track it
builder->CreateCall(higherOrderFn, {arr, boxedFn});
```

### Pattern E: Capturing in Closure
```cpp
// In IRGenerator for closure capture:
if (capturedType->kind == TypeKind::Function && llvmType->isPointerTy()) {
    // Already boxed at call site - store directly
    storeToEnv(capturedValue);
} else if (isPrimitive(capturedType)) {
    // Box primitive for storage
    storeToEnv(boxValue(capturedValue, capturedType));
}
```

---

## Anti-Patterns (DON'T DO THIS)

### Anti-Pattern 1: Double Boxing
```cpp
// WRONG: Boxing an already-boxed value
llvm::Value* boxed = boxValue(fnPtr, fnType);
llvm::Value* doubleBoxed = boxValue(boxed, fnType);  // BUG!
```

### Anti-Pattern 2: Boxing for Specialized Arrays
```cpp
// WRONG: Boxing when pushing to int[]
llvm::Value* boxedInt = boxValue(rawInt, intType);
ts_array_push(intArray, boxedInt);  // BUG! Should be raw
```

### Anti-Pattern 3: Forgetting to Unbox Runtime Results
```cpp
// WRONG: Using boxed result directly as raw value
llvm::Value* boxed = ts_map_get(map, key);
useAsInt(boxed);  // BUG! Need ts_value_get_int first
```

### Anti-Pattern 4: Passing Raw Pointer as Boxed
```cpp
// WRONG: Passing raw function pointer to ts_call_1
llvm::Value* fnPtr = getFunctionPointer();
ts_call_1(fnPtr, arg);  // BUG! ts_call_1 expects TsValue*
```

---

## Success Criteria

### Phase 1 Complete When:
- [ ] Boxing contract documented in code comments
- [ ] Unit tests cover all boxing patterns
- [ ] No test regressions when modifying boxing code

### Phase 2 Complete When:
- [ ] BoxingHelper class centralizes all decisions
- [ ] Debug assertions catch violations
- [ ] All lodash functions work without boxing bugs

---

## Files to Modify

| File | Changes |
|------|---------|
| `IRGenerator_Core.cpp` | Extract boxing into BoxingHelper |
| `IRGenerator_Expressions_Calls.cpp` | Use BoxingHelper for call args |
| `IRGenerator_Expressions_Calls_Builtin.cpp` | Use BoxingHelper for Map/Set |
| `IRGenerator_Functions.cpp` | Use BoxingHelper for closure capture |
| `TsObject.h` | Document TsValue structure |
| `TsObject.cpp` | Add debug assertions |
| `tests/unit/BoxingTests.cpp` | New unit tests |
| `tests/codegen/BoxingCodegenTests.cpp` | New codegen tests |

---

## References

- Issue: sortBy broke after Map fix
- Issue: groupBy double-boxing in closures
- Commit 049d3b8: "Fix groupBy function - function argument boxing"
