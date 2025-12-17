# Epic 23: Access Modifiers

**Goal:** Implement `public`, `private`, and `protected` access modifiers to enforce encapsulation.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 23.1: AST & Parser
- [x] Update `PropertyDefinition` and `MethodDefinition` to store access level (`Public`, `Private`, `Protected`).
- [x] Update `Parser` to consume these keywords.
- [x] Default to `Public` if unspecified.

### Milestone 23.2: Semantic Analysis
- [x] **Visibility Check:**
    - `public`: Accessible from anywhere.
    - `private`: Accessible only within the defining class.
    - `protected`: Accessible within the defining class and subclasses.
- [x] **Analyzer:** Update `visitPropertyAccessExpression` to check visibility against the current context (i.e., "Where are we right now?").

## Action Items
- [x] **Task 1:** Update AST nodes with `AccessModifier` enum.
- [x] **Task 2:** Update Parser.
- [x] **Task 3:** Implement visibility checking logic in `Analyzer`.
- [x] **Task 4:** Create integration test `tests/integration/access_modifiers.ts` (expect compile errors).
