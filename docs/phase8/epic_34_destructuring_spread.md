# Epic 34: Destructuring & Spread

**Goal:** Support modern TypeScript syntax for object and array destructuring, as well as the spread/rest operator.

**Dependencies:**
- Phase 1-7 (Core Compiler)
- Epic 31 (Union & Intersection Types)

## Milestones

### Milestone 34.1: Array Destructuring
- [ ] **AST:** Support `ArrayBindingPattern` in variable declarations.
- [ ] **Analyzer:** Infer types for variables in array destructuring.
- [ ] **CodeGen:** Implement value extraction for array destructuring.

### Milestone 34.2: Object Destructuring
- [ ] **AST:** Support `ObjectBindingPattern` in variable declarations.
- [ ] **Analyzer:** Infer types for variables in object destructuring.
- [ ] **CodeGen:** Implement value extraction for object destructuring.

### Milestone 34.3: Spread & Rest Operators
- [ ] **AST:** Support spread operator in array/object literals and rest parameters in functions.
- [ ] **CodeGen:** Implement spread/rest logic.

## Action Items
- [ ] **Task 1:** Update `dump_ast.js` to output destructuring patterns.
- [ ] **Task 2:** Add `ObjectBindingPattern`, `ArrayBindingPattern`, and `BindingElement` to `AstNodes.h`.
- [ ] **Task 3:** Update `AstLoader.cpp` to parse destructuring patterns.
- [ ] **Task 4:** Implement destructuring support in `Analyzer`.
- [ ] **Task 5:** Implement destructuring support in `IRGenerator`.
- [ ] **Task 6:** Implement spread operator in array literals.
- [ ] **Task 7:** Implement rest parameters in functions.
