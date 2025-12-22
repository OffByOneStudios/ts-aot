# Epic 72: Security Hardening

**Status:** Completed
**Goal:** Implement modern security mitigations to ensure `ts-aot` binaries are as robust as production runtimes.

## Background
To provide a fair comparison with Node.js, we must include the overhead of standard security features. This prevents "cheating" by omitting checks that production environments require.

## Action Items

### Milestone 1: Stack Protection
- [x] Enable LLVM's Stack Smashing Protector (`sspstrong`).
- [x] Update `IRGenerator` to apply the `sspstrong` attribute to all generated functions.
- [x] Implement `__stack_chk_fail` in the runtime.

### Milestone 2: Control Flow Integrity (CFI)
- [x] Implement basic CFI for vtable calls and indirect function calls using `llvm.type.test`.
- [x] Attach `!type` metadata to all specialized functions and vtables.
- [x] Implement `ts_cfi_fail` in the runtime.

### Milestone 3: Memory Safety
- [x] Implement strict bounds checking for all array and buffer accesses.
- [x] Implement null pointer checks for all object property/method accesses.
- [x] Optimization: Skip null checks for `this` in instance methods.

### Milestone 4: Verification & Benchmarking
- [x] Build and link `test_app` with all security features enabled.
- [x] Run Raytracer benchmark to measure overhead.
- [x] Document performance impact.

## Performance Impact (Raytracer)
- **Baseline (Node.js):** ~0.8ms
- **ts-aot (Hardened):** ~2.0ms (~2.5x overhead)
- **Analysis:** The overhead is primarily due to the high frequency of null and bounds checks in the inner loops of the raytracer. Future work should focus on Bounds Check Elimination (BCE) and Null Check Elimination (NCE) passes.

## Verification Plan
- **Security Audit:** Verified that `llvm.type.test` and `sspstrong` are present in the generated IR.
- **Runtime Safety:** Verified that OOB and Null dereferences trigger `ts_panic`.
