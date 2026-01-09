# CONF-024: Array.of()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.of()`
**Conformance Doc:** [ES2015 (ES6) Array](../../conformance/ecmascript-features.md#array-es6)

## Description

Implemented `Array.of()` static method that creates a new Array from its arguments.

Unlike `Array(n)` which creates an array with `n` empty slots, `Array.of(n)` creates an array containing `[n]`.

```typescript
Array.of(1, 2, 3)  // [1, 2, 3]
Array.of(7)        // [7] (not an array of 7 empty slots)
Array.of()         // []
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 72/72 passed (100%)
```

## Implementation

### Analyzer (Analyzer_StdLib.cpp)
- Added `Array.of` function type definition with `hasRest = true` (variadic)
- Return type is `Array<any>`

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Creates an empty array via `ts_array_create()`
- Iterates through all call arguments
- Boxes each argument value
- Pushes each onto the array via `ts_array_push()`
- Returns the resulting array

## Files Modified

- `src/compiler/analysis/Analyzer_StdLib.cpp` - Type definition
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Codegen

## Test Plan

- [x] `tests/node/array/array_of.ts` - 8 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 73/73 passed (100%)
```

## Conformance Update

- ES2015: 68/111 implemented (61%)
- Overall: 120/226 features (53%)
