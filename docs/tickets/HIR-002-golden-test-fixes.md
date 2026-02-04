# HIR-002: Golden IR Test Fixes Plan

**Status:** Planning
**Date:** 2026-02-03
**Current Pass Rate:** 122/146 (83.6%)
**Target Pass Rate:** 140+/146 (95%+)

## Summary

24 tests are currently failing. This document categorizes them by root cause and provides a prioritized fix plan.

---

## Failure Categories

### Category 1: Debug Output Pollution (2 tests)
**Root Cause:** `lowerSetPropStatic` debug message printed to stderr causes test runner to detect compilation "failure"

**Tests:**
- `ecmascript/es2024/test_groupBy.ts`
- `ecmascript/es2024/test_promise_withResolvers.ts`

**Fix:** Remove or guard the debug print statement in HIRToLLVM.cpp:
```cpp
// SPDLOG_DEBUG("lowerSetPropStatic: prop={} valHirType={}", prop, valHirType);
```

**Risk:** None - just removes noise
**Effort:** Low (5 min)
**Priority:** HIGH (easy win)

---

### Category 2: Method/Function Calls on Object Properties (4 tests)
**Root Cause:** Methods stored in object literals crash when called - the function value retrieved from the object property isn't being treated as callable

**Tests:**
- `typescript/objects/method_shorthand.ts` - `obj.getValue()` crashes
- `typescript/functions/function_in_object.ts` - `obj.double(7)` returns wrong value
- `typescript/functions/higher_order_function.ts` - closure call fails
- `javascript/closures/iife_with_call.js` - `.call(obj)` returns undefined

**Root Analysis:**
- Object literal method shorthand `{ getValue() {} }` stores a function pointer
- When calling `obj.getValue()`, we need to:
  1. Get the property (returns boxed closure)
  2. Unbox it to get the function pointer
  3. Call it with correct `this` binding

**Fix Locations:**
- `src/compiler/hir/HIRToLLVM.cpp` - method call lowering
- `src/compiler/hir/ASTToHIR.cpp` - property access + call sequence

**Risk:** Medium - affects all method calls on objects
**Effort:** Medium (1-2 hours)
**Priority:** HIGH (common pattern)

---

### Category 3: Decorator Execution Order (6 tests)
**Root Cause:** Class decorators not being called at class definition time

**Tests:**
- `typescript/decorators/accessor_decorator.ts`
- `typescript/decorators/class_decorator.ts`
- `typescript/decorators/decorator_factory.ts`
- `typescript/decorators/method_decorator.ts`
- `typescript/decorators/parameter_decorator.ts`
- `typescript/decorators/property_decorator.ts`

**Root Analysis:**
- Decorators should execute when the class is defined, not when instantiated
- The decorator function (`@log`) receives the class constructor
- Need to emit decorator calls in module init or before first class use

**Fix Locations:**
- `src/compiler/hir/ASTToHIR.cpp` - visitClassDeclaration
- May need module-level initialization sequence

**Risk:** Medium - needs careful ordering of initialization code
**Effort:** High (2-4 hours)
**Priority:** MEDIUM (decorator pattern less common)

---

### Category 4: hasOwnProperty Returns False (1 test)
**Root Cause:** `hasOwnProperty()` doesn't check the TsMap properties

**Tests:**
- `javascript/property_access/has_property.js`

**Root Analysis:**
- `obj.hasOwnProperty('foo')` should check if 'foo' exists in the object's own properties
- Current implementation may be checking prototype chain or returning false always

**Fix Locations:**
- `src/runtime/src/TsObject.cpp` - `ts_object_has_own_property()`

**Risk:** Low - isolated runtime fix
**Effort:** Low (30 min)
**Priority:** HIGH (easy win)

---

### Category 5: IIFE Issues (3 tests)
**Root Cause:** IIFEs with `this` binding or in loop contexts fail

**Tests:**
- `javascript/closures/iife_in_loop.js` - Exit code 1
- `javascript/closures/module_pattern.js` - Returns undefined
- `javascript/closures/umd_pattern.js` - Exit code 1

**Root Analysis:**
- `(function() {}).call(obj)` doesn't properly bind `this`
- IIFEs in loops may have closure capture issues
- Module pattern (`(function() { var x; return { getX: function() { return x; } } })()`) fails

**Fix Locations:**
- `src/compiler/hir/ASTToHIR.cpp` - IIFE detection and handling
- `src/compiler/hir/HIRToLLVM.cpp` - Function.prototype.call binding

**Risk:** Medium - affects many patterns
**Effort:** Medium (1-2 hours)
**Priority:** MEDIUM

---

### Category 6: Computed Enum Values (1 test)
**Root Cause:** Computed enum members not evaluated at compile time

**Tests:**
- `typescript/computed_enum.ts` - Returns 1 (verification fails)

**Root Analysis:**
- `enum { A = 1 + 2, B = "hello".length }` should compute values at compile time
- `Math.floor(3.7)` in enum needs compile-time evaluation
- Member references like `D = A * 2` need to resolve `A` first

**Fix Locations:**
- `src/compiler/analysis/Analyzer.cpp` - enum value computation
- May need constant folding pass

**Risk:** Low - isolated to enums
**Effort:** Medium (1 hour)
**Priority:** LOW (edge case)

---

### Category 7: Function Features (4 tests)
**Root Cause:** Various function-related issues

