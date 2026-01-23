# TypeScript Conformance - Batch 2 Plan

**Created:** 2026-01-22
**Status:** Audit Complete
**Goal:** Improve TypeScript conformance from 64% to 70%+

## Current State

- **Implemented:** 90/140 runtime features (64%)
- **Partial:** 8 features
- **Not Implemented:** 42 features
- **Type-Only:** 34 features (no runtime impact)

## Audit Results (Phase 1 Complete)

All 8 partial features were audited. **None can be upgraded to ✅** - all remain correctly marked as partial.

| Feature | Status | Test Results |
|---------|--------|--------------|
| Indexable types | ⚠️ Confirmed | Object.keys works, but element assignment returns undefined |
| Hybrid types | ⚠️ Confirmed | Callable objects work, but return values wrong, crashes on method call |
| `this` type | ⚠️ Confirmed | Explicit return type works, `this` return annotation causes linker error |
| `this` parameter | ⚠️ Confirmed | Syntax accepted, but object methods return wrong values |
| Overloads | ⚠️ Confirmed | Class method overloads work for numbers, crash on strings; standalone broken |
| Control flow analysis | ⚠️ Confirmed | typeof/truthiness work, null check narrowing broken |
| Union types | ⚠️ Confirmed | typeof works, Array.isArray/in narrowing broken |
| `asserts` keyword | ⚠️ Confirmed | Syntax compiles, runtime narrowing not supported |

### Key Issues Found

1. **Discriminated unions crash compiler** - interface with `kind` literal property + union causes segfault
2. **Null check narrowing** - `if (s !== null)` doesn't narrow correctly
3. **Array.isArray narrowing** - arrOrNum.length returns 0 after check
4. **`in` operator narrowing** - doesn't access correct property after check
5. **`this` return type** - causes `undefined symbol: *_VTable_Global` linker errors

## Priority Categories

### Tier 1: Audit Partial Features (Quick Wins)

These are marked ⚠️ but may already be more complete:

| Feature | Current Status | Action |
|---------|---------------|--------|
| Indexable types | ⚠️ | Test `[key: string]: T` syntax |
| Hybrid types | ⚠️ | Test callable objects with properties |
| `this` type | ⚠️ | Test `this` return type in fluent APIs |
| `this` parameter | ⚠️ | Test explicit `this` in function params |
| Overloads | ⚠️ | Test overload declarations + implementation |
| Control flow analysis | ⚠️ | Test narrowing after if/switch |
| Union types | ⚠️ | Test union handling and narrowing |

### Tier 2: Low-Effort Features

| Feature | Effort | Notes |
|---------|--------|-------|
| Discriminated unions | Medium | Narrowing based on literal discriminant |
| Index signatures in classes | Low | May already work, needs testing |
| Mixin classes | Medium | Pattern-based, may partially work |

### Tier 3: Type-System Features (Compile-Time Only)

These are ❌ but have no runtime impact - just need analyzer support:

| Feature | Impact |
|---------|--------|
| `keyof` operator | Type-level, erased at runtime |
| `keyof` constraint | Type-level, erased at runtime |
| Indexed access types | Type-level, erased at runtime |
| Conditional types | Type-level, erased at runtime |
| Mapped types | Type-level, erased at runtime |
| Template literal types | Type-level, erased at runtime |

### Tier 4: Skip for Now (High Effort)

| Category | Features | Reason |
|----------|----------|--------|
| Decorators | 6 | Requires TC39 Stage 3 decorator semantics |
| JSX | 4 | Requires JSX parser integration |
| Namespaces | 3 | Requires parser/codegen work |
| Declaration Merging | 6 | Complex type system changes |
| Triple-Slash | 4 | Pre-processor directives |

## Implementation Plan

### Phase 1: Audit and Upgrade Partial Features

For each ⚠️ feature:
1. Create test file in `examples/test_<feature>.ts`
2. Compile and run
3. If working, upgrade to ✅ with notes
4. If broken, document specific gaps

**Test files to create:**
```
examples/test_indexable_types.ts
examples/test_hybrid_types.ts
examples/test_this_type.ts
examples/test_this_parameter.ts
examples/test_overloads.ts
examples/test_control_flow.ts
examples/test_union_narrowing.ts
```

### Phase 2: Implement Discriminated Unions

Discriminated unions are a common pattern:
```typescript
type Shape = { kind: "circle"; radius: number } | { kind: "square"; side: number };

function area(shape: Shape) {
    if (shape.kind === "circle") {
        return Math.PI * shape.radius ** 2;  // shape narrowed to circle
    } else {
        return shape.side ** 2;  // shape narrowed to square
    }
}
```

**Implementation:**
1. Analyzer: Detect literal property checks in conditionals
2. Analyzer: Narrow union type based on discriminant value
3. Codegen: No changes needed (uses narrowed type)

### Phase 3: Test Mixin Pattern

Mixins may already work since we have class expressions:
```typescript
function Timestamped<T extends new(...args: any[]) => {}>(Base: T) {
    return class extends Base {
        timestamp = Date.now();
    };
}
```

**Test:**
1. Create mixin function
2. Apply to base class
3. Verify properties from both are accessible

## Success Criteria

- [x] All 8 partial features audited
- [x] Fixed type narrowing issues (null check, `in` operator, Array.isArray)
- [x] Discriminated unions no longer crash (property access on interfaces is separate issue)
- [x] Conformance improved from 64% to 66%

## Completed Work (2026-01-23)

The following narrowing issues were fixed in `Analyzer_Statements.cpp`:

1. **Null check narrowing** - Fixed by checking for `NullLiteral` and `UndefinedLiteral` AST nodes instead of `Identifier` with name "null"
2. **`in` operator narrowing** - Added support for narrowing union types based on property existence in ObjectType, ClassType, and InterfaceType
3. **Array.isArray narrowing** - Added CallExpression handling to detect `Array.isArray(x)` pattern and narrow to array types
4. **Discriminated union crash** - No longer crashes (was likely fixed in a previous change)

### Test Results
- Node.js tests: 279/279 (100%)
- Golden IR tests: 111/111 (100%)

### Conformance Update
- Control flow analysis: ⚠️ → ✅ (null check narrowing works)
- Union types: ⚠️ → ✅ (typeof, Array.isArray, in narrowing work)
- Total: 92/140 runtime features (66%, up from 64%)

## Files to Modify

| Component | Files |
|-----------|-------|
| Test files | `examples/test_*.ts` |
| Analyzer | `src/compiler/analysis/Analyzer_Expressions.cpp` |
| Conformance | `docs/conformance/typescript-features.md` |

## Estimated Scope

| Phase | Effort | Impact |
|-------|--------|--------|
| Phase 1 (Audit) | 2-3 hours | +0 to +7 features |
| Phase 2 (Discriminated) | 4-6 hours | +2 features |
| Phase 3 (Mixins) | 2-3 hours | +0 to +2 features |

## Notes

- Focus on features that improve real-world compatibility
- Type-only features (Tier 3) can be added later with no runtime changes
- Decorators are increasingly important but require significant work
