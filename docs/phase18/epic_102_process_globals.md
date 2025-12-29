# Epic 102: Process & Globals

**Status:** In Progress (Partial Implementation)
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the global objects and process-level APIs that form the environment of a Node.js application.

## Existing Implementation

The following are **already implemented** in the runtime:

| Feature | Location | Notes |
|---------|----------|-------|
| `process.argv` | `ts_get_process_argv()` in Core.cpp | Returns TsArray |
| `process.env` | `ts_get_process_env()` in Core.cpp | Returns TsMap |
| `process.exit(code)` | `ts_process_exit()` in Core.cpp | Calls exit() |
| `process.cwd()` | `ts_process_cwd()` in Core.cpp | Returns current dir |
| `setTimeout(fn, ms)` | `ts_set_timeout()` in EventLoop.cpp | Uses libuv timer |
| `setInterval(fn, ms)` | `ts_set_interval()` in EventLoop.cpp | Uses libuv timer |
| `console.log(...)` | `ts_console_log()` in Core.cpp | Variadic printing |
| `console.error(...)` | `ts_console_error()` in Core.cpp | Prints to stderr |

**Analyzer:** `src/compiler/analysis/Analyzer_StdLib.cpp` (process object)
**Codegen:** Handled in `IRGenerator_Expressions_Calls_Builtin.cpp`

## Milestones

### Milestone 102.1: Process Object
- [x] **Task 102.1.1:** Implement `process.argv`. *(Exists)*
- [x] **Task 102.1.2:** Implement `process.env` (read). *(Exists)*
- [ ] **Task 102.1.3:** Implement `process.env` write support.
- [x] **Task 102.1.4:** Implement `process.exit(code)`. *(Exists)*
- [ ] **Task 102.1.5:** Implement `process.exitCode`.
- [ ] **Task 102.1.6:** Implement `process.nextTick(callback)`.
- [x] **Task 102.1.7:** Implement `process.cwd()`. *(Exists)*
- [ ] **Task 102.1.8:** Implement `process.chdir(dir)`.
- [ ] **Task 102.1.9:** Implement `process.platform` and `process.arch`.

### Milestone 102.2: Standard I/O
- [ ] **Task 102.2.1:** Implement `process.stdout` as a `Writable` stream.
- [ ] **Task 102.2.2:** Implement `process.stderr` as a `Writable` stream.
- [ ] **Task 102.2.3:** Implement `process.stdin` as a `Readable` stream.

### Milestone 102.3: Timers
- [ ] **Task 102.3.1:** Implement `setTimeout` and `clearTimeout`.
- [ ] **Task 102.3.2:** Implement `setInterval` and `clearInterval`.
- [ ] **Task 102.3.3:** Implement `setImmediate` and `clearImmediate`.

### Milestone 102.4: Console
- [ ] **Task 102.4.1:** Implement `console.log`, `console.error`, `console.warn`, `console.info`.
- [ ] **Task 102.4.2:** Implement `console.time` and `console.timeEnd`.
- [ ] **Task 102.4.3:** Implement `console.trace`.

## Action Items
- [ ] **Planned:** Milestone 102.1 - Process Object.
