# HIR-001: Golden HIR Test Suite

**Status:** In Progress
**Priority:** High
**Category:** Testing Infrastructure
**Last Updated:** 2026-01-29

## Current Progress

**Tests Implemented: 32 / 183 (17%)**

### Completed Test Files

| Category | Files | Count |
|----------|-------|-------|
| AST→HIR Expressions | integer_arithmetic, comparison_ops, boolean_ops, array_ops, unary_ops, property_access, call_expr, string_ops | 8 |
| AST→HIR Statements | if_else, while_loop, for_loop, variable_decl, do_while, switch_stmt, for_of, for_in*, break_continue, continue_stmt, labeled_stmt | 11 |
| AST→HIR Functions | basic_function, closure*, arrows*, declarations*, mutable_closure* | 5 |
| AST→HIR Classes | basic_class | 1 |
| HIR Passes | constant_folding, builtin_resolution, method_resolution, array_method_resolution, math_builtin_resolution | 5 |
| HIR→LLVM Lowering | arithmetic_to_llvm, control_flow_to_llvm | 2 |

*Note: Files marked with * are XFAIL tests tracking known HIR-to-LLVM lowering bugs:*
- `for_in`: HIR correct, but lowering doesn't emit console.log output in for-in body
- `closure`: HIR pipeline does not yet support closure lowering
- `arrows`: Crash when calling stored function pointer
- `declarations`: Uses neg.i64 for float negation, should be neg.f64
- `mutable_closure`: Type mismatch in closure return lowering

## Overview

Implement comprehensive golden tests for the HIR system to ensure correctness of:
1. AST → HIR conversion (ASTToHIR)
2. HIR passes (MethodResolution, BuiltinResolution, TypePropagation, etc.)
3. HIR → LLVM lowering (HIRToLLVM)

## Phase 1: Test Infrastructure

### 1.1 Update Test Runner

Modify `tests/golden_ir/runner.py` to support HIR-CHECK directives:

```python
# Add to runner.py
HIR_CHECK_PATTERNS = [
    'HIR-CHECK:',
    'HIR-CHECK-NOT:',
    'HIR-CHECK-NEXT:',
    'HIR-CHECK-DAG:',
]

def validate_hir_output(hir_output, test_file):
    """Validate HIR output against HIR-CHECK patterns"""
    # Parse HIR-CHECK directives from test file
    # Match patterns against --dump-hir output
    pass
```

**Tasks:**
- [x] Add HIR pattern parsing to runner.py
- [x] Support `--dump-hir` flag detection in RUN lines
- [x] Separate HIR validation from IR validation
- [x] Add combined mode for full pipeline testing

### 1.2 Directory Structure

```bash
mkdir -p tests/golden_hir/{ast_to_hir,passes,lowering}
mkdir -p tests/golden_hir/ast_to_hir/{expressions,statements,functions,classes,other}
mkdir -p tests/golden_hir/passes/{method_resolution,builtin_resolution,type_propagation,constant_folding,dead_code}
mkdir -p tests/golden_hir/lowering/{arithmetic,control_flow,calls,objects,arrays,closures}
```

---

## Phase 2: ASTToHIR Tests

### 2.1 Expressions (15 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| binary_add_int | `a + b` (numbers) | `AddI64` | `expressions/binary_ops.ts` |
| binary_add_float | `a + b` (floats) | `AddF64` | `expressions/binary_ops.ts` |
| binary_sub | `a - b` | `SubI64/SubF64` | `expressions/binary_ops.ts` |
| binary_mul | `a * b` | `MulI64/MulF64` | `expressions/binary_ops.ts` |
| binary_div | `a / b` | `DivI64/DivF64` | `expressions/binary_ops.ts` |
| binary_mod | `a % b` | `ModI64/ModF64` | `expressions/binary_ops.ts` |
| binary_and | `a && b` | `LogicalAnd` | `expressions/binary_ops.ts` |
| binary_or | `a \|\| b` | `LogicalOr` | `expressions/binary_ops.ts` |
| compare_eq | `a === b` | `CmpEqI64/CmpEqPtr` | `expressions/binary_ops.ts` |
| compare_lt | `a < b` | `CmpLtI64/CmpLtF64` | `expressions/binary_ops.ts` |
| bitwise_and | `a & b` | `AndI64` | `expressions/binary_ops.ts` |
| bitwise_or | `a \| b` | `OrI64` | `expressions/binary_ops.ts` |
| bitwise_xor | `a ^ b` | `XorI64` | `expressions/binary_ops.ts` |
| shift_left | `a << b` | `ShlI64` | `expressions/binary_ops.ts` |
| shift_right | `a >> b` | `ShrI64` | `expressions/binary_ops.ts` |

