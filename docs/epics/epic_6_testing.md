# Epic 6: Testing & Integration

**Status:** Completed
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to ensure the correctness of the compiler and runtime through a comprehensive testing strategy. This includes unit tests for individual C++ components and integration tests that compile and execute TypeScript code, verifying the output against the expected behavior (Node.js).

## Milestones

### Milestone 6.1: Unit Testing
Expand the existing unit test suite to cover more runtime components and compiler logic.
- **Goal:** High code coverage for `TsString`, `GC`, and `Analyzer`.

### Milestone 6.2: Integration Test Runner
Create a test runner script that automates the compilation and execution pipeline.
- **Goal:** A script (e.g., Python or Node.js) that takes a `.ts` file, compiles it with `ts-aot`, links it with the runtime, runs the resulting executable, and compares stdout with `node <file.ts>`.

### Milestone 6.3: End-to-End Test Suite
Populate the `tests/integration` directory with various TypeScript scenarios.
- **Goal:** A robust suite covering arithmetic, string manipulation, function calls, recursion, and basic control flow.

## Action Items

### Task 6.1: Unit Tests
- [x] Add tests for `TsString` (concatenation, UTF-8 conversion).
- [x] Add tests for `Analyzer` (type inference logic).

### Task 6.2: Linker Driver
- [x] Create a script or tool to link the generated `.obj` file with `tsruntime.lib` and system libraries (libuv, icu, etc.) to produce a final `.exe`.
- [x] This is a prerequisite for running the compiled code.

### Task 6.3: Test Runner
- [x] Create `scripts/test_runner.py` (or .js).
- [x] Implement the pipeline: `dump_ast` -> `ts-aot` -> `link` -> `run` -> `verify`.

### Task 6.4: Test Cases
- [x] Create `tests/integration/math.ts`.
- [x] Create `tests/integration/strings.ts`.
- [x] Create `tests/integration/recursion.ts`.
