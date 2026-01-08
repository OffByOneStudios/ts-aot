# Epic 97: Streams & Events

**Status:** In Progress
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the core Node.js `events` and `stream` modules. This is a critical prerequisite for `http`, `net`, and advanced `fs` operations.

## Milestones

### Milestone 97.1: Robust EventEmitter
- [x] **Task 97.1.1:** Implement `once(event, listener)`.
- [x] **Task 97.1.2:** Implement `prependListener(event, listener)`.
- [x] **Task 97.1.3:** Implement `removeAllListeners(event?)`.
- [x] **Task 97.1.4:** Implement `setMaxListeners(n)` and warning logic.
- [x] **Task 97.1.5:** Implement `newListener` and `removeListener` internal events.
- [x] **Task 97.1.6:** Implement `error` event special handling (throw if no listeners).

### Milestone 97.2: Stream Base Classes
- [x] **Task 97.2.1:** Define `Stream`, `Readable`, `Writable`, `Duplex`, and `Transform` in `Analyzer_StdLib.cpp`.
- [x] **Task 97.2.2:** Implement the `Readable` state machine (flowing vs paused mode).
- [x] **Task 97.2.3:** Implement the `Writable` buffering and drain logic.
- [x] **Task 97.2.4:** Implement `pipe(dest)` with backpressure support.

### Milestone 97.3: Integration
- [x] **Task 97.3.1:** Refactor `fs.ReadStream` and `fs.WriteStream` to inherit from the new base classes.
- [x] **Task 97.3.2:** Update `compatibility_matrix.md` for `events` and `stream`.

## Action Items
- [x] **Completed:** Milestone 97.3 - Integration & Compatibility Matrix.
