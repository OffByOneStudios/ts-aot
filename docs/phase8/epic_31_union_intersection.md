# Epic 31: Union & Intersection Types

**Goal:** Support union and intersection types in the type system and analyzer.

**Dependencies:**
- Phase 7 (Advanced OOP)

## Milestones

### Milestone 31.1: Type System Support
- [x] **Type System:** Add `UnionType` and `IntersectionType` to `Type.h`.
- [x] **AST:** Update `AstLoader` to parse union and intersection type annotations.

### Milestone 31.2: Analyzer Support
- [x] **Analyzer:** Implement `isAssignableTo` logic for unions (assignable if assignable to any member).
- [x] **Analyzer:** Implement `isAssignableTo` logic for intersections (assignable if assignable to all members).
- [x] **Analyzer:** Support property access on unions (only members common to all types).

### Milestone 31.3: CodeGen
- [x] **CodeGen:** Implement runtime representation for union types (likely using a tagged union or `any` representation).
- [x] **CodeGen:** Ensure correct casting when narrowing or widening union types.

## Action Items
- [x] **Task 1:** Define `UnionType` and `IntersectionType` in `Type.h`.
- [x] **Task 2:** Update `AstLoader.cpp` to handle `|` and `&` in type strings.
- [x] **Task 3:** Update `Analyzer::isAssignableTo` to handle complex types.
- [x] **Task 4:** Create integration tests for union types.
