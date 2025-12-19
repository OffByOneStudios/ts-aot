# Epic 55: Promise & Async Parity

**Status:** In Progress
**Goal:** Finalize the asynchronous programming model and `Promise` implementation.

## Background
The current `TsPromise` is minimal, designed primarily to support the `await` keyword. It lacks many of the standard methods required for robust async orchestration.

## Action Items

### Promise Instance Methods
- [x] Implement `Promise.prototype.catch(onRejected)`.
- [x] Implement `Promise.prototype.finally(onFinally)`.

### Promise Static Methods
- [x] Implement `Promise.all(iterable)`.
- [x] Implement `Promise.race(iterable)`.
- [x] Implement `Promise.allSettled(iterable)`.
- [x] Implement `Promise.any(iterable)`.
- [x] Implement `Promise.reject(reason)`.

### Async Infrastructure
- [x] Implement unhandled rejection tracking and reporting.
- [x] Ensure microtask queue (used by Promises) is correctly integrated with the `libuv` event loop.

## Verification Plan
- Add integration tests for complex async patterns (e.g., `Promise.all` with multiple `fetch` calls).
- [x] Verify that `catch` and `finally` blocks work correctly in generated code.
