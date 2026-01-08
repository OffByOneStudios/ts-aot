# Epic 18: Advanced Control Flow

**Status:** In Progress
**Parent:** [Phase 5 Meta Epic](./meta_epic.md)

## Overview
Implement `switch` statements for cleaner multi-branch logic and `for..of` loops for easier iteration over arrays and strings.

## Milestones

### Milestone 18.1: Switch Statement
- **AST:** Parse `SwitchStatement`, `CaseClause`, `DefaultClause`.
- **Codegen:** Generate LLVM `switch` instruction or a series of conditional branches.
- **Support:** Handle `break` within switch cases.

### Milestone 18.2: For..Of Loop
- **AST:** Parse `ForOfStatement`.
- **Analysis:** Infer type of the iteration variable based on the iterable (Array or String).
- **Codegen:** Lower `for (const x of arr)` to a standard index-based loop or iterator pattern.

## Action Items
- [x] Implement `SwitchStatement` in Analyzer & IRGenerator.
- [x] Implement `ForOfStatement` in Analyzer & IRGenerator.
- [x] Test with `tests/integration/control_flow_advanced.ts`.
