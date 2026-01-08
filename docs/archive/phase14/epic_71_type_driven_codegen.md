# Epic 71: Type-Driven Codegen Enhancements

**Status:** Planned
**Goal:** Use TypeScript's static types to eliminate runtime checks and indirect jumps.

## Background
JITs like V8 use "Inline Caches" (ICs) to optimize property access. Since we have static types, we can do better by resolving these at compile time, but we currently still rely on vtables for many operations.

## Action Items

### Milestone 1: Devirtualization
- [x] Analyze call sites where the receiver's type is a specific class (not an interface or union).
- [x] Replace vtable lookups with direct LLVM `call` instructions to the specialized function.
- [x] Enable LLVM's `Inliner` to work across these direct calls.
- [x] Benchmark with Ray Tracer (observed ~9% improvement, verified direct calls in IR).

### Milestone 2: Optimized Arithmetic
- [x] Ensure that `a + b` where both are `number` results in a single `fadd` instruction without any `TsValue` wrapping/unwrapping.
- [x] Implement unboxed parameter passing for specialized functions and methods.
- [x] Implement "Fast Math" flags for LLVM to allow SIMD vectorization in loops.

### Milestone 3: Bounds Check Elimination (BCE)
- [x] Implement a simple range analysis for loop indices.
- [x] If a loop is `for (let i = 0; i < arr.length; i++)`, eliminate the bounds check inside the loop body.

## Verification Plan
- **Numeric Benchmarks:** Compare assembly output to ensure direct calls and minimal branching.
- **Performance:** Target a 20-30% improvement in the Ray Tracer (Achieved ~75% improvement: 1.2s -> 0.3s).

## Benchmark Results (Ray Tracer)
| Engine | Avg Time (ms) | Total Time (100 iterations) | Status |
| :--- | :--- | :--- | :--- |
| Node.js (V8 v22.18.0) | 0.79ms | 79ms | Baseline |
| C++ (MSVC /O2) | 0.31ms | 31ms | Native Reference |
| ts-aot (Baseline) | ~12.0ms | 1200ms | Before Epic 71 |
| ts-aot (Milestone 1) | ~11.0ms | 1100ms | Devirtualization |
| ts-aot (Milestone 2) | 5.32ms | 532ms | Unboxed Arithmetic |
| ts-aot (Milestone 3) | 2.98ms | 298ms | Fast Math + BCE |
| ts-aot (Epic 70: @struct) | **0.29ms** | 29ms | Value Types + LTO |

## Performance Analysis (ts-aot vs V8 vs C++)
With the completion of Epic 70 and 71, we have achieved a ~40x speedup over the baseline. We are now:
*   **~2.7x faster than V8** (Node.js).
*   **~7% faster than native C++** (MSVC /O2).

The performance advantage over C++ is likely due to LLVM's more aggressive optimization passes (O3) and the use of Link-Time Optimization (LTO) across the TypeScript/C++ boundary, which allows inlining of runtime functions like `Math.sqrt`.

## Future Work
### Milestone 4: Array Specialization
- [x] Implement specialized array types for primitives (e.g., `double[]`) and structs (e.g., `Vector3[]`).
- [x] Store elements contiguously in memory to improve cache locality.
- [x] Eliminate `TsValue` wrapping for array access.
- [x] Verify with `array_spec_test.ts` (Verified direct access to `double[]`).
