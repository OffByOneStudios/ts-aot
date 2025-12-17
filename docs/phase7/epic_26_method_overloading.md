# Epic 26: Method Overloading (Basic)

**Goal:** Support basic method overloading in classes.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 26.1: AST and Type System Support
- [x] **Type System:** Update `ClassType` to store multiple signatures for a method.
- [x] **Analyzer:** Distinguish between overload signatures and the implementation.
- [x] **Analyzer:** Validate that the implementation is compatible with all signatures.

### Milestone 26.2: Overload Resolution
- [x] **Analyzer:** Implement overload resolution in `visitCallExpression`.
- [x] **Analyzer:** Select the best matching signature based on argument types.

### Milestone 26.3: CodeGen
- [x] **CodeGen:** Ensure the implementation is generated correctly.
- [x] **CodeGen:** Ensure calls use the correct implementation (which is shared).

## Action Items
- [x] **Task 1:** Update `ClassType` to support multiple signatures.
- [x] **Task 2:** Update `Analyzer::visitClassDeclaration` to collect all signatures.
- [x] **Task 3:** Implement overload resolution logic.
- [x] **Task 4:** Create integration tests for method overloading.
