# Epic 102: Process & Globals

**Status:** Complete ✅
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
| `process.chdir(dir)` | `ts_process_chdir()` in Core.cpp | Changes current dir |
| `process.exitCode` | `ts_process_get/set_exit_code()` | Get/set exit code |
| `process.platform` | `ts_process_get_platform()` | Returns "win32", "linux", etc. |
| `process.arch` | `ts_process_get_arch()` | Returns "x64", "arm64", etc. |
| `process.stdout` | `ts_process_get_stdout()` | Writable stream (fd 1) |
| `process.stderr` | `ts_process_get_stderr()` | Writable stream (fd 2) |
| `process.stdin` | `ts_process_get_stdin()` | Readable stream (fd 0) |
| `process.nextTick(fn)` | `ts_process_next_tick()` | Schedules microtask |
| `setTimeout(fn, ms)` | `ts_set_timeout()` in EventLoop.cpp | Uses libuv timer |
| `setInterval(fn, ms)` | `ts_set_interval()` in EventLoop.cpp | Uses libuv timer |
| `setImmediate(fn)` | `ts_set_immediate()` in EventLoop.cpp | Uses zero-timeout timer |
| `console.log(...)` | `ts_console_log()` in Primitives.cpp | Variadic printing |
| `console.error(...)` | `ts_console_error()` in Primitives.cpp | Prints to stderr |
| `console.time(label)` | `ts_console_time()` | Starts timer |
| `console.timeEnd(label)` | `ts_console_time_end()` | Ends timer and prints diff |

**Analyzer:** `src/compiler/analysis/Analyzer_StdLib_Process.cpp`
**Codegen:** `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Process.cpp`

## Milestones

### Milestone 102.1: Process Object ✅
- [x] **Task 102.1.1:** Implement `process.argv`.
- [x] **Task 102.1.2:** Implement `process.env` (read).
- [x] **Task 102.1.3:** Implement `process.env` write support.
- [x] **Task 102.1.4:** Implement `process.exit(code)`.
- [x] **Task 102.1.5:** Implement `process.exitCode`.
- [x] **Task 102.1.6:** Implement `process.nextTick(callback)`.
- [x] **Task 102.1.7:** Implement `process.cwd()`.
- [x] **Task 102.1.8:** Implement `process.chdir(dir)`.
- [x] **Task 102.1.9:** Implement `process.platform` and `process.arch`.

### Milestone 102.2: Standard I/O ✅
- [x] **Task 102.2.1:** Implement `process.stdout` as a `Writable` stream.
- [x] **Task 102.2.2:** Implement `process.stderr` as a `Writable` stream.
- [x] **Task 102.2.3:** Implement `process.stdin` as a `Readable` stream.

### Milestone 102.3: Timers & Globals ✅
- [x] **Task 102.3.1:** Implement `setTimeout` and `clearTimeout`.
- [x] **Task 102.3.2:** Implement `setInterval` and `clearInterval`.
- [x] **Task 102.3.3:** Implement `setImmediate` and `clearImmediate`.
  - **Note:** Fixed `setImmediate` to use zero-timeout timer instead of `uv_check_t` to prevent event loop blocking.

### Milestone 102.4: Console ✅
- [x] **Task 102.4.1:** Implement `console.log`, `console.error`, `console.warn`, `console.info`.
- [x] **Task 102.4.2:** Implement `console.time` and `console.timeEnd`.
- [x] **Task 102.4.3:** Implement `console.trace`.

## Verification

**Test file:** `examples/process_test.ts`

```
Testing process and console...
Platform: win32
Arch: x64
CWD: E:\src\github.com\cgrinker\ts-aoc
New CWD: E:\src\github.com\cgrinker
Env PATH exists: true
timer: 0ms
This is an error message
Done!
nextTick callback
setImmediate callback
```

All callbacks fire correctly and the process exits cleanly.

## Notes

- **setImmediate fix:** Changed from `uv_check_t` to zero-timeout `uv_timer_t` because check callbacks only fire after I/O poll, which blocks indefinitely when there's no I/O work.
- **Callback boxing:** Arrow function callbacks must be boxed with `ts_value_make_function()` before passing to runtime timer functions.
