# Epic 54: RegExp & JSON Parity

**Status:** Not Started
**Goal:** Enhance `RegExp` support and `JSON` serialization.

## Background
`TsRegExp` is a thin wrapper around ICU Regex. It needs better integration with the rest of the runtime and support for standard JS RegExp features.

## Action Items

### RegExp Enhancements
- [ ] Support standard flags in `TsRegExp` constructor: `g` (global), `i` (ignoreCase), `m` (multiline), `u` (unicode), `y` (sticky).
- [ ] Implement `RegExp.prototype.lastIndex` for global searches.
- [ ] Implement `RegExp.prototype.source` and `flags` getters.
- [ ] Integrate `RegExp` with `String.prototype.match(regexp)`.
- [ ] Integrate `RegExp` with `String.prototype.replace(regexp, replacement)`.
- [ ] Integrate `RegExp` with `String.prototype.split(regexp)`.

### JSON Enhancements
- [ ] Update `ts_json_stringify` to support the `replacer` argument (function or array).
- [ ] Update `ts_json_stringify` to support the `space` argument for pretty-printing.
- [ ] Ensure circular references are handled (or throw an error as per spec).

## Verification Plan
- Create complex regex tests in `tests/integration/regexp_advanced.ts`.
- Test JSON pretty-printing and filtering in `tests/integration/json_advanced.ts`.
