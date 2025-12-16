# Epic 2: Runtime Library (libtsruntime)

**Status:** Planned
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to implement the "Thin Runtime" required to support compiled TypeScript code. This runtime provides the necessary infrastructure for memory management (Garbage Collection), asynchronous I/O (Event Loop), and core TypeScript primitives (Strings, Objects). The compiler will generate code that links against this library.

## Milestones

### Milestone 2.1: Memory Management
Integrate the Boehm Garbage Collector (`bdwgc`) to manage memory allocation.
- **Goal:** We can allocate memory using `ts_alloc` and have it automatically reclaimed when unreachable.

### Milestone 2.2: Event Loop
Integrate `libuv` to handle the main event loop.
- **Goal:** The runtime can start an event loop and keep the process alive while there are active handles.

### Milestone 2.3: Primitives & Console
Implement the core data structures for TypeScript values and basic I/O.
- **Goal:** We can create `TsString` (UTF-16), use `TaggedValue` for dynamic types, and print to the console.

### Milestone 2.4: Runtime Entry Point
Define the main entry point that initializes the runtime environment.
- **Goal:** A `ts_main` function exists that sets up GC, starts the loop, and calls the user's code.

## Action Items

### Task 2.1: Memory Management
- [x] Create `src/runtime/include/GC.h` to wrap `gc.h`.
- [x] Create `src/runtime/src/Memory.cpp`.
- [x] Implement `void* ts_alloc(size_t size)` that calls `GC_MALLOC`.
- [x] Verify GC linking in `CMakeLists.txt`.

### Task 2.2: Event Loop Integration
- [x] Create `src/runtime/src/EventLoop.cpp`.
- [x] Implement `void ts_loop_init()` to initialize the default libuv loop.
- [x] Implement `void ts_loop_run()` to run the loop.

### Task 2.3: Type System & Primitives
- [x] Create `src/runtime/include/TsObject.h`.
- [x] Define `TaggedValue` struct and `ValueType` enum (as per scaffold).
- [x] Create `src/runtime/include/TsString.h` and `src/runtime/src/TsString.cpp`.
- [x] Implement `TsString` using ICU (or simple wrapper initially) for UTF-16 support.

### Task 2.4: Console & IO
- [ ] Implement `void ts_console_log(TsString* str)` in `src/runtime/src/Primitives.cpp`.
- [ ] Ensure it prints correctly to stdout.

### Task 2.5: Entry Point
- [ ] Update `src/runtime/include/TsRuntime.h` (create if missing) to expose public API.
- [ ] Implement `int ts_main(int argc, char** argv, void (*user_main)())` in `src/runtime/src/Core.cpp`.
- [ ] Ensure `ts_main` initializes GC, runs user code, then runs the event loop.

### Task 2.6: Unit Tests
- [x] Create `tests/unit/RuntimeTests.cpp`.
- [x] Add Catch2 tests for `ts_alloc` (basic check), `TsString`, and `TaggedValue` size/layout.
