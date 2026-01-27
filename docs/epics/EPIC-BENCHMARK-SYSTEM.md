# EPIC: Benchmark System Fixes

**Created:** 2026-01-26
**Status:** In Progress
**Priority:** High

## Overview

The benchmark system (`examples/benchmarks/`) has multiple interrelated issues that cause crashes, incorrect output, or poor performance. This epic tracks all issues and their fixes to ensure work isn't lost across sessions.

---

## Issue Tracker

| ID | Issue | Status | Blocking |
|----|-------|--------|----------|
| BM-01 | Monomorphizer wrong function body | **FIXED** | - |
| BM-02 | GC memory pressure / allocation failures | **PARTIAL** | BM-05 |
| BM-03 | BigInt "unreachable" spam | Open | - |
| BM-04 | Harness summary shows "undefined" | Open | - |
| BM-05 | Sorting QuickSort OOM after iterations | Open | - |
| BM-06 | JSON parse throws exceptions | Open | - |
| BM-07 | Regex stack overflow | Open | - |
| BM-08 | String.length returns 0 for JSON.stringify | Open | BM-06 |

---

## Benchmark Status Matrix

| Benchmark | Compiles | Runs | Correct Output | Notes |
|-----------|----------|------|----------------|-------|
| fibonacci.ts | Yes | Partial | Partial | BigInt unreachable, summary undefined |
| sorting.ts | Yes | Partial | No | OOM on QuickSort after Array.sort |
| json_parse.ts | Yes | No | No | C++ exceptions during parse |
| regex.ts | Yes | No | No | Stack overflow in text generation |
| test_sorting_no_merge.ts | Yes | Yes | Yes | Workaround version works |

---

## FIXED ISSUES

### BM-01: Monomorphizer Wrong Function Body [FIXED]

**Symptom:** Crash when calling functions that share names with benchmark harness functions (e.g., local `formatResult` vs harness `formatResult`).

**Root Cause:** `Monomorphizer::findFunction()` returned the first function with matching name across ALL modules, ignoring which module the call originated from.

**Fix Applied:** (Commit 65a3295)
- Added `modulePath` field to `CallSignature` struct
- Updated `findFunction()` to search calling module first
- Updated all function usage recording sites to include module path

**Files Changed:**
- `src/compiler/analysis/Analyzer.h` - Added modulePath to CallSignature
- `src/compiler/analysis/Monomorphizer.h` - Added modulePath param to findFunction
- `src/compiler/analysis/Monomorphizer.cpp` - Module-aware function lookup
- `src/compiler/analysis/Analyzer_Expressions.cpp` - Record module path
- `src/compiler/analysis/Analyzer_Expressions_Calls.cpp` - Record module path
- `src/compiler/analysis/Analyzer_Expressions_Literals.cpp` - Record module path
- `src/compiler/analysis/Analyzer_Expressions_Access.cpp` - Record module path

**Verification:** `test_reexport.ts` and `test_sorting_no_merge.ts` now work correctly.

---

## OPEN ISSUES

### BM-02: GC Memory Pressure / Allocation Failures [PARTIAL]

**Symptom:** `[FATAL] TsArray: Failed to allocate X bytes` after running several benchmark iterations.

**Root Cause:** Boehm GC not collecting fast enough during tight allocation loops. The benchmark creates many temporary arrays (slices for sorting) that aren't being collected.

**Partial Fix Applied:**
- Added `GC_gcollect()` calls in `ts_alloc` when allocation fails
- Increased GC aggressiveness

**Files to Investigate:**
- `src/runtime/src/Memory.cpp` - GC allocation wrapper
- `src/runtime/include/GC.h` - GC configuration
- `src/runtime/src/TsArray.cpp` - Array allocation

**Remaining Work:**
- [ ] Profile memory usage during benchmark
- [ ] Consider explicit GC.collect() calls between benchmark iterations
- [ ] May need to tune Boehm GC parameters (GC_free_space_divisor, etc.)

---

### BM-03: BigInt "unreachable" Spam

**Symptom:** `fib_iterative(10000)` prints "unreachable" thousands of times during execution.

