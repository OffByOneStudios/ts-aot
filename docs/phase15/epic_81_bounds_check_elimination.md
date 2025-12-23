# Epic 81: Bounds Check Elimination (BCE)

**Status:** In Progress
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

### Milestone 3: LLVM Range Metadata & Optimization Hints
- [x] Define LLVM struct layouts for `TsString`, `TsArray`, `TsTypedArray`, and `TsBuffer`.
- [x] Replace runtime length calls with direct field loads to enable metadata attachment.
- [x] Attach `!range` metadata to length loads to inform LLVM that lengths are non-negative.
- [x] Verify that bounds checks are eliminated in optimized IR.

### Milestone 4: Loop Induction Variable Promotion
- [x] Detect loops where the induction variable is only used for indexing and can be promoted from `double` to `i64`.
- [x] Implement promotion to enable LLVM's `LoopVectorize` and `LoopStrengthReduce` passes.
- [x] Verify promotion in generated IR (induction variables are `i64`, comparisons are `icmp`).

## Verification Plan
- **IR Inspection:** Check that `ts_panic_bounds` calls are removed from loop bodies.
- **Benchmark:** Measure performance on array-heavy workloads (e.g., `Buffer` processing, large `Array` iterations).
- **Promotion Check:** Verify that induction variables in `for` loops are `i64` and use integer arithmetic.
