# Epic 49: Structural Types & Sugar

**Status:** Not Started
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Implement the remaining "syntactic sugar" and structural types that make TypeScript expressive.

## Milestones

### Milestone 49.1: Tuples & Enums
- **Tuples:** Implement as fixed-size arrays or anonymous structs.
- **Enums:** Implement as constant integer or string mappings.

### Milestone 49.2: Type Aliases
- **Analysis:** Resolve `type` declarations to their underlying types.

### Milestone 49.3: for..in & Function Expressions
- **for..in:** Implement object key iteration (requires runtime support for object keys).
- **Function Expressions:** Support anonymous functions assigned to variables.

## Action Items
- [ ] Implement `EnumDeclaration` in `Analyzer` and `IRGenerator`.
- [ ] Implement `TupleType` in the type system.
- [ ] Implement `TypeAliasDeclaration`.
- [ ] Implement `for..in` loop codegen.
