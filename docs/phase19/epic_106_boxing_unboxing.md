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

### Milestone 106.1: Document Current State

- [ ] **Task 106.1.1:** Audit all `ts_*` C functions and document expected types
- [ ] **Task 106.1.2:** Create `docs/runtime/boxing_contract.md` with full API reference
- [ ] **Task 106.1.3:** Add comments to runtime headers marking boxed vs raw params
- [ ] **Task 106.1.4:** Document `boxedValues` and `boxedVariables` set semantics

### Milestone 106.2: Unit Test Infrastructure

- [ ] **Task 106.2.1:** Create `tests/codegen/` directory for IR generation tests
- [ ] **Task 106.2.2:** Implement test harness that compiles TS snippets and checks IR
- [ ] **Task 106.2.3:** Add boxing contract test cases:
  - [ ] Map.set with primitive key → key is boxed
  - [ ] Map.get result → result is unboxed to expected type
  - [ ] Array.push on int[] → value is raw
  - [ ] Array.push on any[] → value is boxed
  - [ ] Callback passed to user function → function is boxed
  - [ ] Closure captures function → no double-boxing
  - [ ] ts_call_1 argument → boxed
  - [ ] Direct typed function call → raw parameters
- [ ] **Task 106.2.4:** Add runtime unit tests for boxing helpers:
  - [ ] `ts_value_make_int` / `ts_value_get_int` round-trip
  - [ ] `ts_value_make_double` / `ts_value_get_double` round-trip
  - [ ] `ts_value_make_string` / `ts_value_get_string` round-trip
  - [ ] `ts_value_make_object` / `ts_value_get_object` round-trip
  - [ ] `ts_value_make_function` / extraction in ts_call_N

### Milestone 106.3: Codegen Refactoring

- [ ] **Task 106.3.1:** Create `BoxingHelper` class to centralize boxing decisions
- [ ] **Task 106.3.2:** Replace ad-hoc boxing code with `BoxingHelper` calls
- [ ] **Task 106.3.3:** Add assertions in debug builds for boxing invariants
- [ ] **Task 106.3.4:** Rename `boxedValues` → `valuesKnownToBeBoxed` for clarity
- [ ] **Task 106.3.5:** Add `isBoxed(llvm::Value*)` method that's authoritative

### Milestone 106.4: Integration Tests

- [ ] **Task 106.4.1:** Create `examples/boxing_tests/` with targeted test cases
- [ ] **Task 106.4.2:** Test: Map<number, number> operations
- [ ] **Task 106.4.3:** Test: Map<string, Array<number>> operations
- [ ] **Task 106.4.4:** Test: Higher-order functions with callbacks (map, filter, reduce)
- [ ] **Task 106.4.5:** Test: Closures capturing primitives
- [ ] **Task 106.4.6:** Test: Closures capturing functions
- [ ] **Task 106.4.7:** Test: Nested closures
- [ ] **Task 106.4.8:** Test: Generic functions with type inference

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
