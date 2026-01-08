# CONF-018: Promise.allSettled()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Promise.allSettled()`
**Conformance Doc:** [ES2020 Promise Methods](../../conformance/ecmascript-features.md#es2020-es11)

## Description

Verify `Promise.allSettled()` which returns a promise that resolves after all of the given promises have either fulfilled or rejected, with an array of objects describing the outcome of each promise.

## Implementation

The feature was already fully implemented in the runtime (`ts_promise_allSettled`) and codegen. Just needed test coverage to verify.

## Files Modified

- None (feature already worked)

## Test Plan

- [x] Unit test: `tests/node/promises/promises_allSettled.ts`
- Tests cover:
  - Mix of resolved and rejected promises
  - All resolved promises
  - All rejected promises
  - Empty array input

## Final Test Results

```
$ All 4 tests pass
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2020 now 30%, overall 44%)
