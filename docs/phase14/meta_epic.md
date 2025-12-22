# Phase 14: Optimization & Security Hardening

**Status:** Planned
**Goal:** Enhance the performance and security of the `ts-aot` compiler to ensure a fair and competitive comparison with production-grade JIT runtimes. This phase focuses on leveraging static type information for aggressive optimizations and implementing industry-standard security mitigations.

## Core Objectives
1.  **Close the Performance Gap:** Target the Ray Tracer and HTTP benchmarks to match or exceed Node.js performance.
2.  **Static Analysis for Memory:** Reduce GC pressure by moving short-lived objects to the stack.
3.  **Security Parity:** Implement stack protection and control-flow integrity to match the security posture of modern runtimes.
4.  **Compiler Infrastructure:** Fine-tune the LLVM pipeline for maximum throughput.

## Milestones

### Epic 70: Memory & Allocation Optimizations
Focus on reducing the overhead of the Boehm GC and improving data locality.
- [x] **Escape Analysis:** Identify objects that do not escape their scope and allocate them on the stack.
- [x] **Small String Optimization (SSO):** Avoid heap allocation for short strings.
- [x] **Value Types (@struct):** Implement stack-allocated value types for high-frequency objects.
- [ ] **Fast-Path Allocator:** Implement a thread-local bump allocator for the nursery generation (if moving beyond basic Boehm).

### Epic 71: Type-Driven Codegen Enhancements
Leverage TypeScript's type system to generate more efficient machine code.
- [ ] **Devirtualization:** Convert vtable calls to direct calls when the concrete class is known.
- [ ] **Aggressive Unboxing:** Ensure `number` and `boolean` stay in registers and avoid `TsValue` boxing in hot loops.
- [ ] **Bounds Check Elimination:** Use static analysis to remove redundant array bounds checks.

### Epic 72: Security Hardening
Implement mitigations against common exploits to ensure a "production-ready" comparison.
- [x] **Stack Smashing Protection (SSP):** Enable LLVM stack canaries.
- [x] **Control Flow Integrity (CFI):** Protect indirect function calls and vtable lookups.
- [x] **Memory Safety:** Implement strict bounds and null checks.

### Epic 73: Compiler Pipeline Tuning
Optimize the compilation process and the final binary.
- [ ] **LLVM Pass Manager Tuning:** Customize the optimization pipeline (O3 + specialized passes).
- [ ] **Link Time Optimization (LTO):** Enable cross-module optimization between the generated code and the C++ runtime.
- [ ] **Profile-Guided Optimization (PGO):** (Future) Use execution profiles to guide hot-path optimization.

### Epic 74: Null Check Elimination (NCE)
Reduce the performance overhead of security hardening by eliminating redundant null checks.
- [x] **Local NCE:** Track non-null values within basic blocks.
- [ ] **Dominator-based NCE:** Propagate non-null information across the CFG.
- [x] **Allocation NCE:** Skip checks for objects returned by `new`.
