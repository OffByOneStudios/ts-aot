# EPIC-PERF-001: Performance Parity with Node.js

**Status:** Planning
**Priority:** High
**Target:** Achieve within 2x of Node.js performance on compute benchmarks

## Executive Summary

This epic covers the architectural changes and optimizations needed to bring ts-aot compiled executables to performance parity with Node.js/V8.

**Key Insight:** Most meaningful optimizations require an intermediate representation (HIR) between AST and LLVM IR. Building HIR first creates a solid foundation and eliminates classes of bugs we currently fight.

---

## Phase 0: High-Level IR (Foundation)

### Why HIR First

Current architecture problems:
- Type information lost through LLVM allocas
- Cross-module symbols confused during codegen
- Method body re-analysis causes regressions
- Single-pass design makes analysis fragile
- Hard to test transformations in isolation

HIR provides:
- Explicit types on every value (SSA form)
- Clear phase separation (parse → HIR → optimize → LLVM)
- Debuggable intermediate states (`--dump-hir`)
- Testable transformations (HIR → HIR)
- Foundation for all subsequent optimizations

### HIR Design

```
Module
├── Functions[]
│   ├── name: string
│   ├── params: (name, type)[]
│   ├── returnType: Type
│   ├── blocks: BasicBlock[]
│   └── metadata: { isAsync, isGenerator, ... }
│
├── Classes[]
│   ├── name: string
│   ├── shape: Shape  // property layout
│   ├── methods: Function[]
│   └── vtable: VTableEntry[]
│
└── Globals[]
    ├── name: string
    ├── type: Type
    └── initializer: Instruction[]

BasicBlock
├── label: string
├── params: (name, type)[]  // phi nodes as block params
├── instructions: Instruction[]
└── terminator: Branch | Return | Switch | Unreachable

Instruction (SSA)
├── result: Value  // %0, %1, etc. (or void)
├── type: Type     // ALWAYS present
└── op: Operation
```

### Instruction Set

```
// Arithmetic (typed - no implicit conversions)
%r = add.i64 %a, %b          // integer add
%r = add.f64 %a, %b          // float add
%r = add.i64.checked %a, %b  // overflow → branch to handler

// Memory
%r = alloc T                  // allocate type T
%r = load T, %ptr             // load with explicit type
     store %val, %ptr         // store value to pointer

// Control flow
     br %target               // unconditional branch
     condbr %cond, %then, %else
     ret %val
     switch %val, %default, [cases...]

// Objects (high-level, not lowered yet)
%r = new_object Shape         // create object with known shape
%r = new_object_dynamic       // create dynamic object (TsMap)
%r = get_prop.static %obj, "name", T   // static property (offset known)
%r = get_prop.dynamic %obj, %key       // dynamic property (hash lookup)
     set_prop.static %obj, "name", %val
     set_prop.dynamic %obj, %key, %val

// Arrays (high-level)
%r = new_array.typed T, %len  // typed array (unboxed elements)
%r = new_array.boxed %len     // boxed array (TsValue* elements)
%r = get_elem %arr, %idx
     set_elem %arr, %idx, %val

// Calls
%r = call @func(%args...)
%r = call_method %obj, "method", (%args...)
%r = call_virtual %obj, vtable_idx, (%args...)

// Type operations
%r = box.int %val             // int64 → TsValue*
%r = box.float %val           // f64 → TsValue*
%r = unbox.int %val           // TsValue* → int64
%r = unbox.float %val         // TsValue* → f64
%r = typecheck %val, T        // runtime type check → bool
```

### Milestones

#### M0.1: HIR Data Structures
**Goal:** Define HIR types in C++

**Files to Create:**
- `src/compiler/hir/HIR.h` - Core types (Module, Function, Block, Instruction)
- `src/compiler/hir/HIRTypes.h` - Type representation
- `src/compiler/hir/HIRBuilder.h` - Builder API for constructing HIR
- `src/compiler/hir/HIRPrinter.h` - Text format for `--dump-hir`

**Tests:**
- `tests/unit/hir/hir_types_test.cpp`
- `tests/unit/hir/hir_builder_test.cpp`

#### M0.2: AST → HIR Lowering
**Goal:** Convert analyzed AST to HIR

**Files to Create:**
- `src/compiler/hir/ASTToHIR.h`
- `src/compiler/hir/ASTToHIR.cpp`

**Key Transformations:**
- Control flow → basic blocks with explicit branches
- Expressions → SSA values
- Type annotations → explicit types on every value
- Destructuring → explicit temporaries
- Spread → explicit loops

**Tests:**
- `tests/golden_hir/basic/` - Basic statements and expressions
- `tests/golden_hir/control_flow/` - If, while, for, switch
- `tests/golden_hir/functions/` - Calls, closures, async

