# Phase 16: Compiler Driver & Self-Hosting Prep

**Status:** Planned
**Goal:** Transform `ts-aot` from a simple transpiler into a self-contained compiler driver that can produce executables directly. This removes the dependency on CMake for building user applications and prepares the ground for self-hosting.

## Core Objectives
1.  **Native Object Emission:** Generate platform-specific object files (`.obj`/`.o`) directly from LLVM IR.
2.  **Embedded Linking:** Integrate `LLD` (LLVM Linker) to link object files into final executables without external tools.
3.  **Driver Orchestration:** Manage the full pipeline: Parse (via Node.js) -> Compile -> Link -> Deploy Runtime.
4.  **Dependency Management:** Automatically locate and bundle necessary DLLs (`tsruntime.dll`, `icu`, `gc`) with the output.

## Milestones

### Epic 84: Object File Emission
Stop dumping text IR and start generating machine code.
- [x] **Target Initialization:** Initialize LLVM X86 targets and MC layers.
- [x] **Target Machine:** Configure `TargetMachine` with correct triple, CPU, and features.
- [x] **Emission Pass:** Create a pass to emit `.obj` files to disk.

### Epic 85: Embedded Linker (LLD)
Link the generated object files into a Windows executable.
- [x] **Build Integration:** Link `ts-aot` against `lldCOFF` and `lldCommon`.
- [x] **Linker Wrapper:** Create a C++ interface to invoke `lld::coff::link`.
- [x] **Library Resolution:** Automatically find `tsruntime.lib` and system libraries (`kernel32`, `user32`, etc.).
- [ ] **Single Binary (Static Linking):** Support producing executables with zero external DLL dependencies.
    - [ ] **Static Triplet:** Transition to `x64-windows-static` for all dependencies.
    - [ ] **Fat Runtime:** Implement the library merging logic to bundle `gc`, `uv`, `icu` into `tsruntime.lib`.
    - [ ] **ICU Filtering:** Implement a minimal ICU data build to reduce binary size.

### Epic 86: Compiler Driver Pipeline
Orchestrate the end-to-end build process.
- [ ] **AST Parsing:** Spawn `node scripts/dump_ast.js` as a subprocess.
- [/] **Pipeline Logic:** Connect Parse -> Compile -> Link. (Compile -> Link is done).
- [ ] **Runtime Deployment:** Copy required DLLs to the output directory.
- [x] **CLI Interface:** Implement standard flags (`-o`, `-c`, `-O3`, `--small-icu`).

## Architecture
The new `ts-aot` will act as a driver:
1.  **Input:** `app.ts`
2.  **Subprocess:** `node dump_ast.js app.ts temp.json`
3.  **Compile:** `ts-aot` reads `temp.json` -> LLVM IR -> `app.obj`
4.  **Link:** `lld::coff::link(app.obj, tsruntime.lib, ...) -> app.exe`
    *   Supports **Fat Linking**: Merging all dependencies into `tsruntime.lib` for a single binary.
    *   Supports **Small ICU**: Stripping locale data to reduce binary size.
5.  **Deploy:** Copy `tsruntime.dll` -> `dirname(app.exe)` (if not using fat linking).