**Root Cause:** BigInt operations hitting unimplemented code paths (likely comparison or arithmetic operations printing "unreachable" instead of proper handling).

**Files to Investigate:**
- `src/runtime/src/TsBigInt.cpp` - BigInt implementation
- `src/compiler/codegen/IRGenerator_Expressions_Binary.cpp` - BigInt operation codegen

**Suspected Location:** Look for `fprintf(stderr, "unreachable")` or similar in BigInt code paths.

**Work Needed:**
- [ ] Find source of "unreachable" output
- [ ] Implement missing BigInt operations or fix comparison logic

---

### BM-04: Harness Summary Shows "undefined"

**Symptom:** Benchmark summary table shows "undefined" for Avg and P95 columns:
```
| Array.sort (100k) | undefined | undefined |   7.57 |
```

**Root Cause:** `formatTime()` function in harness returns undefined for some timing values, or the result object properties aren't being accessed correctly.

**Files to Investigate:**
- `examples/benchmarks/harness/benchmark.ts` - BenchmarkSuite.printSummary()
- `examples/benchmarks/harness/measure.ts` - formatTime() function

**Suspected Issue:** Type inference losing string type for formatTime return, or property access on result objects returning undefined.

**Work Needed:**
- [ ] Check formatTime return type inference
- [ ] Verify result.timing.avg property access works
- [ ] May be related to BM-08 (string length returning 0)

---

### BM-05: Sorting QuickSort OOM After Iterations

**Symptom:** Full `sorting.ts` crashes with OOM on QuickSort AFTER Array.sort completes successfully.

**Root Cause:** Combination of:
1. Array.sort creates many temporary arrays during merge sort
2. GC not collecting between benchmark runs
3. QuickSort then tries to allocate and fails

**Difference from test_sorting_no_merge.ts:** The test version has fewer iterations and no MergeSort, reducing memory pressure.

**Files to Investigate:**
- `examples/benchmarks/compute/sorting.ts` - Check iteration counts
- `src/runtime/src/TsArray.cpp` - Array allocation during slice()

**Work Needed:**
- [ ] Add explicit GC between benchmark runs in harness
- [ ] Reduce iteration counts or add memory guards
- [ ] Related to BM-02

---

### BM-06: JSON Parse Throws Exceptions

**Symptom:**
```
[ts-aot] VectoredException: code=0xe06d7363 addr=00007FFBE1E5A80A module=KERNELBASE.dll
Round-trip verification: FAIL
```

**Root Cause:** C++ exception being thrown during JSON parsing or stringify operations. The exception code `0xe06d7363` is a C++ exception.

**Files to Investigate:**
- `src/runtime/src/TsJSON.cpp` - JSON parse/stringify implementation
- Look for throw statements or std::exception usage

**Suspected Issues:**
1. Invalid JSON being generated
2. Type mismatch during parsing
3. Previous fix for double corruption may have side effects

**Work Needed:**
- [ ] Add debug logging to TsJSON.cpp
- [ ] Create minimal reproduction case
- [ ] Check if related to BM-08 (string length)

---

### BM-07: Regex Stack Overflow

**Symptom:** Stack overflow (0xC00000FD) during "Generating test text..." phase before any regex operations.

**Root Cause:** Cumulative stack exhaustion from:
1. Global regex pattern compilation at module initialization (5 complex patterns)
2. ICU regex compilation uses significant stack
3. `generateText()` creates nested arrays, further consuming stack

**Files to Investigate:**
- `examples/benchmarks/compute/regex.ts` - Global regex patterns at module scope
- `src/runtime/src/TsRegExp.cpp` - ICU regex compilation

**Potential Fixes:**
1. **Quick fix:** Move global regex patterns inside functions (lazy compilation)
2. **Better fix:** Reduce generateText() complexity
3. **Best fix:** Investigate ICU regex stack usage, possibly increase stack size

**Work Needed:**
- [ ] Move regex patterns to local scope in benchmark
- [ ] OR increase default stack size in linker
- [ ] Profile ICU regex compilation stack usage

---

