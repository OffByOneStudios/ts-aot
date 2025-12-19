# Epic 55: Promise & Async Parity

**Status:** Not Started
**Goal:** Finalize the asynchronous programming model and `Promise` implementation.

## Background
The current `TsPromise` is minimal, designed primarily to support the `await` keyword. It lacks many of the standard methods required for robust async orchestration.

## Action Items

### Promise Instance Methods
- [ ] Implement `Promise.prototype.catch(onRejected)`.
- [ ] Implement `Promise.prototype.finally(onFinally)`.

### Promise Static Methods
- [ ] Implement `Promise.all(iterable)`.
- [ ] Implement `Promise.race(iterable)`.
- [ ] Implement `Promise.allSettled(iterable)`.
- [ ] Implement `Promise.any(iterable)`.
- [ ] Implement `Promise.reject(reason)`.

### Async Infrastructure
- [ ] Implement unhandled rejection tracking and reporting.
- [ ] Ensure microtask queue (used by Promises) is correctly integrated with the `libuv` event loop.

## Verification Plan
- Add integration tests for complex async patterns (e.g., `Promise.all` with multiple `fetch` calls).
- Verify that `catch` and `finally` blocks work correctly in generated code.
