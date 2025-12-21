# Epic 73: Compiler Pipeline Tuning

**Status:** Planned
**Goal:** Optimize the LLVM optimization pipeline and linking process.

## Background
The default LLVM optimization passes are good, but we can achieve better results by tailoring the pipeline to our specific language semantics and enabling cross-module optimizations.

## Action Items

### Milestone 1: LLVM Pass Manager
- [x] Transition to the LLVM New Pass Manager.
- [x] Configure a custom pipeline that prioritizes `Inlining`, `SROA` (Scalar Replacement of Aggregates), and `GVN` (Global Value Numbering).
- [x] Implement a "Release" build mode that enables `-O3`.

### Milestone 2: Link Time Optimization (LTO)
- [x] Configure the build system to generate LLVM Bitcode for the C++ runtime.
- [x] Use `ThinLTO` to allow the compiler to optimize across the boundary between generated TypeScript code and the C++ runtime (e.g., inlining `ts_string_create`).

### Milestone 3: Binary Size Optimization
- [ ] Implement dead code elimination for unused runtime functions.
- [ ] Use `strip` and other techniques to minimize the final executable size.

## Verification Plan
- **Binary Size:** Track the size of the `http-server` binary.
- **Performance:** Measure the impact of LTO on the `ts-grep` and `http-server` benchmarks.
