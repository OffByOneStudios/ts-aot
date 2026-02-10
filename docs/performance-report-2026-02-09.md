# ts-aot Performance Report

**Date:** 2026-02-09
**Compiler:** ts-aot (LLVM 18, Boehm GC, ICU strings)
**Baseline:** Node.js v20 LTS (V8 JIT)
**Platform:** Windows x64
**Binary size:** 2.89 MB (external ICU data)

---

## Executive Summary

ts-aot compiled binaries achieve competitive cold start times (~33 ms vs Node.js ~35 ms) but are 3-8000x slower than Node.js on computational and allocation-heavy workloads. The performance gaps are well-understood and map directly to planned optimization phases. The three highest-impact optimizations are: unboxed arrays, hidden classes for objects, and rope strings.

---

## Benchmark Results

### Cold Start

| Metric | ts-aot | Node.js | Ratio |
|--------|--------|---------|-------|
| First run (cold cache) | 88 ms | 35 ms | 2.5x slower |
| Subsequent runs (warm cache) | ~33 ms | ~35 ms | 1.06x faster |

Cold start is the one area where AOT compilation delivers on its promise. With warm filesystem cache, ts-aot binaries start marginally faster than Node.js since there is no JIT warmup or module resolution overhead.

### Recursive Function Calls

| Benchmark | ts-aot | Node.js | Ratio |
|-----------|--------|---------|-------|
| fib(35) recursive | 673 ms | 66 ms | 10.2x slower |
| fib(90) iterative x100k | 21.3 ms | 6.6 ms | 3.2x slower |

### Numerical Computation

| Benchmark | ts-aot | Node.js | Ratio |
|-----------|--------|---------|-------|
| Sieve of Eratosthenes (1M) | 92.3 ms | 13.2 ms | 7.0x slower |
| Matrix multiply (200x200) | 60.8 ms | 11.7 ms | 5.2x slower |

### Sorting

| Benchmark | ts-aot | Node.js | Ratio |
|-----------|--------|---------|-------|
| QuickSort (100k) | 57.2 ms | 4.7 ms | 12.3x slower |
| Array.sort (100k) | 68.8 ms | 15.2 ms | 4.5x slower |

### Allocation

| Benchmark | ts-aot | Node.js | Ratio |
|-----------|--------|---------|-------|
| Object alloc (1M) | 1,142 ms | 5.2 ms | 219x slower |
| Array alloc (10k x 100 push) | 23.8 ms | 3.4 ms | 7.0x slower |
| Map alloc (10k x 10 entries) | 24.0 ms | 5.4 ms | 4.5x slower |
| String concat (100k chars) | 45,605 ms | 5.6 ms | 8,143x slower |

---

## Root Cause Analysis

### 1. Boxed Arrays (affects: all array benchmarks)

**Current:** Every array element is a heap-allocated `TsValue*` (24 bytes: type tag + union). Array access requires pointer indirection and unboxing.

**V8:** Uses "element kinds" to store arrays as contiguous typed buffers (e.g., `PACKED_SMI_ELEMENTS` for integer arrays stores raw int32s inline with no boxing).

**Impact:** 4-12x overhead on array-heavy workloads (sorting, sieve, matrix multiply).

**Solution:** Phase 1 - Unboxed Arrays. Store `number[]` as contiguous `double[]` buffers. Only fall back to boxed elements for mixed-type arrays. Expected improvement: 2-3x.

### 2. All Numbers Are Doubles (affects: integer-heavy computation)

**Current:** TypeScript `number` compiles to `double` everywhere. Even loop counters and array indices are doubles, requiring `fptoui`/`uitofp` conversions at array access boundaries.

**V8:** Uses Small Integer (SMI) tagging. Integers that fit in 31 bits are stored as tagged values with no heap allocation. Integer arithmetic stays in integer registers.

**Impact:** 3-10x overhead on loop-heavy code (fibonacci, sieve).

**Solution:** Phase 2 - SMI / Tagged Integers. Use integer optimization pass to detect integer-only variables and keep them as `i64` throughout. Already partially implemented in `IntegerOptimizationPass` but not applied to array indices or loop-carried values. Expected improvement: 3-5x cumulative.

### 3. TsMap-based Objects (affects: object allocation)

**Current:** Every `{ id: i, value: i * 2, name: "item" }` allocates a `TsMap` (hash table with string keys). Property access is a hash lookup. Each object allocation involves: `ts_alloc(sizeof(TsMap))` + placement new + 3 hash table insertions.

**V8:** Uses hidden classes (Shapes/Maps). Objects with the same property layout share a shape descriptor. Property access is a fixed-offset load from a contiguous struct. Object creation is a single allocation + memcpy of a template.

**Impact:** 219x overhead on object-heavy workloads.

**Solution:** Phase 3 - Hidden Classes / Shapes. For objects with known static shapes (determinable at compile time), allocate as fixed-layout structs. Use shape transitions for dynamic property addition. Expected improvement: 50-100x for this benchmark (bringing it to 2-4x of V8).

### 4. ICU String Concatenation (affects: string operations)

