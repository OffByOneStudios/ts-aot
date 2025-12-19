# Epic 54: RegExp & JSON Parity

**Status:** Completed
**Goal:** Enhance `RegExp` support and `JSON` serialization.

## Background
`TsRegExp` is a thin wrapper around ICU Regex. It needs better integration with the rest of the runtime and support for standard JS RegExp features.

## Action Items

### RegExp Enhancements
- [x] Support standard flags in `TsRegExp` constructor: `g` (global), `i` (ignoreCase), `m` (multiline), `u` (unicode), `y` (sticky).
- [x] Implement `RegExp.prototype.lastIndex` for global searches.
- [x] Implement `RegExp.prototype.source` and `flags` getters.
- [x] Integrate `RegExp` with `String.prototype.match(regexp)`.
- [x] Integrate `RegExp` with `String.prototype.replace(regexp, replacement)`.
- [x] Integrate `RegExp` with `String.prototype.split(regexp)`.
- [x] Implement `String.prototype.search(regexp)`.
- [x] Implement `RegExp` property getters: `global`, `ignoreCase`, `multiline`, `sticky`.

### JSON Enhancements
- [x] Update `ts_json_stringify` to support the `replacer` argument (array).
- [x] Update `ts_json_stringify` to support the `space` argument for pretty-printing.
- [x] Ensure circular references are handled (or throw an error as per spec).
- [x] Support `Date` and `RegExp` serialization in `JSON.stringify`.

## Verification Plan
- [x] Create complex regex tests in `tests/integration/regexp_advanced.ts`.
- [x] Test JSON pretty-printing and filtering in `tests/integration/json_advanced.ts`.
- [x] Test RegExp string methods in `tests/integration/regexp_string_methods.ts`.
- [x] Test RegExp search in `tests/integration/regexp_search.ts`.
