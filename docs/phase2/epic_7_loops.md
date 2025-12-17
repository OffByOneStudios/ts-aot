# Epic 7: Control Flow (Loops)

**Status:** Planned
**Parent:** [Phase 2 Meta Epic](./meta_epic.md)

## Overview
Implement standard iteration constructs to allow algorithms that require looping.

## Milestones

### Milestone 7.1: While Loops
- **AST:** Add `WhileStatement` node.
- **Analyzer:** Type check condition (must be boolean-ish).
- **Codegen:** Generate LLVM IR for loop structure (header, body, exit blocks).

### Milestone 7.2: For Loops
- **AST:** Add `ForStatement` node (initializer, condition, incrementor).
- **Analyzer:** Handle scope for initializer variables (block scope).
- **Codegen:** Generate LLVM IR.

### Milestone 7.3: Loop Control
- **AST:** Add `BreakStatement` and `ContinueStatement`.
- **Codegen:** Implement branching to exit/header blocks.

## Action Items
- [x] Implement `WhileStatement` in AST and Parser.
- [x] Implement `WhileStatement` in Analyzer and IRGenerator.
- [x] Implement `ForStatement` in AST and Parser.
- [x] Implement `ForStatement` in Analyzer and IRGenerator.
- [x] Add integration tests for loops (e.g., calculating sum of 1..N).