#### M0.3: HIR → LLVM Lowering
**Goal:** Generate LLVM IR from HIR (replaces current IRGenerator)

**Files to Create:**
- `src/compiler/hir/HIRToLLVM.h`
- `src/compiler/hir/HIRToLLVM.cpp`

**Key Changes:**
- Much simpler than current IRGenerator (HIR already simplified)
- Type information explicit (no inference during codegen)
- Boxing/unboxing explicit in HIR

**Tests:**
- Run all existing golden_ir tests through new pipeline
- Must be regression-free before proceeding

#### M0.4: Pipeline Integration
**Goal:** New pipeline: AST → Analyze → HIR → LLVM

**Files to Modify:**
- `src/compiler/main.cpp` - Add `--use-hir` flag initially
- `src/compiler/Driver.cpp` - Orchestrate pipeline

**Flags:**
- `--dump-hir` - Print HIR before optimization
- `--dump-hir-opt` - Print HIR after optimization
- `--use-hir` - Use new pipeline (for gradual rollout)

**Tests:**
- All existing tests must pass with `--use-hir`
- Performance benchmarks to verify no regression

---

## Phase 1: Unboxed Arrays

**Prerequisite:** Phase 0 (HIR)
**Expected Impact:** 2-5x speedup for array-heavy code
**Effort:** 2-3 weeks after HIR

### Safety Requirements

**Preconditions for Optimization:**
- Array has explicit element type annotation (`number[]`, `string[]`)
- Element type is primitive or class (not `any`, not union with primitives)
- Array is never assigned to `any[]` or wider type
- No spread from unknown source

**Bailout Triggers:**
- Assignment to `any` typed variable
- Passed to function expecting `any[]`
- Used with `...` from untyped source

**Conservative Default:**
- Only optimize arrays with explicit type AND no widening detected
- Unknown → use boxed array (current behavior)

### Implementation on HIR

```
// Before optimization (HIR):
%arr = new_array.boxed 3
%v0 = box.float 1.0
set_elem %arr, 0, %v0
%v1 = box.float 2.0
set_elem %arr, 1, %v1

// After optimization (HIR):
%arr = new_array.typed f64, 3
set_elem.typed %arr, 0, 1.0
set_elem.typed %arr, 1, 2.0
```

### Milestones

#### M1.1: Array Type Analysis (in Analyzer)

**Action Items:**
- [ ] Track `isHomogeneous` flag on ArrayType
- [ ] Track `elementTypeStable` through mutations
- [ ] Detect widening assignments
- [ ] Mark arrays as "optimizable" or "must be boxed"

**Files:**
- `src/compiler/analysis/Type.h` - Add tracking flags
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Array analysis

**Tests:**
- `tests/golden_hir/arrays/homogeneous_detection.ts`
- `tests/golden_hir/arrays/widening_detection.ts`

#### M1.2: Specialized Array Runtime

**Action Items:**
- [ ] `TsNumberArray` - contiguous f64 storage
- [ ] `TsIntArray` - contiguous i64 storage
- [ ] All array methods for each type

**Files:**
- `src/runtime/include/TsNumberArray.h`
- `src/runtime/src/TsNumberArray.cpp`

**Tests:**
- `tests/golden_ir/arrays/number_array_methods.ts`

#### M1.3: HIR Optimization Pass

**Action Items:**
- [ ] Create `OptimizeArrays` HIR pass
- [ ] Transform `new_array.boxed` → `new_array.typed` when safe
- [ ] Transform `box`/`set_elem` → `set_elem.typed`

**Files:**
- `src/compiler/hir/passes/OptimizeArrays.cpp`

**Tests:**
- `tests/unit/hir/optimize_arrays_test.cpp`

#### M1.4: LLVM Lowering for Typed Arrays

**Action Items:**
- [ ] Lower `new_array.typed` to specialized allocation
- [ ] Lower `set_elem.typed` to direct store

**Files:**
- `src/compiler/hir/HIRToLLVM.cpp`

---

## Phase 2: SMI (Small Integer) Optimization

**Prerequisite:** Phase 0 (HIR)
**Expected Impact:** 2-3x speedup for integer-heavy code
**Effort:** 2-3 weeks after HIR

### Safety Requirements

**Preconditions:**
- Value is known to be integer (not float)
- Arithmetic won't overflow (or we check)

**Guards (runtime):**
- Overflow check on add/sub/mul
- Fallback to float on overflow

**Bailout:**
- Division always produces float
- Overflow → convert to float

### Implementation on HIR

