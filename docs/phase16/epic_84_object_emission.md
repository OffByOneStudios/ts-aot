# Epic 84: Object File Emission

**Status:** Completed
**Parent:** [Phase 16 Meta Epic](./meta_epic.md)

## Overview
Currently, `ts-aot` only outputs LLVM IR (text or bitcode). To become a real compiler, it must generate platform-specific object files (`.obj` on Windows, `.o` on Linux) that can be linked into an executable. This epic involves configuring the LLVM `TargetMachine` and adding an emission pass to the pipeline.

## Milestones

### Milestone 84.1: Target Initialization
Initialize the necessary LLVM targets for the host platform (x86_64).
- [x] **Initialize Targets:** Call `InitializeAllTargetInfos`, `InitializeAllTargets`, `InitializeAllTargetMCs`, `InitializeAllAsmParsers`, `InitializeAllAsmPrinters`.
- [x] **Target Lookup:** Use `TargetRegistry::lookupTarget` to find the correct target for the host triple.

### Milestone 84.2: Target Machine Configuration
Configure the `TargetMachine` to generate code for the specific CPU and OS.
- [x] **CPU & Features:** Detect host CPU (`sys::getHostCPUName`) and features.
- [x] **Options:** Set code model (Small/Large), relocation model (PIC/Static), and optimization level.
- [x] **Data Layout:** Ensure the module's data layout matches the target machine.

### Milestone 84.3: Emission Pass
Add the pass that writes the object file to disk.
- [x] **Output Stream:** Open a `raw_fd_ostream` for the output file.
- [x] **Pass Manager:** Add the `addPassesToEmitFile` pass to the legacy pass manager (LLVM 18 still uses legacy PM for codegen emission in many contexts, or check new PM status).
- [x] **CLI Flag:** Add `--emit-obj <file>` to the `ts-aot` CLI.

## Action Items
- [x] **Task 84.1:** Add `llvm::InitializeNativeTarget()` and friends to `main.cpp`.
- [x] **Task 84.2:** Implement `CodeGenerator` class to wrap `TargetMachine` creation.
- [x] **Task 84.3:** Add `--emit-obj` flag handling in `main.cpp`.
- [x] **Task 84.4:** Verify `.obj` generation by manually linking with `link.exe`.
