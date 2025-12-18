# Epic 49: Structural Types & Sugar

**Status:** In Progress
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Implement the remaining "syntactic sugar" and structural types that make TypeScript expressive.

## Milestones

### Milestone 49.1: Tuples & Enums
- [x] **Tuples:** Implement as fixed-size arrays or anonymous structs.
- [x] **Enums:** Implement as constant integer or string mappings.

### Milestone 49.2: Type Aliases
- [x] **Analysis:** Resolve `type` declarations to their underlying types.

### Milestone 49.3: for..in & Function Expressions
- [x] **for..in:** Implement object key iteration (requires runtime support for object keys).
- [x] **Function Expressions:** Support anonymous functions assigned to variables.

## Action Items
- [x] Implement `EnumDeclaration` in `Analyzer` and `IRGenerator`.
- [x] Implement `TupleType` in the type system.
- [x] Implement `TypeAliasDeclaration`.
- [x] Implement `for..in` loop codegen.
- [x] Implement Function Expressions and Arrow Functions.