### 2.2 Unary Expressions (8 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| unary_neg_int | `-x` (int) | `NegI64` | `expressions/unary_ops.ts` |
| unary_neg_float | `-x` (float) | `NegF64` | `expressions/unary_ops.ts` |
| unary_not | `!x` | `LogicalNot` | `expressions/unary_ops.ts` |
| unary_bitwise_not | `~x` | `NotI64` | `expressions/unary_ops.ts` |
| unary_typeof | `typeof x` | `TypeOf` | `expressions/unary_ops.ts` |
| prefix_increment | `++x` | `AddI64` + `Store` | `expressions/unary_ops.ts` |
| postfix_increment | `x++` | `Copy` + `AddI64` + `Store` | `expressions/unary_ops.ts` |
| prefix_decrement | `--x` | `SubI64` + `Store` | `expressions/unary_ops.ts` |

### 2.3 Call Expressions (10 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| direct_call | `foo()` | `Call` | `expressions/calls.ts` |
| method_call | `obj.method()` | `CallMethod` | `expressions/calls.ts` |
| call_with_args | `foo(a, b)` | `Call` with operands | `expressions/calls.ts` |
| call_spread | `foo(...args)` | `Call` with spread handling | `expressions/calls.ts` |
| new_expression | `new Foo()` | `NewObject` + `Call` constructor | `expressions/calls.ts` |
| super_call | `super()` | `Call` to parent constructor | `expressions/calls.ts` |
| iife | `(() => 42)()` | `Call` + closure | `expressions/calls.ts` |
| callback | `arr.map(x => x)` | `CallMethod` + closure arg | `expressions/calls.ts` |
| optional_call | `foo?.()` | `CondBranch` + `Call` | `expressions/calls.ts` |
| tagged_template | `` tag`text` `` | `Call` with template args | `expressions/calls.ts` |

### 2.4 Property/Element Access (8 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| property_get | `obj.prop` | `GetPropStatic` | `expressions/property_access.ts` |
| property_set | `obj.prop = x` | `SetPropStatic` | `expressions/property_access.ts` |
| property_dynamic | `obj[key]` | `GetPropDynamic` | `expressions/property_access.ts` |
| element_get | `arr[0]` | `GetElem` | `expressions/element_access.ts` |
| element_set | `arr[0] = x` | `SetElem` | `expressions/element_access.ts` |
| typed_element | `typedArr[0]` | `GetElemTyped` | `expressions/element_access.ts` |
| optional_chain | `obj?.prop` | `CondBranch` + `GetPropStatic` | `expressions/property_access.ts` |
| private_field | `this.#field` | `GetPropStatic` (mangled) | `expressions/property_access.ts` |

