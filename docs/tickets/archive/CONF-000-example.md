# CONF-000: Example Ticket (Template)

**Status:** Complete
**Completed:** 2025-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.indexOf()`
**Conformance Doc:** [ecmascript-features.md](../../conformance/ecmascript-features.md#array-methods)

## Description

Implement the `Array.prototype.indexOf()` method that returns the first index at which a given element can be found in the array, or -1 if it is not present.

```typescript
const arr = [1, 2, 3, 4, 5];
arr.indexOf(3);  // returns 2
arr.indexOf(6);  // returns -1
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 40/40 passed (100%)

$ python tests/golden_ir/runner.py
Results: 88 passed, 5 failed, 22 XFAIL
```

## Implementation Plan

1. [x] Analyzer: Add `indexOf` method signature to Array type
2. [x] Codegen: Generate call to `ts_array_indexof` runtime function
3. [x] Runtime: Implement `ts_array_indexof` in TsArray.cpp
4. [x] Test: Add golden IR test for array indexOf

## Files Modified

- `src/compiler/analysis/Analyzer_StdLib_Array.cpp` - Added method signature
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Array.cpp` - Added call generation
- `src/runtime/src/TsArray.cpp` - Implemented `ts_array_indexof`
- `src/runtime/include/TsArray.h` - Declared function

## Test Plan

- [x] Golden IR test: `tests/golden_ir/typescript/arrays/array_indexof.ts`

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 40/40 passed (100%)  # No regression

$ python tests/golden_ir/runner.py
Results: 89 passed, 5 failed, 22 XFAIL  # +1 new test passing
```

## Acceptance Criteria

- [x] `indexOf` returns correct index when element is found
- [x] `indexOf` returns -1 when element is not found
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES5 Array Methods: indexOf ✅)

## Notes

- Implementation uses simple linear search
- Future optimization: specialized path for sorted arrays
