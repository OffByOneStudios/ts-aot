# CONF-019: Math.sign() and Math.trunc()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Math.sign()` and `Math.trunc()`
**Conformance Doc:** [ES2015 Math Methods](../../conformance/ecmascript-features.md#math)

## Description

Verify `Math.sign()` which returns the sign of a number (-1, 0, or 1) and `Math.trunc()` which truncates a number to its integer part.

## Implementation

Both features were already fully implemented in the runtime (`ts_math_sign`, `ts_math_trunc`). Just needed test coverage to verify.

## Files Modified

- None (features already worked)

## Test Plan

- [x] Unit test: `tests/node/math/math_es6.ts`
- Tests cover:
  - Math.sign with positive, negative, and zero
  - Math.trunc with positive and negative floats
  - Math.trunc with already-integer values

## Final Test Results

```
$ All 6 tests pass
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2015 now 46%, overall 45%)
