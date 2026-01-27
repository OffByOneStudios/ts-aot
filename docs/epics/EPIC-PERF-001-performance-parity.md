# EPIC-PERF-001: Performance Parity with Node.js

**Status:** Planning
**Priority:** High
**Target:** Achieve within 2x of Node.js performance on compute benchmarks

## Executive Summary

This epic covers the major optimizations needed to bring ts-aot compiled executables to performance parity with Node.js/V8. The optimizations are ordered by impact-to-effort ratio.

## Current Baseline

| Benchmark | ts-aot | Node.js | Ratio |
|-----------|--------|---------|-------|
| fib_recursive(35) | TBD | TBD | TBD |
| json_parse (250KB) | TBD | TBD | TBD |
| sorting (100k) | TBD | TBD | TBD |
| regex match | TBD | TBD | TBD |

*Baseline measurements to be collected before optimization work begins.*

---

## Optimization 1: Hidden Classes / Shapes

### Overview

Replace dynamic hash-map based property access with fixed-layout objects. When object shape is known at compile time, property access becomes a direct memory offset load instead of a hash table lookup.

**Expected Impact:** 5-10x speedup for property-heavy code
**Effort:** High (4-6 weeks)

### Background

V8 uses "Hidden Classes" (also called "Shapes" or "Maps" in other engines) to optimize property access:

```javascript
const obj = { x: 1, y: 2 };  // Shape: { x: offset 0, y: offset 8 }
obj.x;  // Direct load from offset 0, not hash lookup
```

Currently, ts-aot uses `TsMap` (hash table) for ALL object property access, even when the shape is statically known.

### Milestones

#### M1.1: Shape Analysis Infrastructure
**Goal:** Build compile-time shape inference

**Action Items:**
- [ ] Create `Shape` class to represent object layouts
  - Fields: property name → offset mapping
  - Parent shape pointer (for prototype chain)
  - Transition map for shape evolution
- [ ] Add shape inference pass in Analyzer
  - Track object literal shapes
  - Track class instance shapes
  - Detect shape-stable vs shape-unstable objects
- [ ] Add `--dump-shapes` flag for debugging

**Files to Edit:**
- `src/compiler/analysis/Shape.h` (new)
- `src/compiler/analysis/Shape.cpp` (new)
- `src/compiler/analysis/Analyzer.h` - Add shape tracking
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Object literal analysis
- `src/compiler/analysis/Analyzer_Classes.cpp` - Class shape analysis

**Tests:**
- `tests/golden_ir/typescript/shapes/basic_object_shape.ts`
- `tests/golden_ir/typescript/shapes/class_instance_shape.ts`
- `tests/golden_ir/typescript/shapes/nested_object_shape.ts`

#### M1.2: Fixed-Layout Object Codegen
**Goal:** Generate LLVM structs for known shapes

**Action Items:**
- [ ] Create `TsFixedObject` runtime class
  - Fixed memory layout based on shape
  - Fallback to TsMap for dynamic properties
- [ ] Modify IRGenerator to emit struct types for known shapes
- [ ] Generate direct GEP instructions for property access
- [ ] Handle property access on shape-stable objects

**Files to Edit:**
- `src/runtime/include/TsFixedObject.h` (new)
- `src/runtime/src/TsFixedObject.cpp` (new)
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp`
- `src/compiler/codegen/IRGenerator_Expressions.cpp` - Object literal codegen

**Tests:**
- `tests/golden_ir/typescript/shapes/fixed_object_access.ts`
- `tests/golden_ir/typescript/shapes/fixed_object_method.ts`
- `tests/integration/shapes/shape_benchmark.ts`

#### M1.3: Inline Caching for Polymorphic Access
**Goal:** Handle cases where shape is not statically known

**Action Items:**
- [ ] Implement monomorphic inline cache
  - Cache last-seen shape + offset
  - Fast path: shape match → direct access
  - Slow path: hash lookup + cache update
- [ ] Implement polymorphic inline cache (PIC)
  - Support 2-4 shapes per call site
  - Megamorphic fallback to hash table

**Files to Edit:**
- `src/runtime/include/TsInlineCache.h` (new)
- `src/runtime/src/TsInlineCache.cpp` (new)
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp`

