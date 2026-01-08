# CONF-002: Array.lastIndexOf and String.lastIndexOf

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript ES5
**Feature:** `Array.prototype.lastIndexOf()`, `String.prototype.lastIndexOf()`
**Conformance Doc:** [ecmascript-features.md - ES5 Array Methods](../conformance/ecmascript-features.md#es5-2009---baseline)

## Description

Implement `lastIndexOf()` method for both arrays and strings. This method returns the last index at which a given element can be found, or -1 if not present. Searches backwards from the end of the array/string.

```typescript
const arr = [1, 2, 3, 2, 1];
arr.lastIndexOf(2);  // Returns 3

const str = "hello world hello";
str.lastIndexOf("hello");  // Returns 12
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 44/44 passed (100%)
```

## Implementation Plan

1. [x] Runtime: Add `TsArray::LastIndexOf()` method
2. [x] Runtime: Add `ts_array_lastIndexOf` C API function
3. [x] Runtime: Add `TsString::LastIndexOf()` method
4. [x] Runtime: Add `ts_string_lastIndexOf` C API function
5. [x] Analyzer: Register `lastIndexOf` for both array and string types (already partially done)
6. [x] Codegen: Generate calls to runtime functions (already done, was referencing unimplemented functions)
7. [x] Test: Add test file

## Files Modified

- `src/runtime/include/TsArray.h` - Added `LastIndexOf` method and C API declaration
- `src/runtime/src/TsArray.cpp` - Implemented `LastIndexOf` method
- `src/runtime/include/TsString.h` - Added `LastIndexOf` method and C API declaration
- `src/runtime/src/TsString.cpp` - Implemented `LastIndexOf` method
- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Added `lastIndexOf` for strings
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Added `lastIndexOf` to fallback list
- `src/compiler/analysis/Analyzer_Expressions_Access.cpp` - Added `lastIndexOf` to fallback list

## Test Plan

- [x] Unit test: `tests/node/array/lastindexof.ts`
- [x] JavaScript equivalent: `tests/node/array/lastindexof.js`

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 46/46 passed (100%)  # +2 new tests
```

## Acceptance Criteria

- [x] `Array.prototype.lastIndexOf()` returns correct last index
- [x] `String.prototype.lastIndexOf()` returns correct last index
- [x] Both return -1 for missing elements
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES5 Array Methods: lastIndexOf ❌→✅)

## Notes

- Array lastIndexOf iterates backwards from `length-1` to `0`
- String lastIndexOf uses ICU's `lastIndexOf()` method for full Unicode support
- The codegen was already referencing these runtime functions but they weren't implemented
- Boxing policy was already registered in previous session
