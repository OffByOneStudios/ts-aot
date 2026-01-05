# Epic 106: Golden IR Regression Test Suite

**Status:** Not Started
**Parent:** [Phase 19 Meta Epic](./meta_epic.md)
**Priority:** High - Infrastructure for preventing regressions

## Overview

Create a comprehensive "golden IR" test suite that captures expected LLVM IR output for both TypeScript and JavaScript code paths. This prevents IR regressions and provides confidence when making codegen changes.

**Rationale:**
- Current development causes frequent IR thrashing and regressions
- No systematic way to verify codegen correctness across changes
- Need to catch boxing/unboxing bugs, type inference issues, and optimization problems early
- Both TypeScript (typed) and JavaScript (slow path) need separate test coverage

---

## Architecture

### Golden IR Format

Each test file contains:
1. **Source code** (TypeScript or JavaScript)
2. **Expected IR patterns** using `// CHECK:` assertions (LLVM FileCheck style)
3. **Runtime verification** with expected output

**Example Test File (`tests/golden_ir/typescript/array_map.ts`):**
```typescript
// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @__ts_array_push
// CHECK-NOT: ts_value_get_object
// CHECK: ret i32 0
// OUTPUT: 2,4,6

function user_main(): number {
  const arr = [1, 2, 3];
  const mapped = arr.map(x => x * 2);
  console.log(mapped.join(','));
  return 0;
}
```

### Test Runner Architecture

```
tests/golden_ir/
├── runner.ps1                 # Main test runner
├── typescript/                # TypeScript typed tests
│   ├── arrays/
│   ├── objects/
│   ├── functions/
│   ├── classes/
│   ├── generics/
│   └── control_flow/
├── javascript/                # JavaScript slow-path tests
│   ├── dynamic_types/
│   ├── property_access/
│   ├── operators/
│   └── closures/
└── common/
    ├── assertions.ps1         # CHECK pattern matching
    └── differ.ps1             # IR diff tool
```

---

## Milestone 106.1: Test Infrastructure

**Goal:** Build the test harness that compiles code, captures IR, and validates patterns.

- [ ] **Task 106.1.1:** Create `tests/golden_ir/runner.ps1`
  - Parse `// RUN:` directives to determine compilation command
  - Execute compiler with `--dump-ir` flag
  - Capture IR output and executable output
  - Exit codes: 0 = pass, 1 = compilation failed, 2 = CHECK failed, 3 = OUTPUT mismatch

- [ ] **Task 106.1.2:** Implement CHECK pattern matcher
  - `// CHECK:` - Must appear in order
  - `// CHECK-NEXT:` - Must be on next non-comment line
  - `// CHECK-NOT:` - Must NOT appear between previous CHECK and next CHECK
  - `// CHECK-DAG:` - May appear in any order (Directed Acyclic Graph)
  - Support regex: `{{.*}}`, `{{[0-9]+}}`, `{{ptr|i64}}`

- [ ] **Task 106.1.3:** Implement OUTPUT verification
  - `// OUTPUT:` line specifies expected stdout
  - `// OUTPUT-REGEX:` for pattern matching
  - `// EXIT-CODE:` for expected exit code (default 0)

- [ ] **Task 106.1.4:** Create IR differ tool
  - When CHECK fails, show side-by-side diff
  - Highlight missing/unexpected instructions
  - Suggest fixes based on common patterns

- [ ] **Task 106.1.5:** Add CI integration
  - Create `.github/workflows/golden_ir_tests.yml`
  - Run on every PR and commit to main
  - Report failures with diffs in GitHub Actions

---

## Milestone 106.2: TypeScript Core Language Tests

**Goal:** Comprehensive coverage of TypeScript features with typed optimizations.

### Arrays (20 tests)

- [ ] **Task 106.2.1:** Array literal creation
  ```typescript
  // CHECK: call {{.*}} @__ts_array_create
  // CHECK: call {{.*}} @__ts_array_push_inline
  const arr = [1, 2, 3];
  ```

