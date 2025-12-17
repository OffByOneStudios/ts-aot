# Epic 24: Static Members

**Goal:** Support the `static` keyword for class properties and methods.

## Status
- **Status:** In Progress
- **Date:** 2024-05-22

## Milestones

### 24.1: AST & Parser Support
- [ ] Update `AstNodes.h` to include `isStatic` in `PropertyDeclaration` and `MethodDeclaration`.
- [ ] Update `AstLoader.cpp` to parse `static` modifier from JSON.

### 24.2: Type System & Analysis
- [ ] Update `ClassType` to store static fields and methods.
- [ ] Update `Analyzer` to resolve static member access (e.g., `Class.member`).
- [ ] Enforce that static members are not accessible on instances.
- [ ] Enforce that instance members are not accessible on the class.

### 24.3: Code Generation
- [ ] Implement codegen for static fields (global variables).
- [ ] Implement codegen for static methods (global functions without `this`).
- [ ] Update `Monomorphizer` to handle static member references.

### 24.4: Verification
- [ ] Create `tests/integration/static_members.ts`.
- [ ] Verify correct behavior and initialization of static fields.

## Technical Notes
- Static fields should be initialized at program startup or on first access (if complex). For now, we'll assume simple initialization or handle it in a global init function.
- Static methods do not have a `this` context.
