# Epic 23: Access Modifiers

**Goal:** Implement `public`, `private`, and `protected` access modifiers to enforce encapsulation.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 23.1: AST & Parser
- [ ] Update `PropertyDefinition` and `MethodDefinition` to store access level (`Public`, `Private`, `Protected`).
- [ ] Update `Parser` to consume these keywords.
- [ ] Default to `Public` if unspecified.

### Milestone 23.2: Semantic Analysis
- [ ] **Visibility Check:**
    - `public`: Accessible from anywhere.
    - `private`: Accessible only within the defining class.
    - `protected`: Accessible within the defining class and subclasses.
- [ ] **Analyzer:** Update `visitPropertyAccessExpression` to check visibility against the current context (i.e., "Where are we right now?").

## Action Items
- [ ] **Task 1:** Update AST nodes with `AccessModifier` enum.
- [ ] **Task 2:** Update Parser.
- [ ] **Task 3:** Implement visibility checking logic in `Analyzer`.
- [ ] **Task 4:** Create integration test `tests/integration/access_modifiers.ts` (expect compile errors).
