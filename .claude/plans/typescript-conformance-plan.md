# Plan: TypeScript Features Conformance Improvement

## Current State (Updated 2026-01-22)

**Conformance: 83/140 runtime features (59%)**
- 83 Implemented (✅)
- 7 Partial (⚠️)
- 50 Not Implemented (❌)
- 34 Type-only (🔬) - don't require runtime support

### Phase 1 Complete ✅
Completed features:
- `symbol` full support (Description, uniqueness, Symbol.for/keyFor, as property keys)
- `Symbol.iterator` (Well-known symbols as property keys)
- Iterable protocol (Custom iterables with [Symbol.iterator])
- `instanceof` narrowing (Class and Array checks)
- Non-null assertion (`!`) (Pass-through expression)
- `module.exports` (Property setting and reassignment)
- Type guards (typeof, in, Array.isArray)
- Generic constraints (`extends`) (Interface and type parameter constraints)
- Empty string truthiness fix

### Remaining Partial Features (7)
| Feature | Category | Issue |
|---------|----------|-------|
| Indexable types | Interfaces | Basic support only |
| Hybrid types | Interfaces | Basic support only |
| `this` type | Classes | Explicit class return type works as workaround |
| `this` parameter | Functions | Basic support |
| Overloads | Functions | Declaration only |
| Control flow analysis | Type Narrowing | Basic support |
| Union types | Union/Intersection | Basic support |

## Priority Analysis

### Tier 1: High-Impact, Low-Effort (Complete Partials)
These are marked ⚠️ and likely need minor fixes to reach ✅:

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| `symbol` | Basic Types | Medium | High - needed for iterators |
| `this` type | Classes | Low | Medium |
| `this` parameter | Functions | Low | Medium |
| Overloads | Functions | Low | Low (declaration only) |
| Generic constraints | Generics | Medium | High |
| `instanceof` narrowing | Type Narrowing | Low | High |
| Control flow analysis | Type Narrowing | Medium | High |
| Union types | Union/Intersection | Medium | High |
| Type guards | Union/Intersection | Medium | High |
| `Symbol.iterator` | Iterators | Medium | High |
| Iterable protocol | Iterators | Medium | High |
| Non-null assertion | Type Assertions | Low | Medium |
| `module.exports` | Modules | Low | Medium |
| Indexable types | Interfaces | Medium | Medium |
| Hybrid types | Interfaces | Medium | Low |

### Tier 2: High-Impact Features (Not Implemented)
These unlock significant functionality:

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| `as const` | Type Assertions | Medium | High - const contexts |
| Default type parameters | Generics | Medium | High - better generics |
| Index signatures in classes | Classes | Medium | High - Map-like classes |
| Type predicates (`is`) | Type Narrowing | Medium | High - custom guards |
| Discriminated unions | Type Narrowing | High | High - pattern matching |
| Intersection types | Union/Intersection | High | High - type composition |
| Dynamic `import()` | Modules | High | Medium - code splitting |

### Tier 3: Nice-to-Have (Lower Priority)
Less commonly used or complex features:

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| Decorators (all 6) | Decorators | Very High | Medium - frameworks |
| JSX (all 4) | JSX | Very High | Medium - React |
| Namespaces (all 3) | Namespaces | Medium | Low - legacy |
| Mixins (all 2) | Mixins | High | Low |
| Triple-slash (all 4) | Directives | Low | Low |
| Declaration merging | Merging | Medium | Low |
| Computed enum members | Enums | Medium | Low |
| Ambient enums | Enums | Low | Low |

### Tier 4: Skip / Not Applicable
Type manipulation features that are compile-time only:
- Conditional types, Mapped types, Template literal types
- `keyof`, `infer`, Indexed access types

## Recommended Implementation Order

