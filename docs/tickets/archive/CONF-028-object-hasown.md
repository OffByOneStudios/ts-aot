# CONF-028: Object.hasOwn()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `Object.hasOwn(obj, prop)`
**Conformance Doc:** [ES2022 (ES13)](../../conformance/ecmascript-features.md#es2022-es13)

## Description

`Object.hasOwn()` is an ES2022 static method that checks if an object has a specific own property. It is a more robust alternative to `obj.hasOwnProperty(prop)` because it works even if the object doesn't inherit from Object.prototype.

Behavior:
- Returns `true` if the object has the specified property as its own property
- Returns `false` if the property doesn't exist or is inherited
- Works with objects that don't have `hasOwnProperty` in their prototype chain

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 77/77 passed (100%)
```

## Implementation

### Analyzer (Analyzer_StdLib.cpp)
- Type definition already existed:
  - `Object.hasOwn(obj: object, prop: string) => boolean`

### Codegen (IRGenerator_Expressions_Calls_Builtin.cpp)
- Handler already existed at line 1376
- Calls `ts_object_has_own(obj, prop)` runtime function
- Boxing policy registration already present

### Runtime (TsObject.cpp)
- Fixed `ts_object_has_own()` function:
  - Check TsMap magic at multiple offsets (16, 20, 24) to handle different object layouts
  - Properly unbox the property name argument
  - Use `ts_map_has_v()` for property lookup

### BoxingPolicy (BoxingPolicy.cpp)
- Added static entry: `{"ts_object_has_own", {true, false}}`

## Files Modified

- `src/runtime/src/TsObject.cpp` - Fixed magic offset checks
- `src/compiler/codegen/BoxingPolicy.cpp` - Added boxing policy entry

## Test Plan

- [x] `tests/node/object/object_hasown.ts` - 8 test cases
  - Basic own property detection
  - Non-existent property returns false
  - Empty object handling
  - Numeric keys as strings
  - Property with undefined value
  - Property with null value
  - Property with empty string value

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 78/78 passed (100%)
```

## Conformance Update

- ES2022: 2/10 -> 3/10 implemented (30%)
- Overall: 124/226 -> 125/226 features (55%)
