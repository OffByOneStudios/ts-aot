# HIR Test State

**Last Updated:** 2026-02-03
**Last Build:** Success

---

## Baseline (CURRENT STATE)

| Suite | Passed | Failed | Total | Pass Rate | Command |
|-------|--------|--------|-------|-----------|---------|
| golden_ir | 121 | 25 | 146 | 82.9% | `python tests/golden_ir/runner.py tests/golden_ir` |
| node | 155 | 125 | 280 | 55.4% | `python tests/node/run_tests.py` |
| golden_hir | N/A | N/A | 100+ | N/A | Runner does not exist |

**Note:** golden_hir has 100+ test files but no runner.py. Tests are manually validated via `--dump-hir`.

**Node Test Progress (this session):**
- Previous session: 98/280 (35.0%), 151 compile errors
- After console fixes: 121/280 (43.2%), 117 compile errors
- After comparison fixes: 141/280 (50.4%), 95 compile errors
- After boxing fixes: 148/280 (52.9%), 87 compile errors
- After ts_array_isArray fix: 152/280 (54.3%), 82 compile errors
- After fetch/require fix: 155/280 (55.4%), 75 compile errors
- Delta from session start: +57 tests, -76 compile errors

---

## Golden IR Failures (25 tests) - VERIFIED LIST

### Category 1: IIFE Call Patterns (3 tests)
**Root cause:** IIFE .call() pattern returns undefined
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| javascript/closures/iife_with_call.js | expected '42', got 'undefined' |
| javascript/closures/module_pattern.js | expected secret, got undefined |
| javascript/closures/umd_pattern.js | Exit code 1 |

### Category 2: Decorators (6 tests)
**Root cause:** Decorator invocation not working correctly
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/decorators/accessor_decorator.ts | Output validation failed |
| typescript/decorators/class_decorator.ts | Compilation failed |
| typescript/decorators/decorator_factory.ts | Compilation failed |
| typescript/decorators/method_decorator.ts | Output validation failed |
| typescript/decorators/parameter_decorator.ts | Output validation failed |
| typescript/decorators/property_decorator.ts | Output validation failed |

### Category 3: Array Predicate Callbacks (4 tests)
**Root cause:** Array methods with predicate functions
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/arrays/array_filter.ts | Output mismatch |
| typescript/arrays/array_find.ts | Output mismatch |
| typescript/arrays/array_findIndex.ts | Output mismatch |
| typescript/arrays/array_some.ts | Output mismatch |

### Category 4: Compilation Failures (3 tests)
**Root cause:** Codegen/linker issues
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| ecmascript/es2024/test_groupBy.ts | Compilation failed |
| ecmascript/es2024/test_promise_withResolvers.ts | Compilation failed |
| typescript/regression/closure_capture_string_length.ts | Compilation failed |

### Category 5: Indexable Types (2 tests)
**Root cause:** Index signature handling
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/index_signature_class.ts | Output validation failed |
| typescript/indexable_types.ts | Output validation failed |

### Category 6: Closure CHECK Patterns (1 test)
**Root cause:** CHECK pattern @ts_closure_get_func not found
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/functions/closure_mutable_capture.ts | CHECK pattern not found |

### Category 7: Function Features (3 tests)
**Root cause:** Various function codegen issues
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/functions/destructured_parameters.ts | expected '3', got '0' |
| typescript/functions/function_overloads.ts | Exit code 1, no output |
| typescript/computed_enum.ts | Output validation failed |

### Category 8: Type Narrowing (1 test)
**Root cause:** Discriminated union narrowing
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/narrowing/discriminated_unions.ts | Output validation failed |

### Category 9: Getter/Setter (1 test)
**Root cause:** Setter returning wrong value
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/objects/getter_setter.ts | expected '10\n20', got '10\n[object Object]' |

### Category 10: RegExp (1 test)
**Root cause:** RegExp.exec global flag
**Status:** NOT_STARTED

| Test | Error |
|------|-------|
| typescript/regression/regexp_exec_global.ts | expected 3 matches, got 0 |

---

## Node Test Failures Summary

**75 compile errors** - mostly due to missing runtime functions or naming mismatches
**50 runtime failures** - various issues including access violations

### Fixed Issues (this session):
1. Added missing console functions (ts_console_assert, ts_console_warn, ts_console_info, ts_console_debug)
2. Fixed Object method naming: BuiltinRegistry now uses snake_case (ts_object_has_own, ts_object_from_entries)
3. Fixed i64/i1 type mismatch in comparison operations (lowerCmpEqI64, lowerCmpNeI64)
4. Fixed logical AND/OR type mismatches (lowerLogicalAnd, lowerLogicalOr)
5. Fixed lowerBoxObject to handle all LLVM types (bool, int, double, pointer)
6. Fixed boolean boxing in closure capture (lowerClosureCreate, emitCaptureStore)
7. Fixed ts_value_make_bool signature mismatch in ArgConversion::Box path
8. Added ts_array_isArray runtime function and lowering registration
9. Added fetch and require as global functions in ASTToHIR
10. Added lowering registrations for fetch -> ts_fetch and require -> ts_require

### Remaining compile error categories:
- Undefined symbol: ts_to_string (1 test)
- Undefined symbol: ts_path_indexOf (1 test)
- Various other module methods still need LoweringRegistry entries

---

## Last Action

Fixed undefined symbols for fetch and require:
1. Added `fetch` and `require` to the list of known globals in ASTToHIR.cpp
2. Added lowering registrations in LoweringRegistry.cpp for fetch -> ts_fetch and require -> ts_require
3. Added ts_array_isArray runtime implementation and lowering registration

Result: Node tests improved from 148/280 (52.9%) to 155/280 (55.4%)

---

## Next Action

Continue improving Node test pass rate:

Recommended priority order:
1. **Fix remaining undefined symbols** - ts_to_string (1 test), ts_path_indexOf (1 test)
2. **Fix runtime failures** - investigate the 50 tests with access violations
3. **Continue lowering registry migration** - add more module method registrations

---

## Regression Check Commands

```bash
# Quick check for function tests
python tests/golden_ir/runner.py tests/golden_ir/typescript/functions/

# Quick check for object tests
python tests/golden_ir/runner.py tests/golden_ir/typescript/objects/

# Full golden_ir suite
python tests/golden_ir/runner.py tests/golden_ir
```

---

## Session Start Checklist

At the start of EVERY session:
1. [ ] Read this file FIRST
2. [ ] Check "Next Action" section
3. [ ] Do NOT rely on conversation summaries for test counts
4. [ ] Update this file after any code changes