### BM-08: String.length Returns 0 for JSON.stringify Result

**Symptom:** JSON benchmark shows "0.0 KB" for all JSON sizes, indicating string.length returns 0.

**Root Cause:** Type inference loses string type for `JSON.stringify()` return value:
1. `JSON.stringify()` return may be inferred as `Any` instead of `String`
2. When type is `Any`, `ts_value_length()` is called instead of direct string length
3. `ts_value_length()` returns 0 for unrecognized values

**Files to Investigate:**
- `src/compiler/analysis/Analyzer_StdLib_JSON.cpp` - JSON.stringify return type
- `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` - .length property access
- `src/runtime/src/TsObject.cpp` - ts_value_length() implementation

**Work Needed:**
- [ ] Ensure JSON.stringify analyzer returns TypeKind::String
- [ ] Verify type propagates through variable assignment
- [ ] Fix ts_value_length fallback for raw TsString pointers

---

## Test Files

Location: `tmp/` (not committed, for debugging)

| File | Purpose | Status |
|------|---------|--------|
| test_reexport.ts | Tests re-exported function calls | PASSING |
| test_reexport_renamed.ts | Same but with renamed local function | PASSING |
| test_sorting_no_merge.ts | Sorting without MergeSort | PASSING |
| test_cross_module.ts | Cross-module function calls | PASSING |
| test_double_array.ts | Array of small doubles | PASSING |

---

## Architecture Notes

### Benchmark Harness Structure
```
examples/benchmarks/
├── harness/
│   ├── benchmark.ts    # BenchmarkSuite class, formatTime re-export
│   └── measure.ts      # formatTime, formatBytes, TimingResult
└── compute/
    ├── fibonacci.ts    # Uses BenchmarkSuite
    ├── sorting.ts      # Uses BenchmarkSuite
    ├── json_parse.ts   # Uses BenchmarkSuite
    └── regex.ts        # Uses BenchmarkSuite
```

### Key Runtime Files
```
src/runtime/src/
├── TsArray.cpp         # Array allocation, slice, sort
├── TsBigInt.cpp        # BigInt operations
├── TsJSON.cpp          # JSON parse/stringify
├── TsRegExp.cpp        # ICU regex wrapper
├── TsObject.cpp        # Property access, ts_value_length
└── Memory.cpp          # GC allocation wrapper
```

### Key Compiler Files
```
src/compiler/analysis/
├── Monomorphizer.cpp   # Function specialization
├── Analyzer_StdLib_JSON.cpp  # JSON type definitions
└── Analyzer_Expressions*.cpp # Type inference

src/compiler/codegen/
├── IRGenerator_Expressions_Access.cpp  # .length access
└── IRGenerator_Expressions_Binary.cpp  # BigInt ops
```

---

## Session Log

### Session 2026-01-26 (Current)
- **Fixed:** BM-01 (Monomorphizer wrong function body)
- **Tested:** All 4 compute benchmarks
- **Result:** fibonacci partial, sorting partial, json_parse failing, regex failing
- **Created:** This epic document

### Previous Sessions
- Fixed array element assignment
- Fixed ts_array_set_dynamic
- Fixed closure-captured array unboxing
- Fixed QuickSort stack overflow (increased stack size)
- Partial fix for GC memory pressure

---

## Next Steps (Priority Order)

1. **BM-07 (Regex):** Quick fix - move global patterns to local scope
2. **BM-08 (String length):** Fix JSON.stringify return type
3. **BM-06 (JSON exceptions):** Debug after BM-08 is fixed
4. **BM-03 (BigInt):** Find and fix "unreachable" source
5. **BM-02/BM-05 (GC):** Add explicit GC calls in harness

---

## Commands Reference

```powershell
# Build compiler
cmake --build build --target ts-aot --config Release

# Build runtime
cmake --build build --target tsruntime --config Release

# Compile benchmark
build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/sorting.ts -o tmp/sorting.exe

# Run with timeout
timeout 30 tmp/sorting.exe

# Run golden IR tests
python tests/golden_ir/runner.py tests/golden_ir

# Debug crash
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath tmp/crash.exe
```
