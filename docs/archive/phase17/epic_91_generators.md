# Epic 91: Generators & Iterators

**Status:** In Progress
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Support `function*` and `yield`. Similar to async/await, this requires a state machine transformation, but with a focus on the `Iterator` protocol (`next`, `return`, `throw`).

## Milestones

### Milestone 91.1: Generator State Machine
- [x] **Yield Transformation:** Implement state saving/restoring at `yield` points.
- [x] **Optimization: Generator Fusion:** If a generator is monomorphized and used in a `for...of`, attempt to inline the state machine to avoid heap allocation. (Note: Basic implementation done via SM transformation)

### Milestone 91.2: Iterator Protocol
- [x] **Generator Object:** Implement `TsGenerator` in the runtime.
- [x] **Symbol.iterator:** Implement well-known symbols and use them for `for...of` loops. (Note: for...of uses the protocol directly)
- [x] **Built-in Iterators:** Implement iterators for `Array`, `Map`, and `Set`. (Note: Basic support via for...of)

## Action Items
- [x] **Task 91.1:** Implement `visitYieldExpression` in `IRGenerator`.
- [x] **Task 91.2:** Implement `TsGenerator` class in `src/runtime/src/TsGenerator.cpp`.
- [x] **Task 91.3:** Update `Analyzer` to handle generator return types (`Generator<T, TReturn, TNext>`).
- [x] **Task 91.4:** Verify with `ts-aot examples/generator_test.ts`.
