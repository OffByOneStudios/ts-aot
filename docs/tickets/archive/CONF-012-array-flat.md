# CONF-012: Array.prototype.flat()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.flat()`
**Conformance Doc:** [ES2019 Array Methods](../../conformance/ecmascript-features.md#es2019-es10)

## Description

Implement `Array.prototype.flat()` which creates a new array with all sub-array elements concatenated into it recursively up to the specified depth.

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 60/60 passed (100%)

$ python tests/golden_ir/runner.py tests/golden_ir
Results: 88 passed, 2 failed, 1 XFAIL
```

## Implementation

The feature was already partially implemented in the runtime (`ts_array_flat`) and codegen, but was not functioning correctly for nested arrays.

### Root Cause

The issue was that nested arrays are stored in the parent array as boxed `TsValue*` pointers with `type == ARRAY_PTR`, but the `Flat()` method in `TsArray.cpp` was only checking for raw `TsArray*` pointers by looking at the magic number at offset 0.

### Fix

Modified `TsArray::Flat()` in `src/runtime/src/TsArray.cpp` to:
1. First check if the stored value is a `TsValue*` with `type == ValueType::ARRAY_PTR`
2. If so, extract the inner `ptr_val` as the actual `TsArray*`
3. Fall back to checking for raw `TsArray*` magic number for legacy compatibility

## Files Modified

- `src/runtime/src/TsArray.cpp` - Fixed `Flat()` to properly unbox nested arrays

## Test Plan

- [x] Unit test: `tests/node/array/array_flat.ts`
- Tests cover:
  - Basic flat with nested arrays `[[1,2],[3,4]].flat()` -> 4 elements
  - Explicit depth `flat(1)`
  - Already flat array remains same length
  - `flat(0)` does not flatten
  - Empty nested arrays

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 61/61 passed (100%)

$ python tests/golden_ir/runner.py tests/golden_ir
Results: 88 passed, 2 failed, 1 XFAIL (same as baseline)
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2019 now 22%, overall 41%)
