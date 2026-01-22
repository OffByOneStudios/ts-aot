# CONF-002: Union Type and Iterator Fixes

**Status:** Planning
**Category:** TypeScript
**Priority:** High
**Created:** 2026-01-22

## Summary

Phase 2 conformance work uncovered several bugs related to union types and iterators that need to be fixed to improve TypeScript conformance.

## Issues Discovered

### Issue 1: Union Type Parameters Crash Compiler

**Severity:** Critical
**Feature:** Union types (`|`)

**Description:**
Function parameters with union types like `string | number` cause a segmentation fault during compilation.

**Reproduction:**
```typescript
function processValue(value: string | number): string {
    if (typeof value === "string") {
        return "String: " + value;
    } else {
        return "Number: " + value;
    }
}
```

**Error:**
```
Segmentation fault (exit code 139)
```

**Location:** Likely in `Analyzer_Types.cpp` or type parsing code

---

### Issue 2: Null Comparison Bug in Union Types

**Severity:** High
**Feature:** Union types, Equality narrowing

**Description:**
When a parameter has type `string | null`, comparing with `=== null` doesn't work correctly - returns `[object Object]` instead of detecting null.

**Reproduction:**
```typescript
function greet(name: string | null): string {
    if (name === null) {
        return "Hello, stranger!";  // Never reached
    }
    return "Hello, " + name + "!";
}

greet(null);  // Returns "Hello, [object Object]!" instead of "Hello, stranger!"
```

**Note:** Union types in index signatures work correctly:
```typescript
const obj: { [key: string]: number | undefined } = { undef: undefined };
// This works fine
```

**Location:** Likely in `IRGenerator_Expressions_Binary.cpp` (equality comparison codegen)

---

### Issue 3: Custom Iterables Codegen Error

**Severity:** Medium
**Feature:** Iterable protocol, Symbol.iterator

**Description:**
Custom classes implementing `[Symbol.iterator]` fail with an LLVM IR verification error about invalid bitcast.

**Reproduction:**
```typescript
class Range {
    private start: number;
    private end: number;

    constructor(start: number, end: number) {
        this.start = start;
        this.end = end;
    }

    [Symbol.iterator](): Iterator<number> {
        let current = this.start;
        const end = this.end;
        return {
            next(): IteratorResult<number> {
                if (current <= end) {
                    return { value: current++, done: false };
                }
                return { value: undefined as any, done: true };
            }
        };
    }
}

for (const num of new Range(1, 5)) {
    console.log(num);
}
```

**Error:**
```
LLVM Module verification failed: Invalid bitcast
  %14 = bitcast ptr %num3 to i64
```

**Note:** Using generators with for-of works. The issue is specific to custom iterator objects.

**Location:** `IRGenerator_Statements.cpp` (for-of loop codegen)

---

### Issue 4: yield* Delegation with for-of

**Severity:** Medium
**Feature:** yield* delegation

**Description:**
When using `yield*` to delegate to another generator, iterating with for-of returns 0 values instead of the delegated values. Manual `.next()` iteration works correctly.

**Reproduction:**
```typescript
function* countTo(n: number) {
    for (let i = 1; i <= n; i++) {
        yield i;
    }
}

function* combined() {
    yield* countTo(3);  // Should yield 1, 2, 3
}

// Manual iteration works:
const gen = combined();
gen.next().value;  // 1 ✓
gen.next().value;  // 2 ✓
gen.next().value;  // 3 ✓

// for-of iteration fails:
let total = 0;
for (const n of combined()) {
    total += n;
}
console.log(total);  // 0 (should be 6)
```

**Location:** `IRGenerator_Statements.cpp` (for-of with generators) or generator state machine

---

## Implementation Plan

### Phase 1: Union Type Parameter Crash (Critical)

**Goal:** Allow union types in function parameters without crashing

**Files to Investigate:**
- `src/compiler/analysis/Analyzer_Types.cpp` - Type parsing
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Expression type inference
- `src/compiler/codegen/IRGenerator_Functions.cpp` - Function parameter codegen

