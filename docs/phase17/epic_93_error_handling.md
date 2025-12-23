# Epic 93: Error Handling & Stack Traces

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement robust `try/catch/finally` support and runtime stack trace generation.

## Milestones

### Milestone 93.1: Exception Flow
- [ ] **Try/Catch:** Implement LLVM `invoke` and `landingpad` for exception handling.
- [ ] **Finally Blocks:** Ensure `finally` executes on both normal and exceptional exits.

### Milestone 93.2: Stack Traces
- [ ] **Runtime Support:** Integrate `libbacktrace` or similar to capture native stack frames.
- [ ] **Source Mapping:** Map native addresses back to TypeScript source lines in `Error.stack`.

## Action Items
- [ ] **Task 93.1:** Update `IRGenerator` to support `TryStatement`.
- [ ] **Task 93.2:** Implement `ts_throw` and `ts_catch` in `src/runtime/Memory.cpp`.
- [ ] **Task 93.3:** Verify with `ts-aot examples/error_test.ts`.
