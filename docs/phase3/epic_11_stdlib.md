# Epic 11: Standard Library

**Status:** Planned
**Parent:** [Phase 3 Meta Epic](./meta_epic.md)

## Overview
Implement the `Math` object and essential algorithms.

## Milestones

### Milestone 11.1: Math Object
- **Runtime:** Implement `ts_math_min`, `ts_math_max`, `ts_math_abs`.
- **Codegen:** Recognize `Math.min(...)` etc. and emit calls.

### Milestone 11.2: Array Utilities
- **Runtime:** Implement `ts_array_sort` (optional, if needed for Day 2).

## Action Items
- [ ] Implement `Math` functions in Runtime.
- [ ] Add Codegen support for `Math` static methods.
- [ ] Test with `tests/integration/stdlib.ts`.