### 2.5 Control Flow Statements (12 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| if_simple | `if (x) { }` | `CondBranch` | `statements/if_else.ts` |
| if_else | `if (x) { } else { }` | `CondBranch` + blocks | `statements/if_else.ts` |
| if_else_if | `if (x) { } else if (y) { }` | Multiple `CondBranch` | `statements/if_else.ts` |
| while_loop | `while (x) { }` | `CondBranch` + back edge | `statements/loops.ts` |
| for_loop | `for (let i = 0; i < n; i++)` | Init + `CondBranch` + update | `statements/loops.ts` |
| for_of | `for (const x of arr)` | Iterator protocol | `statements/loops.ts` |
| for_in | `for (const k in obj)` | Object key iteration | `statements/loops.ts` |
| do_while | `do { } while (x)` | Block + `CondBranch` | `statements/loops.ts` |
| break | `break` | `Branch` to exit | `statements/loops.ts` |
| continue | `continue` | `Branch` to header | `statements/loops.ts` |
| switch | `switch (x) { case 1: }` | `Switch` | `statements/switch.ts` |
| switch_default | `default:` in switch | `Switch` with default | `statements/switch.ts` |

### 2.6 Variable Declarations (6 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| let_decl | `let x = 1` | `Alloca` + `Store` | `statements/variable_decl.ts` |
| const_decl | `const x = 1` | `Alloca` + `Store` | `statements/variable_decl.ts` |
| destructure_array | `const [a, b] = arr` | Multiple `GetElem` + `Store` | `statements/variable_decl.ts` |
| destructure_object | `const {a, b} = obj` | Multiple `GetPropStatic` + `Store` | `statements/variable_decl.ts` |
| default_value | `const {a = 1} = obj` | `CondBranch` for undefined check | `statements/variable_decl.ts` |
| rest_element | `const [a, ...rest] = arr` | Slice operation | `statements/variable_decl.ts` |

### 2.7 Functions (12 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| function_decl | `function foo() { }` | Function in module | `functions/declarations.ts` |
| function_params | `function foo(a, b)` | Params in function | `functions/declarations.ts` |
| function_return | `return x` | `Return` | `functions/declarations.ts` |
| arrow_simple | `x => x` | Anonymous function | `functions/arrows.ts` |
| arrow_block | `x => { return x }` | Anonymous function with block | `functions/arrows.ts` |
| closure_capture | `const fn = () => x` | `MakeClosure` + captures | `functions/closures.ts` |
| closure_mutable | `let x; () => x++` | Mutable capture | `functions/closures.ts` |
| generator | `function* gen()` | Generator function | `functions/generators.ts` |
| yield | `yield x` | `Yield` | `functions/generators.ts` |
| yield_star | `yield* other` | `YieldStar` | `functions/generators.ts` |
| async | `async function foo()` | Async function | `functions/async.ts` |
| await | `await promise` | `Await` | `functions/async.ts` |

### 2.8 Classes (12 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| class_decl | `class Foo { }` | Class in module with shape | `classes/declarations.ts` |
| class_expr | `const Foo = class { }` | Anonymous class | `classes/expressions.ts` |
| constructor | `constructor() { }` | Constructor method | `classes/declarations.ts` |
| instance_method | `method() { }` | Method in vtable | `classes/methods.ts` |
| static_method | `static method() { }` | Static method | `classes/methods.ts` |
| instance_property | `prop = 1` | Property in shape | `classes/properties.ts` |
| static_property | `static prop = 1` | Global variable | `classes/properties.ts` |
| extends | `class B extends A` | Inheritance chain | `classes/inheritance.ts` |
| super_call | `super()` | Parent constructor call | `classes/inheritance.ts` |
| super_method | `super.method()` | Parent method call | `classes/inheritance.ts` |
| private_field | `#field = 1` | Private property | `classes/private_fields.ts` |
| static_block | `static { }` | Static initializer | `classes/static_blocks.ts` |

### 2.9 Other Constructs (8 tests)

| Test | TypeScript Construct | Expected HIR | File |
|------|---------------------|--------------|------|
| try_catch | `try { } catch { }` | `SetupTry` + `GetException` | `statements/try_catch.ts` |
| throw | `throw new Error()` | `Throw` | `statements/try_catch.ts` |
| regexp | `/pattern/flags` | `Call ts_regexp_from_literal` | `other/regexp.ts` |
| bigint | `123n` | `Call ts_bigint_create_str` | `other/bigint.ts` |
| jsx_element | `<div />` | `Call ts_jsx_create_element` | `other/jsx.ts` |
| jsx_children | `<div>{x}</div>` | JSX with children | `other/jsx.ts` |
| spread_array | `[...arr]` | Spread in array | `other/spread.ts` |
| spread_object | `{...obj}` | Spread in object | `other/spread.ts` |

