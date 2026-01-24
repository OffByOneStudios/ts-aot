# TypeScript Conformance - Batch 3 Plan

**Created:** 2026-01-23
**Status:** Complete
**Goal:** Improve TypeScript conformance from 66% toward 75%+

## Completed Work (2026-01-23)

### Phase 1: Discriminated Union Narrowing
- Added narrowing code to `Analyzer_Statements.cpp` for `shape.kind === "literal"` pattern
- **Limitation:** Works when union members have different properties, but full support requires literal types (not yet implemented)
- Status: ⚠️ Partial (changed from ❌)

### Phase 2: Default Type Parameters
- Updated `dump_ast.js` to output default type from TypeScript AST
- Added `defaultType` field to `TypeParameter` struct in `AstNodes.h`
- Updated `parseTypeParameter()` in `AstLoader_Core.cpp`
- Added `defaultType` to `TypeParameterType` in `Type.h`
- Updated all locations that create TypeParameterType to also set defaultType
- Updated `inferTypeArguments()` to use default type when not inferred
- Status: ✅ Complete (was already marked ✅)

### Phase 3: Index Signatures in Classes
- Added `IndexSignature` AST node and parsing in `dump_ast.js` and `AstLoader_Core.cpp`
- Added `indexSignatureValueType` field to `ClassType` in `Type.h`
- Classes with index signatures get `__properties__` TsMap field in layout
- Element access/assignment uses `emitInlineMapGet/Set` on `__properties__`
- Test: `tests/golden_ir/typescript/index_signature_class.ts`
- Status: ✅ Complete (changed from ❌)

### Test Results
- Node.js tests: 279/279 (100%)
- Golden IR tests: 111/112 (99.1%)

## Current State

- **TypeScript:** 93/140 runtime features (66%)
- **ECMAScript:** 214/230 features (93%)
- **Node.js:** ~99% coverage
- **Partial features remaining:** 6

## Next: Batch 4 - Fix Partial Features

See `PLAN-typescript-conformance-batch4.md` for continued work.

## Analysis

### Remaining Partial Features (6)

| Feature | Category | Audit Result | Fix Effort |
|---------|----------|--------------|------------|
| Indexable types | Interfaces | Element assignment returns undefined | Medium |
| Hybrid types | Interfaces | Return values wrong, crashes on method call | High |
| `this` type | Classes | VTable linker errors with `this` annotation | High |
| `this` parameter | Functions | Object methods return wrong values | Medium |
| Overloads | Functions | Standalone broken, class crashes on strings | High |
| `asserts` keyword | Narrowing | Syntax only, no runtime narrowing | Medium |

### Not Implemented - By Priority

**Tier 1: High Impact, Achievable**
| Feature | Category | Why Important | Effort |
|---------|----------|---------------|--------|
| Discriminated unions | Narrowing | Very common TS pattern | Medium |
| Default type parameters | Generics | Common in library code | Low |
| Index signatures in classes | Classes | Common pattern | Low |

**Tier 2: Medium Impact**
| Feature | Category | Notes |
|---------|----------|-------|
| `never` exhaustiveness | Narrowing | Switch exhaustiveness checks |
| Computed enum members | Enums | Expressions in enum values |
| Dynamic `import()` | Modules | Hard for AOT, but common |

**Tier 3: Low Priority (High Effort)**
| Feature | Count | Why Skip |
|---------|-------|----------|
| Decorators | 6 | Requires TC39 Stage 3 semantics |
| Namespaces | 3 | Requires parser/codegen work |
| JSX | 4 | Requires JSX parser |
| Mixins | 2 | Complex pattern |
| Triple-slash | 4 | Pre-processor directives |

## Implementation Plan

### Phase 1: Discriminated Union Narrowing

**Goal:** Enable narrowing based on literal discriminant property checks.

**Pattern:**
```typescript
interface Circle { kind: "circle"; radius: number; }
interface Square { kind: "square"; side: number; }
type Shape = Circle | Square;

function area(shape: Shape) {
    if (shape.kind === "circle") {
        return shape.radius * shape.radius * 3.14; // shape narrowed to Circle
    }
}
```

**Implementation:**
1. In `visitIfStatement`, detect `shape.kind === "literal"` pattern
2. When condition is `PropertyAccess === StringLiteral`:
   - Get the union type of the object
   - Find union members where the property has matching literal type
   - Narrow to those members
3. Requires: Checking property types in InterfaceType for literal types

**Files:**
- `src/compiler/analysis/Analyzer_Statements.cpp`
- Possibly `src/compiler/analysis/Type.h` (for literal type tracking)

### Phase 2: Default Type Parameters

**Goal:** Support default values for generic type parameters.

**Pattern:**
```typescript
interface Container<T = string> {
    value: T;
}
const c: Container = { value: "hello" }; // T defaults to string
```

**Implementation:**
1. Parse default type from AST (already captured?)
2. In type parameter resolution, use default when not specified
3. Monomorphization should use default type

**Files:**
- `src/compiler/analysis/Analyzer_Classes.cpp`
- `src/compiler/analysis/Type.h`

### Phase 3: Index Signatures in Classes

**Goal:** Support `[key: string]: T` syntax in class definitions.

**Pattern:**
```typescript
class Dictionary {
    [key: string]: number;
}
const d = new Dictionary();
d.foo = 42;
```

**Implementation:**
1. Parse IndexSignature in class members
2. Store index signature type in ClassType
3. Codegen: Use dynamic property access when type has index signature

**Files:**
- `src/compiler/analysis/Analyzer_Classes.cpp`
- `src/compiler/codegen/IRGenerator_Expressions.cpp`

### Phase 4: Fix Remaining Partial Features

**`this` parameter fix:**
- Object methods with explicit `this` type return wrong values
- Likely codegen issue with `this` binding

**Overloads fix:**
- Standalone function overloads return 0/undefined
- Class method overloads crash on string arguments
- Need to investigate dispatch logic

## Success Criteria

- [ ] Discriminated union narrowing works
- [ ] Default type parameters work
- [ ] Index signatures in classes work
- [ ] At least 2 partial features upgraded to ✅
- [ ] Conformance reaches 75%+ (105/140)

## Test Plan

Create test files for each feature:
```
tests/golden_ir/typescript/discriminated_unions.ts
tests/golden_ir/typescript/default_type_params.ts
tests/golden_ir/typescript/class_index_signature.ts
```

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Discriminated unions complex | Medium | Start with simple cases |
| Breaking existing code | Medium | Run full test suite after each change |
| Type system changes cascade | High | Incremental changes, test frequently |

## Estimated Impact

| Phase | Features | % Increase |
|-------|----------|------------|
| Phase 1 | +2 | 66% → 68% |
| Phase 2 | +1 | 68% → 69% |
| Phase 3 | +1 | 69% → 70% |
| Phase 4 | +2-4 | 70% → 73%+ |

**Target: 75%+ conformance (105/140 features)**
