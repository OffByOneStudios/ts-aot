# Epic 71: Type-Driven Codegen Enhancements

**Status:** Planned
**Goal:** Use TypeScript's static types to eliminate runtime checks and indirect jumps.

## Background
JITs like V8 use "Inline Caches" (ICs) to optimize property access. Since we have static types, we can do better by resolving these at compile time, but we currently still rely on vtables for many operations.

## Action Items

### Milestone 1: Devirtualization
- [ ] Analyze call sites where the receiver's type is a specific class (not an interface or union).
- [ ] Replace vtable lookups with direct LLVM `call` instructions to the specialized function.
- [ ] Enable LLVM's `Inliner` to work across these direct calls.

### Milestone 2: Optimized Arithmetic
- [ ] Ensure that `a + b` where both are `number` results in a single `fadd` instruction without any `TsValue` wrapping/unwrapping.
- [ ] Implement "Fast Math" flags for LLVM to allow SIMD vectorization in loops.

### Milestone 3: Bounds Check Elimination (BCE)
- [ ] Implement a simple range analysis for loop indices.
- [ ] If a loop is `for (let i = 0; i < arr.length; i++)`, eliminate the bounds check inside the loop body.

## Verification Plan
- **Numeric Benchmarks:** Compare assembly output to ensure direct calls and minimal branching.
- **Performance:** Target a 20-30% improvement in the Ray Tracer.