- [ ] **Task 106.2.2:** Array.map() with arrow function
  ```typescript
  // CHECK: define {{.*}} @lambda_
  // CHECK: call {{.*}} @__ts_array_map
  // CHECK-NOT: ts_value_make_int
  arr.map(x => x * 2);
  ```

- [ ] **Task 106.2.3:** Array.filter() optimization
  ```typescript
  // CHECK: call {{.*}} @__ts_array_filter
  // CHECK: call {{.*}} @emitToBoolean
  arr.filter(x => x > 5);
  ```

- [ ] **Task 106.2.4:** Array.reduce() with typed accumulator
- [ ] **Task 106.2.5:** Array destructuring `[a, b] = arr`
- [ ] **Task 106.2.6:** Array spread `[...arr1, ...arr2]`
- [ ] **Task 106.2.7:** Array.includes() with primitives
- [ ] **Task 106.2.8:** Array.indexOf() optimization
- [ ] **Task 106.2.9:** Array.slice() with bounds
- [ ] **Task 106.2.10:** Array.concat() multiple arrays
- [ ] **Task 106.2.11:** for-of loop specialized access
  ```typescript
  // CHECK: call {{.*}} @__ts_array_get_inline
  // CHECK-NOT: ts_value_get_object
  for (const x of arr) { }
  ```
- [ ] **Task 106.2.12:** Array.push() unboxed primitives
- [ ] **Task 106.2.13:** Array.pop() return type
- [ ] **Task 106.2.14:** Array.shift() / unshift()
- [ ] **Task 106.2.15:** Array.sort() with comparator
- [ ] **Task 106.2.16:** Array.reverse() in-place
- [ ] **Task 106.2.17:** Array.join() string coercion
- [ ] **Task 106.2.18:** Array.find() with predicate
- [ ] **Task 106.2.19:** Array.findIndex() optimization
- [ ] **Task 106.2.20:** Array.every() / some() short-circuit

### Objects (15 tests)

- [ ] **Task 106.2.21:** Object literal creation
  ```typescript
  // CHECK: call {{.*}} @__ts_map_create
  // CHECK: call {{.*}} @__ts_map_set_at
  const obj = { a: 1, b: 2 };
  ```

- [ ] **Task 106.2.22:** Object property access static
  ```typescript
  // CHECK: call {{.*}} @ts_object_get_property
  // CHECK-NOT: ts_value_get_string
  obj.a
  ```

- [ ] **Task 106.2.23:** Object property assignment
- [ ] **Task 106.2.24:** Object computed property `obj[key]`
- [ ] **Task 106.2.25:** Object spread `{ ...obj }`
- [ ] **Task 106.2.26:** Object destructuring `{ a, b } = obj`
- [ ] **Task 106.2.27:** Object.keys() iteration
- [ ] **Task 106.2.28:** Object.values() typed return
- [ ] **Task 106.2.29:** Object.entries() tuples
- [ ] **Task 106.2.30:** Object.assign() merge
- [ ] **Task 106.2.31:** Optional chaining `obj?.prop`
- [ ] **Task 106.2.32:** Nullish coalescing `obj.prop ?? default`
- [ ] **Task 106.2.33:** Shorthand property `{ x }` from variable
  ```typescript
  // CHECK: call {{.*}} @visitIdentifier
  // CHECK: call {{.*}} @__ts_map_set_at
  const x = 42;
  const obj = { x };
  ```
- [ ] **Task 106.2.34:** Method shorthand `{ foo() {} }`
- [ ] **Task 106.2.35:** Getter/setter properties

### Functions (15 tests)

- [ ] **Task 106.2.36:** Named function definition
  ```typescript
  // CHECK: define {{.*}} @func_
  // CHECK: !dbg !{{[0-9]+}}
  function add(a: number, b: number): number { return a + b; }
  ```

