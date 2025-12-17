# Epic 24: Advanced Class Features

**Goal:** Implement `static` members, `readonly` properties, and Getters/Setters.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 24.1: Static Members
- [ ] **AST:** Add `isStatic` flag to properties/methods.
- [ ] **Semantics:** Static members are accessed via Class Name, not instance.
- [ ] **CodeGen:**
    - Static Fields: Global variables (mangled as `ClassName_FieldName`).
    - Static Methods: Normal functions (no `this` pointer).

### Milestone 24.2: Readonly Properties
- [ ] **AST:** Add `isReadonly` flag.
- [ ] **Semantics:** Prevent assignment to `readonly` properties outside of the constructor.

### Milestone 24.3: Getters and Setters
- [ ] **AST:** Add `GetAccessor` and `SetAccessor` nodes (or flags on MethodDefinition).
- [ ] **Semantics:**
    - `obj.prop` access resolves to Getter call.
    - `obj.prop = val` resolves to Setter call.
- [ ] **CodeGen:**
    - Generate methods `get_prop` and `set_prop`.
    - Rewrite property access/assignment in `IRGenerator` to call these methods.

## Action Items
- [ ] **Task 1:** Implement Static members (Parser, Analyzer, CodeGen).
- [ ] **Task 2:** Implement Readonly checks (Analyzer).
- [ ] **Task 3:** Implement Getters/Setters (Parser, Analyzer, CodeGen).
- [ ] **Task 4:** Create integration tests.