```
// Integer arithmetic in HIR:
%r = add.i64.checked %a, %b, overflow: %slow_path

block %slow_path:
    %af = cast.i64_to_f64 %a
    %bf = cast.i64_to_f64 %b
    %rf = add.f64 %af, %bf
    br %continue(%rf)
```

### Milestones

#### M2.1: Integer Type Tracking

**Action Items:**
- [ ] Distinguish `TypeKind::Int` vs `TypeKind::Double` consistently
- [ ] Integer literals → Int, float literals → Double
- [ ] Track through arithmetic operations

**Files:**
- `src/compiler/analysis/Analyzer_Expressions.cpp`

#### M2.2: Checked Arithmetic in HIR

**Action Items:**
- [ ] Add `add.i64.checked`, `sub.i64.checked`, etc.
- [ ] Generate overflow branch in HIR

**Files:**
- `src/compiler/hir/HIR.h`
- `src/compiler/hir/ASTToHIR.cpp`

#### M2.3: LLVM Lowering with Overflow

**Action Items:**
- [ ] Use `llvm.sadd.with.overflow` intrinsics
- [ ] Generate branch to float fallback

**Files:**
- `src/compiler/hir/HIRToLLVM.cpp`

---

## Phase 3: Hidden Classes / Shapes

**Prerequisite:** Phase 0 (HIR)
**Expected Impact:** 5-10x for property-heavy code
**Effort:** 4-6 weeks after HIR

### Safety Requirements

**Preconditions for Fixed Shape:**
- Object created from class (not literal with dynamic properties)
- No `delete` on object
- No dynamic property assignment (`obj[expr] = val` where expr is not literal)
- No prototype mutation

**Detection of Shape Mutation:**
```typescript
// UNSAFE - must use dynamic shape:
delete obj.x;                    // property deletion
obj[key] = val;                  // dynamic key (unless key is const string)
Object.defineProperty(obj, ...); // descriptor manipulation
obj.__proto__ = something;       // prototype mutation
```

**Bailout:**
- If any mutation detected, fall back to TsMap
- Conservative: only optimize explicit `class` instances initially

### Implementation on HIR

```
// Class with known shape:
%obj = new_object Shape{x:0, y:8}  // offsets known
set_prop.static %obj, "x", %val    // direct store to offset 0

// Object with unknown/mutable shape:
%obj = new_object_dynamic
set_prop.dynamic %obj, "x", %val   // hash table lookup
```

### Milestones

#### M3.1: Shape Representation

**Action Items:**
- [ ] Create `Shape` class (property → offset map)
- [ ] Compute shapes for all class definitions
- [ ] Track shape stability through program

**Files:**
- `src/compiler/analysis/Shape.h`
- `src/compiler/analysis/Shape.cpp`

#### M3.2: Fixed Object Runtime

**Action Items:**
- [ ] `TsFixedObject` with inline storage
- [ ] Fallback to TsMap for overflow properties

**Files:**
- `src/runtime/include/TsFixedObject.h`
- `src/runtime/src/TsFixedObject.cpp`

#### M3.3: Property Access Optimization Pass

**Action Items:**
- [ ] HIR pass to convert dynamic → static access
- [ ] Shape propagation analysis

**Files:**
- `src/compiler/hir/passes/OptimizePropertyAccess.cpp`

---

## Phase 4: Method Inline Caching

**Prerequisite:** Phase 3 (Shapes)
**Expected Impact:** 2-3x for method-heavy code
**Effort:** 1-2 weeks after Shapes

### Safety Requirements

**Guard:** Check shape matches cached shape before using cached method.

```
// Inline cache structure:
struct MethodCache {
    Shape* cachedShape;
    void* cachedMethod;
};

// Generated code:
if (obj->shape == cache.cachedShape) {
    // Fast path: call cached method directly
    cache.cachedMethod(obj, args...);
} else {
    // Slow path: lookup and update cache
    cache.cachedShape = obj->shape;
    cache.cachedMethod = lookup(obj->shape, "methodName");
    cache.cachedMethod(obj, args...);
}
```

### Milestones

#### M4.1: Cache Infrastructure

**Files:**
- `src/runtime/include/TsInlineCache.h`
- `src/runtime/src/TsInlineCache.cpp`

#### M4.2: HIR Method Call Lowering

**Action Items:**
- [ ] `call_method` → inline cache check + call

**Files:**
- `src/compiler/hir/HIRToLLVM.cpp`

---

## Phase 5: Escape Analysis

**Prerequisite:** Phase 0 (HIR with CFG)
**Expected Impact:** Reduced GC pressure
**Effort:** 2-3 weeks

### Safety Requirements

**Preconditions for Stack Allocation:**
- Object does not escape defining function
- "Escape" means: returned, stored to global, stored to heap object, captured by closure, thrown