- [ ] **Task 106.2.37:** Arrow function capture
  ```typescript
  // CHECK: define {{.*}} @lambda_
  // CHECK: load {{.*}} @x_global
  const x = 5;
  const fn = () => x;
  ```

- [ ] **Task 106.2.38:** Function hoisting
  ```typescript
  // CHECK: call {{.*}} @ts_cell_create
  // CHECK: call {{.*}} @ts_cell_set
  console.log(hoisted());
  function hoisted() { return 42; }
  ```

- [ ] **Task 106.2.39:** Closure with mutable capture
  ```typescript
  // CHECK: call {{.*}} @ts_cell_create
  // CHECK: call {{.*}} @ts_cell_get
  let count = 0;
  return () => ++count;
  ```

- [ ] **Task 106.2.40:** Nested closures (3 levels)
- [ ] **Task 106.2.41:** Function with default parameters
- [ ] **Task 106.2.42:** Function with rest parameters `...args`
- [ ] **Task 106.2.43:** Function with destructured parameters
- [ ] **Task 106.2.44:** Function return type inference
- [ ] **Task 106.2.45:** Generic function `<T>`
- [ ] **Task 106.2.46:** Higher-order function (function returning function)
- [ ] **Task 106.2.47:** Recursive function optimization
- [ ] **Task 106.2.48:** IIFE `(function() {})()` without closure
  ```typescript
  // CHECK-NOT: ts_cell_create
  const result = (function() { return 42; })();
  ```
- [ ] **Task 106.2.49:** IIFE with closure capture
  ```typescript
  // CHECK: call {{.*}} @ts_cell_get
  const x = 5;
  const result = (function() { return x; })();
  ```
- [ ] **Task 106.2.50:** Function stored in object property
  ```typescript
  // CHECK: call {{.*}} @ts_value_make_function
  // CHECK: call {{.*}} @__ts_map_set_at
  const obj = { fn: () => 42 };
  ```

### Control Flow (10 tests)

- [ ] **Task 106.2.51:** If-else optimization
  ```typescript
  // CHECK: icmp
  // CHECK: br i1
  // CHECK-NOT: call {{.*}} @ts_value_to_bool
  if (x > 5) { } else { }
  ```

- [ ] **Task 106.2.52:** While loop
- [ ] **Task 106.2.53:** For loop with optimization
- [ ] **Task 106.2.54:** For-of loop specialized
- [ ] **Task 106.2.55:** For-in loop on objects
- [ ] **Task 106.2.56:** Switch statement
- [ ] **Task 106.2.57:** Try-catch-finally
- [ ] **Task 106.2.58:** Break/continue in loops
- [ ] **Task 106.2.59:** Return in nested blocks
- [ ] **Task 106.2.60:** Ternary operator `cond ? a : b`

---

## Milestone 106.3: JavaScript Slow Path Tests

**Goal:** Verify dynamic JavaScript features use proper boxing and slow-path operations.

### Dynamic Types (10 tests)

- [ ] **Task 106.3.1:** Variable without type annotation
  ```javascript
  // CHECK: call {{.*}} @ts_value_make_int
  // CHECK: store {{.*}} @x_global
  var x = 42;
  ```

- [ ] **Task 106.3.2:** Dynamic addition `a + b`
  ```javascript
  // CHECK: call {{.*}} @ts_value_add
  // CHECK-NOT: add i64
  var result = a + b;
  ```

- [ ] **Task 106.3.3:** Dynamic subtraction/multiply/divide
- [ ] **Task 106.3.4:** Type coercion in operators
  ```javascript
  // CHECK: call {{.*}} @ts_value_add
  // OUTPUT: 42string
  var result = 42 + "string";
  ```

- [ ] **Task 106.3.5:** Loose equality `==`
  ```javascript
  // CHECK: call {{.*}} @ts_value_eq
  if (1 == "1") { }
  ```