**Tests:**
- `tests/golden_ir/typescript/shapes/inline_cache_mono.ts`
- `tests/golden_ir/typescript/shapes/inline_cache_poly.ts`
- `tests/integration/shapes/polymorphic_access.ts`

#### M1.4: Prototype Chain Optimization
**Goal:** Fast prototype property lookup

**Action Items:**
- [ ] Shape-based prototype chain traversal
- [ ] Cache prototype chain lookups
- [ ] Handle `__proto__` mutations (invalidate caches)

**Files to Edit:**
- `src/runtime/src/TsObject.cpp`
- `src/runtime/src/TsFixedObject.cpp`

**Tests:**
- `tests/golden_ir/typescript/shapes/prototype_lookup.ts`
- `tests/golden_ir/typescript/shapes/prototype_mutation.ts`

---

## Optimization 2: Unboxed Arrays

### Overview

When array element type is known and homogeneous, store elements directly without TsValue boxing. A `number[]` becomes a contiguous array of doubles, not an array of TsValue pointers.

**Expected Impact:** 2-5x speedup for array-heavy code
**Effort:** Medium (2-3 weeks)

### Background

Current implementation:
```cpp
// number[] currently stored as:
TsArray {
    std::vector<TsValue*> elements;  // Each element is boxed
}
```

Optimized implementation:
```cpp
// number[] should be:
TsTypedArray<double> {
    double* data;
    size_t length;
}
```

### Milestones

#### M2.1: Array Type Specialization in Analyzer
**Goal:** Track array element types precisely

**Action Items:**
- [ ] Enhance ArrayType to track element homogeneity
- [ ] Add `isHomogeneous` flag to ArrayType
- [ ] Track element type through array operations (push, map, filter)
- [ ] Detect mixed arrays that need boxing

**Files to Edit:**
- `src/compiler/analysis/Type.h` - Enhance ArrayType
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Array literal analysis
- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Array method return types

**Tests:**
- `tests/golden_ir/typescript/arrays/homogeneous_number_array.ts`
- `tests/golden_ir/typescript/arrays/homogeneous_string_array.ts`
- `tests/golden_ir/typescript/arrays/mixed_array_detection.ts`

#### M2.2: Specialized Array Runtime Classes
**Goal:** Create unboxed array implementations

**Action Items:**
- [ ] Create `TsNumberArray` - contiguous double storage
- [ ] Create `TsIntArray` - contiguous int64 storage (for index arrays)
- [ ] Create `TsStringArray` - contiguous TsString* storage
- [ ] Implement all array methods for each specialized type

**Files to Edit:**
- `src/runtime/include/TsNumberArray.h` (new)
- `src/runtime/src/TsNumberArray.cpp` (new)
- `src/runtime/include/TsIntArray.h` (new)
- `src/runtime/src/TsIntArray.cpp` (new)
- `src/runtime/include/TsStringArray.h` (new)
- `src/runtime/src/TsStringArray.cpp` (new)

**Tests:**
- `tests/golden_ir/typescript/arrays/number_array_ops.ts`
- `tests/golden_ir/typescript/arrays/string_array_ops.ts`
- `tests/integration/arrays/array_benchmark.ts`

#### M2.3: Specialized Array Codegen
**Goal:** Generate code for specialized arrays

**Action Items:**
- [ ] Detect homogeneous arrays in IRGenerator
- [ ] Generate specialized array construction calls
- [ ] Generate direct element access (no unboxing)
- [ ] Handle array method calls on specialized arrays

**Files to Edit:**
- `src/compiler/codegen/IRGenerator_Expressions.cpp` - Array literal codegen
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` - Element access
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Array.cpp`

**Tests:**
- `tests/golden_ir/typescript/arrays/specialized_push.ts`
- `tests/golden_ir/typescript/arrays/specialized_map.ts`
- `tests/golden_ir/typescript/arrays/specialized_reduce.ts`

#### M2.4: Array Transition Handling
**Goal:** Handle arrays that change from homogeneous to mixed

