# Epic 92: Advanced Classes

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement modern class features including Decorators, Private Fields, and Static Blocks.

## Milestones

### Milestone 92.1: Private State
- [x] **Private Fields:** Support `#field` syntax.
- [x] **Optimization: Fixed Offset Access:** Use LLVM GEP with constant offsets for private fields, bypassing dynamic property lookups.

### Milestone 92.2: Class Initialization
- [x] **Static Blocks:** Implement `static { ... }` initialization blocks.
- [ ] **Decorators:** Implement Stage 3 Decorators (Class, Method, Field, Getter/Setter).

### Milestone 92.3: Class Expressions & Mixins
- [ ] **Mixins:** Update AST and Codegen to support `class extends expression`.

### Milestone 92.4: Meta-Programming (Comptime)
- [x] **Intrinsic Namespace:** Reserve `ts_aot` namespace for compiler hints.
- [/] **@ts_aot.comptime:** Implement function execution during compilation.
    - *Collision Avoidance:* Use the `ts_aot` prefix to ensure no overlap with other macro systems (like `ts-macros` or `bun` macros).
- [x] **Constant Folding:** Inline results of comptime functions into the IR (Literals).
- [x] **Comptime Blocks:** Support `ts_aot.comptime(() => { ... })` for top-level or local code generation (Literals).

## Action Items
- [x] **Task 92.1:** Update `AstLoader` to handle `#` private identifiers.
- [x] **Task 92.2:** Implement private field name mangling in `Analyzer`.
- [x] **Task 92.3:** Implement static block execution in `IRGenerator_Classes.cpp`.
- [x] **Task 92.4:** Verify with `ts-aot examples/class_advanced_test.ts`.
- [ ] **Task 92.5:** Update `AstNodes.h` and `AstLoader` to support decorators on functions.
- [x] **Task 92.6:** Implement `ts_aot` intrinsic recognition in `Analyzer`.
- [ ] **Task 92.7:** Integrate LLVM OrcJIT for `comptime` execution.