---

## Phase 3: HIR Pass Tests

### 3.1 MethodResolutionPass (20 tests)

Tests that method calls on typed receivers are resolved to specialized opcodes.

| Test | Input | Expected Transformation | File |
|------|-------|------------------------|------|
| array_push | `arr.push(x)` | `CallMethod` → `ArrayPush` | `method_resolution/array_methods.ts` |
| array_pop | `arr.pop()` | `CallMethod` → `Call ts_array_pop` | `method_resolution/array_methods.ts` |
| array_shift | `arr.shift()` | `CallMethod` → `Call ts_array_shift` | `method_resolution/array_methods.ts` |
| array_unshift | `arr.unshift(x)` | `CallMethod` → `Call ts_array_unshift` | `method_resolution/array_methods.ts` |
| array_map | `arr.map(fn)` | `CallMethod` → `Call ts_array_map` | `method_resolution/array_methods.ts` |
| array_filter | `arr.filter(fn)` | `CallMethod` → `Call ts_array_filter` | `method_resolution/array_methods.ts` |
| array_reduce | `arr.reduce(fn, init)` | `CallMethod` → `Call ts_array_reduce` | `method_resolution/array_methods.ts` |
| array_find | `arr.find(fn)` | `CallMethod` → `Call ts_array_find` | `method_resolution/array_methods.ts` |
| array_indexOf | `arr.indexOf(x)` | `CallMethod` → `Call ts_array_indexOf` | `method_resolution/array_methods.ts` |
| array_join | `arr.join(',')` | `CallMethod` → `Call ts_array_join` | `method_resolution/array_methods.ts` |
| string_charAt | `str.charAt(i)` | `CallMethod` → `Call ts_string_charAt` | `method_resolution/string_methods.ts` |
| string_substring | `str.substring(a, b)` | `CallMethod` → `Call ts_string_substring` | `method_resolution/string_methods.ts` |
| string_split | `str.split(',')` | `CallMethod` → `Call ts_string_split` | `method_resolution/string_methods.ts` |
| string_toLowerCase | `str.toLowerCase()` | `CallMethod` → `Call ts_string_toLowerCase` | `method_resolution/string_methods.ts` |
| map_get | `map.get(key)` | `CallMethod` → `Call ts_map_get` | `method_resolution/map_methods.ts` |
| map_set | `map.set(key, val)` | `CallMethod` → `Call ts_map_set` | `method_resolution/map_methods.ts` |
| map_has | `map.has(key)` | `CallMethod` → `Call ts_map_has` | `method_resolution/map_methods.ts` |
| set_add | `set.add(val)` | `CallMethod` → `Call ts_set_add` | `method_resolution/set_methods.ts` |
| set_has | `set.has(val)` | `CallMethod` → `Call ts_set_has` | `method_resolution/set_methods.ts` |
| set_delete | `set.delete(val)` | `CallMethod` → `Call ts_set_delete` | `method_resolution/set_methods.ts` |

### 3.2 BuiltinResolutionPass (15 tests)

Tests that global builtin calls are resolved correctly.