- [ ] **Task 106.3.6:** Strict equality `===`
  ```javascript
  // CHECK: call {{.*}} @ts_value_strict_eq
  if (1 === "1") { }
  ```

- [ ] **Task 106.3.7:** typeof operator
  ```javascript
  // CHECK: call {{.*}} @ts_value_typeof
  // OUTPUT: number
  console.log(typeof 42);
  ```

- [ ] **Task 106.3.8:** Truthiness checks
  ```javascript
  // CHECK: call {{.*}} @ts_value_to_bool
  if (x) { }
  ```

- [ ] **Task 106.3.9:** Dynamic comparison `<`, `>`
- [ ] **Task 106.3.10:** in operator `'prop' in obj`

### Dynamic Property Access (10 tests)

- [ ] **Task 106.3.11:** Get property by string
  ```javascript
  // CHECK: call {{.*}} @ts_object_get_prop
  var val = obj.foo;
  ```

- [ ] **Task 106.3.12:** Set property by string
  ```javascript
  // CHECK: call {{.*}} @ts_object_set_prop
  obj.foo = 42;
  ```

- [ ] **Task 106.3.13:** Get property by dynamic key
  ```javascript
  // CHECK: call {{.*}} @ts_object_get_dynamic
  var val = obj[key];
  ```

- [ ] **Task 106.3.14:** Array access by dynamic index
  ```javascript
  // CHECK: call {{.*}} @ts_object_get_dynamic
  // CHECK: magic check for TsArray
  var val = arr[i];
  ```

- [ ] **Task 106.3.15:** Delete property
- [ ] **Task 106.3.16:** Has property check
- [ ] **Task 106.3.17:** Object.keys() on any type
- [ ] **Task 106.3.18:** Object.values() on any type
- [ ] **Task 106.3.19:** For-in loop on any object
- [ ] **Task 106.3.20:** Property chain `obj.a.b.c`

### Closures & IIFEs (10 tests)

- [ ] **Task 106.3.21:** IIFE without closure
  ```javascript
  // CHECK-NOT: ts_cell_create
  var result = (function() { return 42; })();
  ```

- [ ] **Task 106.3.22:** IIFE with closure
- [ ] **Task 106.3.23:** IIFE returning object literal
  ```javascript
  // CHECK: call {{.*}} @ts_call_0
  // CHECK: call {{.*}} @ts_value_get_object
  // CHECK: store {{.*}} @result_global
  var result = (function() { return { a: 1 }; })();
  ```

- [ ] **Task 106.3.24:** IIFE with .call(this)
- [ ] **Task 106.3.25:** IIFE with multiple returns
- [ ] **Task 106.3.26:** Nested IIFEs
- [ ] **Task 106.3.27:** IIFE in while loop
- [ ] **Task 106.3.28:** Counter closure pattern
- [ ] **Task 106.3.29:** Module pattern with IIFE
- [ ] **Task 106.3.30:** UMD pattern (like lodash)

---

## Milestone 106.4: Edge Cases & Regression Tests

**Goal:** Capture specific bugs that have occurred and ensure they don't regress.

### Boxing/Unboxing Bugs (10 tests)

