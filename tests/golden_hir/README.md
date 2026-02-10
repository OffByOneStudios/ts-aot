# Golden HIR Tests

This directory contains golden tests for the HIR (High-level Intermediate Representation) system. These tests validate:
1. **ASTToHIR** - TypeScript AST nodes convert to correct HIR opcodes
2. **HIR Passes** - Passes transform HIR correctly (MethodResolution, BuiltinResolution, etc.)
3. **HIRToLLVM** - HIR opcodes lower to correct LLVM IR

## Test Format

Tests use the same format as golden_ir tests with additional `HIR-CHECK` directives:

```typescript
// RUN: ts-aot %s --dump-hir --dump-ir -o %t.exe && %t.exe
// HIR-CHECK: ArrayPush
// HIR-CHECK-NOT: CallMethod "push"
// CHECK: call {{.*}} @ts_array_push
// OUTPUT: 42

function user_main(): number {
    const arr: number[] = [];
    arr.push(42);
    console.log(arr[0]);
    return 0;
}
```

### Directives

| Directive | Purpose |
|-----------|---------|
| `HIR-CHECK: pattern` | Pattern must appear in HIR output (in order) |
| `HIR-CHECK-NOT: pattern` | Pattern must NOT appear in HIR output |
| `HIR-CHECK-NEXT: pattern` | Pattern must appear on next HIR line |
| `HIR-CHECK-DAG: pattern` | Pattern may appear in any order |
| `CHECK: pattern` | Pattern must appear in LLVM IR output |
| `OUTPUT: text` | Expected stdout from execution |

## Directory Structure

```
tests/golden_hir/
├── README.md                    # This file
├── ast_to_hir/                  # ASTToHIR conversion tests
│   ├── expressions/             # Expression lowering
│   │   ├── binary_ops.ts
│   │   ├── unary_ops.ts
│   │   ├── calls.ts
│   │   ├── property_access.ts
│   │   ├── element_access.ts
│   │   └── conditionals.ts
│   ├── statements/              # Statement lowering
│   │   ├── variable_decl.ts
│   │   ├── if_else.ts
│   │   ├── loops.ts
│   │   ├── switch.ts
│   │   ├── try_catch.ts
│   │   └── return.ts
│   ├── functions/               # Function lowering
│   │   ├── declarations.ts
│   │   ├── expressions.ts
│   │   ├── arrows.ts
│   │   ├── closures.ts
│   │   ├── generators.ts
│   │   └── async.ts
│   ├── classes/                 # Class lowering
│   │   ├── declarations.ts
│   │   ├── expressions.ts
│   │   ├── inheritance.ts
│   │   ├── methods.ts
│   │   ├── properties.ts
│   │   ├── private_fields.ts
│   │   └── static_blocks.ts
│   └── other/                   # Other constructs
│       ├── regexp.ts
│       ├── bigint.ts
│       ├── jsx.ts
│       └── spread.ts
├── passes/                      # HIR pass tests
│   ├── method_resolution/       # MethodResolutionPass
│   │   ├── array_methods.ts
│   │   ├── string_methods.ts
│   │   ├── map_methods.ts
│   │   └── set_methods.ts
│   ├── builtin_resolution/      # BuiltinResolutionPass
│   │   ├── console.ts
│   │   ├── math.ts
│   │   ├── object_static.ts
│   │   ├── array_static.ts
│   │   └── json.ts
│   ├── type_propagation/        # TypePropagationPass
│   │   ├── constants.ts
│   │   ├── binary_ops.ts
│   │   └── calls.ts
│   ├── constant_folding/        # ConstantFoldingPass
│   │   ├── arithmetic.ts
│   │   ├── comparisons.ts
│   │   └── strings.ts
│   └── dead_code/               # DeadCodeEliminationPass
│       ├── unreachable.ts
│       ├── unused_vars.ts
│       └── conditional.ts
└── lowering/                    # HIRToLLVM tests
    ├── arithmetic/
    │   ├── int_ops.ts
    │   ├── float_ops.ts
    │   ├── checked_overflow.ts
    │   └── string_concat.ts
    ├── control_flow/
    │   ├── branches.ts
    │   ├── switches.ts
    │   ├── loops.ts
    │   └── phi_nodes.ts
    ├── calls/
    │   ├── direct.ts
    │   ├── indirect.ts
    │   └── virtual.ts
    ├── objects/
    │   ├── new_object.ts
    │   ├── property_access.ts
    │   └── shapes.ts
    ├── arrays/
    │   ├── new_array.ts
    │   ├── element_access.ts
    │   └── typed_arrays.ts
    └── closures/
        ├── captures.ts
        ├── mutable.ts
        └── nested.ts
```

## Running Tests

```bash
# Run all HIR tests
python tests/golden_hir/runner.py

# Run specific category
python tests/golden_hir/runner.py tests/golden_hir/passes/

# Run single test with verbose output
python tests/golden_hir/runner.py --verbose tests/golden_hir/passes/method_resolution/array_methods.ts
```

## Current Test Status

**Total: 37 tests**

### ASTToHIR Coverage

| Category | Tests | Passing | Status |
|----------|-------|---------|--------|
| Expressions | 8 | 8 | ✅ Complete |
| Statements | 11 | 11 | ✅ Complete |
| Functions | 5 | 5 | ✅ Complete |
| Classes | 6 | 6 | ✅ Complete |

**Expressions (8 tests):**
- integer_arithmetic, comparison_ops, boolean_ops, array_ops
- unary_ops, property_access, call_expr, string_ops

