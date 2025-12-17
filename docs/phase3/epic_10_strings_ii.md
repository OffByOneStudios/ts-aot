# Epic 10: Advanced Strings

**Status:** Planned
**Parent:** [Phase 3 Meta Epic](./meta_epic.md)

## Overview
Enhance `TsString` to support parsing and iteration.

## Milestones

### Milestone 10.1: String Access
- **Runtime:** Implement `Length()` and `CharCodeAt(index)`.
- **Codegen:** Support `str.length` property access.
- **Codegen:** Support `str.charCodeAt(i)` method call.

### Milestone 10.2: String Splitting
- **Runtime:** Implement `Split(separator)`. Returns `TsArray*` of strings.
- **Codegen:** Support `str.split(sep)`.

## Action Items
- [ ] Implement `TsString::Length` and `TsString::CharCodeAt`.
- [ ] Implement `TsString::Split`.
- [ ] Add AST/Codegen support for `length` property on strings.
- [ ] Add AST/Codegen support for `charCodeAt` and `split`.
- [ ] Test with `tests/integration/strings_ii.ts`.
