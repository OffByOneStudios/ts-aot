# CONF-011: Array.from()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Array.from()` static method
**Conformance Doc:** [ES2015 Array Methods](docs/conformance/ecmascript-features.md#array-es6)

## Description

Implement the `Array.from()` static method which creates a new Array instance from an array-like or iterable object.

Syntax:
```typescript
Array.from(arrayLike)
Array.from(arrayLike, mapFn)
Array.from(arrayLike, mapFn, thisArg)
```

Common use cases:
- Convert strings to character arrays: `Array.from("hello")`
- Convert Set/Map to arrays: `Array.from(mySet)`
- Create arrays with map function: `Array.from({length: 5}, (_, i) => i)`
- Convert NodeList to array (web): `Array.from(document.querySelectorAll('div'))`

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 59/59 passed (100%)

$ python tests/golden_ir/runner.py e:/src/github.com/cgrinker/ts-aoc/tests/golden_ir
Results: 88 passed, 2 failed, 1 XFAIL
```

## Implementation Summary

1. [x] Add `Array.from` to analyzer stdlib - Already registered
2. [x] Implement codegen handler for `Array.from` call - Added to IRGenerator_Expressions_Calls_Builtin.cpp
3. [x] Implement `ts_array_from` in runtime - Added to TsArray.cpp
4. [x] Support array source objects
5. [x] Support optional `mapFn` callback via ts_call_2
6. [x] Register boxing policy for ts_array_from

### Key Changes

- **Analyzer:** `Array.from` was already registered with proper type signature
- **Codegen:** Added handler after `Array.isArray` in `IRGenerator_Expressions_Calls_Builtin.cpp:1467-1505`
- **Runtime:** Added `ts_array_from` in `TsArray.cpp` - handles arrays, strings, and array-like objects
- **Boxing Policy:** Registered `ts_array_from` with `{true, true, true}` for all 3 arguments boxed
- **Bug Fix:** Fixed `Buffer.from` catching `Array.from` in `IRGenerator_Expressions_Calls_Builtin_Buffer.cpp`

## Files Modified

- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Array.from codegen handler
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Buffer.cpp` - Fix Buffer.from to not catch Array.from
- `src/compiler/codegen/BoxingPolicy.cpp` - Register ts_array_from
- `src/runtime/src/TsArray.cpp` - Implement ts_array_from, remove debug output
- `tests/node/array/array_from.ts` - New test file

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 60/60 passed (100%)  # +1 new test

$ python tests/golden_ir/runner.py e:/src/github.com/cgrinker/ts-aoc/tests/golden_ir
Results: 88 passed, 2 failed, 1 XFAIL (no regression)
```

## Acceptance Criteria

- [x] Array.from works with arrays
- [x] Array.from mapFn callback works
- [x] All existing tests still pass
- [x] Conformance matrix updated (ES2015: 49/111 = 44%)
