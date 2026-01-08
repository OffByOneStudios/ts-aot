# CONF-004: Date.prototype.toJSON

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript ES5
**Feature:** `Date.prototype.toJSON()`
**Conformance Doc:** [ecmascript-features.md - ES5 Other](../conformance/ecmascript-features.md#other)

## Description

Implement `toJSON()` method for Date objects. This method returns the same string as `toISOString()` and is called by `JSON.stringify()` when serializing Date objects.

```typescript
const date = new Date('2024-01-15T12:30:00Z');
console.log(date.toJSON());  // "2024-01-15T12:30:00.000Z"
console.log(JSON.stringify({ date }));  // {"date":"2024-01-15T12:30:00.000Z"}
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 47/47 passed (100%)
```

## Implementation Plan

1. [x] Runtime: Add `TsDate::ToJSON()` method (calls ToISOString)
2. [x] Runtime: Add `Date_toJSON` C API function
3. [x] Analyzer: Add `toJSON` method to Date class type
4. [x] Codegen: Add `toJSON` to Date method handling
5. [x] Test: Add test file
6. [x] Update conformance matrix
7. [x] Archive ticket

## Files Modified

- `src/runtime/include/TsDate.h` - Added `ToJSON()` method declaration and `Date_toJSON` C API
- `src/runtime/src/TsDate.cpp` - Implemented `ToJSON()` method (delegates to ToISOString)
- `src/compiler/analysis/Analyzer_StdLib.cpp` - Added `toJSON` to Date class methods
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Added `toJSON` to Date method handling

## Test Plan

- [x] Unit test: `tests/node/date/date_toJSON.ts`
- [x] JavaScript equivalent: `tests/node/date/date_toJSON.js`

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 49/49 passed (100%)  # +2 new tests
```

## Acceptance Criteria

- [x] `Date.prototype.toJSON()` returns ISO string format
- [x] `toJSON()` and `toISOString()` return identical values
- [x] All new tests pass
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES5 Other: toJSON ❌→✅)

## Notes

- `toJSON()` simply delegates to `toISOString()` per the ECMAScript specification
- This is used by `JSON.stringify()` when serializing Date objects
