# Phase 20: Self-Hosting (Bootstrapping)

**Status:** Planned
**Goal:** The ultimate milestone: `ts-aot` should be able to compile the TypeScript compiler itself.

## Core Objectives
1.  **Compile `tsc`:** Generate a native executable of the TypeScript compiler.
2.  **In-Process Compilation:** Use the compiled `tsc` to parse code at runtime.

## Milestones

### Epic 105: Compiling `tsc`
- [ ] **Source Ingestion:** Feed the `typescript` npm package source into `ts-aot`.
- [ ] **Performance:** Measure the speed of the AOT-compiled `tsc` vs `node tsc.js`.

### Epic 106: In-Process Compilation
- [ ] **Embed `tsc`:** Link the compiled `tsc` code into the `ts-aot` compiler.
- [ ] **Remove `dump_ast.js`:** Replace the external Node.js dependency with the internal compiled parser.
