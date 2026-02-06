# HIR-005: Test Fix Plan - Batch 2

**Status:** In Progress
**Starting Pass Rate:** 82.2% (120/146)
**Current Pass Rate:** 87.7% (128/146)
**Target Pass Rate:** 90%+

## Progress Log

### 2026-02-05: Phase 3 Fixes

**Fixed (3 tests):**
1. `higher_order_function.ts` - Fixed duplicate closure parameter bug (crash)
2. `intersection_types.ts` - Fixed inline intersection param property access
3. `iife_with_closure.ts` (TypeScript) - Fixed as side effect of closure param detection

**Root Cause (higher_order_function.ts):** Arrow functions were getting duplicate closure parameters - ASTToHIR added `__closure__` to params, and then HIRToLLVM added another implicit `__closure` when `fn->captures` was non-empty. This caused LLVM verification failures.

**Root Cause (intersection_types.ts):** The trampoline function `getOrCreateTrampoline()` was incorrectly treating regular function parameters as closure context. For a function like `inlineIntersection(x: {a: number})`, the parameter `x` was treated as the closure context pointer instead of a user argument.

**Fix:**
1. In `HIRToLLVM.cpp`, check if function already has `__closure__` param before adding implicit one
2. In `getOrCreateTrampoline()`, detect functions with explicit `__closure__` param vs regular functions without

**Files Changed:**
- `src/compiler/hir/HIRToLLVM.cpp` - Multiple fixes:
  - `forwardDeclareFunction()`: Check for existing `__closure__` param
  - `lowerFunction()`: Check for existing `__closure__` param, fix parameter mapping
  - `getOrCreateTrampoline()`: Detect `__closure__` param name to determine context param count

---

### 2026-02-05: Phase 2 Fixes

**Fixed (4 tests):**
1. `array_filter.ts` - Fixed closure invoke return type handling
2. `array_find.ts` - Fixed closure invoke return type handling
3. `array_findIndex.ts` - Fixed closure invoke return type handling
4. `array_some.ts` - Fixed closure invoke return type handling

**Root Cause:** The trampoline functions generated for closures return boxed `TsValue*`, but `ts_closure_invoke_1d_bool` expected a raw `bool` return. Fixed by changing the runtime function to expect boxed returns and unbox them.

**Files Changed:**
- `src/runtime/src/TsClosure.cpp` - Updated `ts_closure_invoke_1d_bool` and `ts_closure_invoke_1d_void` to expect `TsValue* (*)(void*, TsValue*)` signature

### 2026-02-04: Phase 1 Fixes

**Fixed (2 tests):**
1. `closure_capture_string_length.ts` - Added unboxing in `lowerCallIndirect` for closure return values based on expected HIR type
2. `test_groupBy.ts` - Added primitive type boxing before `ts_object_get_property` in `lowerCallMethod`

**Not Fixed (1 test):**
3. `accessor_decorator.ts` - Compiler segfaults during HIR-to-LLVM lowering. Complex issue with decorator function parameter handling in truthiness expressions.

## Failure Categorization

### Category 1: Compilation Failures (3 tests)

| Test | Error | Root Cause | Status |
|------|-------|------------|--------|
| `closure_capture_string_length.ts` | Type mismatch: `ts_call_0` returns ptr, used as i64 for `ts_int_to_string` | Closure return type not propagated correctly | **FIXED** |
| `test_groupBy.ts` | Type mismatch: double vs ptr in callback | Method call on primitive type | **FIXED** |
| `accessor_decorator.ts` | Compiler crash | Decorator function parameter tracking issue | Open |

**Fix Approach:** These are LLVM verification failures - need to fix type handling in HIRToLLVM.

---

### Category 2: Array Callback Methods Not Working (4 tests) - **FIXED**

| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| `array_filter.ts` | `6,7,8,9,10` | `6,7,8,9,10` | **FIXED** |
| `array_find.ts` | `30` | `30` | **FIXED** |
| `array_findIndex.ts` | `2`, `-1` | `2`, `-1` | **FIXED** |
| `array_some.ts` | `true`, `false` | `true`, `false` | **FIXED** |

**Root Cause:** The trampoline functions generated for closures in `getOrCreateTrampoline()` return boxed `TsValue*` (even for bool-returning functions), but `ts_closure_invoke_1d_bool` expected a raw `bool` return type.

**Fix:** Updated `ts_closure_invoke_1d_bool` in `TsClosure.cpp` to expect `TsValue* (*)(void*, TsValue*)` signature and unbox the return value with `ts_value_get_bool()`.

---

### Category 3: Decorator Invocation Missing (6 tests)

| Test | Issue |
|------|-------|
| `class_decorator.ts` | Decorator not called at class definition time |
| `decorator_factory.ts` | Factory not invoked |
| `method_decorator.ts` | Method decorator not called |
| `parameter_decorator.ts` | Parameter decorator not called |
| `property_decorator.ts` | Property decorator not called |
| `accessor_decorator.ts` | Compilation failure (see Category 1) |

