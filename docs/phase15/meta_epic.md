# Phase 15: Advanced Optimizations & Node.js Parity

**Status:** Planned
**Goal:** Achieve performance parity with Node.js (V8) on core benchmarks by implementing advanced compiler optimizations and establishing a rigorous benchmarking suite.

## Core Objectives
1.  **Eliminate Indirect Call Overhead:** Use devirtualization to convert vtable lookups into direct calls.
2.  **Safe & Fast Arrays:** Implement Bounds Check Elimination (BCE) to remove redundant safety checks in loops.
3.  **Rigorous Comparison:** Build a benchmarking harness that compares `ts-aot` directly against Node.js with identical workloads.
4.  **Final Performance Push:** Target <10% performance delta vs V8 on the Raytracer and HTTP benchmarks.

## Milestones

### Epic 79: Runtime Feature Parity
Implement missing standard library features required for advanced benchmarks.
- [x] **TypedArrays:** Implement `Uint8Array`, `Uint32Array`, and `Float64Array`.
- [x] **DataView:** Implement `DataView` for structured binary access.
- [x] **Number Methods:** Implement `Number.prototype.toString(radix)` and `toFixed()`.
- [x] **Advanced String Methods:** Implement `split(RegExp)`, `replace(RegExp)`, etc.

### Epic 80: Devirtualization
Convert virtual method calls to direct calls when the receiver's type is known.
- [ ] **Class Hierarchy Analysis (CHA):** Track class inheritance to identify "final" methods.
- [ ] **Monomorphic Call Site Optimization:** Use monomorphization data to devirtualize calls in specialized functions.
- [ ] **Inlining Devirtualized Calls:** Ensure LLVM can inline the resulting direct calls.

### Epic 81: Bounds Check Elimination (BCE)
Remove redundant array bounds checks using static analysis.
- [ ] **Induction Variable Analysis:** Identify standard `for (let i = 0; i < arr.length; i++)` patterns.
- [ ] **Range Analysis:** Prove that an index `i` is always within `[0, arr.length)`.
- [ ] **Loop Unrolling & Vectorization:** Enable LLVM to vectorize loops once bounds checks are removed.

### Epic 82: Node.js Benchmarking Suite
Establish a "Ground Truth" for performance comparison.
- [x] **Standardized Benchmarks:** Port Raytracer, Crypto, and HTTP benchmarks to a unified format.
- [x] **Automated Runner:** Create a script to run benchmarks in both `ts-aot` and `node`, reporting execution time, memory usage, and P99 latency.
- [x] **Performance Regression Tracking:** Ensure new optimizations don't regress existing gains.

### Epic 83: IR Verification & Regression Guard
Ensure optimizations stay active and don't silently regress.
- [x] **LLVM FileCheck:** Implement embedded IR assertions in TypeScript tests.
- [x] **Type Snapshots:** Lock down the Analyzer's inference logic.
- [x] **Optimization Guards:** Verify that critical paths (like SHA-256) remain specialized.
- [x] **Performance Baselines:** Automated regression detection for core benchmarks.
