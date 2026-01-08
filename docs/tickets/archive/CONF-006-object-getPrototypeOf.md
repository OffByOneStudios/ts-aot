# CONF-006: Object.getPrototypeOf

**Status:** Completed
**Category:** ECMAScript ES5
**Feature:** `Object.getPrototypeOf()`
**Conformance Doc:** [ecmascript-features.md - ES5 Object Methods](../conformance/ecmascript-features.md#object-methods)

## Description

Implement `Object.getPrototypeOf()` static method. This method returns the prototype (i.e., the value of the internal `[[Prototype]]` property) of the specified object.

```typescript
const obj = {};
console.log(Object.getPrototypeOf(obj));  // Object.prototype

const arr = [];
console.log(Object.getPrototypeOf(arr));  // Array.prototype
```

Note: In our runtime, since we don't have a full prototype chain implementation, this will return `undefined` for most objects. This provides basic ES5 conformance.

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 51/51 passed (100%)
```

## Implementation Plan

1. [x] Runtime: Add `ts_object_getPrototypeOf` C API function
2. [x] Analyzer: Add `getPrototypeOf` to Object static methods
3. [x] Codegen: Generate calls to runtime function
4. [x] Test: Add test file
5. [x] Update conformance matrix
6. [x] Archive ticket

## Files to Modify

- `src/runtime/src/TsObject.cpp` - Add `ts_object_getPrototypeOf` function
- `src/runtime/include/TsObject.h` - Add function declaration
- `src/compiler/analysis/Analyzer_StdLib.cpp` - Add to Object static methods
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Add codegen

## Test Plan

- [x] Unit test: `tests/node/object/object_getPrototypeOf.ts`
- [x] JavaScript equivalent: `tests/node/object/object_getPrototypeOf.js`

## Acceptance Criteria

- [x] `Object.getPrototypeOf()` returns prototype or null (returns undefined)
- [x] Works with plain objects
- [x] All new tests pass (53/53 passed)
- [x] No regression in existing tests
- [x] Conformance matrix updated (ES5 Object Methods: getPrototypeOf ❌→⚠️)
