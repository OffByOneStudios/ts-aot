# Epic 22: Interfaces

**Goal:** Implement TypeScript interfaces for structural typing contracts. Interfaces in TypeScript are purely compile-time constructs and do not emit code, but they enforce correctness.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 22.1: AST & Parser
- [x] Add `InterfaceDeclaration` AST node.
- [x] Add `implements` clause to `ClassDeclaration`.
- [x] Update `Parser` to handle `interface Foo { ... }`.

### Milestone 22.2: Semantic Analysis (Declaration)
- [x] **Symbol Table:** Register interface names and their members.
- [x] **Type Checker:** Allow interfaces to extend other interfaces.

### Milestone 22.3: Semantic Analysis (Implementation)
- [x] **Class Check:** Verify that a class implementing an interface defines all required members.
- [x] **Type Compatibility:** Update `Type::isAssignableTo` to support structural matching against interfaces.

## Action Items
- [x] **Task 1:** Define `InterfaceDeclaration` node.
- [x] **Task 2:** Implement parsing for interfaces.
- [x] **Task 3:** Update `Analyzer` to register interfaces.
- [x] **Task 4:** Implement "Class implements Interface" check in `Analyzer`.
- [x] **Task 5:** Create integration test `tests/integration/interfaces.ts`.
