# TypeScript Conformance Plan: Batch 6 (Safe IR Changes)

**Status:** Complete
**Date:** 2026-01-23
**Priority:** Safety-first approach to IR generation changes

## Risk Assessment Summary

After completing keyof operator support, we need to carefully plan next steps. IR generation changes carry risk of breaking existing functionality.

### Risk Categories

| Risk Level | Features | IR Changes Required |
|------------|----------|---------------------|
| **NONE** | Triple-slash directives | Parser only |
| **LOW** | Indexed access types, Ambient enums | Type resolution only |
| **MEDIUM** | Conditional types, Mapped types | Complex type computation |
| **HIGH** | Decorators, Mixins, Namespaces, Dynamic import | Major IR restructuring |

## Recommended Implementation Order

### Phase 1: Zero IR Risk (Parser/Preprocessor Only)

#### 1.1 Triple-Slash Directives
**Files to Modify:**
- `scripts/dump_ast.js` - Parse `/// <reference ... />` comments
- `src/compiler/ast/AstLoader_Core.cpp` - Load reference paths
- No IR generation changes required

**Specific Changes:**
```
dump_ast.js:
  - In parseSourceFile, check for leading /// comments
  - Extract path="...", types="...", lib="..." attributes
  - Add tripleSlashReferences array to AST output

AstLoader_Core.cpp:
  - Load tripleSlashReferences from JSON
  - Resolve paths relative to current file
  - Queue referenced files for compilation
```

**Test:** `tests/golden_ir/typescript/directives/reference_path.ts`

**Risk:** Zero - no IR changes, no runtime changes

---

### Phase 2: Low IR Risk (Type Resolution Only)

#### 2.1 Indexed Access Types (T['prop'])
**Current Status:** ❌ Not implemented
**Value:** HIGH - common in Pick<T,K>, Omit<T,K> patterns

**Files to Modify:**
- `src/compiler/analysis/Analyzer_Helpers.cpp` (parseType function)
- `src/compiler/analysis/Analyzer_Expressions_Access.cpp` (already modified for keyof)

**Specific Changes:**
```cpp
// In parseType(), after handling existing types:
// Handle indexed access: T[K]
if (node type is IndexedAccessType) {
    auto objectType = parseType(node.objectType);
    auto indexType = parseType(node.indexType);

    if (indexType->kind == TypeKind::String && isStringLiteral(indexType)) {
        // T['prop'] - return the specific property type
        if (objectType is ClassType or ObjectType) {
            return objectType->fields[indexType->stringValue];
        }
    }
    return std::make_shared<Type>(TypeKind::Any);
}
```

**IR Changes:** None - purely type-level, erased at compile time

**Test:** `tests/golden_ir/typescript/types/indexed_access_type.ts`

**Risk:** LOW - only analyzer changes, no codegen

---

#### 2.2 Ambient Enums
**Current Status:** ❌ Not implemented
**Value:** LOW - rare in practice

**Files to Modify:**
- `scripts/dump_ast.js` - Mark `declare enum` in AST
- `src/compiler/ast/AstNodes.h` - Add `isDeclare` flag to EnumDeclaration
- `src/compiler/codegen/IRGenerator_Statements.cpp` - Skip codegen for declare enums

**Specific Changes:**
```cpp
// In visitEnumDeclaration:
if (enumDecl->isDeclare) {
    // Ambient enum - no code generation needed
    // Type information already registered in analyzer
    return;
}
```

**IR Changes:** Removal only (skip codegen) - cannot break existing code

**Test:** `tests/golden_ir/typescript/declarations/ambient_enum.ts`

**Risk:** LOW - skip codegen, no new IR patterns

---

### Phase 3: Medium IR Risk (New Type Patterns)

#### 3.1 Conditional Types (T extends U ? X : Y)
**Current Status:** ❌ Not implemented
**Value:** MEDIUM - common in library code, rare in application code

**Files to Modify:**
- `src/compiler/analysis/Analyzer_Helpers.cpp` - Parse conditional types
- `src/compiler/analysis/Type.h` - Add ConditionalType class
- `src/compiler/analysis/Analyzer.cpp` - Evaluate conditional types

**Specific Changes:**
```cpp
// New type class
class ConditionalType : public Type {
    std::shared_ptr<Type> checkType;    // T
    std::shared_ptr<Type> extendsType;  // U
    std::shared_ptr<Type> trueType;     // X
    std::shared_ptr<Type> falseType;    // Y
};

// Resolution logic
std::shared_ptr<Type> resolveConditionalType(ConditionalType* ct) {
    if (isSubtypeOf(ct->checkType, ct->extendsType)) {
        return ct->trueType;
    }
    return ct->falseType;
}
```