**Root Cause:** HIR pipeline isn't emitting decorator invocation code.

**Fix Approach:** Add decorator invocation in ASTToHIR or HIRToLLVM.

---

### Category 4: Object Method/Function Invocation Issues (3 tests)

| Test | Expected | Actual | Issue | Status |
|------|----------|--------|-------|--------|
| `function_in_object.ts` | Output | CRASH (0xc0000005) | Function stored in object crashes when called | Open |
| `function_overloads.ts` | Various outputs | Wrong output | Overload resolution incorrect | Open |
| `getter_setter.ts` | `10, 20` | `10, 20` | - | **FIXED** |
| `closure_mutable_capture.ts` | CHECK pattern | Pattern not found | Mutable capture codegen wrong | Open |

**Root Cause:** Functions stored in objects and object property setters have issues in HIR lowering.

---

### Category 5: Type/Property Access Issues (4 tests)

| Test | Issue | Status |
|------|-------|--------|
| `intersection_types.ts` | Inline intersection param properties return undefined | **FIXED** |
| `discriminated_unions.ts` | Union narrowing not working | Open |
| `index_signature_class.ts` | Index access returns wrong value | Open |
| `indexable_types.ts` | Index access returns wrong value | Open |
| `computed_enum.ts` | Computed enum values incorrect | Open |

**Root Cause:** Complex type scenarios not handled in HIR property access lowering.

---

### Category 6: Miscellaneous (4 tests)

| Test | Issue |
|------|-------|
| `destructured_parameters.ts` | Parameter destructuring not working |
| `regexp_exec_global.ts` | Regex exec returns 0 matches |
| `test_string_wellformed.ts` | String wellformed check failing |

---

## Prioritized Fix Plan

### Phase 1: Compilation Fixes (High Impact - Unblocks Testing)
**Tests Fixed:** 3
**New Pass Rate:** ~84%

1. **Fix closure return type propagation**
   - File: `HIRToLLVM.cpp` - `lowerCall` for closures
   - Issue: Return type from `ts_call_N` needs conversion before use

2. **Fix callback parameter typing in groupBy**
   - File: `ASTToHIR.cpp` or `HIRToLLVM.cpp`
   - Issue: Callback params typed as double instead of ptr

---

### Phase 2: Array Callback Return Values (High Impact - Core Feature)
**Tests Fixed:** 4
**Cumulative Pass Rate:** ~87%

1. **Audit array method lowering**
   - Check how `ts_array_filter`, `ts_array_find`, `ts_array_some` receive callback results
   - Ensure callback return value (bool/value) is correctly passed to runtime

2. **Verify callback invocation in ArrayHandler**
   - File: `handlers/ArrayHandler.cpp`
   - Ensure callback is called with correct arguments and result is used

---

### Phase 3: Decorator Invocation (Medium Impact - TypeScript Feature)
**Tests Fixed:** 5
**Cumulative Pass Rate:** ~90%

1. **Add decorator invocation in ASTToHIR**
   - Emit HIR instructions to call decorator at class/method definition
   - Handle decorator factories (call factory first, then apply result)

---

### Phase 4: Object Function/Setter Issues (Medium Impact)
**Tests Fixed:** 3-4
**Cumulative Pass Rate:** ~93%

1. **Fix function-in-object invocation**
   - Debug crash when calling function stored in object property
   - Likely issue with function pointer resolution

2. **Fix object setters**
   - Setter return value being stored instead of being applied

---

### Phase 5: Type/Property Access (Lower Priority)
**Tests Fixed:** 4-5
**Cumulative Pass Rate:** ~95%+

1. Fix intersection type parameter property access
2. Fix discriminated union narrowing
3. Fix index signature access

---

## Recommended First Steps

1. **Start with Phase 1** - Fix the 3 compilation errors. This is the lowest-hanging fruit.

2. **Then Phase 2** - Array callbacks are a core feature. Fixing filter/find/some will have high impact on real-world code.

3. **Phase 3-5** - Can be done in parallel or deferred based on priority.

## Files to Investigate

For compilation errors:
- [HIRToLLVM.cpp](../src/compiler/hir/HIRToLLVM.cpp) - Type handling in `lowerCall`

For array callbacks:
- [ArrayHandler.cpp](../src/compiler/hir/handlers/ArrayHandler.cpp) - Method lowering
- [TsArray.cpp](../src/runtime/src/TsArray.cpp) - Runtime implementation

For decorators:
- [ASTToHIR.cpp](../src/compiler/hir/ASTToHIR.cpp) - AST to HIR conversion
- Search for decorator handling

## Success Metrics

| Phase | Tests | Pass Rate | Status |
|-------|-------|-----------|--------|
| Starting | 120/146 | 82.2% | - |
| After Phase 1 | 122/146 | 83.6% | **DONE** |
| After Phase 2 | 125/146 | 85.6% | **DONE** |
| After Phase 3 | 128/146 | 87.7% | **DONE** |
| After Phase 4 | ~132/146 | ~90% | Pending |
| After Phase 5 | ~137/146 | ~94% | Pending |
