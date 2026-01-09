# CONF-026: Object.is()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Object.is(value1, value2)`
**Conformance Doc:** [ES2015 (ES6) Object](../../conformance/ecmascript-features.md#object-es6)

## Description

Implemented the ES6 `Object.is()` static method which performs SameValue comparison of two values.

Behavior differs from strict equality (`===`):
- `Object.is(NaN, NaN)` returns `true` (unlike `===`)
- `Object.is(0, -0)` returns `false` (unlike `===`)
- `Object.is(-0, -0)` returns `true`

The SameValue algorithm is defined in the ECMAScript specification and is used internally for Map keys and other operations.

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 74/74 passed (100%)
```

## Implementation

### Analyzer (Analyzer_StdLib.cpp)
- Added `Object.is` function type with two `Any` parameters and `Boolean` return type

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Added `Object.is` handler in the `Object` static methods section
- Boxes both arguments and calls `ts_object_is` runtime function

### Runtime (TsObject.cpp)
- Implemented `ts_object_is(val1, val2)`:
  - Handles all value types (undefined, boolean, int, double, string, objects)
  - Special NaN handling: NaN equals NaN
  - Special zero handling: Uses division by zero sign trick to distinguish +0/-0
  - String comparison by value using `strcmp`
  - Object/array/function comparison by reference

### BoxingPolicy (BoxingPolicy.cpp)
- Registered `ts_object_is` with `{true, true}` - both args boxed, returns bool

## Files Modified

- `src/compiler/analysis/Analyzer_StdLib.cpp` - Type definition
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Codegen handler
- `src/compiler/codegen/BoxingPolicy.cpp` - Boxing policy registration
- `src/runtime/include/TsObject.h` - Function declaration
- `src/runtime/src/TsObject.cpp` - Runtime implementation

## Test Plan

- [x] `tests/node/object/object_is.ts` - 12 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 75/75 passed (100%)
```

## Conformance Update

- ES2015: 70/111 implemented (63%)
- Overall: 122/226 features (54%)