**Action Items:**
- [ ] Detect type-widening operations
- [ ] Implement array migration (specialized → boxed)
- [ ] Optimize common case: array stays homogeneous

**Files to Edit:**
- `src/runtime/src/TsNumberArray.cpp`
- `src/runtime/src/TsArray.cpp`

**Tests:**
- `tests/golden_ir/typescript/arrays/array_transition.ts`
- `tests/integration/arrays/mixed_push.ts`

---

## Optimization 3: SMI (Small Integer) Optimization

### Overview

Use tagged pointers to represent small integers inline, avoiding heap allocation. If the lowest bit is 1, the value is a 63-bit signed integer. If 0, it's a pointer.

**Expected Impact:** 2-3x speedup for integer-heavy code
**Effort:** Medium (2-3 weeks)

### Background

Current: All numbers are boxed as `double` or `TsValue*`
Optimized: Integers -2^62 to 2^62-1 stored inline with tag bit

```cpp
// Tagged pointer representation
union TsTaggedValue {
    int64_t smi;      // If (value & 1) == 1, this is (integer << 1) | 1
    TsValue* boxed;   // If (value & 1) == 0, this is a pointer
};
```

### Milestones

#### M3.1: Tagged Value Infrastructure
**Goal:** Create tagged value representation

**Action Items:**
- [ ] Define `TsTaggedValue` type
- [ ] Create inline helper functions:
  - `ts_is_smi(value)` - Check if SMI
  - `ts_smi_to_int(value)` - Extract integer
  - `ts_int_to_smi(value)` - Create SMI
  - `ts_untag(value)` - Get pointer if not SMI
- [ ] Update TsValue to work with tagged values

**Files to Edit:**
- `src/runtime/include/TsTaggedValue.h` (new)
- `src/runtime/include/TsValue.h` - Add tagging support
- `src/runtime/src/TsValue.cpp`

**Tests:**
- `tests/golden_ir/typescript/smi/smi_basic.ts`
- `tests/golden_ir/typescript/smi/smi_overflow.ts`

#### M3.2: SMI-Aware Arithmetic
**Goal:** Fast integer arithmetic without boxing

**Action Items:**
- [ ] Generate SMI fast paths for +, -, *, /, %
- [ ] Check for overflow → fallback to double
- [ ] Optimize loop counters as SMI

**Files to Edit:**
- `src/compiler/codegen/IRGenerator_Expressions_Binary.cpp`
- `src/compiler/codegen/IRGenerator_Statements.cpp` - Loop codegen

**Tests:**
- `tests/golden_ir/typescript/smi/smi_arithmetic.ts`
- `tests/golden_ir/typescript/smi/smi_loop_counter.ts`
- `tests/integration/smi/smi_benchmark.ts`

#### M3.3: SMI-Aware Comparisons
**Goal:** Fast integer comparisons

**Action Items:**
- [ ] Generate SMI fast paths for <, >, <=, >=, ==, !=
- [ ] No boxing needed for SMI vs SMI comparison
- [ ] Mixed SMI/double comparison handling

**Files to Edit:**
- `src/compiler/codegen/IRGenerator_Expressions_Binary.cpp`

**Tests:**
- `tests/golden_ir/typescript/smi/smi_comparison.ts`
- `tests/golden_ir/typescript/smi/smi_mixed_comparison.ts`

#### M3.4: SMI Array Indices
**Goal:** Optimize array indexing with SMI

**Action Items:**
- [ ] Array indices are always SMI (or throw RangeError)
- [ ] Direct index extraction without unboxing
- [ ] Bounds check with SMI

