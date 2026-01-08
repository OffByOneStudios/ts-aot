# CONF-001: console.error and console.warn

**Status:** Complete
**Completed:** 2026-01-08
**Category:** Node.js
**Feature:** `console.error()`, `console.warn()`
**Conformance Doc:** [nodejs-features.md - Console](../conformance/nodejs-features.md#console)

## Description

Implement `console.error()` and `console.warn()` methods. These are essential logging functions that output to stderr instead of stdout.

```typescript
console.error("Error message");
console.warn("Warning message");
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 42/42 passed (100%)

$ python tests/golden_ir/runner.py .
Results: 88 passed, 3 failed (path issues)
```

## Implementation Plan

1. [x] Analyzer: Add `error` and `warn` methods to console object type (already done)
2. [x] Codegen: Generate calls to `ts_console_error` and `ts_console_warn` (already done)
3. [x] Runtime: Fix `ts_console_error_value` to output to stderr (was using stdout)
4. [x] Test: Add test file for console methods

## Files Modified

- `src/runtime/src/Primitives.cpp` - Refactored to use parameterized `ts_console_print_value_to_stream`

## Test Plan

- [x] Unit test: `tests/node/console/console_methods.ts`
- [x] JavaScript equivalent: `tests/node/console/console_methods.js`

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 44/44 passed (100%)  # +2 new tests
```

## Acceptance Criteria

- [x] `console.error()` outputs to stderr
- [x] `console.warn()` outputs to stderr
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (Console: 3/19, 16%)

## Notes

- The feature was already partially implemented in analyzer and codegen
- Fixed `ts_console_error_value` which was incorrectly calling `ts_console_log_value` (stdout)
- Created `ts_console_print_value_to_stream` helper to avoid code duplication
