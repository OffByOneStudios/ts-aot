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
| ts-aot (Baseline) | ~12.0ms | 1200ms | Before Epic 71 |
| ts-aot (Milestone 1) | ~11.0ms | 1100ms | Devirtualization |
| ts-aot (Milestone 2) | 5.32ms | 532ms | Unboxed Arithmetic |
| ts-aot (Milestone 3) | 2.98ms | 298ms | Fast Math + BCE |

## Performance Analysis (ts-aot vs V8)
With the completion of Epic 71, we have achieved a ~4x speedup over the baseline. V8 is now ~3.8x faster (down from 15x at the start of the project).

Remaining bottlenecks:
1.  **Allocation & GC Pressure:** Still the primary bottleneck. Boehm GC is not optimized for high-frequency short-lived allocations.
2.  **Escape Analysis:** Objects like `Vector3` should be stack-allocated or scalar-replaced.
3.  **Inlining:** While devirtualization helped, more aggressive inlining of small methods is needed.

**Next Steps:**
- **Epic 73:** Escape Analysis & Stack Allocation.
- **Epic 74:** Generational GC / Bump-pointer allocation.
