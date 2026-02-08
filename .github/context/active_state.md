# Active Project State

**Last Updated:** 2026-02-08
**Current Phase:** Extension System Hardening

## Current Focus
1. Fixing extension system type resolution and inheritance
2. Fixing remaining 32 failing node tests (9 compile errors, 23 runtime failures)
3. Epic plan: `C:\Users\cgrin\.claude\plans\cryptic-exploring-pudding.md`

## Active Tasks
1. **Fix remaining node test failures** - 248/280 passing (88.6%)
2. **Extension type inheritance** - Done (property/method/static method inheritance chain walking)
3. **Extension return type patching** - Done (Pass 3 links bare ClassTypes to registered types)

## Recent Accomplishments (2026-02-08)
*   **Buffer indexing (`buf[i]`) in HIRToLLVM:**
    - `lowerGetElem` now detects Buffer-typed operands (HIRTypeKind::Class, className=="Buffer")
    - Routes to `ts_buffer_read_uint8(buf, i)` instead of `ts_array_get(buf, i)` which crashed
    - `lowerSetElem` also handles Buffer: calls `ts_buffer_write_uint8(buf, value, i)` instead of `ts_array_set`
    - Fixed: buffer_advanced, buffer_encoding, buffer_typed_array, buffer_variable_length, crypto_hkdf
*   **Buffer.from([array]) dispatch:**
    - `ts_buffer_from_string` now detects TsArray (magic 0x41525259) and dispatches to `TsBuffer::FromArray`
    - `FromArray` uses `GetElementDouble` instead of `Get` (TS stores numbers as f64)
*   **WeakSet add/has/delete operations:**
    - MapSetHandler now handles WeakSet/WeakMap via className detection
    - ASTToHIR uses `ts_weakset_create` for `new WeakSet()` (was `ts_set_create`)
    - `ts_set_add_wrapper` unboxes context for Set.add chaining support
*   **Node tests: 248/280 (88.6%)**, Golden IR: 146/146 (100%)

*   **Extension inheritance chain walking:**
    - `findProperty`, `findMethod`, `findStaticMethod` in ExtensionRegistry now walk up the `extends` chain
    - E.g., `ClientHttp2Stream.id` resolves via `ClientHttp2Stream` → `Http2Stream` inheritance
    - Fixed http2_client.ts (3/7 → 7/7 passing)
*   **Extension return type patching (Pass 3):**
    - `registerTypesFromExtensions()` now has a third pass that patches method return types
    - `convertExtTypeRef()` creates bare `ClassType(name)` without methods/fields; Pass 3 replaces these with fully-populated registered types
    - Also patches object method return types (e.g., `http2.connect()` → `ClientHttp2Session`)
    - Fixed http2_request.ts (4/6 → 6/6 passing)
*   **Extension class type recognition in ASTToHIR:**
    - `extTypeRefToHIR()` now checks `ExtensionRegistry::isExtensionType()` for class type names
    - Maps extension class types to `HIRType::makeClass(name, 0)` instead of `makeAny()`
*   **Extension property getter return types:**
    - ASTToHIR property getters now use `extTypeRefToHIR(propDef->type)` for return type
    - Previously all extension property getters returned `HIRType::makeAny()`
*   **Extension return type propagation (extTypeRefToHIR):**
    - ASTToHIR now maps ext.json return types to proper HIR types (string→String, number→Int64, etc.)
    - Previously all extension method return types were `HIRType::makeAny()`, losing type information
    - Enables String.length fast path and proper type-aware string interpolation
*   **String.length fast path in HIRToLLVM:**
    - `lowerGetPropStatic` now detects String-typed operands and calls `ts_string_length()` directly
    - Previously always went through `ts_object_get_dynamic()` which returned undefined for raw TsString pointers
*   **Type-aware string interpolation (ts_string_from_value):**
    - Template literal interpolation now dispatches to `ts_string_from_int`/`ts_string_from_double`/`ts_string_from_bool` based on LLVM type
    - Fixes compile errors when interpolating properly-typed numeric values
*   **Readline string casting fixes:**
    - Removed invalid `dynamic_cast<TsString*>` (TsString is NOT a TsObject subclass)
    - Simplified to direct `(TsString*)` cast for "ptr" lowered parameters
    - Fixed readline_basic.ts and readline_getprompt.ts
*   **Node tests: 240/280 (85.7%)**, Golden IR: 146/146 (100%)

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