| Test | Input | Expected Transformation | File |
|------|-------|------------------------|------|
| console_log | `console.log(x)` | `Call ts_console_log` | `builtin_resolution/console.ts` |
| console_error | `console.error(x)` | `Call ts_console_error` | `builtin_resolution/console.ts` |
| math_floor | `Math.floor(x)` | `Call ts_math_floor` | `builtin_resolution/math.ts` |
| math_ceil | `Math.ceil(x)` | `Call ts_math_ceil` | `builtin_resolution/math.ts` |
| math_sqrt | `Math.sqrt(x)` | `Call ts_math_sqrt` | `builtin_resolution/math.ts` |
| math_pow | `Math.pow(x, y)` | `Call ts_math_pow` | `builtin_resolution/math.ts` |
| math_random | `Math.random()` | `Call ts_math_random` | `builtin_resolution/math.ts` |
| object_keys | `Object.keys(obj)` | `Call ts_object_keys` | `builtin_resolution/object_static.ts` |
| object_values | `Object.values(obj)` | `Call ts_object_values` | `builtin_resolution/object_static.ts` |
| object_entries | `Object.entries(obj)` | `Call ts_object_entries` | `builtin_resolution/object_static.ts` |
| object_assign | `Object.assign(a, b)` | `Call ts_object_assign` | `builtin_resolution/object_static.ts` |
| array_isArray | `Array.isArray(x)` | `Call ts_array_isArray` | `builtin_resolution/array_static.ts` |
| array_from | `Array.from(iter)` | `Call ts_array_from` | `builtin_resolution/array_static.ts` |
| json_parse | `JSON.parse(str)` | `Call ts_json_parse` | `builtin_resolution/json.ts` |
| json_stringify | `JSON.stringify(obj)` | `Call ts_json_stringify` | `builtin_resolution/json.ts` |

### 3.3 ConstantFoldingPass (10 tests)

Tests that constant expressions are evaluated at compile time.

| Test | Input | Expected Output | File |
|------|-------|-----------------|------|
| fold_add | `3 + 5` | `ConstInt 8` | `constant_folding/arithmetic.ts` |
| fold_mul | `4 * 7` | `ConstInt 28` | `constant_folding/arithmetic.ts` |
| fold_div | `20 / 4` | `ConstInt 5` | `constant_folding/arithmetic.ts` |
| fold_neg | `-42` | `ConstInt -42` | `constant_folding/arithmetic.ts` |
| fold_float | `3.14 * 2.0` | `ConstFloat 6.28` | `constant_folding/arithmetic.ts` |
| fold_cmp_true | `5 > 3` | `ConstBool true` | `constant_folding/comparisons.ts` |
| fold_cmp_false | `2 > 3` | `ConstBool false` | `constant_folding/comparisons.ts` |
| fold_not | `!false` | `ConstBool true` | `constant_folding/comparisons.ts` |
| fold_string | `"a" + "b"` | `ConstString "ab"` | `constant_folding/strings.ts` |
| no_fold_var | `x + 1` | `AddI64` (not folded) | `constant_folding/arithmetic.ts` |

### 3.4 DeadCodeEliminationPass (8 tests)

Tests that unreachable and unused code is removed.

| Test | Input | Expected Result | File |
|------|-------|-----------------|------|
| unreachable_after_return | `return x; y = 1;` | `y = 1` removed | `dead_code/unreachable.ts` |
| unreachable_after_throw | `throw e; x = 1;` | `x = 1` removed | `dead_code/unreachable.ts` |
| unused_assignment | `let x = 1; return 0;` | `x = 1` removed | `dead_code/unused_vars.ts` |
| const_true_branch | `if (true) { a } else { b }` | `else` block removed | `dead_code/conditional.ts` |
| const_false_branch | `if (false) { a } else { b }` | `if` block removed | `dead_code/conditional.ts` |
| empty_block | Empty basic block | Block removed | `dead_code/unreachable.ts` |
| never_executed_loop | `while (false) { }` | Loop removed | `dead_code/conditional.ts` |
| keep_side_effects | `let x = foo(); return 0;` | Call kept | `dead_code/unused_vars.ts` |

---

## Phase 4: HIRToLLVM Tests

### 4.1 Arithmetic Lowering (15 tests)

