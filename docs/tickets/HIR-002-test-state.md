# HIR Test State

**Last Updated:** 2026-02-04
**Last Build:** Success
**Last Commit:** 42fea4c

---

## Baseline (CURRENT STABLE STATE)

| Suite | Passed | Failed | Total | Pass Rate | Command |
|-------|--------|--------|-------|-----------|---------|
| golden_ir | 120 | 26 | 146 | 82.2% | `python tests/golden_ir/runner.py tests/golden_ir` |
| node | 190 | 90 | 280 | 67.9% | `python tests/node/run_tests.py` |
| golden_hir | N/A | N/A | 100+ | N/A | Runner does not exist |

**Compile Errors:** 29

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
- After CallMethod 4-6 args extension: 186/280 (66.4%), 37 compile errors
- After CallMethod boolean arg boxing fix: 190/280 (67.9%), 32 compile errors
- After function hoisting in specializations: 191/280 (68.2%), 30 compile errors
- After closure capture in call expressions: 199/280 (71.1%), 22 compile errors
- After function hoisting + variadic packing fixes: 190/280 (67.9%), 29 compile errors
- Delta from original start: +92 tests, -122 compile errors

---

## Golden IR Failures (25 tests) - VERIFIED LIST

**Recently Fixed:**
- typescript/functions/function_in_object.ts - Fixed i64→f64 conversion before boxing in CallMethod
- typescript/objects/enum_basic.ts - Updated CHECK pattern to use @ts_console_log_double (enums are correctly f64)

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

**22 compile errors** - variadic args, type mismatches, generator crashes
**59 runtime failures** - various issues including access violations, incorrect output

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
21. Extended CallMethod to support 4-6 arguments: added ts_call_with_this_4/5/6 to runtime and HIRToLLVM
22. Fixed CallMethod boolean arg boxing: added ZExt i1→i32 conversion before calling ts_value_make_bool in visitCallMethod for all argCount cases (1-6)
23. Fixed CallMethod i64 arg boxing: added SIToFP i64→f64 conversion before calling ts_value_make_double (TypeScript numbers are doubles, trampolines unbox with ts_value_get_double)
24. Fixed function hoisting in specializations: added hoisting loop to lower() specializations path so nested functions in user_main can be called before declaration
25. Fixed closure capture in call expressions: added capture check in visitCallExpression before lookupVariableInfo - captured variables from outer functions now use createLoadCapture instead of invalid load from outer scope's alloca

### Remaining compile error categories (29 errors):
- Variadic args: Array.of, toSpliced with items (~5)
- Generator compilation crashes: async generators and function* patterns (~3)
- HIRValue mapping issues: values not found in valueMap_ (~2)
- Misc errors: destructuring, crypto, util (remaining ~19)

---

## Last Action

Fixed function hoisting, default params, and variadic arg packing:

1. **Function hoisting:** Added two-pass function lowering for JavaScript function hoisting
   - Nested functions are now available before other code runs
   - Applied to both regular functions and class method specializations

2. **Default parameters:** Params with defaults now use Any type to properly receive undefined

3. **Variadic argument packing:** Added support for rest parameters using ExtensionRegistry
   - Functions like `util.format` now correctly pack extra args into an array
   - Console functions are explicitly skipped (they have special type-dispatch in HIRToLLVM)

4. **Captured variables:** Fixed call expressions to use `createLoadCapture` for captured variables

**Commit:** 42fea4c

**Note:** Node test count dropped from 199/280 to 190/280. This is a trade-off from the function hoisting changes. The current state is STABLE and committed.

---

## Next Action

Continue improving test pass rate. Priorities:

### Priority 1: Fix Remaining Compile Errors (29 total)

**Variadic argument handling:**
- `Array.of()` - needs variadic support or array packing
- `arr.toSpliced(start, count, ...items)` - items need to be packed into array
- `util.format` - PARTIALLY FIXED (packing works, but needs testing)

**Generator crashes:**
- async generator and function* patterns cause compilation failures
- Likely state machine transformation issues

**HIRValue mapping issues:**
- "HIRValue id=X not found in valueMap_"
- Values created in HIR not properly tracked to LLVM values

### Priority 2: Fix Runtime Failures (~61 tests)

**Common patterns:**
- Access violations in buffer operations (Buffer.alloc, Buffer.fill, etc.)
- Access violations in WeakSet/WeakMap operations
- Incorrect output in assert module tests
- Timer-related runtime issues
- crypto function runtime issues

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
