# Phase 19: Ecosystem Validation

**Status:** In Progress
**Goal:** Validate the compiler by successfully compiling and running popular npm packages.

## Reality Check
Most npm packages ship **pre-compiled JavaScript**, not TypeScript source. To compile real npm packages, we need:
1. ~~**TypeScript-native libraries**~~ (works now - import .ts files)
2. **JavaScript slow path** (boxed TsValue* for untyped JS) - NOT IMPLEMENTED

For now, we focus on validating with TypeScript-native code and our own lodash-like library.

## Progress

### ✅ Basic TypeScript Library Imports
- [x] Import functions from other .ts files
- [x] Monomorphization works across module boundaries
- [x] Function calls with arguments work correctly
- [x] Added `charAt()` string method
- [x] Fixed int-to-double coercion bug (monomorphized function returns)
- [x] Barrel exports (`export * from './...'`) work
- [x] Multi-file library imports (ts-utils) work
- [x] Array methods: push, pop, shift, unshift

### 🔧 Minor Issue: Array Join Display
When printing arrays with `join()`, boxed integers display as addresses.
Cosmetic issue - the values are correct internally (sum works correctly).

## Core Objectives
1.  **Utility Libraries:** Compile `lodash` and `date-fns`.
2.  **Server Frameworks:** Compile `express` or `fastify`.
3.  **Reactive:** Compile `rxjs`.

## Milestones

### Epic 104: TypeScript Library Support (NEW)
- [x] Basic import of .ts files
- [x] Function monomorphization across modules
- [x] Fix int-to-double coercion bug for function returns
- [x] `export * from './...'` barrel exports
- [x] Array push/pop/shift/unshift methods
- [x] JSON imports (compile-time embedding)
- [x] Class exports/imports across modules
- [x] Generic function exports/imports (identity, first, map)

### Epic 105: Lodash Compilation Support
**See:** [epic_105_lodash.md](./epic_105_lodash.md)

Two-phase approach:
- **Phase 1:** ts-lodash - TypeScript implementation of core lodash functions
- **Phase 2:** JavaScript slow path - Compile real lodash.js

Progress:
- [ ] Phase 1: ts-lodash library (40+ functions)
- [ ] Phase 2: JavaScript slow path with TsValue operations
- [ ] Phase 2: JSDoc type extraction for optimization
- [ ] Benchmark vs Node.js

### Epic 106: Date-fns Support
- [ ] **Date-fns:** Validate `Date` implementation completeness.

### Epic 107: Server Frameworks
- [ ] **Express.js:** The ultimate test of `http`, `stream`, and `events` compatibility.
- [ ] **Middleware:** Verify that standard middleware (body-parser, cors) works.

### Epic 107: Reactive Programming
- [ ] **RxJS:** Test complex generic types and closure-heavy code.