**Conservative Analysis:**
- If ANY path might escape, treat as escaping
- Closures: assume captured variables escape
- Async: assume all locals escape (may be resumed later)

### Implementation on HIR

```
// Analysis marks allocations:
%obj = new_object Shape  ; escape=false → stack allocate
%arr = new_array 10      ; escape=true → heap allocate
```

### Milestones

#### M5.1: Escape Analysis Pass

**Action Items:**
- [ ] Build CFG from HIR blocks
- [ ] Forward data flow analysis for escape
- [ ] Mark allocations with escape info

**Files:**
- `src/compiler/hir/analysis/EscapeAnalysis.cpp`

#### M5.2: Stack Allocation in LLVM Lowering

**Action Items:**
- [ ] Non-escaping `new_object` → `alloca`
- [ ] Ensure lifetime correctness

**Files:**
- `src/compiler/hir/HIRToLLVM.cpp`

---

## Phase 6: String Interning

**Prerequisite:** Phase 0 (HIR)
**Expected Impact:** 1.5-2x for string comparisons
**Effort:** 1 week

### Safety Requirements

**Invariant:** Interned strings can use pointer equality.

**Implementation:**
- All string literals interned at compile time
- Runtime interning optional for computed strings
- Equality: if both interned, compare pointers

### Milestones

#### M6.1: Compile-Time Interning

**Files:**
- `src/compiler/hir/HIRToLLVM.cpp` - Intern string constants

#### M6.2: Runtime Intern Table

**Files:**
- `src/runtime/include/TsStringIntern.h`
- `src/runtime/src/TsStringIntern.cpp`

---

## Phase 7: Custom Generational GC (Future)

**Prerequisite:** All above phases stable
**Expected Impact:** Better latency, throughput
**Effort:** 8-12 weeks

**Deferred:** This is a major undertaking. Focus on Phases 0-6 first.

---

## Testing Strategy

### Unit Tests (per phase)
```
tests/unit/hir/           # HIR data structure tests
tests/unit/passes/        # Individual optimization pass tests
```

### Golden HIR Tests
```
tests/golden_hir/         # AST → HIR transformation tests
                          # Check HIR text output matches expected
```

### Integration Tests
```
tests/golden_ir/          # Full pipeline: TS → exe → output
                          # Must all pass before/after each phase
```

### Benchmark Regression
```
benchmarks/baseline.json  # Captured before optimization work
benchmarks/current.json   # Current performance

# CI check: fail if >10% regression
```

---

## Implementation Order

| Phase | Weeks | Cumulative Benefit |
|-------|-------|-------------------|
| 0. HIR Foundation | 4-6 | Stability, debuggability |
| 1. Unboxed Arrays | 2-3 | 2-3x array code |
| 2. SMI Integers | 2-3 | 3-5x numeric code |
| 3. Hidden Classes | 4-6 | 5-10x property access |
| 4. Inline Cache | 1-2 | 2-3x method calls |
| 5. Escape Analysis | 2-3 | Reduced GC |
| 6. String Interning | 1 | Faster comparisons |

**Total: 16-24 weeks for full optimization suite**

---

## Success Criteria

- [ ] All golden_ir tests pass at each phase
- [ ] No performance regression >5% on any benchmark
- [ ] Compute benchmarks within 2x of Node.js
- [ ] `--dump-hir` provides debuggable output
- [ ] Each optimization can be disabled via flag

---

## Appendix: Current vs Proposed Pipeline

### Current (Fragile)
```
dump_ast.js → JSON → AST → Analyzer ──────────────> IRGenerator → LLVM
                           │                              │
                           └── types, symbols,            │
                               modules, all               │
                               interleaved ───────────────┘
```

### Proposed (Robust)
```
dump_ast.js → JSON → AST → Analyzer → HIR Builder → HIR
                           │                         │
                           └── types, symbols,       │
                               modules resolved      │
                                                     ▼
                              ┌──────────────────────┴──────────────────────┐
                              │                                              │
                              ▼                                              ▼
                         --dump-hir                                    Optimization
                                                                       Passes
                                                                          │
                                                                          ▼
                                                                    Optimized HIR
                                                                          │
                                                                          ▼
                                                                   HIRToLLVM
                                                                          │
                                                                          ▼
                                                                      LLVM IR
```

---

## References

- [MLIR: Multi-Level IR](https://mlir.llvm.org/) - Inspiration for HIR design
- [Cranelift IR](https://github.com/bytecodealliance/wasmtime/tree/main/cranelift) - Another HIR example
- [V8 TurboFan IR](https://v8.dev/docs/turbofan) - JS-specific IR
- [Sea of Nodes](https://darksi.de/d.sea-of-nodes/) - Alternative IR design