**Files to Edit:**
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp`
- `src/runtime/src/TsArray.cpp`

**Tests:**
- `tests/golden_ir/typescript/smi/smi_array_index.ts`

---

## Optimization 4: Inline Caching for Method Calls

### Overview

Cache method lookup results at call sites. After first lookup, subsequent calls skip the hash table lookup entirely.

**Expected Impact:** 2-3x speedup for method-heavy code
**Effort:** Low-Medium (1-2 weeks)

### Milestones

#### M4.1: Monomorphic Method Cache
**Goal:** Cache single method per call site

**Action Items:**
- [ ] Create `MethodCache` structure
  - Cached shape/class pointer
  - Cached method pointer
- [ ] Generate cache check + fast path
- [ ] Fallback to slow lookup on cache miss

**Files to Edit:**
- `src/runtime/include/TsMethodCache.h` (new)
- `src/runtime/src/TsMethodCache.cpp` (new)
- `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp`

**Tests:**
- `tests/golden_ir/typescript/inline_cache/method_cache_mono.ts`
- `tests/integration/inline_cache/method_benchmark.ts`

#### M4.2: Polymorphic Method Cache
**Goal:** Support 2-4 receiver types per call site

**Action Items:**
- [ ] Extend cache to support multiple entries
- [ ] Linear search through entries (small N is fast)
- [ ] Megamorphic fallback for highly polymorphic sites

**Files to Edit:**
- `src/runtime/src/TsMethodCache.cpp`
- `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp`

**Tests:**
- `tests/golden_ir/typescript/inline_cache/method_cache_poly.ts`
- `tests/integration/inline_cache/polymorphic_methods.ts`

---

## Optimization 5: Escape Analysis + Stack Allocation

### Overview

Objects that don't escape their defining function can be allocated on the stack instead of the heap, avoiding GC overhead entirely.

**Expected Impact:** Variable, reduces GC pressure significantly
**Effort:** Medium (2-3 weeks)

### Milestones

#### M5.1: Escape Analysis Pass
**Goal:** Identify non-escaping allocations

**Action Items:**
- [ ] Create escape analysis pass
- [ ] Track object references through:
  - Function parameters
  - Return values
  - Closure captures
  - Global assignments
- [ ] Mark objects as "definitely escapes" or "may not escape"

**Files to Edit:**
- `src/compiler/analysis/EscapeAnalysis.h` (new)
- `src/compiler/analysis/EscapeAnalysis.cpp` (new)
- `src/compiler/analysis/Analyzer.h`

**Tests:**
- `tests/golden_ir/typescript/escape/no_escape_local.ts`
- `tests/golden_ir/typescript/escape/escape_return.ts`
- `tests/golden_ir/typescript/escape/escape_closure.ts`

#### M5.2: Stack Allocation Codegen
**Goal:** Generate alloca for non-escaping objects

**Action Items:**
- [ ] Use LLVM alloca for non-escaping objects
- [ ] Ensure proper lifetime (no use-after-free)
- [ ] Handle nested non-escaping objects

**Files to Edit:**
- `src/compiler/codegen/IRGenerator_Expressions.cpp`
- `src/compiler/codegen/IRGenerator.h` - Track escape info

**Tests:**
- `tests/golden_ir/typescript/escape/stack_alloc_object.ts`
- `tests/golden_ir/typescript/escape/stack_alloc_array.ts`
- `tests/integration/escape/escape_benchmark.ts`

---

## Optimization 6: String Interning

### Overview

Deduplicate identical strings at runtime. Enables pointer comparison instead of content comparison for equality checks.

**Expected Impact:** 1.5-2x for string-heavy code
**Effort:** Low (1 week)

### Milestones

#### M6.1: String Intern Table
**Goal:** Global intern table for strings

**Action Items:**
- [ ] Create `TsStringInternTable` singleton
- [ ] Intern all string literals at compile time
- [ ] Optional runtime interning for computed strings
- [ ] Use pointer comparison for interned strings

**Files to Edit:**
- `src/runtime/include/TsStringIntern.h` (new)
- `src/runtime/src/TsStringIntern.cpp` (new)
- `src/runtime/src/TsString.cpp`
- `src/compiler/codegen/IRGenerator_Expressions.cpp` - String literal codegen

**Tests:**
- `tests/golden_ir/typescript/strings/string_intern.ts`
- `tests/golden_ir/typescript/strings/string_equality.ts`
- `tests/integration/strings/string_benchmark.ts`

---

## Optimization 7: Custom Generational GC

### Overview

Replace Boehm GC with a custom generational garbage collector optimized for JavaScript allocation patterns (many short-lived objects).

**Expected Impact:** Reduced pause times, better throughput
**Effort:** Very High (8-12 weeks)

### Milestones

#### M7.1: Nursery (Young Generation)
**Goal:** Fast bump-pointer allocation for new objects

**Action Items:**
- [ ] Create nursery memory region (e.g., 4MB)
- [ ] Bump-pointer allocation
- [ ] Write barrier for old→young pointers
- [ ] Minor GC: copy surviving objects to old gen

**Files to Edit:**
- `src/runtime/include/TsGC.h` (new)
- `src/runtime/src/TsGC.cpp` (new)
- `src/runtime/include/GC.h` - Replace Boehm interface
- `src/runtime/src/Memory.cpp`

**Tests:**
- `tests/integration/gc/nursery_alloc.ts`
- `tests/integration/gc/minor_gc.ts`

#### M7.2: Old Generation
**Goal:** Mark-sweep for long-lived objects

**Action Items:**
- [ ] Old generation heap management
- [ ] Mark phase: trace from roots
- [ ] Sweep phase: reclaim unmarked objects
- [ ] Major GC triggering heuristics

**Files to Edit:**
- `src/runtime/src/TsGC.cpp`

**Tests:**
- `tests/integration/gc/major_gc.ts`
- `tests/integration/gc/gc_stress.ts`

#### M7.3: Incremental/Concurrent Collection
**Goal:** Reduce pause times

**Action Items:**
- [ ] Incremental marking
- [ ] Concurrent sweep
- [ ] Tri-color marking with write barriers

**Files to Edit:**
- `src/runtime/src/TsGC.cpp`

**Tests:**
- `tests/integration/gc/incremental_gc.ts`
- `tests/integration/gc/pause_time.ts`

---

## Regression Prevention

### Benchmark Suite

All optimizations must maintain correctness. Run these before and after each optimization:

```powershell
# Correctness tests
python tests/golden_ir/runner.py .
python tests/node/run_tests.py

