# Epic 98: Module Resolution

**Status:** Planned
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement Node.js-compatible module resolution for both CommonJS (`require`) and ESM (`import`).

## Milestones

### Milestone 98.1: CommonJS Support
- [ ] **Task 98.1.1:** Implement `require()` builtin function.
- [ ] **Task 98.1.2:** Implement `node_modules` lookup logic.
- [ ] **Task 98.1.3:** Support `package.json` "main" field.
- [ ] **Task 98.1.4:** Support `.json` file imports.

### Milestone 98.2: ESM Support
- [ ] **Task 98.2.1:** Implement `import` statement codegen.
- [ ] **Task 98.2.2:** Support `package.json` "exports" and "type": "module".
- [ ] **Task 98.2.3:** Implement top-level await in modules.

### Milestone 98.3: Global Objects
- [ ] **Task 98.3.1:** Implement `__filename` and `__dirname` for CJS.
- [ ] **Task 98.3.2:** Implement `import.meta.url` for ESM.

## Action Items
- [ ] **Current:** Milestone 98.1 - CommonJS Support.