**Approach:**
1. Add debug logging to trace where the crash occurs
2. Check how union types are parsed vs how they're stored
3. Ensure union type representation works for parameters (may need to box as `any`)
4. Add test case to golden_ir tests

**Test:**
```typescript
// tests/golden_ir/typescript/types/union_params.ts
function process(value: string | number): string {
    if (typeof value === "string") return value;
    return String(value);
}
console.log(process("hello"));
console.log(process(42));
// OUTPUT: hello
// OUTPUT: 42
```

---

### Phase 2: Null Comparison Fix

**Goal:** Fix `=== null` comparison for union types

**Files to Investigate:**
- `src/compiler/codegen/IRGenerator_Expressions_Binary.cpp` - Binary expressions
- `src/runtime/src/TsValue.cpp` - Value comparison

**Approach:**
1. Check how null values are boxed/represented at runtime
2. Verify the equality comparison codegen for null
3. May need to add special case for `x === null` pattern
4. Check if the issue is in type narrowing or actual comparison

**Test:**
```typescript
// tests/golden_ir/typescript/types/null_comparison.ts
function test(x: string | null): string {
    if (x === null) return "null";
    return x;
}
console.log(test(null));
console.log(test("hello"));
// OUTPUT: null
// OUTPUT: hello
```

---

### Phase 3: Custom Iterables

**Goal:** Fix codegen for custom [Symbol.iterator] implementations

**Files to Investigate:**
- `src/compiler/codegen/IRGenerator_Statements.cpp` - for-of codegen
- `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp` - Iterator method calls

**Approach:**
1. Compare codegen for generator-based iteration (works) vs custom iterator (fails)
2. The bitcast error suggests type mismatch - iterator value being treated as wrong type
3. Check how IteratorResult is being unpacked
4. May need different handling for custom iterators vs generators

**Test:**
```typescript
// tests/golden_ir/typescript/iterators/custom_iterable.ts
class Counter {
    max: number;
    constructor(max: number) { this.max = max; }
    [Symbol.iterator]() {
        let i = 0;
        const max = this.max;
        return {
            next() {
                if (i < max) return { value: i++, done: false };
                return { value: undefined, done: true };
            }
        };
    }
}
for (const n of new Counter(3)) console.log(n);
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
```

---

### Phase 4: yield* with for-of

**Goal:** Fix yield* delegation when used with for-of loops

**Files to Investigate:**
- `src/compiler/codegen/IRGenerator_Generators.cpp` - Generator codegen
- `src/compiler/codegen/IRGenerator_Statements.cpp` - for-of loop iteration

**Approach:**
1. Check how yield* state is tracked in the generator state machine
2. Compare manual .next() path vs for-of iteration path
3. The issue may be in how the outer generator's done state is checked
4. yield* needs to forward done status from inner to outer generator

**Test:**
```typescript
// tests/golden_ir/typescript/generators/yield_star_forof.ts
function* inner() { yield 1; yield 2; }
function* outer() { yield* inner(); }
let sum = 0;
for (const n of outer()) sum += n;
console.log(sum);
// OUTPUT: 3
```

---

## Verification

After each fix:
1. Run golden_ir tests: `python tests/golden_ir/runner.py typescript`
2. Run node tests: `python tests/node/run_tests.py`
3. Verify no regressions

## Acceptance Criteria

- [ ] Union type parameters compile without crashing
- [ ] `=== null` comparison works correctly
- [ ] Custom iterables work with for-of loops
- [ ] yield* delegation works with for-of loops
- [ ] All existing tests continue to pass
- [ ] Update conformance matrix:
  - Union types: ⚠️ → ✅
  - Iterable protocol: Keep ✅ (add note if needed)
  - yield* delegation: Keep ✅ (add note if needed)

## Estimated Conformance Impact

Current: 88/140 (63%)
After fixes: ~90/140 (64%) - Union types fully working adds 1-2 features
