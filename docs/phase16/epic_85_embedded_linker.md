# Epic 85: Embedded Linker (LLD)

**Status:** Completed
**Parent:** [Phase 16 Meta Epic](./meta_epic.md)

## Overview
To remove the dependency on external build tools (like MSVC's `link.exe` or MinGW's `ld`), we will embed LLVM's linker (LLD) directly into `ts-aot`. This allows the compiler to take object files and produce a final executable in a single process.

## Milestones

### Milestone 85.1: Build System Integration
Link `ts-aot` itself against the LLD libraries.
- [x] **CMake Update:** Modify `CMakeLists.txt` to find and link `lldCOFF`, `lldCommon`, and other required LLD libraries.
- [x] **Header Fixes:** If LLD headers are missing from vcpkg include paths (common issue), forward-declare the necessary entry points or include them locally.

### Milestone 85.2: Linker Wrapper
Create a clean C++ interface to invoke the linker.
- [x] **Argument Construction:** Build a vector of strings representing the command-line arguments for the linker (e.g., `/OUT:app.exe`, `/ENTRY:mainCRTStartup`, `app.obj`).
- [x] **Invocation:** Call `lld::coff::link(args, stdout, stderr, exit_early, disable_output)`.
- [x] **Error Handling:** Capture and report linker errors to the user.
- [x] **Optimization:** Enabled `/OPT:REF` and `/OPT:ICF` for aggressive dead code elimination.

### Milestone 85.3: Library Resolution
The linker needs to know where to find system libraries and the runtime.
- [x] **Runtime Library:** Locate `tsruntime.lib` relative to the `ts-aot` executable.
- [x] **System Libraries:** Automatically add `kernel32.lib`, `user32.lib`, etc., to the link arguments.
- [x] **Lib Paths:** Use `GetModuleFileName` to find the compiler's location and derive library paths. (Implemented via `--lib-path` for now).
- [x] **Static Linking (Single Binary):**
    - [x] **VCPKG Triplet:** Switch to `x64-windows-static` to ensure all dependencies are available as static archives.
    - [x] **Library Merging:** Use `lib.exe` or `llvm-lib` to merge `gc.lib`, `uv.lib`, `icuuc.lib`, etc., into a single `tsruntime.lib`.
    - [x] **ICU Data Filtering:** Create a custom ICU data build to strip the 30MB data blob down to <1MB.

## Action Items
- [x] **Task 85.1:** Update `CMakeLists.txt` to link `lld` libraries.
- [x] **Task 85.2:** Create `src/compiler/LinkerDriver.h/cpp`.
- [x] **Task 85.3:** Implement `LinkerDriver::link(outputFile, objectFiles)`.
- [x] **Task 85.4:** Verify linking a simple "Hello World" object file into an executable.
- [x] **Task 85.5:** Configure `vcpkg` for static linking.
- [x] **Task 85.6:** Implement `TS_FAT_RUNTIME` merging logic in CMake.
- [x] **Task 85.7:** Implement ICU data filtering.