- [ ] **Task 106.4.1:** Function in object property (Issue #11)
  ```typescript
  // CHECK: call {{.*}} @visitIdentifier
  // CHECK-NOT: store ptr @double_func
  function double(x: number): number { return x * 2; }
  const obj = { double };
  // OUTPUT: 14
  console.log(obj.double(7));
  ```

- [ ] **Task 106.4.2:** IIFE assignment double-unboxing (Issue #11)
  ```typescript
  // CHECK: call {{.*}} @ts_call_0
  // CHECK-NOT: call {{.*}} @ts_value_get_object
  // CHECK: store {{.*}} @result_global
  const result = (function() { return { a: 1 }; })();
  // OUTPUT: 1
  console.log(result.a);
  ```

- [ ] **Task 106.4.3:** Cell variable in object shorthand
- [ ] **Task 106.4.4:** Generic array push boxing
- [ ] **Task 106.4.5:** Boolean-typed pointer to emitToBoolean
- [ ] **Task 106.4.6:** Set<T> for-of element access
- [ ] **Task 106.4.7:** Wrapper function for ts_call_N compatibility
- [ ] **Task 106.4.8:** Optional parameter undefined checks
- [ ] **Task 106.4.9:** Direct call optimization with closures
- [ ] **Task 106.4.10:** Function magic check before ts_call_N

### Type Inference (5 tests)

- [ ] **Task 106.4.11:** Contextual typing for arrow parameters
- [ ] **Task 106.4.12:** Generic type constraint propagation
- [ ] **Task 106.4.13:** Union type narrowing in if-else
- [ ] **Task 106.4.14:** Template literal type inference
- [ ] **Task 106.4.15:** Readonly array type preservation

---

## Milestone 106.5: Performance Benchmarks

**Goal:** Track performance metrics alongside correctness.

- [ ] **Task 106.5.1:** Add `// PERF:` annotations
  ```typescript
  // PERF: compile-time < 1s
  // PERF: runtime < 0.1s
  // PERF: binary-size < 50KB
  ```

- [ ] **Task 106.5.2:** Track compilation time per test
- [ ] **Task 106.5.3:** Track executable size
- [ ] **Task 106.5.4:** Track runtime performance
- [ ] **Task 106.5.5:** Generate performance report
- [ ] **Task 106.5.6:** Detect performance regressions (>10% slowdown)

---

## Milestone 106.6: Interactive Tools

**Goal:** Developer-friendly tools for maintaining golden tests.

- [ ] **Task 106.6.1:** `update_golden.ps1` script
  - Recompile test and update CHECK patterns
  - Show diff before accepting changes
  - Bulk update all tests or specific category

- [ ] **Task 106.6.2:** `new_golden_test.ps1` generator
  - Scaffold new test file with template
  - Run compiler and suggest initial CHECK patterns
  - Add to test index automatically

- [ ] **Task 106.6.3:** `debug_golden.ps1` helper
  - Run single test with verbose output
  - Show IR side-by-side with CHECK patterns
  - Highlight mismatches in color

- [ ] **Task 106.6.4:** VS Code integration
  - Syntax highlighting for `// CHECK:` patterns
  - Run test from editor command
  - Show failures inline with CodeLens

---

## Success Criteria

### MVP (Minimum Viable Product)
- [ ] 50+ golden IR tests covering core features
- [ ] Test runner with CHECK pattern matching
- [ ] CI integration catching regressions
- [ ] Documentation for adding new tests

### Full Suite
- [ ] 150+ tests covering TypeScript and JavaScript
- [ ] Performance benchmarking integrated
- [ ] Interactive tools for maintenance
- [ ] Zero false positives in CI

### Long-term
- [ ] Used as primary regression prevention
- [ ] All new features require golden IR test
- [ ] Automated suggestion of CHECK patterns
- [ ] Performance regression alerts in PRs

---

## Dependencies

### Compiler Changes
- [ ] `--dump-ir=<file>` flag to write IR to file (currently prints to stdout)
- [ ] Stable IR formatting (consistent ordering, deterministic names)
- [ ] Debug metadata optional (tests shouldn't break on debug info changes)

### Infrastructure
- [ ] PowerShell 7+ for test runner
- [ ] Git integration for diffing
- [ ] GitHub Actions runner with LLVM tools

---

## References
- [LLVM FileCheck](https://llvm.org/docs/CommandGuide/FileCheck.html)
- [lit - LLVM Integrated Tester](https://llvm.org/docs/CommandGuide/lit.html)
- [Rust's compile-test](https://github.com/rust-lang/rust/tree/master/src/tools/compiletest)
- [V8's test262 harness](https://github.com/v8/v8/tree/main/test/test262)
