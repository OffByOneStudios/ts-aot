# CONF-025: Array.prototype.fill()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.prototype.fill(value, start?, end?)`
**Conformance Doc:** [ES2015 (ES6) Array](../../conformance/ecmascript-features.md#array-es6)

## Description

Implemented the ES6 `Array.prototype.fill()` method which fills all elements of an array from a start index to an end index with a static value.

Behavior:
- `arr.fill(value)` - fills entire array with value
- `arr.fill(value, start)` - fills from start index to end
- `arr.fill(value, start, end)` - fills from start to end (exclusive)
- Negative indices are relative to end of array
- Returns the modified array (allows chaining)

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 74/74 passed (100%)
```

## Implementation

### Analyzer (Analyzer_Expressions_*.cpp)
- Added `fill` method handling in multiple locations for type inference

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Added `fill` handler after `slice` in `tryGenerateBuiltinCall()`
- Handles specialized arrays (int[], double[]) by converting values appropriately
- Uses same value conversion pattern as `push()` for consistency

### Runtime (TsArray.cpp)
- Implemented `ts_array_fill(arr, value, start, end)`
- Handles negative indices
- Clamps indices to valid range
- Returns the modified array

### BoxingPolicy (BoxingPolicy.cpp)
- Registered `ts_array_fill` with `{false, false, false, false}` - all raw values

## Files Modified

- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Codegen handler
- `src/compiler/codegen/BoxingPolicy.cpp` - Boxing policy registration
- `src/runtime/include/TsArray.h` - Function declaration
- `src/runtime/src/TsArray.cpp` - Runtime implementation

## Test Plan

- [x] `tests/node/array/array_fill.ts` - 8 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 74/74 passed (100%)
```

## Conformance Update

- ES2015: 69/111 implemented (62%)
- Overall: 121/226 features (54%)
