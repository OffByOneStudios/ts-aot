# Epic 102: Process & Globals

**Status:** In Progress 🟡 (Extended APIs 102.5-102.7, 102.10 complete)
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

---

## Extended Milestones (Remaining Process APIs)

The following milestones cover the remaining `process` module APIs. They are grouped by functionality and prioritized with commonly-used public APIs first.

### Milestone 102.5: Process Info (High Priority) ✅
Basic process information properties that are commonly used.

- [x] **Task 102.5.1:** Implement `process.pid` - Returns the process ID.
  - Runtime: `ts_process_get_pid()` returning `getpid()` / `GetCurrentProcessId()`
- [x] **Task 102.5.2:** Implement `process.ppid` - Returns the parent process ID.
  - Runtime: `ts_process_get_ppid()` returning `getppid()` / `GetCurrentProcessId()` (Windows has no direct equivalent)
- [x] **Task 102.5.3:** Implement `process.version` - Returns the Node.js version string.
  - Runtime: `ts_process_get_version()` returning a compatibility version (e.g., "v20.0.0")
- [x] **Task 102.5.4:** Implement `process.versions` - Returns an object with version strings.
  - Runtime: Return a `TsMap` with keys: `node`, `v8`, `uv`, `icu`, `ts-aot`
- [x] **Task 102.5.5:** Implement `process.argv0` - The original argv[0] value.
  - Runtime: `ts_process_get_argv0()` returning the executable name
- [x] **Task 102.5.6:** Implement `process.execPath` - Path to the Node.js executable.
  - Runtime: `ts_process_get_exec_path()` returning the current executable path
- [x] **Task 102.5.7:** Implement `process.execArgv` - Command-line options passed to Node.js.
  - Runtime: `ts_process_get_exec_argv()` returning empty array (AOT has no interpreter args)
- [x] **Task 102.5.8:** Implement `process.title` - Get/set the process title.
  - Runtime: `ts_process_get/set_title()` using `uv_get_process_title()` / `uv_set_process_title()`

### Milestone 102.6: High-Resolution Time & Resource Usage (High Priority) ✅
Performance measurement and resource monitoring APIs.

- [x] **Task 102.6.1:** Implement `process.hrtime()` - Returns high-resolution time as [seconds, nanoseconds].
  - Runtime: `ts_process_hrtime()` using `uv_hrtime()`
- [ ] **Task 102.6.2:** Implement `process.hrtime.bigint()` - Returns high-resolution time as BigInt nanoseconds.
  - Depends on BigInt support in the compiler (deferred)
- [x] **Task 102.6.3:** Implement `process.uptime()` - Returns process uptime in seconds.
  - Runtime: `ts_process_uptime()` tracking start time
- [x] **Task 102.6.4:** Implement `process.memoryUsage()` - Returns memory usage object.
  - Runtime: `ts_process_memory_usage()` with `rss`, `heapTotal`, `heapUsed`, `external`, `arrayBuffers`
- [ ] **Task 102.6.5:** Implement `process.memoryUsage.rss()` - Returns resident set size.
  - Runtime: Implemented as `ts_process_memory_usage_rss()` but needs codegen for `.rss()` call syntax
- [x] **Task 102.6.6:** Implement `process.cpuUsage()` - Returns CPU usage object.
  - Runtime: `ts_process_cpu_usage()` with `user` and `system` microseconds using `uv_getrusage()`
- [x] **Task 102.6.7:** Implement `process.resourceUsage()` - Returns resource usage object.
  - Runtime: `ts_process_resource_usage()` using `uv_getrusage()` with full rusage fields

### Milestone 102.7: Process Control (Medium Priority) ✅
Process lifecycle and signal handling.

- [x] **Task 102.7.1:** Implement `process.kill(pid, signal)` - Send a signal to a process.
  - Runtime: `ts_process_kill()` using `uv_kill()`
- [x] **Task 102.7.2:** Implement `process.abort()` - Abort the process immediately.
  - Runtime: `ts_process_abort()` calling `abort()`
- [x] **Task 102.7.3:** Implement `process.umask()` - Get/set the file mode creation mask.
  - Runtime: `ts_process_umask()` using `umask()` (Unix-only, stub returning 0 on Windows)
- [x] **Task 102.7.4:** Implement `process.emitWarning(warning)` - Emit a process warning.
  - Runtime: `ts_process_emit_warning()` printing to stderr with warning format

### Milestone 102.8: Process Events (Medium Priority) 🔴
Event-based process APIs for lifecycle hooks.

- [ ] **Task 102.8.1:** Implement `process.on('exit', callback)` - Register exit handler.
  - Runtime: Store exit callbacks and invoke on process exit
- [ ] **Task 102.8.2:** Implement `process.on('beforeExit', callback)` - Register before-exit handler.
  - Runtime: Invoke when event loop is about to exit
