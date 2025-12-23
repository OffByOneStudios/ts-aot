# Epic 95: Extended Runtime & Primitives

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Expand the runtime library to support `BigInt`, `Symbol`, and full `Set`/`Map` implementations.

## Milestones

### Milestone 95.1: New Primitives
- [ ] **BigInt:** Implement arbitrary-precision integers using `libtommath` or similar.
- [ ] **Symbol:** Implement unique and global symbols.

### Milestone 95.2: Collections
- [ ] **Set:** Implement `Set` with proper iterator support.
- [ ] **Map:** Complete `Map` implementation (ensure full parity with ES spec).

## Action Items
- [ ] **Task 95.1:** Add `TsBigInt` to `src/runtime/Primitives.cpp`.
- [ ] **Task 95.2:** Implement `TsSymbol` and `Symbol.for`.
- [ ] **Task 95.3:** Verify with `ts-aot examples/runtime_ext_test.ts`.
