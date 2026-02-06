# Active Project State

**Last Updated:** 2026-02-06
**Current Phase:** Extension System Migration

## Current Focus
1. Migrating Node.js-specific code from core analyzer/runtime to extension system
2. Epic plan: `C:\Users\cgrin\.claude\plans\cryptic-exploring-pudding.md`

## Active Tasks
1. **Extension Migration Epic** - Phase 1 complete, Phase 2 in progress
2. **Phase 2: Create missing .ext.json contracts** - 12 modules need contracts

## Recent Accomplishments (2026-02-06)
*   **Extension Migration Phase 1 (Analyzer Infrastructure) - COMPLETE**
    - Step 1.1: Two-pass inheritance resolution in `registerTypesFromExtensions()`
    - Step 1.2: Function-typed parameter conversion in `convertExtTypeRef()` for callback inference
    - Step 1.3: Replaced hardcoded `BUILTIN_MODULES` list with `ExtensionRegistry::isRegisteredModule()` query
    - Step 1.4: `getExpectedCallbackType()` now checks registered method signatures before hardcoded patterns
    - Golden IR: 146/146 (100%), Node tests: 216/280 (77.1%) - no regressions
*   **Test Runner Baseline Tracking** - Both golden IR and node test runners now save/load JSON baselines for regression detection
*   **Documentation** - Audit of all Node.js-specific analyzer code (`docs/extensions/audit-nodejs-hardcoding.md`, `docs/extensions/analyzer-hardcoding-analysis.md`)

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
