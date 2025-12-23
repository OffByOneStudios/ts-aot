# Phase 17: Language Completeness

**Status:** Planned
**Goal:** Achieve 100% syntax compliance with TypeScript while leveraging type information for "no-cost" optimizations.

## Core Objectives
1.  **Async/Await:** Implement the state machine transformation required for modern async TS.
2.  **Generators:** Support `function*` and `yield`.
3.  **Error Handling:** Full `try/catch` support with stack unwinding.
4.  **Modern Syntax:** Support Optional Chaining, Nullish Coalescing, and Private Fields.
5.  **Extended Runtime:** Implement BigInt, Symbols, and full Map/Set support.

## Optimization Principles
Unlike a JIT, `ts-aot` has access to the full type graph before emitting code. We will use this to:
*   **Box Elimination:** Avoid `TsValue` wrapping for locals and fields with known primitive types.
*   **Null-Check Elimination:** Use non-nullable type information to skip `if (obj == null)` checks in optional chaining and property access.
*   **Static Dispatch:** Use monomorphization to turn dynamic Map/Set lookups into specialized, type-safe collection calls.
*   **State Machine Packing:** Pack async/generator state into minimal, type-specific structs rather than generic objects.

## Milestones

### Epic 90: Async/Await & Async Iteration
- [x] **AST Transformation:** Convert `async` functions into a state machine class.
- [ ] **Optimization: State Machine Packing:** Use type info to store locals in the state struct as raw primitives (e.g., `double` instead of `TsValue`).
- [x] **Promise Integration:** Connect the state machine `step()` function to `TsPromise::then`.
- [x] **Awaiter Pattern:** Implement the `__awaiter` helper logic in C++.
- [ ] **Top-level Await:** Support `await` at the top level of modules.
- [ ] **Async Iteration:** Support `for await...of` and `AsyncIterable`.

### Epic 91: Generators & Iterators
- [x] **Generator Object:** Implement `TsGenerator` in the runtime.
- [x] **Yield Handling:** Save/Restore stack frame state (locals) across yield points.
- [ ] **Optimization: Generator Fusion:** If a generator is monomorphized and used in a `for...of`, attempt to inline the state machine to avoid heap allocation.
- [x] **Iterator Protocol:** Support `next()`, `return()`, `throw()`.
- [ ] **Symbol.iterator:** Implement well-known symbols and use them for `for...of` loops.

### Epic 92: Advanced Classes
- [ ] **Decorators:** Implement stage 3 decorators support.
- [ ] **Private Fields:** Support `#field` syntax.
- [ ] **Optimization: Fixed Offset Access:** Use LLVM GEP with constant offsets for private fields, bypassing dynamic property lookups.
- [ ] **Static Blocks:** Implement `static { ... }` initialization blocks.
- [ ] **Mixins:** Update AST to support `class extends expression` and implement in codegen.

### Epic 93: Error Handling
- [ ] **Exception Mechanism:** Finalize the `setjmp/longjmp` mechanism or transition to C++ exceptions for better interop.
- [ ] **Stack Traces:** Capture C++ stack traces and map them back to TypeScript source lines using debug info.
- [ ] **Finally Blocks:** Ensure `finally` is executed on both success and error paths (including `return` from `try`).

### Epic 94: Modern Syntax Sugar
- [ ] **Optional Chaining:** Implement `?.` for property access, element access, and calls.
- [ ] **Optimization: Null-Check Elimination:** If the operand is non-nullable according to the analyzer, emit no check.
- [ ] **Nullish Coalescing:** Implement `??` and `??=` operators.
- [ ] **Logical Assignment:** Implement `&&=` and `||=` operators.
- [ ] **Tagged Templates:** Support `` tag`template` `` syntax.

### Epic 95: Extended Types & Runtime
- [ ] **BigInt:** Implement `TsBigInt` in the runtime.
- [ ] **Optimization: Native i64 Promotion:** If a BigInt is used in a context where it's known to fit in 64 bits, use native `i64` math.
- [ ] **Symbols:** Implement `TsSymbol` and the global symbol registry.
- [ ] **Set:** Implement the `Set` class in the runtime.
- [ ] **Full Map:** Update `TsMap` to support non-string keys.
- [ ] **Optimization: Specialized Collections:** Use monomorphization to generate specialized C++ templates for `Map<K, V>` when types are known.
