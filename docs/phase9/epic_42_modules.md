# Epic 42: Modules (ESM)

**Goal:** Support `import` and `export` for multi-file projects.

**Dependencies:**
- Phase 1-8

## Milestones

### Milestone 42.1: Export Support
- [ ] **AST:** Support `export` keyword.
- [ ] **Analyzer:** Track exported symbols in the `SymbolTable`.

### Milestone 42.2: Import Support
- [ ] **AST:** Support `import` statement.
- [ ] **Analyzer:** Resolve imported symbols from other files.
- [ ] **CodeGen:** Implement cross-module linking.

## Action Items
- [ ] **Task 1:** Implement `export` in Parser and Analyzer.
- [ ] **Task 2:** Implement `import` in Parser and Analyzer.
- [ ] **Task 3:** Update `test_runner.py` to handle multi-file compilation.
