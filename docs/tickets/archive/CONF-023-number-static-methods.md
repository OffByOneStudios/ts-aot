# CONF-023: Number Static Methods and Constants

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Number.isFinite()`, `Number.isNaN()`, `Number.isInteger()`, `Number.isSafeInteger()`, `Number.EPSILON`, `Number.MAX_SAFE_INTEGER`, `Number.MIN_SAFE_INTEGER`
**Conformance Doc:** [ES2015 (ES6) Number](../../conformance/ecmascript-features.md#number)

## Description

Implemented the ES6 Number static methods and constants:

- `Number.isFinite(value)` - Check if value is a finite number
- `Number.isNaN(value)` - Check if value is NaN (more precise than global `isNaN`)
- `Number.isInteger(value)` - Check if value is an integer
- `Number.isSafeInteger(value)` - Check if value is a safe integer (-2^53+1 to 2^53-1)
- `Number.EPSILON` - Smallest difference between two representable numbers
- `Number.MAX_SAFE_INTEGER` - Maximum safe integer (2^53 - 1)
- `Number.MIN_SAFE_INTEGER` - Minimum safe integer (-(2^53 - 1))

Also added global constants:
- `Infinity` - Positive infinity
- `NaN` - Not a Number

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 71/71 passed (100%)
```

## Implementation

### Analyzer (Analyzer_StdLib.cpp)
- Added Number object type with isFinite, isNaN, isInteger, isSafeInteger function types
- Added constant type definitions for EPSILON, MAX_SAFE_INTEGER, MIN_SAFE_INTEGER, etc.
- Added global Infinity and NaN symbols

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Implemented Number.isFinite using LLVM FCmp instructions:
  - `val == val` (not NaN) AND `val != +Inf` AND `val != -Inf`
- Implemented Number.isNaN using LLVM FCmpUNO:
  - Returns true only when `val` is unordered with itself (NaN property)
- Implemented Number.isInteger:
  - isFinite AND `floor(val) == val`
- Implemented Number.isSafeInteger:
  - isInteger AND `-9007199254740991 <= val <= 9007199254740991`
- Implemented constants using LLVM ConstantFP/ConstantInt

### Codegen (IRGenerator_Expressions_Access.cpp)
- Added handlers for global `Infinity` and `NaN` identifiers

## Files Modified

- `src/compiler/analysis/Analyzer_StdLib.cpp` - Type definitions
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Codegen for methods/constants
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` - Global Infinity/NaN

## Test Plan

- [x] `tests/node/number/number_static_methods.ts` - 24 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 72/72 passed (100%)
```

## Conformance Update

- ES2015: 67/111 implemented (60%)
- Overall: 119/226 features (53%)
