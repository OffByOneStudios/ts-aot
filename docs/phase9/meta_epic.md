# Phase 9: Generics & Modules

**Status:** Not Started
**Goal:** Implement generics and module support to enable more complex and modular codebases.

## Epics

### [Epic 41: Generics](./epic_41_generics.md)
Support parameterized types for classes and functions.
- **Key Features:**
    - Generic classes (`class Box<T>`).
    - Generic functions (`function identity<T>(arg: T): T`).
    - Type constraints (`T extends Base`).

### [Epic 42: Modules (ESM)](./epic_42_modules.md)
Support multi-file compilation and code organization.
- **Key Features:**
    - `export` statement.
    - `import` statement.
    - Module resolution and linking.

## Success Criteria
- Can define and use generic classes and functions.
- Can split code across multiple files and use `import`/`export`.
- Compiler can resolve and link multiple modules into a single executable.
