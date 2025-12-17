# Epic 12: Hash Maps

**Status:** Planned
**Parent:** [Phase 3 Meta Epic](./meta_epic.md)

## Overview
Implement a hash map data structure for O(1) lookups.

## Milestones

### Milestone 12.1: Runtime Map
- **C++:** Implement `TsMap` using `std::unordered_map` (wrapping GC-allocated memory).
- **API:** `create`, `set`, `get`, `has`.
- **Keys:** Support string keys initially.

### Milestone 12.2: Compiler Support
- **AST:** Support `new Map()`.
- **Codegen:** Generate calls to runtime map functions.

## Action Items
- [ ] Implement `TsMap` in Runtime.
- [ ] Add Codegen support for `Map` methods.
- [ ] Test with `tests/integration/maps.ts`.
