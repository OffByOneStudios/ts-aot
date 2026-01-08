# CONF-005: Object.getOwnPropertyNames

**Status:** Completed
**Category:** ECMAScript ES5
**Feature:** `Object.getOwnPropertyNames()`
**Conformance Doc:** [ecmascript-features.md - ES5 Object Methods](../conformance/ecmascript-features.md#object-methods)

## Description

Implement `Object.getOwnPropertyNames()` static method. This method returns an array of all properties (including non-enumerable properties except for those using Symbol) found directly in a given object.

```typescript
const obj = { a: 1, b: 2 };
console.log(Object.getOwnPropertyNames(obj));  // ["a", "b"]

const arr = [1, 2, 3];
console.log(Object.getOwnPropertyNames(arr));  // ["0", "1", "2", "length"]
```

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 49/49 passed (100%)
```

## Implementation Plan

1. [x] Runtime: Add `ts_object_getOwnPropertyNames` C API function
2. [x] Analyzer: Add `getOwnPropertyNames` to Object static methods
3. [x] Codegen: Generate calls to runtime function
4. [x] Test: Add test file
5. [x] Update conformance matrix
6. [x] Archive ticket

## Files to Modify

- `src/runtime/src/TsObject.cpp` - Add `ts_object_getOwnPropertyNames` function
- `src/runtime/include/TsObject.h` - Add function declaration
- `src/compiler/analysis/Analyzer_StdLib.cpp` - Add to Object static methods
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Add codegen

## Test Plan

- [x] Unit test: `tests/node/object/object_getOwnPropertyNames.ts`
- [x] JavaScript equivalent: `tests/node/object/object_getOwnPropertyNames.js`

## Acceptance Criteria

- [x] `Object.getOwnPropertyNames()` returns array of property names
- [x] Works with plain objects
- [x] All new tests pass
- [x] No regression in existing tests (51/51 passed)
- [x] Conformance matrix updated (ES5 Object Methods: getOwnPropertyNames ❌→✅)
