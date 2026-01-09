# CONF-027: Array.prototype.findLast() and findLastIndex()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.findLast(callback)` and `Array.prototype.findLastIndex(callback)`
**Conformance Doc:** [ES2023 (ES14) Array](../../conformance/ecmascript-features.md#es2023-es14)

## Description

Implemented ES2023 `Array.prototype.findLast()` and `Array.prototype.findLastIndex()` methods which iterate from the end of the array to find matching elements.

Behavior:
- `findLast(callback)` - returns the last element for which callback returns true
- `findLastIndex(callback)` - returns the index of the last element for which callback returns true
- Both methods iterate in reverse order (from end to beginning)
- `findLast` returns undefined (coerced to 0 for typed number arrays) when no match found
- `findLastIndex` returns -1 when no match found

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 76/76 passed (100%)
```

## Implementation

### Analyzer (Analyzer_Expressions_Calls.cpp, Analyzer_Expressions_Access.cpp)
- Added `findLast` to existing `find` type inference (returns element type)
- Added `findLastIndex` to existing `findIndex` type inference (returns Int)

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Extended existing array iteration handler to include `findLast` and `findLastIndex`
- Added return type handling for `findLastIndex` (returns i64)
- Added unboxing for `findLast` result

### Runtime (TsArray.cpp)
- Implemented `TsArray::FindLast()` - iterates from length-1 to 0
- Implemented `TsArray::FindLastIndex()` - iterates from length-1 to 0
- Added `ts_array_findLast` and `ts_array_findLastIndex` extern "C" wrappers

### Header (TsArray.h)
- Added method declarations for `FindLast` and `FindLastIndex`
- Added extern "C" function declarations

## Files Modified

- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Type inference
- `src/compiler/analysis/Analyzer_Expressions_Access.cpp` - Method signature
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Already extended in prior session
- `src/runtime/include/TsArray.h` - Declarations
- `src/runtime/src/TsArray.cpp` - Implementation

## Test Plan

- [x] `tests/node/array/array_findlast.ts` - 10 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 76/76 passed (100%)
```

## Conformance Update

- ES2023: 0/8 -> 2/8 implemented (25%)
- Overall: 124/226 features (55%)
