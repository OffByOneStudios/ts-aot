# Epic 1: Project Infrastructure & Build System

**Status:** Planned
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to establish the foundational repository structure, dependency management, and build configuration for the `ts-aot` project. This ensures that we can compile C++ code, link against third-party libraries via `vcpkg`, and have a clean separation between the compiler and the runtime.

## Milestones

### Milestone 1.1: Repository Skeleton
Create the physical directory structure and initial configuration files.
- **Goal:** A developer can clone the repo and see the intended structure.

### Milestone 1.2: Dependency Management (vcpkg)
Configure `vcpkg` to download and install all required C++ libraries.
- **Goal:** `vcpkg install` runs successfully and provides LLVM, fmt, etc.

### Milestone 1.3: CMake Build System
Set up the CMake build targets for the compiler executable and the runtime static library.
- **Goal:** `cmake .. && cmake --build .` compiles a "Hello World" executable and a dummy library.

## Action Items

### Task 1.1: Create Directory Structure
- [x] Create root folders: `src/compiler`, `src/runtime`, `scripts`, `tests`.
- [x] Create subfolders for compiler: `ast`, `analysis`, `codegen`.
- [x] Create subfolders for runtime: `include`, `src`.

### Task 1.2: Configure vcpkg
- [x] Create `vcpkg.json` in the root.
- [x] Add dependencies: `llvm`, `nlohmann-json`, `fmt`, `cxxopts`, `libuv`, `bdwgc`, `icu`, `catch2`.
- [x] Verify version constraints (if any).

### Task 1.3: Root CMake Configuration
- [x] Create root `CMakeLists.txt`.
- [x] Set C++ Standard to 20.
- [x] Configure `CMAKE_MODULE_PATH` for vcpkg.
- [x] Define subdirectories (`src/compiler`, `src/runtime`).

### Task 1.4: Runtime CMake Configuration
- [x] Create `src/runtime/CMakeLists.txt`.
- [x] Define `tsruntime` static library target.
- [x] Link dependencies (`libuv`, `bdwgc`, `icu`).
- [x] Set up include directories.

### Task 1.5: Compiler CMake Configuration
- [x] Create `src/compiler/CMakeLists.txt`.
- [x] Define `ts-aot` executable target.
- [x] Link dependencies (`llvm`, `nlohmann-json`, `fmt`).
- [x] Link against `tsruntime` (if needed for shared headers, though usually separate).

### Task 1.6: Verification
- [x] Create a dummy `src/compiler/main.cpp` that prints "Hello ts-aot" using `fmt`.
- [x] Create a dummy `src/runtime/src/Core.cpp` that exports a function.
- [x] Verify the build succeeds.
