# Epic 81: Bounds Check Elimination (BCE)

**Status:** Planned
**Goal:** Eliminate redundant array bounds checks in hot loops to enable vectorization and reduce branching.

## Background
Security hardening (Epic 72) added mandatory bounds checks to every array access. In loops like `for (let i = 0; i < arr.length; i++)`, the check `i < arr.length` is performed twice: once by the loop condition and once by the safety check.

## Action Items

### Milestone 1: Loop Pattern Recognition
- [x] Identify induction variables in `for` and `while` loops.
- [x] Recognize the `arr.length` property access as a loop invariant (if the array is not modified).

### Milestone 2: Range Analysis
- [x] Implement a simple range tracker for local variables (length alias tracking).
- [x] Prove that if `0 <= i < length` is checked by the loop, the internal `emitBoundsCheck` can be skipped.
- [x] Support aliases (e.g., `const len = arr.length; for (let i = 0; i < len; i++)`).
- [x] Support both `for` and `while` loops across global and local scopes.

### Milestone 3: LLVM Loop Optimizations
- [ ] Ensure that removing bounds checks allows LLVM's `LoopStrengthReduce` and `LoopVectorize` passes to trigger.

## Verification Plan
- **IR Inspection:** Check that `ts_panic_bounds` calls are removed from loop bodies.
- **Benchmark:** Measure performance on array-heavy workloads (e.g., `Buffer` processing, large `Array` iterations).
