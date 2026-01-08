# Epic 24: Advanced Class Features

**Goal:** Implement `static` members, `readonly` properties, and Getters/Setters.

**Dependencies:**
- Phase 6 (Basic Classes)

## Milestones

### Milestone 24.1: Static Members
- [x] **AST:** Add `isStatic` flag to properties/methods.
- [x] **Semantics:** Static members are accessed via Class Name, not instance.
- [x] **CodeGen:**
    - Static Fields: Global variables (mangled as `ClassName_FieldName`).
    - Static Methods: Normal functions (no `this` pointer).

### Milestone 24.2: Readonly Properties
- [x] **AST:** Add `isReadonly` flag.
- [x] **Semantics:** Prevent assignment to `readonly` properties outside of the constructor.

### Milestone 24.3: Getters and Setters
- [x] **AST:** Add `GetAccessor` and `SetAccessor` nodes (or flags on MethodDefinition).
- [x] **Semantics:**
    - `obj.prop` access resolves to Getter call.
    - `obj.prop = val` resolves to Setter call.
- [x] **CodeGen:**
    - Generate methods `get_prop` and `set_prop`.
    - Rewrite property access/assignment in `IRGenerator` to call these methods.

### Milestone 24.4: Parameter Properties
- [x] **AST:** Add `access` and `isReadonly` to `Parameter`.
- [x] **Semantics:** Automatically create fields and inject assignments in constructor.

## Action Items
- [x] **Task 1:** Implement Static members (Parser, Analyzer, CodeGen).
- [x] **Task 2:** Implement Readonly checks (Analyzer).
- [x] **Task 3:** Implement Getters/Setters (Parser, Analyzer, CodeGen).
- [x] **Task 4:** Implement Parameter Properties.
- [x] **Task 5:** Create integration tests.
