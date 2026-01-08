# CONF-016: String.prototype.replaceAll()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `String.prototype.replaceAll()`
**Conformance Doc:** [ES2021 String Methods](../../conformance/ecmascript-features.md#es2021-es12)

## Description

Verify `String.prototype.replaceAll()` which replaces all occurrences of a search string with a replacement string.

## Implementation

The feature was already fully implemented in the runtime (`ts_string_replaceAll`) and codegen. Just needed test coverage to verify.

## Files Modified

- None (feature already worked)

## Test Plan

- [x] Unit test: `tests/node/string/string_replaceAll.ts`
- Tests cover:
  - Replace all occurrences of a word
  - Replace single character
  - No matches (string unchanged)
  - Replace with empty string (deletion)
  - Empty string input

## Final Test Results

```
$ All 5 tests pass
```

## Acceptance Criteria

- [x] Feature works as specified
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES2021 now 50%, overall 42%)
