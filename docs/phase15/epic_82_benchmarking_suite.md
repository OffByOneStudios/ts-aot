# Epic 82: Node.js Benchmarking Suite

**Status:** Completed
**Goal:** Create a reproducible, automated benchmarking suite to compare `ts-aot` against Node.js (V8).

## Background
To claim "production-grade" performance, we need a direct comparison with the industry standard. This requires running the exact same TypeScript/JavaScript code in both environments and measuring key metrics.

## Action Items

### Milestone 1: Benchmark Suite Development
- [x] **SHA-256 (Numeric/Bitwise):** Implement pure TS SHA-256 to test `Uint32Array` and bitwise ops.
- [x] **LRU Cache (Collections):** Implement a Map-based LRU cache to test `TsMap` and GC.
- [x] **JSON Transformer (Strings/JSON):** Parse, modify, and stringify large JSON objects.
- [x] **Data Pipeline (HOFs):** Use `map/filter/reduce` on large datasets to test closures and calls.
- [x] **Async Ping-Pong (Event Loop):** Stress `Promises` and `libuv` integration.

### Milestone 2: Automated Runner
- [x] Create `scripts/benchmark_compare.py`.
- [ ] Support flags for `--compiler-opts` (to test different `ts-aot` levels) and `--node-opts`.
- [x] Generate a comparison table (Execution Time, Memory, Speedup/Slowdown).

### Milestone 3: Standardization & CI
- [ ] Ensure all benchmarks run unmodified in both `ts-aot` and `node`.
- [ ] Add a GitHub Action to run benchmarks on every PR to prevent performance regressions.

## Verification Plan
- **Baseline:** Establish the current delta vs Node.js for the Raytracer.
- **Target:** Achieve <1.1x slowdown (within 10% of V8) for compute-heavy tasks.
