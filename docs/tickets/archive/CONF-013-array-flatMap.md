# CONF-013: Array.prototype.flatMap()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.flatMap()`
**Conformance Doc:** [ES2019 Array Methods](../../conformance/ecmascript-features.md#es2019-es10)

## Description

Verify `Array.prototype.flatMap()` which first maps each element using a mapping function, then flattens the result into a new array (equivalent to map + flat(1)).

## Implementation

The feature was already fully implemented in the runtime (`ts_array_flatMap`) and codegen. Just needed test coverage to verify.

## Files Modified

- None (feature already worked)

## Test Plan

- [x] Unit test: `tests/node/array/array_flatMap.ts`
- Tests cover:
  - Basic flatMap creating pairs `[1,2,3].flatMap(x => [x, x*2])` -> 6 elements
  - Single element returns
  - Filtering with empty arrays
  - Empty input array
  - Equivalence to map + flat

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 62/62 passed (100%)
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2019 now 33%, overall 41%)
