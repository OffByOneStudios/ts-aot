# CONF-022: String.trimStart() and String.trimEnd()

**Status:** Complete
**Completed:** 2026-01-08
**Category:** ECMAScript
**Feature:** `String.prototype.trimStart()`, `String.prototype.trimEnd()`
**Conformance Doc:** [ES2019 (ES10)](../../conformance/ecmascript-features.md#es2019-es10)

## Description

Implemented `String.prototype.trimStart()` and `String.prototype.trimEnd()` methods that remove whitespace from the beginning or end of a string respectively. Also supports the legacy aliases `trimLeft()` and `trimRight()`.

## Baseline Test Results

```
$ python tests/node/run_tests.py
Results: 70/70 passed (100%)
```

## Implementation

### Runtime (TsString.cpp)
- Added `TrimStart()` method using ICU's `u_isWhitespace()` to detect whitespace
- Added `TrimEnd()` method with same approach
- Added `ts_string_trimStart` and `ts_string_trimEnd` extern C wrappers

### Codegen (IRGenerator)
- Added boxing policy registration for both functions (non-boxed args, non-boxed returns)
- Added handlers in `IRGenerator_Expressions_Calls_Builtin.cpp` for `trimStart`/`trimLeft` and `trimEnd`/`trimRight`
- Added handlers in `IRGenerator_Expressions.cpp` for string method dispatch

## Files Modified

- `src/runtime/include/TsString.h` - Method declarations
- `src/runtime/src/TsString.cpp` - Method implementations
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Boxing policy + codegen
- `src/compiler/codegen/IRGenerator_Expressions.cpp` - String method dispatch

## Test Plan

- [x] `tests/node/string/string_trim_start_end.ts` - 12 test cases

## Final Test Results

```
$ python tests/node/run_tests.py
Results: 71/71 passed (100%)
```

## Conformance Update

- ES2019: 6/9 implemented (67%)
- Overall: 112/226 features (50%)
