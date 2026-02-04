# HIR Test State

**Last Updated:** 2026-02-03
**Last Build:** Success

---

## Baseline (CURRENT STATE)

| Suite | Passed | Failed | Total | Pass Rate | Command |
|-------|--------|--------|-------|-----------|---------|
| golden_ir | 121 | 25 | 146 | 82.9% | `python tests/golden_ir/runner.py tests/golden_ir` |
| node | 180 | 100 | 280 | 64.3% | `python tests/node/run_tests.py` |
| golden_hir | N/A | N/A | 100+ | N/A | Runner does not exist |

**Note:** golden_hir has 100+ test files but no runner.py. Tests are manually validated via `--dump-hir`.

**Node Test Progress (cumulative):**
- Previous session start: 98/280 (35.0%), 151 compile errors
- After console fixes: 121/280 (43.2%), 117 compile errors
- After comparison fixes: 141/280 (50.4%), 95 compile errors
- After boxing fixes: 148/280 (52.9%), 87 compile errors
- After ts_array_isArray fix: 152/280 (54.3%), 82 compile errors
- After fetch/require fix: 155/280 (55.4%), 75 compile errors
- After Array.from fix: 156/280 (55.7%), 74 compile errors
- After ts_to_string/ts_path_indexOf fix: 157/280 (56.1%), 72 compile errors
- After Math/JSON fixes: 160/280 (57.1%), 67 compile errors
- After timer function fixes: 174/280 (62.1%), 50 compile errors
- After try/catch block restore fix: 176/280 (62.9%), 48 compile errors
- After Number.isFinite/isNaN/etc inline handlers: 178/280 (63.6%), 45 compile errors
- After crypto ToI64 conversions: 180/280 (64.3%), 43 compile errors
- Delta from original start: +82 tests, -108 compile errors

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

**43 compile errors** - closure capture in user_main, variadic args, type mismatches, generator crashes
**57 runtime failures** - various issues including access violations, incorrect output

### Fixed Issues (cumulative):
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
11. Added ArgConversion::ToI64 for array method index params (toSpliced, with, splice)
12. Added ts_array_from lowering registration
13. Fixed Math function type mismatches (Math.sign, Math.trunc expect f64, not i64)
14. Fixed JSON.stringify type mismatch (needs boxing)
15. Added hardcoded handlers for timer functions (setTimeout, setInterval, setImmediate, clearTimeout, clearInterval, clearImmediate) in HIRToLLVM to handle mangled names and type conversions (double→i64 for delay)
16. Fixed try/catch block restore bug in visitFunctionDeclaration - nested functions inside try blocks now properly restore currentBlock_ instead of entry block
17. Fixed ts_string_at index type conversion (double to i64) in LoweringRegistry
18. Added inline LLVM IR handlers for Number.isFinite, Number.isNaN, Number.isInteger, Number.isSafeInteger in HIRToLLVM
19. Fixed ts_object_is to box both arguments before calling runtime function
20. Added crypto function ToI64 conversions (randomBytes, randomInt, pbkdf2Sync, pbkdf2, scryptSync, scrypt) in LoweringRegistry

### Remaining compile error categories (43 errors):
- Closure capture in user_main: "no closure parameter available" errors (~20)
- Variadic args not packed into array: Array.of, toSpliced with items (~7)
- Call parameter type mismatches: local functions expecting ptr but receiving native types
- dgram tests: CallMethod with 6 args not implemented
- Generator compilation crashes: async generators and function* patterns

---

## Last Action

Added crypto function ToI64 conversions in LoweringRegistry.cpp:
- Added registrations for ts_crypto_randomBytes, ts_crypto_randomInt, ts_crypto_pbkdf2Sync, ts_crypto_pbkdf2, ts_crypto_scryptSync, ts_crypto_scrypt
- All use ArgConversion::ToI64 for integer parameters (size, min, max, iterations, keylen)

Also fixed in this session:
- ts_string_at index type conversion (double to i64)
- Inline LLVM IR handlers for Number.isFinite/isNaN/isInteger/isSafeInteger
- ts_object_is boxing for both arguments

Result: Node tests improved from 176/280 (62.9%) to 180/280 (64.3%), +4 tests, -5 compile errors

---

## Next Action

Continue improving Node test pass rate:

### Priority 1: Fix Remaining Compile Errors (43 total)

**Closure capture in user_main (~20 errors):**
- Error: "LoadCapture 'varname': no closure parameter available"
- Cause: Tests use arrow functions that capture variables from user_main
- Arrow functions in user_main try to capture variables but user_main is not a closure
- May need to promote captured vars to allocas or handle user_main specially

**Variadic argument handling (~7 errors):**
- `Array.of()` - ts_array_of needs to be variadic or use array packing
- `arr.toSpliced(start, count, ...items)` - items need to be packed into array

**Local function type mismatches (~5 errors):**
- Functions like `test_str_any(ptr, i1)` receiving wrong types
- Need to analyze calling context and fix type conversions

**dgram CallMethod errors (~3 errors):**
- CallMethod with 6+ args not implemented in HIRToLLVM
- Need to extend visitCallMethod to handle more argument counts

**Generator crashes (~3 errors):**
- async generator and function* patterns cause compilation failures
- Likely state machine transformation issues

### Priority 2: Fix Runtime Failures (~57 tests)

**Common patterns:**
- Access violations in buffer operations (Buffer.alloc, Buffer.fill, etc.)
- Access violations in WeakSet/WeakMap operations
- Incorrect output in assert module tests
- Timer-related runtime issues (event loop not running, callback not invoked)
- crypto function runtime issues (hash, hmac returning null/undefined)

### Priority 3: Implement Missing Runtime Functions

**Array static methods:**
- `ts_array_of` - Array.of(...items) static method needs variadic support

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
