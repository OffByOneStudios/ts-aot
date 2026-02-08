# Active Project State

**Last Updated:** 2026-02-07
**Current Phase:** Extension System Hardening

## Current Focus
1. Fixing extension system calling conventions (self-pointer, constructors, return types)
2. Fixing remaining 45 failing node tests (11 compile errors, 34 runtime failures)
3. Epic plan: `C:\Users\cgrin\.claude\plans\cryptic-exploring-pudding.md`

## Active Tasks
1. **Fix remaining node test failures** - 236/280 passing (84.3%)
2. **Extension constructor support** - Done (systemic fix for 12+ extension types)
3. **Module-level property getters** - Done (http.STATUS_CODES, cluster.isMaster, etc.)

## Recent Accomplishments (2026-02-07)
*   **Extension Constructor Support (systemic fix):**
    - LoweringRegistry now registers constructor lowerings from ext.json
    - ASTToHIR checks ExtensionRegistry for constructors in visitNewExpression
    - `new URL(...)`, `new PerformanceObserver(...)`, etc. now call actual factory functions instead of creating generic TsMap
    - Fixed 12+ extension types: URL, URLSearchParams, PerformanceObserver, AsyncLocalStorage, AsyncResource, StringDecoder, etc.
*   **Auto-prepend self pointer for instance methods/getters:**
    - LoweringRegistry auto-prepends `ptrArg()` for extension instance methods and property getters
    - Fixed HTTP ext.json (33 methods were missing self pointer)
    - Updated all ext.json files to remove redundant leading "ptr" from instance method args
*   **Constructor return type fixes:**
    - Factory functions now return raw pointers (not boxed TsValue*)
    - Fixed: perf_hooks, async_hooks, string_decoder constructors
*   **PerformanceObserver method fixes:**
    - Removed `dynamic_cast` in observe/disconnect/takeRecords (RTTI not shared across libs)
    - Direct cast from auto-prepended self pointer
*   **Module-level property getters:**
    - ASTToHIR now checks ExtensionRegistry for property getters on module objects (e.g., `http.STATUS_CODES`)
    - LoweringRegistry registers object property getter lowerings from ext.json
    - HIRToLLVM `lowerGetElem` uses dynamic property access for `Any`-typed operands with numeric indices
    - ConsoleHandler handles `i32` return types from extension getters (e.g., `cluster.isMaster`)
    - http.cpp getters now return boxed `TsValue*` via `ts_value_make_object()`
    - Fixed: http_status_codes test, cluster_fork compile error
*   **Node tests: 230 → 236/280 (84.3%)** - STATUS_CODES + URL tests + perf_hooks observer
*   **Golden IR: 146/146 (100%)** - no regressions

## Previous Accomplishments (2026-02-06)
*   **Extension Migration Phase 1 (Analyzer Infrastructure) - COMPLETE**
    - Two-pass inheritance resolution, function-typed parameter conversion, ExtensionRegistry-based module checks
    - Golden IR: 146/146 (100%), Node tests: 216/280 (77.1%) - no regressions
*   **Test Runner Baseline Tracking** - Both golden IR and node test runners now save/load JSON baselines for regression detection

## Previous Accomplishments (2026-02-05)
*   **Escape Analysis Pass (Phase 5 perf roadmap):** Stack allocation for non-escaping objects
    - SSA-based EscapeAnalysisPass analyzes def-use chains to find non-escaping NewObject/NewArrayBoxed
    - `alloca` + `ts_map_init_inplace`/`ts_array_init_inplace` replaces `ts_alloc` for non-escaping objects
    - Safety limits: max 4 stack objects, 512 bytes per function; disabled for async/generators
    - sizeof(TsMap)=64, sizeof(TsArray)=56; align 16 on all stack allocations
    - Golden IR tests: 146/146 (100%) - no regressions
*   **Golden IR Tests 100%:** All 146/146 golden IR tests passing
*   **RegExp exec/literal fix:** Added `ts_value_is_null` special case in HIRToLLVM
*   **Decorator fixes (all 3 tests):** Fixed Monomorphizer triple-processing of non-generic classes
*   **IIFE .call() support:** Added `ts_set_call_this`/`ts_get_call_this` runtime functions
*   **String switch lowering:** Fixed computed enum const-eval in HIR pipeline
*   **Destructured parameters:** Fixed function overloads in HIR pipeline
*   **Index signatures:** Fixed string comparison in HIR pipeline
*   **TsClosure boxing:** Fixed for functions stored in object properties

## Previous Accomplishments (2026-01-30)
*   **Bitwise Operations Fixed:** Added f64->i64 conversion in HIRToLLVM for bitwise ops
*   **Dead Code Elimination Fixed:** Added terminator checks to stop statement processing after return/throw
*   **Generator Support:** Full state machine transformation for function* and yield
*   **BigInt Support:** Runtime calls for arbitrary precision arithmetic
*   **Class Expressions:** Fixed naming alignment and vtable registration

## Lessons Learned (for future sessions)
1. **Check all code paths**: When fixing a pattern (like terminator handling), search for ALL places it occurs
2. **Type conversions in lowering**: TypeScript numbers are f64, but some LLVM ops need i64 (bitwise). Always add conversion helpers.
3. **Run full test suite**: After any fix, run the complete test suite to catch regressions
4. **Update docs immediately**: Update ticket and epic docs before committing to maintain accurate project state
5. **LoweringRegistry prevents auto-boxing**: When a runtime function is NOT registered, the generic call path auto-boxes String-typed arguments. Register with `.ptrArg()` to prevent boxing.
6. **ts_value_is_null vs raw nullptr**: `ts_value_is_null()` only checks boxed JavaScript null. For functions returning raw nullptr (like RegExp_exec), generate `ts_value_is_null(arg) || arg == null`.
7. **Monomorphizer and non-generic classes**: Non-generic classes in `classUsages` get triple-processed causing duplicate/conflicting HIR entries. Guard with `if (classNode->typeParameters.empty()) continue;`
8. **Extension constructors return raw pointers**: Factory functions (`ts_*_create`) must return raw pointers, NOT boxed `TsValue*` via `ts_value_make_object()`. The compiler stores the return as a raw ptr and passes it as self to instance methods.
9. **dynamic_cast fails across static libs**: When a class is defined in one .lib and `dynamic_cast` is called from another .lib, RTTI may not be shared. Use direct casts when the type is known from the calling convention.

## Conformance Status
- TypeScript: 99% (117/118 runtime features)
- ECMAScript: 93% (214/230 features)
- Node.js: 99% (1031/1040 features)

## Performance Optimization Roadmap

| Phase | Optimization | Expected Speedup | Status |
|-------|--------------|------------------|--------|
| 1 | Unboxed Arrays | 2-3x | Planned |
| 2 | SMI (Tagged Integers) | 3-5x cumulative | Planned |
| 3 | Hidden Classes/Shapes | 5-10x cumulative | Planned |
| 4 | Method Inline Cache | 6-12x cumulative | Planned |
| 5 | Escape Analysis | Variable | **Done** |
| 6 | String Interning | +10-20% | Planned |
| 7 | Custom Generational GC | Better latency | Future |

See [EPIC-PERF-001](../docs/epics/EPIC-PERF-001-performance-parity.md) for details.
