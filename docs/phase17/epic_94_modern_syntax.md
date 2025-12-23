# Epic 94: Modern Syntax & Operators

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement modern TypeScript/JavaScript operators and syntax features.

## Milestones

### Milestone 94.1: Short-circuiting Operators
- [ ] **Optional Chaining:** Implement `?.` for properties, calls, and element access.
- [ ] **Nullish Coalescing:** Implement `??` and `??=`.

### Milestone 94.2: Template Literals
- [ ] **Tagged Templates:** Implement tagged template literal calls.

### Milestone 94.3: Destructuring
- [ ] **Advanced Destructuring:** Support nested patterns and rest elements in arrays/objects.

## Action Items
- [ ] **Task 94.1:** Implement `OptionalExpression` in `IRGenerator`.
- [ ] **Task 94.2:** Implement `BinaryExpression` logic for `??`.
- [ ] **Task 94.3:** Verify with `ts-aot examples/modern_syntax_test.ts`.
