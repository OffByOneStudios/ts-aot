# Epic 20: Structural Types

**Status:** Planned
**Parent:** [Phase 5 Meta Epic](./meta_epic.md)

## Overview
Introduce types that define structure and constants: Tuples, Enums, and Type Aliases.

## Milestones

### Milestone 20.1: Type Aliases
- **AST:** Parse `TypeAliasDeclaration`.
- **Analysis:** Register alias in Symbol Table. Resolve alias to underlying type during analysis.
- **Codegen:** No runtime impact (purely compile-time).

### Milestone 20.2: Enums
- **AST:** Parse `EnumDeclaration`.
- **Analysis:** Assign values (auto-incrementing integers or string literals).
- **Codegen:** Replace enum member access with constant values.

### Milestone 20.3: Tuples
- **AST:** Parse `TupleType`.
- **Analysis:** Validate fixed length and specific element types.
- **Codegen:** Likely implemented as a specialized struct or a fixed-size array in LLVM.

## Action Items
- [ ] Implement `TypeAliasDeclaration`.
- [ ] Implement `EnumDeclaration`.
- [ ] Implement `TupleType` support.
- [ ] Test with `tests/integration/structural_types.ts`.
