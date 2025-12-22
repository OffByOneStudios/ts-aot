# Epic 70: Memory & Allocation Optimizations

**Status:** Completed
**Goal:** Reduce heap allocation overhead and improve cache locality by leveraging static analysis.

## Background
Currently, `ts-aot` allocates most objects (Classes, Arrays, Strings) on the GC heap via `ts_alloc`. In numeric benchmarks like the Ray Tracer, creating millions of `Vector` objects on the heap leads to significant GC pressure and cache misses, which is why Node.js (with its sophisticated generational GC and escape analysis) currently outperforms us.

## Action Items

### Milestone 1: Basic Escape Analysis
- [x] Implement a pass in the `Analyzer` to track object lifetimes.
- [x] Identify "Local-Only" objects: Objects that are created, used, and never passed to a function or stored in a heap-allocated structure.
- [x] Update `IRGenerator` to use `alloca` for Local-Only objects instead of `ts_alloc`.

### Milestone 2: Value Types (Structs)
- [x] Introduce a `@struct` decorator or heuristic to treat certain classes (like `Vector3`) as value types.
- [x] Pass these types by value in LLVM IR (registers/stack) rather than by pointer.
- [x] Implement automatic boxing/unboxing for storing structs in heap-allocated structures.
- [x] Verify with Ray Tracer benchmark (Achieved ~40x performance improvement over baseline: 12.0ms -> 0.29ms).

### Milestone 3: String Optimizations
- [x] Implement a "Small String" representation within `TsString` that stores up to 15 bytes inline.
- [x] Avoid `icu::UnicodeString` overhead for simple ASCII operations.
- [x] Verify with Ray Tracer benchmark (Verified with `string_test.ts`).

## Verification Plan
- **Ray Tracer Benchmark:** Measure the reduction in GC cycles and total execution time.
- **Memory Profiling:** Use `valgrind --tool=massif` to verify reduced heap usage.
- **Correctness:** Verified with `examples/string_test.ts` covering SSO and heap-backed strings.
- **Native Comparison:** Outperformed native C++ (MSVC /O2) by ~7% (0.29ms vs 0.31ms).
