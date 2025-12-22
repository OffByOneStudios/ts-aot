# Epic 72: Security Hardening

**Status:** Completed
**Goal:** Implement modern security mitigations to ensure `ts-aot` binaries are as robust as production runtimes.

## Background
To provide a fair comparison with Node.js, we must include the overhead of standard security features. This prevents "cheating" by omitting checks that production environments require.

## Action Items

### Milestone 1: Stack Protection
- [x] Enable LLVM's Stack Smashing Protector (`-fstack-protector-strong`).
- [x] Update `IRGenerator` to apply the `sspstrong` attribute to all generated functions.
- [x] Verify that the runtime handles `__stack_chk_fail` (or equivalent on Windows).

### Milestone 2: Control Flow Integrity (CFI)
- [x] Implement basic CFI for vtable calls.
- [x] Ensure that indirect calls only target valid function entry points.
- [x] Explore LLVM's `cfi` attributes for generated code.

### Milestone 3: Memory Safety
- [x] Implement strict bounds checking for all array and buffer accesses (where not eliminated by Epic 71).
- [x] Ensure that `null`/`undefined` dereferences result in a clean runtime panic rather than a segfault.

## Verification Plan
- **Security Audit:** Use tools like `checksec` to verify that the resulting binaries have SSP and other protections enabled.
- **Fuzzing:** (Future) Use `libFuzzer` on the HTTP parser and String implementation.
