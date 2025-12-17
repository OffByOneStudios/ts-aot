# Epic 5: Code Generation (LLVM)

**Status:** Planned
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to translate the analyzed and monomorphized AST into LLVM IR and subsequently generate native object files. This is the final stage of the compilation pipeline, where we bridge the gap between our internal representation and machine code.

## Milestones

### Milestone 5.1: LLVM Infrastructure
Set up the LLVM code generation environment, including the Context, Module, and IRBuilder.
- **Goal:** We can initialize an LLVM module and dump empty IR.

### Milestone 5.2: Function Generation
Iterate over the specialized functions identified by the Monomorphizer and generate LLVM Function definitions.
- **Goal:** The generated IR contains function declarations (e.g., `define void @add_int_int(i64 %0, i64 %1)`).

### Milestone 5.3: Instruction Generation
Implement the logic to visit AST nodes within function bodies and emit corresponding LLVM instructions.
- **Goal:** Function bodies contain actual logic (arithmetic, calls, returns).

### Milestone 5.4: Object File Emission
Configure the LLVM TargetMachine and emit the generated module as a native object file (`.obj` or `.o`).
- **Goal:** The compiler produces a file that can be linked with the runtime.

## Action Items

### Task 5.1: Infrastructure Setup
- [x] Create `src/compiler/codegen/IRGenerator.h` and `src/compiler/codegen/IRGenerator.cpp`.
- [x] Initialize `llvm::LLVMContext`, `llvm::Module`, `llvm::IRBuilder`.
- [x] Add a method to dump the module IR to stdout.

### Task 5.2: Function Prototypes
- [x] Implement `generatePrototypes` to create LLVM Functions for each `Specialization`.
- [x] Map internal types (`TypeKind::Int`) to LLVM types (`llvm::Type::getInt64Ty`).

### Task 5.3: Body Generation
- [x] Implement AST visitor for code generation (`visitBinaryExpression`, `visitCallExpression`, etc.).
- [x] Generate basic arithmetic instructions (`add`, `sub`, `mul`).
- [x] Generate function calls.

### Task 5.4: Runtime Integration
- [x] Declare external runtime functions (`ts_console_log`, `ts_alloc`) in the LLVM module.
- [x] Generate calls to these runtime functions for specific AST nodes (e.g., `console.log`).

### Task 5.5: Object Emission
- [x] Initialize LLVM targets (`InitializeAllTargetInfos`, etc.).
- [x] Configure `TargetMachine`.
- [x] Write the module to an object file.
