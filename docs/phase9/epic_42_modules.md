# Epic 42: Modules (ESM)

**Goal:** Support `import` and `export` for multi-file projects.

**Dependencies:**
- Phase 1-8

## Milestones

### Milestone 42.1: Export Support
- [x] **AST:** Support `export` keyword.
- [x] **Analyzer:** Track exported symbols in the `SymbolTable`.

### Milestone 42.2: Import Support
- [x] **AST:** Support `import` statement.
- [x] **Analyzer:** Resolve imported symbols from other files.
- [x] **CodeGen:** Implement cross-module linking.

## Action Items
- [x] **Task 1:** Implement `export` in Parser and Analyzer.
- [x] **Task 2:** Implement `import` in Parser and Analyzer.
- [x] **Task 3:** Update `test_runner.py` to handle multi-file compilation.
- [x] Update AST nodes and Loader for `ImportDeclaration` and `ExportDeclaration`.
- [x] Implement recursive module loading in `Analyzer`.
- [x] Implement named export/import resolution.
- [x] Refactor `Monomorphizer` to wrap top-level code in synthetic init functions.
- [x] Implement `user_main` to call module initializers in topological order.
- [x] Support `llvm::GlobalVariable` for exported constants and module-level variables.
- [x] Integration test with multi-file project.
