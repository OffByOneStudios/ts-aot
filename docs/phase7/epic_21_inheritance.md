# Epic 21: Inheritance

**Goal:** Implement class inheritance using the `extends` keyword, allowing classes to inherit fields and methods from a base class, override methods, and use `super`.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 21.1: AST & Parser
- [x] Update `ClassDeclaration` to support optional `extends` clause (Identifier).
- [x] Update `Parser` to handle `class Child extends Parent { ... }`.
- [x] Update `CallExpression` or `SuperExpression` to handle `super()` and `super.method()`.

### Milestone 21.2: Semantic Analysis
- [x] **Symbol Table:** Resolve parent class name. Store parent reference in `ClassType`.
- [x] **Type Checker:**
    - [x] Ensure parent is a class.
    - [x] Check for circular inheritance.
    - [x] Validate method overrides (signature compatibility).
    - [x] Validate `super()` calls in constructor (must be first statement if derived).
- [x] **Field Layout:** Calculate total fields (Parent fields + Child fields).

### Milestone 21.3: Code Generation (VTable & Layout)
- [x] **Struct Layout:**
    - [x] Flattened (`{ vptr, parent_fields..., child_fields... }`).
- [x] **VTable Generation:**
    - [x] Create a new VTable for the child.
    - [x] Copy function pointers from Parent VTable.
    - [x] Overwrite pointers for overridden methods.
    - [x] Append pointers for new methods.

### Milestone 21.4: Code Generation (Logic)
- [x] **Constructor:**
    - [x] If `super()` is called, invoke Parent constructor.
    - [x] Pass `this` (casted if necessary) to parent constructor.
- [x] **Super Calls:**
    - [x] `super.method()`: Statically resolve to Parent's implementation of `method`.

## Action Items
- [x] **Task 1:** Update AST and Parser for `extends`.
- [x] **Task 2:** Update `Analyzer` to resolve inheritance chain.
- [x] **Task 3:** Implement VTable inheritance logic in `IRGenerator`.
- [x] **Task 4:** Implement `super()` constructor calls in `IRGenerator`.
- [x] **Task 5:** Create integration test `tests/integration/inheritance.ts`.
