# HIR-001: Golden IR Test Failures Tracking

**Status:** In Progress
**Created:** 2026-02-03
**Current Pass Rate:** 63/146 (43.2%)
**Target:** 146/146 (100%)

## Summary

This ticket tracks all golden IR test failures after the HIR pipeline migration. Tests are categorized by failure type to enable systematic fixing.

---

## Category 1: Compilation Failures (20 tests)

These tests fail to compile due to type mismatches, undefined symbols, or IR verification errors.

### ECMAScript Tests

| Test | Status | Error Type | Notes |
|------|--------|------------|-------|
| [ ] `ecmascript/es2015/test_tagged_templates.ts` | BLOCKED | Type mismatch | `i1` passed where `ptr` expected for array operations |
| [ ] `ecmascript/es2015/test_well_known_symbols.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `ecmascript/es2020/test_bigint.ts` | BLOCKED | Type mismatch | `trunc ptr to i32` - BigInt operations on ptr type |
| [ ] `ecmascript/es2020/test_string_matchAll.ts` | BLOCKED | Undefined symbol | `ts_string_matchAll` not implemented |
| [ ] `ecmascript/es2022/test_private_methods.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `ecmascript/es2024/test_promise_withResolvers.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `ecmascript/es2024/test_string_wellformed.ts` | BLOCKED | Unknown | Needs investigation |

### JavaScript Tests

| Test | Status | Error Type | Notes |
|------|--------|------------|-------|
| [ ] `javascript/dynamic_types/typeof_operator.js` | BLOCKED | Unknown | Needs investigation |
| [ ] `javascript/property_access/dynamic_key.js` | BLOCKED | Unknown | Needs investigation |

### TypeScript Tests

| Test | Status | Error Type | Notes |
|------|--------|------------|-------|
| [ ] `typescript/arrays/array_at.ts` | BLOCKED | Type mismatch | `double` passed where `i64` expected |
| [ ] `typescript/arrays/array_reduce.ts` | BLOCKED | Type mismatch | `add ptr %acc, %x` - arithmetic on ptr |
| [ ] `typescript/decorators/accessor_decorator.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/decorators/class_decorator.ts` | BLOCKED | Return type mismatch | Function returns wrong type |
| [ ] `typescript/decorators/method_decorator.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/functions/rest_parameters.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/narrowing/discriminated_unions.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/objects/object_computed_property.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/operators/keyof_operator.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/regression/padend_in_template_literal.ts` | BLOCKED | Unknown | Needs investigation |
| [ ] `typescript/regression/sort_return_type.ts` | BLOCKED | Unknown | Needs investigation |

---

## Category 2: CHECK Pattern Failures (39 tests)

These tests compile and run correctly but CHECK patterns don't match HIR output. Most need pattern updates to match new HIR function names.

### JavaScript Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `javascript/closures/counter_closure.js` | PATTERN | `ts_cell_create` vs `ts_closure_init_capture` | Output correct, pattern mismatch |
| [ ] `javascript/closures/iife_object_literal.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/dynamic_addition.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/dynamic_comparison.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/dynamic_operators.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/loose_equality.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/strict_equality.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/truthiness.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/dynamic_types/type_coercion.js` | PATTERN | Unknown | Needs investigation |
| [ ] `javascript/property_access/set_property.js` | PATTERN | Unknown | Needs investigation |

### TypeScript Array Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `typescript/arrays/array_destructuring.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_every.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_filter.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_find.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_findIndex.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_indexof.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_join.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_literal.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_map.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_some.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/array_spread.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/arrays/for_of_array.ts` | PATTERN | Unknown | Needs investigation |

### TypeScript Control Flow Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `typescript/control_flow/for_loop.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/control_flow/ternary_operator.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/control_flow/while_loop.ts` | PATTERN | Unknown | Needs investigation |

### TypeScript Function Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `typescript/functions/arrow_capture.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/functions/closure_mutable_capture.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/functions/default_parameters.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/functions/function_in_object.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/functions/higher_order_function.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/functions/iife_assignment.ts` | PATTERN | Unknown | Needs investigation |

### TypeScript Object Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `typescript/objects/method_shorthand.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/objects/object_literal.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/objects/object_property_access.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/objects/object_property_assignment.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/objects/object_shorthand_property.ts` | PATTERN | Unknown | Needs investigation |

### TypeScript Regression Tests

| Test | Status | Pattern Issue | Notes |
|------|--------|---------------|-------|
| [ ] `typescript/regression/array_concat.ts` | PATTERN | Unknown | Also has output failure |
| [ ] `typescript/regression/array_includes.ts` | PATTERN | Unknown | Needs investigation |
| [ ] `typescript/regression/mutable_variable_reassign.ts` | PATTERN | `fcmp {{.*}} oeq` | Missing float comparison |

---

## Category 3: Output Validation Failures (24 tests)

These tests compile but produce incorrect output - indicates runtime bugs.

### JavaScript Closure Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `javascript/closures/iife_in_loop.js` | RUNTIME | Unknown | Needs investigation |
| [ ] `javascript/closures/iife_with_call.js` | RUNTIME | Unknown | Needs investigation |
| [ ] `javascript/closures/module_pattern.js` | RUNTIME | Unknown | Needs investigation |
| [ ] `javascript/closures/umd_pattern.js` | RUNTIME | Unknown | Needs investigation |

### JavaScript Property Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `javascript/dynamic_types/in_operator.js` | RUNTIME | Unknown | Likely ts_object_has_property issue |
| [ ] `javascript/property_access/delete_property.js` | RUNTIME | Unknown | Needs investigation |
| [ ] `javascript/property_access/has_property.js` | RUNTIME | Unknown | Needs investigation |

### TypeScript Array Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/arrays/array_concat.ts` | RUNTIME | `6` vs wrong | Array concat returning wrong length |

