# Epic 92: Advanced Classes

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement modern class features including Decorators, Private Fields, and Static Blocks.

## Milestones

### Milestone 92.1: Private State
- [ ] **Private Fields:** Support `#field` syntax.
- [ ] **Optimization: Fixed Offset Access:** Use LLVM GEP with constant offsets for private fields, bypassing dynamic property lookups.

### Milestone 92.2: Class Initialization
- [ ] **Static Blocks:** Implement `static { ... }` initialization blocks.
- [ ] **Decorators:** Implement Stage 3 Decorators (Class, Method, Field, Getter/Setter).

### Milestone 92.3: Class Expressions & Mixins
- [ ] **Mixins:** Update AST and Codegen to support `class extends expression`.

## Action Items
- [ ] **Task 92.1:** Update `AstLoader` to handle `#` private identifiers.
- [ ] **Task 92.2:** Implement private field name mangling in `Analyzer`.
- [ ] **Task 92.3:** Implement static block execution in `IRGenerator_Classes.cpp`.
- [ ] **Task 92.4:** Verify with `ts-aot examples/class_advanced_test.ts`.
