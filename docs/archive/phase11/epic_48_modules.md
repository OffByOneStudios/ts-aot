# Epic 48: Modules (ESM)

**Status:** Completed
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Support for `import` and `export` allows the project to scale beyond single-file scripts. This requires a module resolution system and changes to how the compiler processes multiple files.

## Milestones

### Milestone 48.1: Export Support
- [x] **AST:** Parse `ExportDeclaration`.
- [x] **Analysis:** Mark symbols as exported in the module's symbol table.

### Milestone 48.2: Import Support
- [x] **AST:** Parse `ImportDeclaration`.
- [x] **Resolution:** Locate the target file based on the import path.
- [x] **Analysis:** Merge exported symbols from the target module into the current scope.

### Milestone 48.3: Multi-file Linking
- [x] **Codegen:** Generate separate LLVM modules or a single merged module.
- [x] **Linking:** Ensure symbols are correctly resolved across object files.

## Action Items
- [x] Implement a `ModuleResolver` to handle file paths.
- [x] Update `Analyzer` to process imports before analyzing the main body.
- [x] Update `IRGenerator` to handle cross-module symbol references.
- [x] Support for `import * as ns` (Namespace imports).
- [x] Support for `export default` and default imports.
- [x] Support for circular dependencies via declaration hoisting.