# Performance benchmarks
build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/fibonacci.ts -o tmp/fib.exe
tmp/fib.exe

build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/json_parse.ts -o tmp/json.exe
tmp/json.exe

build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/sorting.ts -o tmp/sort.exe
tmp/sort.exe

build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/regex.ts -o tmp/regex.exe
tmp/regex.exe
```

### Performance Tracking

Create `benchmarks/results/` directory to track performance over time:

```
benchmarks/results/
├── baseline.json          # Initial measurements
├── opt1_shapes.json       # After hidden classes
├── opt2_arrays.json       # After unboxed arrays
└── ...
```

### CI Integration

Add performance regression detection to CI:
- Fail if any benchmark regresses by >10%
- Warn if any benchmark regresses by >5%

---

## Implementation Order

Based on impact/effort analysis:

| Phase | Optimization | Weeks | Cumulative Speedup |
|-------|--------------|-------|-------------------|
| 1 | Unboxed Arrays (M2) | 2-3 | 2-3x |
| 2 | SMI Optimization (M3) | 2-3 | 3-5x |
| 3 | Hidden Classes (M1) | 4-6 | 5-10x |
| 4 | Method Inline Cache (M4) | 1-2 | 6-12x |
| 5 | Escape Analysis (M5) | 2-3 | Variable |
| 6 | String Interning (M6) | 1 | +10-20% |
| 7 | Custom GC (M7) | 8-12 | Better latency |

**Recommended start:** Optimization 2 (Unboxed Arrays) - highest bang for buck, moderate complexity.

---

## Success Criteria

- [ ] All compute benchmarks within 2x of Node.js
- [ ] No correctness regressions (all tests pass)
- [ ] Startup time remains under 10ms
- [ ] Memory usage within 1.5x of Node.js

---

## References

- [V8 Hidden Classes](https://v8.dev/blog/fast-properties)
- [V8 Elements Kinds](https://v8.dev/blog/elements-kinds)
- [V8 Inline Caches](https://mrale.ph/blog/2015/01/11/whats-up-with-monomorphism.html)
- [SpiderMonkey Shapes](https://spidermonkey.dev/docs/Hidden_Classes)
- [JavaScriptCore Structure](https://webkit.org/blog/7846/concurrent-javascript-it-can-work/)
