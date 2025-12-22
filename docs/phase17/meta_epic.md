# Phase 17: Language Completeness

**Status:** Planned
**Goal:** Achieve 100% syntax compliance with TypeScript to enable compiling complex libraries.

## Core Objectives
1.  **Async/Await:** Implement the state machine transformation required for modern async TS.
2.  **Generators:** Support `function*` and `yield`.
3.  **Error Handling:** Full `try/catch` support with stack unwinding.

## Milestones

### Epic 90: Async/Await State Machines
- [ ] **AST Transformation:** Convert `async` functions into a state machine class in the AST (or during IR generation).
- [ ] **Promise Integration:** Connect the state machine `step()` function to `TsPromise::then`.
- [ ] **Awaiter Pattern:** Implement the `__awaiter` helper logic in C++.

### Epic 91: Generators & Iterators
- [ ] **Generator Object:** Implement `TsGenerator` in the runtime.
- [ ] **Yield Handling:** Save/Restore stack frame state (locals) across yield points.
- [ ] **Iterator Protocol:** Support `next()`, `return()`, `throw()`.

### Epic 92: Advanced Classes
- [ ] **Decorators:** Implement stage 3 decorators support.
- [ ] **Mixins:** Support class expressions and mixin patterns.
- [ ] **Abstract Classes:** Enforce abstract method implementation at compile time (already partially done).

### Epic 93: Error Handling
- [ ] **Exception Mechanism:** Decide between C++ Exceptions (`try/catch`) or a custom `setjmp/longjmp` mechanism for performance.
- [ ] **Stack Traces:** Capture C++ stack traces and map them back to TypeScript source lines using debug info.
- [ ] **Finally Blocks:** Ensure `finally` is executed on both success and error paths.
