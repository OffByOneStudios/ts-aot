# Epic 43: Promises & Microtask Queue

The goal of this epic is to implement the `Promise` object in the runtime and the infrastructure for microtasks.

## Action Items

### Runtime Infrastructure
- [ ] Define `TsPromise` class in `src/runtime/include/TsPromise.h`.
- [ ] Implement Promise states: `Pending`, `Fulfilled`, `Rejected`.
- [ ] Implement `then()`, `catch()`, and `finally()` registration.
- [ ] Implement the Microtask Queue (a simple FIFO of function pointers/closures).

### Compiler Support
- [ ] Register `Promise` as a global class in `Analyzer`.
- [ ] Support generic `Promise<T>` in the `Analyzer`.
- [ ] Implement `new Promise((resolve, reject) => ...)` analysis.

### Verification
- [ ] Unit test for `TsPromise` in C++.
- [ ] Integration test for basic Promise chaining in TypeScript.