**IR Changes:** None if resolved at compile time

**Test:** `tests/golden_ir/typescript/types/conditional_type.ts`

**Risk:** MEDIUM - complex type resolution, but no IR if fully resolved

---

### Phase 4: Features to DEFER (High Risk)

#### Defer: Decorators (6 features)
**Reason:** Requires function wrapping, class modification at runtime
**IR Impact:** New calling conventions, wrapper functions
**Defer until:** After core language stability achieved

#### Defer: Mixins (2 features)
**Reason:** Dynamic class construction, prototype manipulation
**IR Impact:** Runtime class factory patterns
**Defer until:** After decorators

#### Defer: Namespaces (3 features)
**Reason:** Complex scoping, declaration merging
**IR Impact:** New module structure
**Defer until:** Evaluate if needed for real-world code

#### Defer: Dynamic import() (1 feature)
**Reason:** Runtime module loading incompatible with AOT
**IR Impact:** Would require dynamic linking
**Defer until:** Consider marking as N/A for AOT

---

## Batch 6 Implementation Plan

### Week 1: Triple-Slash Directives

| Day | Task | File | Function |
|-----|------|------|----------|
| 1 | Parse /// comments in dump_ast.js | scripts/dump_ast.js | parseSourceFile |
| 1 | Add tripleSlashReferences to JSON output | scripts/dump_ast.js | transformAST |
| 2 | Load references in AstLoader | AstLoader_Core.cpp | loadSourceFile |
| 2 | Queue referenced files for compilation | AstLoader_Core.cpp | loadProgram |
| 3 | Create golden IR tests | tests/golden_ir/ | New files |
| 3 | Update conformance matrix | docs/conformance/ | typescript-features.md |

### Week 2: Indexed Access Types

| Day | Task | File | Function |
|-----|------|------|----------|
| 1 | Parse IndexedAccessType in dump_ast.js | scripts/dump_ast.js | transformType |
| 1 | Load IndexedAccessTypeNode in AST | AstLoader_Core.cpp | loadType |
| 2 | Resolve T['prop'] in parseType | Analyzer_Helpers.cpp | parseType |
| 2 | Handle string literal index types | Analyzer_Helpers.cpp | parseType |
| 3 | Create golden IR tests | tests/golden_ir/ | indexed_access_type.ts |
| 3 | Update conformance matrix | docs/conformance/ | typescript-features.md |

### Week 3: Ambient Enums

| Day | Task | File | Function |
|-----|------|------|----------|
| 1 | Parse declare keyword for enums | scripts/dump_ast.js | transformEnumDecl |
| 1 | Add isDeclare flag to EnumDeclaration | AstNodes.h | EnumDeclaration |
| 2 | Skip codegen for ambient enums | IRGenerator_Statements.cpp | visitEnumDeclaration |
| 3 | Create golden IR tests | tests/golden_ir/ | ambient_enum.ts |
| 3 | Update conformance matrix | docs/conformance/ | typescript-features.md |

---

## Safety Checklist Before Each Change

Before modifying any IRGenerator file:

- [ ] Run full test suite: `python tests/node/run_tests.py && python tests/golden_ir/runner.py`
- [ ] Note baseline pass rate
- [ ] Make ONE change at a time
- [ ] Run tests after each change
- [ ] Verify no regressions (pass rate >= baseline)
- [ ] If regression, revert immediately and analyze

## Files at Risk

These files require extra caution:

| File | Last Modified | Risk Reason |
|------|--------------|-------------|
| `IRGenerator_Expressions_Access.cpp` | Today | Just modified for keyof |
| `IRGenerator_Expressions_Binary.cpp` | Today | Boxing changes for any type |
| `IRGenerator_Functions.cpp` | Today | Overload handling changes |
| `IRGenerator_Statements.cpp` | Today | Switch exhaustiveness |

**Rule:** Wait 1 week before modifying these files again to ensure stability.

---

## Expected Outcomes

After Batch 6 completion:

| Metric | Current | Target |
|--------|---------|--------|
| TypeScript Features | 102/140 (73%) | 106/140 (76%) |
| Node.js Tests | 277/279 (99.3%) | 277/279 (99.3%) |
| Golden IR Tests | 116/116 (100%) | 120/120 (100%) |

Features added:
- Triple-slash reference path ✅
- Triple-slash reference types ✅
- Triple-slash reference lib ✅
- Indexed access types ✅
- Ambient enums ✅
