# Phase 18: Node.js Runtime Compatibility

**Status:** Planned
**Goal:** Implement the standard library APIs that developers expect from a Node.js-compatible runtime.

## Verification Strategy
To ensure feature parity with Node.js LTS, we have established a [Compatibility Matrix](./compatibility_matrix.md).
1.  **Baseline:** Generated from Node.js v22 LTS using `scripts/analyze_node_compat.js`.
2.  **Tracking:** Each Epic will update the matrix, changing status from 🔴 (Missing) to 🟡 (Partial) or 🟢 (Implemented).
3.  **Compliance Tests:** A new test suite `tests/compliance/` will verify the existence and basic signature of these APIs.

## Core Objectives
1.  **File System:** Full `fs` module support (sync and async).
2.  **Networking:** `net` and `http` modules (building on our existing prototype).
3.  **Streams:** The fundamental `stream` API (Readable, Writable, Duplex, Transform).

## Milestones

### [Epic 96: The File System (fs)](./epic_96_filesystem.md)
- [ ] **Sync API:** `readFileSync`, `writeFileSync`, `statSync`, etc.
- [ ] **Async API:** `readFile`, `writeFile` using `libuv` thread pool.
- [ ] **File Handles:** `open`, `read`, `write`, `close`.
- [ ] **Verification:** Update `compatibility_matrix.md` for `fs` module.

### Epic 97: Streams & Events
- [ ] **EventEmitter:** A robust C++ implementation of `EventEmitter`.
- [ ] **Stream Base:** Abstract base classes for Streams.
- [ ] **Piping:** Efficient `src.pipe(dest)` implementation (using `sendfile` where possible).
- [ ] **Verification:** Update `compatibility_matrix.md` for `events` and `stream`.

### Epic 98: Module Resolution
- [ ] **CommonJS Support:** `require()` implementation that searches `node_modules`.
- [ ] **ESM Support:** `import` implementation.
- [ ] **JSON Imports:** Support `require('./data.json')`.
