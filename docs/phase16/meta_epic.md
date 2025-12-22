# Phase 16: Developer Experience & Workflow

**Status:** Planned
**Goal:** Transform `ts-aot` from a "research project" into a "developer tool" and improve the AI collaboration workflow.

## Core Objectives
1.  **Unified CLI:** Users should run `ts-aot build app.ts` and get an executable. No manual `dump_ast`, no `cmake` commands visible.
2.  **Context Persistence:** Ensure that if the chat session resets, the AI can immediately resume work without re-reading the entire codebase.
3.  **Portable Builds:** The output should be a standalone folder/binary, not dependent on the user's `vcpkg` installation.

## Milestones

### Epic 85: The `ts-aot` CLI
A wrapper tool (written in Python initially, later C++ or Rust) to orchestrate the build.
- [ ] **Command: `build`:** Automates `dump_ast.js` -> `ts-aot.exe` -> `clang/cl` -> `linker`.
- [ ] **Command: `run`:** Builds and runs in one step (like `go run`).
- [ ] **Command: `init`:** Scaffolds a `tsconfig.json` and project structure.

### Epic 86: Context Persistence Protocol
A set of files in `.github/context/` that serve as the "Long Term Memory" for the AI.
- [ ] **`active_state.md`:** The current "RAM". Updated at the end of every major task. Contains: "Current Phase", "Active Epic", "Last Error", "Next Step".
- [ ] **`architecture_decisions.md`:** A log of *why* we chose Boehm GC, LLVM 18, etc.
- [ ] **`known_issues.md`:** A list of technical debt and broken features.
- [ ] **Instruction Update:** Update `.github/copilot-instructions.md` to mandate reading `active_state.md` on startup.

### Epic 87: Portable Distribution
- [ ] **Static Runtime:** Link the C++ runtime library statically.
- [ ] **Dependency Bundling:** Script to copy necessary DLLs (icu, openssl) to the output folder.
- [ ] **Installer/Zip:** A script to package the compiler for distribution.
