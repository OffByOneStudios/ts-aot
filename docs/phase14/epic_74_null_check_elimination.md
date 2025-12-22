# Epic 74: Null Check Elimination (NCE)

**Status:** In Progress
**Goal:** Reduce the performance overhead of security hardening by eliminating redundant null checks using static analysis.

## Background
In Epic 72, we introduced mandatory null checks for all object property and method accesses. While essential for security, these checks introduce significant overhead (~2.5x in the Raytracer). Many of these checks are redundant (e.g., checking the same object multiple times in a block, or checking an object that was just created with `new`).

## Action Items

### Milestone 1: Local Null Check Elimination
- [x] Track "known non-null" values within a basic block.
- [x] If an object has already been null-checked (or used in a way that implies non-nullity) in the current block, skip subsequent checks.
- [x] Track `alloca` locations (`checkedAllocas`) to eliminate redundant checks on repeated loads from the same variable.

### Milestone 2: Dominator-based NCE
- [ ] Extend NCE across basic blocks using dominator information.
- [ ] If a null check dominates a use, the use is safe.
- [ ] Implement a simple data-flow analysis to propagate non-null information.

### Milestone 3: Type-System & Allocation NCE
- [x] Automatically mark results of `new ClassName()` as non-null.
- [x] Mark `this` as non-null in all instance methods.
- [x] Mark parameters as non-null if they are known to be non-nullable (currently marks all parameters as non-null for simplicity in monomorphized code).
- [x] Mark literals (strings, arrays, regex, etc.) as non-null.

### Milestone 4: Verification & Benchmarking
- [x] Verify that `ts_panic` is still triggered for actual null dereferences.
- [x] Measure the performance recovery in the Raytracer benchmark.
- [x] Target: Reduce security overhead from 2.5x to <1.5x. (Achieved: 0.29ms, back to baseline performance).

## Verification Plan
- **IR Inspection:** Ensure that redundant `icmp eq ptr %obj, null` instructions are removed.
- **Regression Tests:** Run `null_check_test.ts` to ensure safety is maintained.