### TypeScript Decorator Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/decorators/decorator_factory.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/decorators/parameter_decorator.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/decorators/property_decorator.ts` | RUNTIME | Unknown | Needs investigation |

### TypeScript Function Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/functions/destructured_parameters.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/functions/function_overloads.ts` | RUNTIME | Unknown | Needs investigation |

### TypeScript Object Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/objects/getter_setter.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/objects/object_assign.ts` | RUNTIME | Unknown | Needs investigation |

### TypeScript Type Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/computed_enum.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/index_signature_class.ts` | RUNTIME | Unknown | Needs investigation |
| [ ] `typescript/indexable_types.ts` | RUNTIME | Unknown | Needs investigation |

### TypeScript Regression Tests

| Test | Status | Expected vs Actual | Notes |
|------|--------|-------------------|-------|
| [ ] `typescript/regression/benchmark_timing.ts` | RUNTIME | `elapsed_positive: false` | Timer not working correctly |
| [ ] `typescript/regression/class_method_optional_params.ts` | RUNTIME | `Count: 0` vs `Count: 2` | Optional param handling |
| [ ] `typescript/regression/json_double_roundtrip.ts` | RUNTIME | `roundtrip: FAIL` | Float comparison issue |
| [ ] `typescript/regression/local_class_callback.ts` | RUNTIME | Missing executions | Callback not being called |
| [ ] `typescript/regression/math_min_max_boxed.ts` | RUNTIME | `max: 50` vs `max: 95` | Math.max returning wrong value |
| [ ] `typescript/regression/regexp_exec_global.ts` | RUNTIME | `Found 0 matches` vs `Found 3 matches` | RegExp exec not working |

---

## Priority Order

1. **Compilation Failures** - These block any progress on those tests
2. **Output Validation Failures** - These indicate real bugs affecting correctness
3. **CHECK Pattern Failures** - These are mostly cosmetic (output is correct)

## Common Root Causes to Investigate

### Type Mismatches in HIR
- `i1` vs `ptr` for boolean results used as objects
- `double` vs `i64` for numeric operations
- `ptr` used in arithmetic operations

### Missing Runtime Functions
- `ts_string_matchAll`

### HIR Pattern Changes
- `ts_cell_create` → `ts_closure_init_capture`
- Different function naming conventions

---

## Progress Log

| Date | Tests Fixed | Pass Rate | Notes |
|------|-------------|-----------|-------|
| 2026-02-03 | 0 | 43.2% | Initial ticket created |

---

## Related Files

- `src/compiler/hir/HIRToLLVM.cpp` - HIR to LLVM lowering
- `src/compiler/hir/HIRBuilder.cpp` - HIR generation
- `src/runtime/src/TsObject.cpp` - Runtime object operations
- `tests/golden_ir/` - Test files
