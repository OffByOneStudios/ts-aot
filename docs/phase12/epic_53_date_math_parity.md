# Epic 53: Date & Math Parity

**Status:** Not Started
**Goal:** Complete the implementation of `Date` and `Math` utilities.

## Background
`TsDate` currently only supports getting the current time and basic getters. `Math` is mostly complete but may miss some modern or less common functions.

## Action Items

### Date Implementation
- [x] Implement all `set*` methods (`setFullYear`, `setMonth`, `setDate`, `setHours`, `setMinutes`, `setSeconds`, `setMilliseconds`).
- [x] Implement UTC variants for all getters and setters (`getUTCFullYear`, `setUTCFullYear`, etc.).
- [x] Implement `Date.prototype.toISOString()`.
- [x] Implement `Date.prototype.toString()` and `toDateString()`.
- [ ] Improve `TsDate::Create(const char* dateStr)` using ICU's `DateFormat` for robust parsing.

### Math Implementation
- [x] Audit `Math` against ES2020 and implement missing functions:
    - [x] `Math.log10()`, `Math.log2()`, `Math.log1p()`.
    - [x] `Math.expm1()`, `Math.cosh()`, `Math.sinh()`, `Math.tanh()`.
    - [x] `Math.acosh()`, `Math.asinh()`, `Math.atanh()`.
    - [x] `Math.cbrt()`, `Math.hypot()`.
    - [x] `Math.trunc()`, `Math.fround()`, `Math.clz32()`.

## Verification Plan
- Add unit tests in `tests/unit/TsDateTest.cpp`.
- Add integration tests for `Date` and `Math` usage in TypeScript.
