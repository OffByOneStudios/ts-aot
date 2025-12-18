# Epic 48: Modules (ESM)

**Status:** Not Started
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Support for `import` and `export` allows the project to scale beyond single-file scripts. This requires a module resolution system and changes to how the compiler processes multiple files.

## Milestones

### Milestone 48.1: Export Support
- **AST:** Parse `ExportDeclaration`.
- **Analysis:** Mark symbols as exported in the module's symbol table.

### Milestone 48.2: Import Support
- **AST:** Parse `ImportDeclaration`.
- **Resolution:** Locate the target file based on the import path.
- **Analysis:** Merge exported symbols from the target module into the current scope.

### Milestone 48.3: Multi-file Linking
- **Codegen:** Generate separate LLVM modules or a single merged module.
- **Linking:** Ensure symbols are correctly resolved across object files.

## Action Items
- [ ] Implement a `ModuleResolver` to handle file paths.
- [ ] Update `Analyzer` to process imports before analyzing the main body.
- [ ] Update `IRGenerator` to handle cross-module symbol references.
