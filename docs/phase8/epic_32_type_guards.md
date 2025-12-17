# Epic 32: Type Guards

**Goal:** Implement `typeof`, `instanceof`, and type narrowing.

**Dependencies:**
- Epic 31 (Union Types)

## Milestones

### Milestone 32.1: `typeof` Operator
- [ ] **AST:** Support `typeof` in expressions.
- [ ] **Analyzer:** Infer string literal types for `typeof` (e.g., `"string"`, `"number"`).
- [ ] **CodeGen:** Implement `ts_typeof` in the runtime.

### Milestone 32.2: `instanceof` Operator
- [ ] **AST:** Support `instanceof` in expressions.
- [ ] **Analyzer:** Validate `instanceof` against class types.
- [ ] **CodeGen:** Implement `ts_instanceof` using VTable or class ID checks.

### Milestone 32.3: Type Narrowing
- [ ] **Analyzer:** Implement flow-sensitive type narrowing in `if` statements.
- [ ] **Analyzer:** Support narrowing union types based on `typeof` checks.

## Action Items
- [ ] **Task 1:** Add `typeof` and `instanceof` to AST and Parser.
- [ ] **Task 2:** Implement runtime support for `typeof`.
- [ ] **Task 3:** Implement flow-sensitive narrowing in the `Analyzer`.
- [ ] **Task 4:** Create integration tests for type narrowing.
