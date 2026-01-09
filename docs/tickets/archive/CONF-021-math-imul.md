# CONF-021: Math.imul()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Math.imul()`
**Conformance Doc:** [ES2015 Math Methods](../../conformance/ecmascript-features.md#math)

## Description

Implement `Math.imul()` which performs C-like 32-bit integer multiplication.

## Implementation

1. Added `ts_math_imul(int32_t a, int32_t b)` to runtime (`src/runtime/src/Math.cpp`)
2. Added codegen handler for `Math.imul()` in `IRGenerator_Expressions_Calls_Builtin.cpp`
3. Registered boxing policy for `ts_math_imul`
4. Added type definition in analyzer (`Analyzer_StdLib.cpp`) - returns int
5. Added `imul` to the list of int-returning Math methods in `Analyzer_Expressions_Calls.cpp`

## Files Modified

- `src/runtime/src/Math.cpp` - Added `ts_math_imul` function
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Added codegen and boxing policy
- `src/compiler/analysis/Analyzer_StdLib.cpp` - Added type definition
- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Added to int-returning methods list

## Test Plan

- [x] Unit test: `tests/node/math/math_imul.ts`
- Tests cover:
  - Basic multiplication (2 * 4 = 8)
  - Negative numbers (-1 * 8 = -8)
  - Both negative (-2 * -2 = 4)
  - Zero (0 * 100 = 0)
  - 32-bit overflow (0xffffffff * 5 = -5)

## Final Test Results

```
$ All 70 tests pass (1 new test)
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2015 now 54%, overall 49%)
