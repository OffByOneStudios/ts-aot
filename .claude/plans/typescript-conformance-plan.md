# Plan: TypeScript Features Conformance Improvement

## Current State

**Conformance: 75/140 runtime features (54%)**
- 75 Implemented (✅)
- 15 Partial (⚠️)
- 50 Not Implemented (❌)
- 34 Type-only (🔬) - don't require runtime support

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

### Phase 1: Quick Wins (Partials → Full)
1. **Non-null assertion (`!`)** - Already partial, just needs cleanup
2. **`module.exports`** - Likely minor CommonJS fix
3. **`this` type/parameter** - Minor analyzer work
4. **`instanceof` narrowing** - Already basic support
5. **Overloads** - Declaration only, verify works

### Phase 2: Iterator/Symbol Completion
6. **`symbol` full support** - Critical for iterators
7. **`Symbol.iterator`** - Already partial
8. **Iterable protocol** - Builds on above
9. **Generic constraints** - Better generics

### Phase 3: Type Narrowing
10. **Control flow analysis** - Improve existing
11. **Union types** - Improve existing
12. **Type guards** - Improve existing
13. **Type predicates (`is`)** - New feature
14. **Discriminated unions** - Builds on above

### Phase 4: Advanced Features
15. **`as const`** - Const contexts
16. **Default type parameters** - Better generics
17. **Index signatures in classes** - Map-like classes
18. **Intersection types** - Type composition
19. **Dynamic `import()`** - Code splitting

### Phase 5: Framework Support (Optional)
20. **Decorators** - Needed for Angular, NestJS
21. **JSX** - Needed for React

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
| Current | 75/140 (54%) |
| After Phase 1 | 80/140 (57%) |
| After Phase 2 | 85/140 (61%) |
| After Phase 3 | 92/140 (66%) |
| After Phase 4 | 100/140 (71%) |
| After Phase 5 | 110/140 (79%) |

## Immediate Next Steps

1. Start with Phase 1 quick wins
2. Create test files for each feature before implementing
3. Update conformance matrix after each feature
4. Focus on features that unblock other features first

## Notes

- Many "Not Implemented" features in Type Manipulation are type-system only and won't affect runtime behavior
- Decorators are a significant undertaking - defer unless specifically needed
- JSX requires significant parser and codegen changes - defer unless React support is a priority