| Test | HIR Input | Expected LLVM | File |
|------|-----------|---------------|------|
| add_i64 | `AddI64` | `add i64` | `lowering/arithmetic/int_ops.ts` |
| sub_i64 | `SubI64` | `sub i64` | `lowering/arithmetic/int_ops.ts` |
| mul_i64 | `MulI64` | `mul i64` | `lowering/arithmetic/int_ops.ts` |
| div_i64 | `DivI64` | `sdiv i64` | `lowering/arithmetic/int_ops.ts` |
| mod_i64 | `ModI64` | `srem i64` | `lowering/arithmetic/int_ops.ts` |
| neg_i64 | `NegI64` | `sub i64 0,` | `lowering/arithmetic/int_ops.ts` |
| add_f64 | `AddF64` | `fadd double` | `lowering/arithmetic/float_ops.ts` |
| sub_f64 | `SubF64` | `fsub double` | `lowering/arithmetic/float_ops.ts` |
| mul_f64 | `MulF64` | `fmul double` | `lowering/arithmetic/float_ops.ts` |
| div_f64 | `DivF64` | `fdiv double` | `lowering/arithmetic/float_ops.ts` |
| add_checked | `AddI64Checked` | `@llvm.sadd.with.overflow` | `lowering/arithmetic/checked_overflow.ts` |
| sub_checked | `SubI64Checked` | `@llvm.ssub.with.overflow` | `lowering/arithmetic/checked_overflow.ts` |
| mul_checked | `MulI64Checked` | `@llvm.smul.with.overflow` | `lowering/arithmetic/checked_overflow.ts` |
| string_concat | `StringConcat` | `call {{.*}} @ts_string_concat` | `lowering/arithmetic/string_concat.ts` |
| bitwise_and | `AndI64` | `and i64` | `lowering/arithmetic/int_ops.ts` |

### 4.2 Control Flow Lowering (10 tests)

| Test | HIR Input | Expected LLVM | File |
|------|-----------|---------------|------|
| branch | `Branch` | `br label` | `lowering/control_flow/branches.ts` |
| cond_branch | `CondBranch` | `br i1 {{.*}}, label` | `lowering/control_flow/branches.ts` |
| switch | `Switch` | `switch i64` | `lowering/control_flow/switches.ts` |
| phi | `Phi` | `phi` | `lowering/control_flow/phi_nodes.ts` |
| select | `Select` | `select i1` | `lowering/control_flow/branches.ts` |
| return | `Return` | `ret` | `lowering/control_flow/branches.ts` |
| return_void | `ReturnVoid` | `ret void` | `lowering/control_flow/branches.ts` |
| unreachable | `Unreachable` | `unreachable` | `lowering/control_flow/branches.ts` |
| loop_header | Loop structure | Proper back edges | `lowering/control_flow/loops.ts` |
| nested_loops | Nested loops | Correct nesting | `lowering/control_flow/loops.ts` |

### 4.3 Call Lowering (8 tests)

| Test | HIR Input | Expected LLVM | File |
|------|-----------|---------------|------|
| call_direct | `Call @func` | `call {{.*}} @func` | `lowering/calls/direct.ts` |
| call_args | `Call @func, %a, %b` | `call {{.*}} @func({{.*}})` | `lowering/calls/direct.ts` |
| call_indirect | `CallIndirect %ptr` | `call ptr %ptr` | `lowering/calls/indirect.ts` |
| call_virtual | `CallVirtual %obj, 0` | GEP + load + call | `lowering/calls/virtual.ts` |
| call_method | `CallMethod %obj, "name"` | `call {{.*}} @ts_object_call_method` | `lowering/calls/direct.ts` |
| call_console | `Call @console_log` | Specialized lowering | `lowering/calls/direct.ts` |
| call_vararg | Call with varargs | Proper vararg handling | `lowering/calls/direct.ts` |
| tail_call | Tail position call | `musttail call` (if applicable) | `lowering/calls/direct.ts` |

### 4.4 Object/Array Lowering (10 tests)

