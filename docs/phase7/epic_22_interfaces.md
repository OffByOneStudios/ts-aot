# Epic 22: Interfaces

**Goal:** Implement TypeScript interfaces for structural typing contracts. Interfaces in TypeScript are purely compile-time constructs and do not emit code, but they enforce correctness.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 22.1: AST & Parser
- [ ] Add `InterfaceDeclaration` AST node.
- [ ] Add `implements` clause to `ClassDeclaration`.
- [ ] Update `Parser` to handle `interface Foo { ... }`.

### Milestone 22.2: Semantic Analysis (Declaration)
- [ ] **Symbol Table:** Register interface names and their members.
- [ ] **Type Checker:** Allow interfaces to extend other interfaces.

### Milestone 22.3: Semantic Analysis (Implementation)
- [ ] **Class Check:** Verify that a class implementing an interface defines all required members.
- [ ] **Type Compatibility:** Update `Type::isAssignableTo` to support structural matching against interfaces.

## Action Items
- [ ] **Task 1:** Define `InterfaceDeclaration` node.
- [ ] **Task 2:** Implement parsing for interfaces.
- [ ] **Task 3:** Update `Analyzer` to register interfaces.
- [ ] **Task 4:** Implement "Class implements Interface" check in `Analyzer`.
- [ ] **Task 5:** Create integration test `tests/integration/interfaces.ts`.