**Tests:**
- `typescript/functions/destructured_parameters.ts` - Parameter destructuring
- `typescript/functions/function_overloads.ts` - Overload resolution

**Root Analysis:**
- Destructured parameters `function foo({x, y})` need to extract properties
- Function overloads need correct signature matching

**Fix Locations:**
- `src/compiler/hir/ASTToHIR.cpp` - parameter handling

**Risk:** Medium
**Effort:** Medium (1-2 hours)
**Priority:** MEDIUM

---

### Category 8: Index Signature Classes (2 tests)
**Root Cause:** Classes with index signatures don't work properly

**Tests:**
- `typescript/index_signature_class.ts`
- `typescript/indexable_types.ts`

**Root Analysis:**
- `class Foo { [key: string]: any }` should allow dynamic property access
- Need to route through TsMap for dynamic keys

**Fix Locations:**
- `src/compiler/hir/ASTToHIR.cpp` - class element handling

**Risk:** Medium
**Effort:** Medium (1 hour)
**Priority:** LOW

---

### Category 9: Discriminated Unions (1 test)
**Root Cause:** Switch narrowing on discriminant property fails

**Tests:**
- `typescript/narrowing/discriminated_unions.ts`

**Root Analysis:**
- `switch (shape.kind)` should narrow `shape` to correct type
- Each case should have access to type-specific properties

**Fix Locations:**
- `src/compiler/analysis/Analyzer.cpp` - type narrowing
- `src/compiler/hir/ASTToHIR.cpp` - switch statement handling

**Risk:** Medium
**Effort:** High (2-3 hours)
**Priority:** MEDIUM

---

### Category 10: RegExp exec() with Global Flag (1 test)
**Root Cause:** Global flag regex exec() doesn't iterate properly

**Tests:**
- `typescript/regression/regexp_exec_global.ts`

**Root Analysis:**
- `pattern.exec(text)` with `/g` flag should advance `lastIndex`
- While loop should find all matches, not just first
- Runtime needs to track and update `lastIndex` property

**Fix Locations:**
- `src/runtime/src/TsRegex.cpp` - exec() implementation
- Need to store/update lastIndex on regex object

**Risk:** Low - isolated to regex
**Effort:** Medium (1 hour)
**Priority:** MEDIUM

---

### Category 11: Benchmark Timing (1 test)
**Root Cause:** `performance.now()` returns 0 or same value

**Tests:**
- `typescript/regression/benchmark_timing.ts`

**Root Analysis:**
- `elapsed = performance.now() - start` should be positive after work
- Either performance.now() not implemented or returning constant

**Fix Locations:**
- `src/runtime/src/TsPerformance.cpp` - now() implementation

**Risk:** Low
**Effort:** Low (30 min)
**Priority:** LOW (nice to have)

---

## Implementation Plan

### Phase 1: Quick Wins (1 hour)
1. **Remove debug output** - Fix Category 1 (2 tests)
2. **Fix hasOwnProperty** - Fix Category 4 (1 test)
3. **Fix performance.now** - Fix Category 11 (1 test)

**Expected improvement:** 122 → 126 tests passing (86.3%)

### Phase 2: Method Calls (2 hours)
1. **Fix object literal method calls** - Fix Category 2 (4 tests)
2. **Fix Function.prototype.call** - Partial fix for Category 5

**Expected improvement:** 126 → 130+ tests passing (89%+)

### Phase 3: IIFE and Closures (2 hours)
1. **Fix IIFE patterns** - Fix remaining Category 5 (3 tests)
2. **Fix higher-order function returns**

**Expected improvement:** 130 → 133+ tests passing (91%+)

### Phase 4: Decorators (3 hours)
1. **Implement decorator execution at definition time** - Fix Category 3 (6 tests)

**Expected improvement:** 133 → 139+ tests passing (95%+)

### Phase 5: Remaining Issues (4 hours)
1. Fix computed enums (Category 6)
2. Fix function features (Category 7)
3. Fix index signatures (Category 8)
4. Fix discriminated unions (Category 9)
5. Fix regex global exec (Category 10)

**Expected improvement:** 139 → 144+ tests passing (98%+)

---

## Regression Prevention

Before fixing each category:
1. Run full test suite to establish baseline
2. Create minimal reproduction test in `tmp/`
3. Fix the issue
4. Run full test suite to verify no regressions
5. Move to next category

### Key Files to Test After Each Fix

After HIR changes:
- All array tests (`tests/golden_ir/typescript/arrays/`)
- All function tests (`tests/golden_ir/typescript/functions/`)
- All object tests (`tests/golden_ir/typescript/objects/`)

After Runtime changes:
- Run `tests/node/run_tests.py` for Node.js API conformance

---

## Files to Modify

| Category | Files |
|----------|-------|
| Debug output | `src/compiler/hir/HIRToLLVM.cpp` |
| Method calls | `src/compiler/hir/HIRToLLVM.cpp`, `ASTToHIR.cpp` |
| Decorators | `src/compiler/hir/ASTToHIR.cpp` |
| hasOwnProperty | `src/runtime/src/TsObject.cpp` |
| RegExp exec | `src/runtime/src/TsRegex.cpp` |
| performance.now | `src/runtime/src/TsPerformance.cpp` |
| Computed enums | `src/compiler/analysis/Analyzer.cpp` |
| Discriminated unions | `src/compiler/analysis/Analyzer.cpp` |
