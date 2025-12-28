# Epic 101: Buffer & Primitives

**Status:** Planned
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Enhance the `Buffer` implementation to match Node.js parity and improve performance of binary data handling.

## Milestones

### Milestone 101.1: Buffer API Parity
- [ ] **Task 101.1.1:** Implement `Buffer.from(array|string|buffer)`.
- [ ] **Task 101.1.2:** Implement `Buffer.alloc(size)` and `Buffer.allocUnsafe(size)`.
- [ ] **Task 101.1.3:** Implement `Buffer.concat(list)`.
- [ ] **Task 101.1.4:** Implement `buffer.copy()`, `buffer.slice()`, `buffer.fill()`.

### Milestone 101.2: Encodings
- [ ] **Task 101.2.1:** Support `hex` encoding/decoding.
- [ ] **Task 101.2.2:** Support `base64` and `base64url` encoding/decoding.
- [ ] **Task 101.2.3:** Optimize `utf8` handling.

### Milestone 101.3: TypedArray Integration
- [ ] **Task 101.3.1:** Ensure `Buffer` inherits from `Uint8Array` (or behaves as such).
- [ ] **Task 101.3.2:** Implement `buffer.buffer` (ArrayBuffer) access.

## Action Items
- [ ] **Planned:** Milestone 101.1 - Buffer API Parity.
