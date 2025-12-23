# Epic 83: IR Verification & Regression Guard

**Status:** In Progress
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
As we implement advanced optimizations (integer specialization, devirtualization, BCE), the compiler's IR generation becomes increasingly complex and fragile. This epic establishes a "White-Box" testing framework that verifies the *quality* and *structure* of the generated LLVM IR, ensuring that optimizations stay active and don't silently regress during development.

## Milestones

### Milestone 83.1: LLVM FileCheck Integration
Adopt the industry-standard `FileCheck` methodology to verify IR patterns directly from TypeScript source files.
- [x] **Tooling:** Implemented a Python-based `FileCheck` clone in `test_runner.py` (since `FileCheck.exe` was missing from vcpkg).
- [x] **Test Runner Support:** Updated `scripts/test_runner.py` to support `// CHECK:`, `// CHECK-NEXT:`, and `// CHECK-NOT:`.
- [x] **Initial Test Suite:** Created `examples/ir_verify_test.ts` to verify integer specialization.

### Milestone 83.2: Type Inference Snapshots
Prevent regressions in the `Analyzer` by snapshotting inferred types for complex expressions.
- [x] **Compiler Flag:** Added `--dump-types` to `ts-aot` to output a stable, sorted list of inferred types with source locations.
- [x] **Snapshot Testing:** Integrated `// TYPE-CHECK:` assertions into `test_runner.py`.
- [x] **Benchmark Guarding:** Added type snapshots to `sha256.ts` and `raytracer.ts`.

### Milestone 83.3: Performance Regression Guard
Automate the detection of performance regressions in core benchmarks.
- [ ] **Baseline Tracking:** Store baseline execution times for `sha256`, `raytracer`, and `grep`.
- [ ] **CI Integration:** Fail the build if performance regresses by more than 5% without an explicit baseline update.

## Action Items
- [x] **Task 83.1:** Implement `// CHECK:` directive parsing in `test_runner.py`.
- [x] **Task 83.2:** Add `Analyzer::dumpTypes()` to output type inference results with source locations.
- [x] **Task 83.3:** Create `examples/ir_verify_test.ts` with `FileCheck` assertions for integer specialization.
- [x] **Task 83.4:** Integrate `FileCheck` into the local development loop.
- [x] **Task 83.5:** Snapshot type inference for `sha256` and `raytracer`.
