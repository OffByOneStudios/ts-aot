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
- [ ] Implement `split` in `TsString`.
- [ ] Implement `trim` in `TsString`.
- [ ] Implement `substring` in `TsString`.
- [ ] Add integration tests for string parsing.