- [ ] **Task 102.8.3:** Implement `process.on('uncaughtException', callback)` - Handle uncaught exceptions.
  - Runtime: Hook into exception handling
- [ ] **Task 102.8.4:** Implement `process.on('warning', callback)` - Handle process warnings.
  - Runtime: Invoke when `emitWarning` is called
- [ ] **Task 102.8.5:** Implement `process.setUncaughtExceptionCaptureCallback()` and `hasUncaughtExceptionCaptureCallback()`.
  - Runtime: Global exception capture callback management

### Milestone 102.9: Event Loop Handles (Low Priority) 🔴
Event loop control for keep-alive behavior.

- [ ] **Task 102.9.1:** Implement `process.ref()` - Keep the process alive.
  - Runtime: `uv_ref()` on dummy handle
- [ ] **Task 102.9.2:** Implement `process.unref()` - Allow the process to exit.
  - Runtime: `uv_unref()` on dummy handle
- [ ] **Task 102.9.3:** Implement `process.getActiveResourcesInfo()` - Get info about active resources.
  - Runtime: Walk libuv handles and return info array

### Milestone 102.10: Configuration & Features (Low Priority) ✅
Static configuration and feature detection.

- [x] **Task 102.10.1:** Implement `process.config` - Return compile-time configuration.
  - Runtime: `ts_process_get_config()` returns a static object with build config
- [x] **Task 102.10.2:** Implement `process.features` - Return feature flags.
  - Runtime: `ts_process_get_features()` returns object with `inspector`, `debug`, `uv`, `ipv6`, `tls_*`, etc.
- [x] **Task 102.10.3:** Implement `process.release` - Return release metadata.
  - Runtime: `ts_process_get_release()` returns object with `name`, `lts`
- [ ] **Task 102.10.4:** Implement `process.allowedNodeEnvironmentFlags` - Return allowed env flags.
  - Runtime: Return a Set of allowed NODE_* environment flags (deferred - needs Set iteration)

### Milestone 102.11: Text Encoding/Decoding (Medium Priority) 🔴
Global text encoding APIs.

- [ ] **Task 102.11.1:** Implement `TextEncoder` class.
  - Runtime: `TsTextEncoder` class wrapping ICU UTF-8 encoding
  - Methods: `encode(string)` returns Uint8Array, `encodeInto(string, buffer)`
- [ ] **Task 102.11.2:** Implement `TextDecoder` class.
  - Runtime: `TsTextDecoder` class wrapping ICU decoding
  - Methods: `decode(buffer)` returns string
  - Constructor options: `label` (encoding), `fatal`, `ignoreBOM`

### Milestone 102.12: Memory Info (Low Priority) 🔴
Advanced memory information APIs.

- [ ] **Task 102.12.1:** Implement `process.constrainedMemory()` - Returns memory limit or undefined.
  - Runtime: Return cgroup memory limit on Linux, undefined elsewhere
- [ ] **Task 102.12.2:** Implement `process.availableMemory()` - Returns available memory.
  - Runtime: Return available system memory

### Milestone 102.13: Internal/Debug APIs (Very Low Priority) 🔴
These are internal Node.js APIs that are rarely used in production code. Implementation is optional.

- [ ] **Task 102.13.1:** Implement `process._getActiveHandles()` - Get active libuv handles.
- [ ] **Task 102.13.2:** Implement `process._getActiveRequests()` - Get active libuv requests.
- [ ] **Task 102.13.3:** Implement `process._tickCallback()` - Process microtask queue.
- [ ] **Task 102.13.4:** Stub remaining `process._*` internal APIs as no-ops.
  - `_debugEnd`, `_debugProcess`, `_events`, `_eventsCount`, `_exiting`,
    `_fatalException`, `_kill`, `_linkedBinding`, `_maxListeners`,
    `_preload_modules`, `_rawDebug`, `_startProfilerIdleNotifier`,
    `_stopProfilerIdleNotifier`

### Milestone 102.14: Diagnostics & Reporting (Very Low Priority) 🟡
Diagnostic reporting APIs.

- [ ] **Task 102.14.1:** Implement `process.report` object.
  - Methods: `getReport()`, `writeReport()`, `directory`, `filename`, `signal`
- [x] **Task 102.14.2:** Implement `process.debugPort` property.
  - Runtime: `ts_process_get_debug_port()` returns debug port number (default 9229)

## Priority Summary

| Priority | Milestones | Description |
|----------|------------|-------------|
| ✅ Complete | 102.1-102.7, 102.10 | Core process, I/O, timers, console, process info, hrtime, resources, config |
| 🔴 Medium | 102.8, 102.11 | Process events, TextEncoder/Decoder |
| 🔴 Low | 102.9, 102.12 | Event loop handles, memory info |
| 🟡 Very Low | 102.13, 102.14 | Internal/debug APIs, diagnostics (102.14.2 done) |