**Current:** `TsString` wraps `icu::UnicodeString`. Each `result = result + "a"` creates a new UnicodeString by copying the old one + appending. This is O(n) per concatenation, making the benchmark O(n^2) = O(10^10) character copies for 100k iterations.

**V8:** Uses rope strings (ConsString). `a + b` creates a lightweight node pointing to both strings without copying. The rope is only flattened when the string content is actually read. This makes concatenation O(1) amortized.

**Impact:** 8,143x overhead. This is the worst regression by far.

**Solution:** Phase 6 - String Interning / Rope Strings. Implement a `TsRopeString` that defers concatenation. Only flatten when content is accessed (e.g., `.length`, `.indexOf()`, conversion to C string). Expected improvement: 1000x+ for this benchmark.

### 5. Boehm GC vs Generational GC (affects: allocation throughput)

**Current:** Boehm GC is a conservative, non-moving collector. It doesn't distinguish young/old generations and has higher allocation overhead than V8's generational GC.

**V8:** Uses a generational garbage collector with a young generation nursery. Short-lived objects (common in benchmarks) are allocated in the nursery and collected cheaply with a scavenge pass.

**Impact:** Contributes to the 7x overhead on array allocation and compounds with the TsMap overhead on object allocation.

**Solution:** Phase 7 - Custom Generational GC. Replace Boehm GC with a two-generation collector that uses bump allocation in the nursery. Expected improvement: 2-3x on allocation-heavy workloads.

### 6. Function Call Overhead (affects: recursive code)

**Current:** Every function call goes through the standard C ABI calling convention. No inlining of recursive calls. No tail call optimization.

**V8:** JIT-compiles hot functions, inlines small callees, and uses on-stack replacement (OSR) to upgrade interpreted code to optimized code mid-execution.

**Impact:** 10x on recursive fibonacci, 3x on iterative loops.

**Solution:** The LLVM `-O2` flag should handle function inlining for non-recursive cases. For recursive functions, the overhead is inherent to AOT compilation (no profile-guided optimization). Consider adding PGO support or loop unrolling hints.

---

## Optimization Priority Matrix

| Phase | Optimization | Target Benchmarks | Expected Speedup | Effort |
|-------|-------------|-------------------|------------------|--------|
| 1 | Unboxed Arrays | sort, sieve, matmul | 2-3x | Medium |
| 2 | SMI / Tagged Integers | fib, sieve, loops | 3-5x cumulative | High |
| 3 | Hidden Classes / Shapes | object alloc | 50-100x | High |
| 4 | Method Inline Cache | method calls | 2-3x | Medium |
| 5 | Escape Analysis | ~~stack alloc~~ | Variable | **Done** |
| 6 | Rope Strings | string concat | 1000x+ | Medium |
| 7 | Generational GC | all allocation | 2-3x | Very High |

### Recommended Order

1. **Rope Strings (Phase 6)** - Low effort, eliminates the worst regression (8143x)
2. **Unboxed Arrays (Phase 1)** - Medium effort, broad impact across many benchmarks
3. **Hidden Classes (Phase 3)** - High effort, eliminates 219x object allocation gap
4. **SMI (Phase 2)** - High effort, improves all numeric computation

---

## Benchmark Methodology

- Each benchmark was run on the same machine sequentially
- ts-aot binaries compiled with `-O2` (LLVM optimization level 2)
- Node.js run without flags (default V8 JIT settings)
- Warmup iterations included where noted
- Times are wall-clock milliseconds from `performance.now()`
- Results are averages of 3-5 runs (see benchmark source files)

### Source Files

| Benchmark | ts-aot source | Node.js source |
|-----------|--------------|----------------|
| Fibonacci | `tmp/bench_fib_simple.ts` | `tmp/bench_fib_node.js` |
| Compute | `tmp/bench_compute.ts` | `tmp/bench_compute_node.js` |
| Sorting | `tmp/bench_sort.ts` | `tmp/bench_sort_node.js` |
| Allocation | `tmp/bench_alloc.ts` | `tmp/bench_alloc_node.js` |
| Cold Start | `tmp/bench_coldstart.exe` | `tmp/bench_coldstart_node.js` |

---

## Compiler Issues Encountered

During benchmarking, several compiler issues were identified:

1. **Compiler crash: `getOperandValue: unknown operand type`** - The original benchmark harness (`examples/benchmarks/`) uses a `BenchmarkSuite` class that stores closures in arrays (`Array<{ name: string; fn: () => void; options?: BenchmarkOptions }>`). This pattern triggers a segfault in HIRToLLVM at `getOperandValue`. Likely caused by storing function references inside object literals inside arrays.

2. **Parser error: `TemplateTail`** - Files using template literals with embedded expressions that span multiple complex sub-expressions fail to parse with the native parser. Affected: `json_parse.ts`, `regex.ts`.

3. **Parser error: `.toFixed()` on expressions** - Calling `.toFixed(2)` on parenthesized numeric expressions like `(x * 1000).toFixed(2)` causes a parser error. The native parser fails to recognize that the `(expr)` result is callable.

4. **Crash on number[] array push/access pattern** - Pushing `performance.now()` results into a `number[]` array and then iterating causes access violations. Root cause under investigation.
