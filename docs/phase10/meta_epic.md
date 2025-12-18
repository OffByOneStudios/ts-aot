# Phase 10: Async / Await & Event Loop

This phase focuses on bringing asynchronous programming to `ts-aot`. This involves implementing the Promise API, integrating a real event loop (libuv), and performing the complex AST transformation required to turn `async`/`await` into a state machine.

## Milestones

### Epic 43: Promises & Microtask Queue
Implement the core `Promise` class in the runtime and a microtask queue to handle `.then()` callbacks.
- [x] Implement `TsPromise` in the runtime (C++).
- [x] Implement `Promise.resolve`, `Promise.reject`, and `Promise.all`.
- [x] Implement a Microtask Queue that runs after the current task but before the next event loop tick.


### Epic 44: Libuv Event Loop Integration
Integrate `libuv` into the runtime to handle asynchronous I/O and timers.
- [x] Initialize `uv_loop_t` in `ts_main`.
- [x] Implement `setTimeout` and `setInterval` using `uv_timer_t`.
- [x] Update `ts_main` to run the loop until no more work remains.

### Epic 45: Async/Await Transformation
Implement the compiler pass to transform `async` functions into state-machine based generators.
- [ ] Update AST to support `AsyncKeyword` and `AwaitExpression`.
- [ ] Implement the "Async-to-Generator" transformation in the `Monomorphizer`.
- [ ] Generate state machine IR that can suspend and resume execution.

### Epic 46: Asynchronous I/O
Expose asynchronous versions of existing APIs.
- [ ] Implement `fs.promises.readFile`.
- [ ] Implement a basic `fetch` using `libuv` (or a wrapper).

## Action Items
- [x] Setup `libuv` dependency in `vcpkg.json`.
- [x] Create `TsPromise` runtime object.
- [x] Implement `await` keyword parsing and analysis.
- [x] Implement `Promise.resolve` and `Promise.then` codegen.
- [x] Implement `await` codegen (blocking version).