### Phase 1: Quick Wins (Partials → Full) ✅ COMPLETE
1. ✅ **Non-null assertion (`!`)** - Pass-through expression
2. ✅ **`module.exports`** - Property setting and reassignment
3. ⚠️ **`this` type/parameter** - Explicit class return works as workaround
4. ✅ **`instanceof` narrowing** - Class and Array checks
5. ⚠️ **Overloads** - Declaration only (as expected)
6. ✅ **`symbol` full support** - Description, uniqueness, as property keys
7. ✅ **`Symbol.iterator`** - Well-known symbols as property keys
8. ✅ **Iterable protocol** - Custom iterables work
9. ✅ **Generic constraints** - Interface and type parameter constraints
10. ✅ **Type guards** - typeof, in, Array.isArray

### Phase 2: High-Impact Features (CURRENT)
Target: 83/140 (59%) → 90/140 (64%)

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| `as const` | Type Assertions | Medium | High - const contexts |
| Default type parameters | Generics | Medium | High - better generics |
| Type predicates (`is`) | Type Narrowing | Medium | High - custom guards |
| Discriminated unions | Type Narrowing | High | High - pattern matching |
| Index signatures in classes | Classes | Medium | Medium |

### Phase 3: Type System Improvements
Target: 90/140 (64%) → 100/140 (71%)

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| Intersection types (`&`) | Union/Intersection | High | High |
| Improve control flow analysis | Type Narrowing | Medium | Medium |
| Improve union types | Union/Intersection | Medium | Medium |
| `never` type exhaustiveness | Type Narrowing | Medium | Medium |
| `asserts` keyword | Type Narrowing | Medium | Medium |

### Phase 4: Advanced Features
Target: 100/140 (71%) → 110/140 (79%)

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| Dynamic `import()` | Modules | High | Medium |
| Computed enum members | Enums | Medium | Low |
| Namespaces | Namespaces | Medium | Low |

### Phase 5: Framework Support (Optional)
Target: 110/140 (79%) → 120/140 (86%)

| Feature | Category | Effort | Impact |
|---------|----------|--------|--------|
| Decorators (all 6) | Decorators | Very High | Medium |
| JSX (all 4) | JSX | Very High | Medium |
| Mixins | Mixins | High | Low |

## Test Coverage Gaps

Current golden IR tests cover:
- Arrays (14 tests)
- Control flow (10 tests)
- Functions (14 tests)
- Objects (15 tests)
- Regression (4 tests)

Missing test categories:
- Classes (inheritance, abstract, static blocks)
- Generics (constraints, defaults)
- Type narrowing (instanceof, discriminated unions)
- Iterators/Generators (custom iterables)
- Symbols (full coverage)

## Files to Modify

| Phase | Files |
|-------|-------|
| 1 | `Analyzer_Expressions.cpp`, `IRGenerator_Expressions.cpp` |
| 2 | `Analyzer_StdLib_Symbol.cpp`, `IRGenerator_Iterators.cpp` |
| 3 | `Analyzer_ControlFlow.cpp`, `Analyzer_TypeNarrowing.cpp` |
| 4 | `Analyzer_Generics.cpp`, `IRGenerator_Classes.cpp` |
| 5 | `Analyzer_Decorators.cpp` (new), `IRGenerator_JSX.cpp` (new) |

## Success Metrics

| Phase | Target Conformance |
|-------|-------------------|
| Phase 1 (Complete) | 83/140 (59%) ✅ |
| After Phase 2 | 90/140 (64%) |
| After Phase 3 | 100/140 (71%) |
| After Phase 4 | 110/140 (79%) |
| After Phase 5 | 120/140 (86%) |

## Immediate Next Steps

1. Start with Phase 1 quick wins
2. Create test files for each feature before implementing
3. Update conformance matrix after each feature
4. Focus on features that unblock other features first

## Notes

- Many "Not Implemented" features in Type Manipulation are type-system only and won't affect runtime behavior
- Decorators are a significant undertaking - defer unless specifically needed
- JSX requires significant parser and codegen changes - defer unless React support is a priority
