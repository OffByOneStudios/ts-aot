# CONF-015: Array.prototype.at()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.at()`
**Conformance Doc:** [ES2022 Array Methods](../../conformance/ecmascript-features.md#es2022-es13)

## Description

Verify `Array.prototype.at()` which returns the element at a given index, supporting negative indices to count from the end.

## Implementation

The feature was already fully implemented in the runtime (`ts_array_at`) and codegen. Just needed test coverage to verify.

## Files Modified

- None (feature already worked)

## Test Plan

- [x] Unit test: `tests/node/array/array_at.ts`
- Tests cover:
  - Positive index (first element)
  - Middle index
  - Negative index -1 (last element)
  - Negative index -2 (second to last)
  - Last element via positive index

## Final Test Results

```
$ All 5 tests pass
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2022 now 20%, overall 42%)
