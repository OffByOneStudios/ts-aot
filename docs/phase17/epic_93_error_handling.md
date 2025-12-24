# Epic 93: Error Handling & Stack Traces

**Status:** In Progress
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement robust `try/catch/finally` support and runtime stack trace generation.

## Milestones

### Milestone 93.1: Exception Flow
- [x] **Try/Catch:** Implement `setjmp`/`longjmp` based exception handling.
- [x] **Finally Blocks:** Ensure `finally` executes on normal exit, exceptions, `return`, `break`, and `continue`.
- [x] **Stack Integrity:** Ensure exception handlers are popped correctly during control flow transitions.

### Milestone 93.2: Stack Traces
- [x] **Runtime Support:** Integrate `DbgHelp` (Windows) to capture native stack frames.
- [x] **Source Mapping:** Map native addresses back to TypeScript source lines in `Error.stack` using LLVM `DIBuilder`.

## Action Items
- [x] **Task 93.1:** Update `IRGenerator` to support `TryStatement`.
- [x] **Task 93.2:** Implement `ts_throw` and `ts_catch` in `src/runtime/Memory.cpp`.
- [x] **Task 93.3:** Verify with `ts-aot examples/error_test.ts`.
- [x] **Task 93.4:** Implement `Error` class and `stack` property in `src/runtime/src/TsError.cpp`.
- [x] **Task 93.5:** Integrate `llvm::DIBuilder` into `IRGenerator` to emit debug metadata.
- [x] **Task 93.6:** Implement Windows symbolication in `TsError.cpp`.
