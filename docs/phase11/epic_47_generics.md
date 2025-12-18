# Epic 47: Generics

**Status:** In Progress
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Generics allow for reusable components that work with a variety of types rather than a single one. This is a major architectural addition to the compiler's type system and monomorphization pass.

## Milestones

### Milestone 47.1: Generic Functions
- **AST:** Support `TypeParameter` in function declarations.
- **Analysis:** Track type parameters in the scope.
- **Monomorphization:** Generate specialized versions of functions for each unique set of type arguments.

### Milestone 47.2: Generic Classes
- **AST:** Support `TypeParameter` in class declarations.
- **Analysis:** Handle generic fields and methods.
- **Codegen:** Monomorphize classes based on usage (e.g., `Box<number>` vs `Box<string>`).

### Milestone 47.3: Type Constraints
- **Analysis:** Implement `T extends U` logic.
- **Validation:** Ensure type arguments satisfy constraints.

## Action Items
- [x] Update `Analyzer` to handle `TypeParameter` symbols.
- [x] Implement monomorphization logic for generic functions.
- [x] Implement monomorphization logic for generic classes.
- [ ] Add integration tests for `Array<T>` and custom generic containers.
- [ ] Implement generic constraints (`T extends U`).
