# Phase 18: Node.js Runtime Compatibility

**Status:** Planned
**Goal:** Implement the standard library APIs that developers expect from a Node.js-compatible runtime.

## Core Objectives
1.  **File System:** Full `fs` module support (sync and async).
2.  **Networking:** `net` and `http` modules (building on our existing prototype).
3.  **Streams:** The fundamental `stream` API (Readable, Writable, Duplex, Transform).

## Milestones

### Epic 95: The File System (fs)
- [ ] **Sync API:** `readFileSync`, `writeFileSync`, `statSync`, etc.
- [ ] **Async API:** `readFile`, `writeFile` using `libuv` thread pool.
- [ ] **File Handles:** `open`, `read`, `write`, `close`.

### Epic 96: Streams & Events
- [ ] **EventEmitter:** A robust C++ implementation of `EventEmitter`.
- [ ] **Stream Base:** Abstract base classes for Streams.
- [ ] **Piping:** Efficient `src.pipe(dest)` implementation (using `sendfile` where possible).

### Epic 97: Module Resolution
- [ ] **CommonJS Support:** `require()` implementation that searches `node_modules`.
- [ ] **ESM Support:** `import` implementation.
- [ ] **JSON Imports:** Support `require('./data.json')`.