| Test | HIR Input | Expected LLVM | File |
|------|-----------|---------------|------|
| new_object | `NewObjectDynamic` | `call {{.*}} @ts_object_create` | `lowering/objects/new_object.ts` |
| new_object_shape | `NewObject shape=#1` | `call {{.*}} @ts_object_create_with_shape` | `lowering/objects/new_object.ts` |
| get_prop_static | `GetPropStatic` | GEP with shape offset | `lowering/objects/property_access.ts` |
| set_prop_static | `SetPropStatic` | GEP + store | `lowering/objects/property_access.ts` |
| get_prop_dynamic | `GetPropDynamic` | `call {{.*}} @ts_object_get_property` | `lowering/objects/property_access.ts` |
| new_array | `NewArrayBoxed` | `call {{.*}} @ts_array_create` | `lowering/arrays/new_array.ts` |
| new_array_typed | `NewArrayTyped` | `call {{.*}} @ts_typed_array_create` | `lowering/arrays/new_array.ts` |
| get_elem | `GetElem` | `call {{.*}} @ts_array_get` | `lowering/arrays/element_access.ts` |
| set_elem | `SetElem` | `call {{.*}} @ts_array_set` | `lowering/arrays/element_access.ts` |
| array_length | `ArrayLength` | GEP to length field | `lowering/arrays/element_access.ts` |

### 4.5 Closure Lowering (6 tests)

| Test | HIR Input | Expected LLVM | File |
|------|-----------|---------------|------|
| make_closure | `MakeClosure` | `call {{.*}} @ts_closure_create` | `lowering/closures/captures.ts` |
| load_capture | `LoadCapture %idx` | GEP into closure captures | `lowering/closures/captures.ts` |
| store_capture | `StoreCapture %idx, %val` | GEP + store | `lowering/closures/mutable.ts` |
| closure_param | Hidden closure parameter | First param is closure ptr | `lowering/closures/captures.ts` |
| nested_closure | Nested closures | Correct capture chaining | `lowering/closures/nested.ts` |
| closure_call | Call through closure | Extract fn ptr + call | `lowering/closures/captures.ts` |

---

## Implementation Schedule

### Week 1: Infrastructure (COMPLETE)
- [x] Update runner.py with HIR-CHECK support
- [x] Create directory structure
- [x] Write 5 example tests to validate infrastructure

### Week 2: ASTToHIR Tests (IN PROGRESS)
- [x] Expression tests (8/31 implemented)
- [x] Statement tests (11/18 implemented)
- [ ] Function tests (5/12 implemented)

### Week 3: ASTToHIR + Pass Tests
- [ ] Class tests (1/12 implemented)
- [ ] Other construct tests (0/8 tests)
- [ ] MethodResolutionPass tests (2/20 implemented)

### Week 4: Pass + Lowering Tests
- [ ] BuiltinResolutionPass tests (3/15 implemented)
- [ ] ConstantFoldingPass tests (1/10 implemented)
- [ ] DeadCodeEliminationPass tests (0/8 tests)
- [ ] Arithmetic lowering tests (1/15 implemented)

### Week 5: Completion
- [ ] Control flow lowering tests (1/10 implemented)
- [ ] Call lowering tests (0/8 tests)
- [ ] Object/Array lowering tests (0/10 tests)
- [ ] Closure lowering tests (0/6 tests)
- [ ] CI integration

---

## Success Criteria

1. **Coverage**: All 80+ HIR opcodes have at least one test
2. **Pass Coverage**: All HIR passes have specific transformation tests
3. **Regression Detection**: Any change to ASTToHIR or HIRToLLVM that changes output fails tests
4. **CI Integration**: Tests run on every PR and block merges on failure
5. **Documentation**: Each test clearly documents what it validates

## Total Test Count

| Category | Count |
|----------|-------|
| ASTToHIR Expressions | 31 |
| ASTToHIR Statements | 18 |
| ASTToHIR Functions | 12 |
| ASTToHIR Classes | 12 |
| ASTToHIR Other | 8 |
| MethodResolutionPass | 20 |
| BuiltinResolutionPass | 15 |
| ConstantFoldingPass | 10 |
| DeadCodeEliminationPass | 8 |
| HIRToLLVM Lowering | 49 |
| **Total** | **183** |
