# Epic 90: Async/Await & Async Iteration

**Status:** In Progress
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement the state machine transformation for `async`/`await` and `for await...of`. This involves transforming linear code into a re-entrant state machine that can suspend execution at `await` points and resume when promises resolve.

## Milestones

### Milestone 90.1: State Machine Transformation
- [x] **AST Transformation:** Implement a pass to convert `async` functions into a state machine class.
- [ ] **Optimization: State Machine Packing:** Use type information to store local variables in the state struct as raw primitives (e.g., `double` instead of `TsValue`).
- [x] **Context Management:** Capture `this` and arguments into the state machine instance.

### Milestone 90.2: Runtime Integration
- [x] **Awaiter Pattern:** Implement the `__awaiter` helper logic in the C++ runtime.
- [x] **Promise Integration:** Connect the state machine's `step()` function to `TsPromise::then`.
- [x] **Microtask Queue:** Ensure `await` resumptions are correctly scheduled on the microtask queue.

### Milestone 90.3: Advanced Async Features
- [x] **Top-level Await:** Support `await` at the top level of modules.
- [x] **Async Iteration:** Implement `for await...of` and Async Generators.

### Milestone 90.4: Generic Async Iteration Protocol
- [x] **Symbol.asyncIterator:** Implement generic lookup for custom async iterables.
- [x] **Boxing Idempotency:** Fix "double-boxing" issues in IR generation to ensure stable pointers.
- [ ] **Protocol Verification:** Ensure custom iterators (manual `next()` calls) work correctly in `for await...of`.

## Action Items
- [x] **Task 90.1:** Create `src/compiler/analysis/AsyncTransformer.h/cpp`.
- [x] **Task 90.2:** Implement state machine struct generation in `IRGenerator_Async.cpp`.
- [x] **Task 90.3:** Implement `ts_awaiter_create` and `ts_awaiter_step` in `src/runtime/src/TsPromise.cpp`.
- [x] **Task 90.4:** Verify with `ts-aot examples/async_test.ts`.
- [x] **Task 90.5:** Implement `for await...of` and Async Generators.
- [x] **Task 90.6:** Implement generic `Symbol.asyncIterator` lookup and fix boxing bugs.
- [ ] **Task 90.7:** Verify with `ts-aot examples/async_iteration_test.ts`.
