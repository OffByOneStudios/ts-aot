# Epic 10: Enhanced String Manipulation

**Status:** Planned
**Parent:** [Phase 2 Meta Epic](./meta_epic.md)

## Overview
Add string processing capabilities required for parsing input formats.

## Milestones

### Milestone 10.1: String Methods
- **Runtime:** Implement `split`, `trim`, `substring`, `indexOf` in `TsString`.
- **Codegen:** Map method calls to runtime functions.

### Milestone 10.2: String Indexing
- **Runtime:** Support accessing characters by index (returning string of length 1).
- **Codegen:** Handle `str[i]`.

## Action Items
- [x] Implement `split` in `TsString`.
- [x] Implement `length` and `charCodeAt` in `TsString`.
- [x] Implement `trim` in `TsString`.
- [x] Implement `substring` in `TsString`.
- [x] Add integration tests for string parsing.