**Statements (11 tests):**
- if_else, while_loop, for_loop, do_while, for_of, for_in
- switch_stmt, variable_decl, break_continue, continue_stmt, labeled_stmt

**Functions (5 tests):**
- basic_function, declarations, arrows, closure, mutable_closure

**Classes (6 tests):**
- basic_class, instance_method, inheritance, properties, constructor, static_method

### HIR Pass Coverage

| Pass | Tests | Passing | Status |
|------|-------|---------|--------|
| MethodResolutionPass | 2 | 2 | ✅ Core working |
| BuiltinResolutionPass | 2 | 2 | ✅ Core working |
| ConstantFoldingPass | 1 | 1 | ✅ Core working |

**Pass tests (5 total):**
- method_resolution, array_method_resolution
- builtin_resolution, math_builtin_resolution
- constant_folding

### HIRToLLVM Coverage

| Category | Tests | Passing | Status |
|----------|-------|---------|--------|
| Arithmetic | 1 | 1 | ✅ Working |
| Control Flow | 1 | 1 | ✅ Working |

**Lowering tests (2 total):**
- arithmetic_to_llvm, control_flow_to_llvm

## Next Tests to Add

### Priority 1: Core Language Features
- [ ] try_catch.ts - Exception handling
- [ ] async_await.ts - Async functions
- [ ] generator.ts - Generator functions
- [ ] spread_operator.ts - Spread in arrays/calls

### Priority 2: More Pass Tests
- [ ] dead_code_elimination.ts - DCE pass validation
- [ ] type_propagation.ts - Type inference in HIR
- [ ] string_method_resolution.ts - String.split, etc.
- [ ] map_set_methods.ts - Map/Set operations

### Priority 3: Edge Cases
- [ ] nested_closures.ts - Multiple closure levels
- [ ] recursive_functions.ts - Self-referential calls
- [ ] virtual_dispatch.ts - Class method overrides
- [ ] optional_chaining.ts - ?. operator

## Example Tests

### Method Resolution Test

```typescript
// tests/golden_hir/passes/method_resolution/array_methods.ts
// RUN: ts-aot %s --dump-hir --dump-ir -o %t.exe && %t.exe
//
// Test that array.push() is resolved to ArrayPush opcode
// HIR-CHECK: define @user_main
// HIR-CHECK: ArrayPush
// HIR-CHECK-NOT: CallMethod "push"
//
// Verify correct LLVM lowering
// CHECK: call {{.*}} @ts_array_push
//
// OUTPUT: 3

function user_main(): number {
    const arr: number[] = [1, 2];
    arr.push(3);
    console.log(arr.length);
    return 0;
}
```

### Constant Folding Test

```typescript
// tests/golden_hir/passes/constant_folding/arithmetic.ts
// RUN: ts-aot %s --dump-hir -o %t.exe && %t.exe
//
// Test that constant arithmetic is folded at HIR level
// HIR-CHECK: define @user_main
// HIR-CHECK: ConstInt 15
// HIR-CHECK-NOT: AddI64
// HIR-CHECK-NOT: MulI64
//
// OUTPUT: 15

function user_main(): number {
    const result = 3 * 5;  // Should be folded to ConstInt 15
    console.log(result);
    return 0;
}
```

### Closure Capture Test

```typescript
// tests/golden_hir/ast_to_hir/functions/closures.ts
// RUN: ts-aot %s --dump-hir --dump-ir -o %t.exe && %t.exe
//
// Test that closures correctly capture variables
// HIR-CHECK: define @user_main
// HIR-CHECK: MakeClosure
// HIR-CHECK: captures
//
// CHECK: call {{.*}} @ts_closure_create
//
// OUTPUT: 10

function user_main(): number {
    let x = 5;
    const addX = (y: number) => x + y;
    console.log(addX(5));
    return 0;
}
```

### Checked Arithmetic Test

```typescript
// tests/golden_hir/lowering/arithmetic/checked_overflow.ts
// RUN: ts-aot %s --dump-hir --dump-ir -o %t.exe && %t.exe
//
// Test checked arithmetic with overflow detection
// HIR-CHECK: AddI64Checked
//
// CHECK: @llvm.sadd.with.overflow.i64
// CHECK: extractvalue
// CHECK: br i1
//
// OUTPUT: overflow detected

function user_main(): number {
    try {
        const max = 9223372036854775807n;  // Max i64
        const result = max + 1n;  // Should overflow
    } catch (e) {
        console.log("overflow detected");
    }
    return 0;
}
```

### CallVirtual Test

```typescript
// tests/golden_hir/lowering/calls/virtual.ts
// RUN: ts-aot %s --dump-hir --dump-ir -o %t.exe && %t.exe
//
// Test virtual method dispatch
// HIR-CHECK: CallVirtual
//
// CHECK: getelementptr {{.*}} 8
// CHECK: load ptr
// CHECK: getelementptr
// CHECK: call
//
// OUTPUT: Dog says woof

class Animal {
    speak(): string { return "..."; }
}

class Dog extends Animal {
    speak(): string { return "woof"; }
}

function user_main(): number {
    const animal: Animal = new Dog();
    console.log("Dog says " + animal.speak());
    return 0;
}
```

## Integration with CI

These tests should run as part of the CI pipeline:

```yaml
- name: Run HIR Golden Tests
  run: python tests/golden_hir/runner.py
```

Failures should block merges to ensure HIR correctness.
