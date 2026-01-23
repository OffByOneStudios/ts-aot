# TypeScript Conformance Batch Plan

**Status:** Planning
**Category:** TypeScript
**Goal:** Increase TypeScript conformance from 56% to 70%+

## Current State

From `docs/conformance/typescript-features.md`:
- **Implemented:** 78 features
- **Partial:** 12 features
- **Not Implemented:** 50 features
- **Type-Only:** 34 features (no runtime support needed)
- **Current Conformance:** 78/140 runtime features (56%)

## Prioritized Feature Groups

### Group 1: Quick Wins (Low Complexity, High Impact)

These features are straightforward to implement and commonly used:

| Feature | Category | Complexity | Notes |
|---------|----------|------------|-------|
| `as const` assertions | Type Assertions | Low | Pass-through at runtime, type-level assertion |
| Default type parameters | Generics | Low | Works via type inference at call sites |
| Type predicates (`is`) | Type Narrowing | Low | Returns boolean at runtime |
| Intersection types (`&`) | Union/Intersection | Medium | Object literals merge properties |

**Estimated Impact:** +4 features (56% -> 59%)

### Group 2: Type Narrowing Enhancements

Building on existing narrowing infrastructure:

| Feature | Category | Complexity | Notes |
|---------|----------|------------|-------|
| Discriminated unions | Type Narrowing | Medium | Check discriminant property, narrow to specific union member |
| `never` type exhaustiveness | Type Narrowing | Low | Compile-time check, no runtime needed |
| `asserts` keyword | Type Narrowing | Medium | Throws if assertion fails |

**Estimated Impact:** +3 features (59% -> 61%)

### Group 3: Iterator/Symbol Completion

Completing partial implementations:

| Feature | Category | Current | Target |
|---------|----------|---------|--------|
| `Symbol.iterator` | Iterators | ⚠️ Partial | ✅ Full |
| Iterable protocol | Iterators | ⚠️ Partial | ✅ Full |
| Custom iterables with `[Symbol.iterator]` | Iterators | ⚠️ Partial | ✅ Full |

**Estimated Impact:** Fixes 2 partial -> full

### Group 4: Enum Completion

| Feature | Category | Complexity | Notes |
|---------|----------|------------|-------|
| Computed enum members | Enums | Medium | Evaluate expressions at compile time |

**Estimated Impact:** +1 feature

### Group 5: Class Enhancements

| Feature | Category | Complexity | Notes |
|---------|----------|------------|-------|
| Index signatures in classes | Classes | Medium | `[key: string]: T` support |
| Complete `this` type | Classes | Low | Fix partial implementation |

**Estimated Impact:** +1 feature, 1 partial -> full

## Recommended Implementation Order

### Phase A: Foundation (Quick Wins)

1. **`as const` assertions** (Type Assertions)
   - Runtime: Pass-through (already partially done)
   - Analyzer: Mark as const type
   - Test: `const arr = [1, 2, 3] as const;`

2. **Default type parameters** (Generics)
   - Analyzer: Apply default when type arg not provided
   - Test: `function foo<T = string>(x: T): T`

3. **Type predicates (`is`)** (Type Narrowing)
   - Analyzer: Track predicate return type
   - Codegen: Return boolean from predicate function
   - Test: `function isString(x: any): x is string`

### Phase B: Type System (Narrowing & Unions)

4. **Intersection types (`&`)** (Union/Intersection)
   - Analyzer: Merge object types
   - Codegen: Object spread at runtime
   - Test: `type AB = A & B;`

5. **Discriminated unions** (Type Narrowing)
   - Analyzer: Check discriminant property value
   - Codegen: Conditional based on property
   - Test: `type Shape = Circle | Square` with `kind` property

6. **`asserts` keyword** (Type Narrowing)
   - Analyzer: Mark function as assertion
   - Codegen: Throw if condition false
   - Test: `function assert(x: any): asserts x is string`

### Phase C: Completions

7. **Symbol.iterator completion** (Iterators)
   - Runtime: Complete Symbol.iterator property key support
   - Test: `for (const x of customIterable)`

8. **Computed enum members** (Enums)
   - Analyzer: Evaluate constant expressions
   - Test: `enum E { A = 1 + 2, B = "foo".length }`

9. **Index signatures in classes** (Classes)
   - Analyzer: Track index signature type
   - Codegen: Use TsMap for indexed properties
   - Test: `class Foo { [key: string]: number; }`

## Files to Modify

### Analyzer
- `src/compiler/analysis/Analyzer_Types.cpp` - Type manipulation
- `src/compiler/analysis/Analyzer_Expressions.cpp` - as const, predicates
- `src/compiler/analysis/Analyzer_Statements.cpp` - Discriminated unions
- `src/compiler/analysis/Analyzer.h` - New type kinds if needed

### Codegen
- `src/compiler/codegen/IRGenerator_Expressions.cpp` - as const
- `src/compiler/codegen/IRGenerator_Statements.cpp` - asserts
- `src/compiler/codegen/IRGenerator_Classes.cpp` - Index signatures

### Runtime
- `src/runtime/src/TsSymbol.cpp` - Iterator completion
- `src/runtime/src/TsObject.cpp` - Intersection/index signatures

## Test Files to Create

```
tests/golden_ir/typescript/
├── as_const.ts
├── default_type_params.ts
├── type_predicates.ts
├── intersection_types.ts
├── discriminated_unions.ts
├── asserts_keyword.ts
├── computed_enums.ts
└── class_index_signatures.ts
```

## Success Metrics

| Metric | Before | Target |
|--------|--------|--------|
| Implemented | 78 | 87 |
| Partial | 12 | 9 |
| Conformance | 56% | 62% |

## Risk Assessment

| Risk | Probability | Mitigation |
|------|-------------|------------|
| Intersection types complex | Medium | Start with simple cases |
| Discriminated unions complex | Medium | Limit to string literal discriminants |
| Iterator bugs affect other tests | Low | Run full test suite after |

## Not In Scope (Deferred)

These features are deferred due to complexity or limited use:

- **Decorators** - Requires major runtime support, framework-specific
- **Namespaces** - Legacy feature, modules preferred
- **Triple-slash directives** - Build tooling, not runtime
- **JSX** - Requires complete JSX transform pipeline
- **Mixins** - Complex class patterns
- **Declaration merging** - Type-system complexity
- **Dynamic import()** - AOT fundamental limitation

## Verification

After each feature:
```powershell
# Build
cmake --build build --config Release

# Run tests
python tests/node/run_tests.py
python tests/golden_ir/runner.py

# Verify no regressions
```
