# CONF-003: Array.reduceRight

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript ES5
**Feature:** `Array.prototype.reduceRight()`
**Conformance Doc:** [ecmascript-features.md - ES5 Array Methods](../conformance/ecmascript-features.md#es5-2009---baseline)

## Description

Implement `reduceRight()` method for arrays. This method applies a function against an accumulator and each element of the array (from right to left) to reduce it to a single value.

```typescript
const arr = [1, 2, 3, 4, 5];
const sum = arr.reduceRight((acc, val) => acc + val, 0);
// Returns 15 (same as reduce for sum, but processes 5,4,3,2,1)

const product = arr.reduceRight((acc, val) => acc * val);
// Returns 120 (without initial value, uses last element as initial)
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 46/46 passed (100%)
```

## Implementation Plan

1. [x] Runtime: Add `TsArray::ReduceRight()` method
2. [x] Runtime: Add `ts_array_reduceRight` C API function
3. [x] Analyzer: Add `reduceRight` to method recognition
4. [x] Codegen: Generate calls to runtime function
5. [x] Codegen: Register boxing policy
6. [x] Test: Add test file

## Files Modified

- `src/runtime/include/TsArray.h` - Added `ReduceRight` method and C API declaration
- `src/runtime/src/TsArray.cpp` - Implemented `ReduceRight` method (iterates from length-1 down to 0)
- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Added `reduceRight` to method recognition
- `src/compiler/analysis/Analyzer_Expressions_Access.cpp` - Added `reduceRight` to property access
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Combined reduce/reduceRight handling

## Test Plan

- [x] Unit test: `tests/node/array/reduceRight.ts`
- [x] Note: JavaScript version crashes compiler (known issue with untyped JS)

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 47/47 passed (100%)  # +1 new test
```

## Acceptance Criteria

- [x] `Array.prototype.reduceRight()` processes elements right to left
- [x] Works with initial value
- [x] Works without initial value (uses last element as initial)
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES5 Array Methods: reduceRight ❌→✅)

## Notes

- Implementation iterates backwards from `length-1` to `0`
- When no initial value is provided, uses `arr[length-1]` as initial and starts at `length-2`
- Callback receives (accumulator, currentValue, index, array) as with standard reduce
- String arrays have a known issue with boxing/unboxing in reduce callbacks (pre-existing)
