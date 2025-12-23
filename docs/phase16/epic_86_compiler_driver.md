# Epic 86: Compiler Driver Pipeline

**Status:** Planned
**Parent:** [Phase 16 Meta Epic](./meta_epic.md)

## Overview
This epic ties everything together. `ts-aot` will become the single entry point for the user. It will orchestrate the entire compilation pipeline: parsing TypeScript (via a Node.js subprocess), compiling IR to object code, linking the executable, and deploying the necessary runtime DLLs.

## Milestones

### Milestone 86.1: AST Parsing Subprocess
Since we don't have a native TypeScript parser yet, we must shell out to Node.js.
- [ ] **Subprocess Execution:** Use `std::system` or platform-specific APIs to run `node scripts/dump_ast.js <input> <temp_json>`.
- [ ] **Node Detection:** Ensure `node` is in the PATH or allow configuring the path.
- [ ] **Temp File Management:** Create and clean up temporary JSON files.

### Milestone 86.2: Pipeline Logic
Implement the high-level driver logic.
- [ ] **Driver Class:** Create a `Driver` class that manages the workflow.
- [ ] **Steps:**
    1.  `Driver::parse(inputTs) -> jsonPath`
    2.  `Driver::compile(jsonPath) -> objPath`
    3.  `Driver::link(objPath) -> exePath`
    4.  `Driver::deployRuntime(exePath)`

### Milestone 86.3: Runtime Deployment
The generated executable depends on `tsruntime.dll` and other DLLs (ICU, GC).
- [ ] **DLL Discovery:** Locate the required DLLs relative to the compiler executable.
- [ ] **Copy Logic:** Copy these DLLs to the output directory alongside the generated executable.

### Milestone 86.4: CLI Interface
Expose the pipeline via command-line flags.
- [ ] **Flags:**
    -   `ts-aot <file.ts>` (Default: compile to exe)
    -   `-o <output>` (Specify output filename)
    -   `-c` (Compile to object file only, no link)
    -   `--emit-ir` (Dump IR, legacy behavior)
    -   `-O<level>` (Optimization level)

## Action Items
- [ ] **Task 86.1:** Implement `Driver::runNodeParser`.
- [ ] **Task 86.2:** Implement `Driver::deployRuntime`.
- [ ] **Task 86.3:** Update `main.cpp` to use the `Driver` class instead of just `Compiler`.
- [ ] **Task 86.4:** Test the full flow: `ts-aot examples/hello.ts` -> `hello.exe` -> Run it.
