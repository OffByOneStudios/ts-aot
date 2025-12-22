# Epic 70: Memory & Allocation Optimizations

**Status:** Planned
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

### Milestone 3: String Optimizations
- [ ] Implement a "Small String" representation within `TsString` that stores up to 14 bytes inline.
- [ ] Avoid `icu::UnicodeString` overhead for simple ASCII operations.

## Verification Plan
- **Ray Tracer Benchmark:** Measure the reduction in GC cycles and total execution time.
- **Memory Profiling:** Use `valgrind --tool=massif` to verify reduced heap usage.
