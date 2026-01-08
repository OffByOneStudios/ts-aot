# Epic 95: Extended Runtime & Primitives

**Status:** In Progress
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Expand the runtime library to support `BigInt`, `Symbol`, and full `Set`/`Map` implementations.

## Milestones

### Milestone 95.1: New Primitives
- [x] **BigInt:** Implement arbitrary-precision integers using `libtommath` or similar.
- [x] **Symbol:** Implement unique and global symbols.

### Milestone 95.2: Collections
- [x] **Set:** Implement `Set` with proper iterator support.
- [x] **Map:** Complete `Map` implementation (ensure full parity with ES spec).

## Action Items
- [x] **Task 95.1:** Add `TsBigInt` to `src/runtime/Primitives.cpp`.
- [x] **Task 95.2:** Implement `TsSymbol` and `Symbol.for`.
- [x] **Task 95.3:** Verify with `ts-aot examples/map_set_test.ts`.
