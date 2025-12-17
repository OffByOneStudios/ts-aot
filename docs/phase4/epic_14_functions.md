# Epic 14: Functions & Scopes

**Status:** Planned
**Parent:** [Phase 4 Meta Epic](./meta_epic.md)

## Overview
Enable users to define their own functions, allowing for recursion and better code organization.

## Milestones

### Milestone 14.1: Function Declaration
- **AST:** Parse `FunctionDeclaration`.
- **Analysis:** Track function signatures in the symbol table.
- **Codegen:** Generate LLVM functions for user code.

### Milestone 14.2: Function Calls & Returns
- **Codegen:** Update `CallExpression` to handle user functions (currently only handles built-ins).
- **Codegen:** Implement `ReturnStatement`.

## Action Items
- [ ] Implement `FunctionDeclaration` in Analyzer.
- [ ] Implement `FunctionDeclaration` in IRGenerator.
- [ ] Implement `ReturnStatement` in IRGenerator.
- [ ] Test with `tests/integration/functions.ts` (simple call).
- [ ] Test with `tests/integration/recursion.ts` (factorial/fibonacci).
