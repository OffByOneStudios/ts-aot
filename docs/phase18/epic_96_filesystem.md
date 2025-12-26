# Epic 96: The File System (fs)

**Status:** In Progress
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the Node.js `fs` module, providing both synchronous and asynchronous file system operations.

## Milestones

### Milestone 96.1: Synchronous IO
- [x] **Task 96.1.1:** Define `fs` module in `Analyzer_StdLib.cpp`.
- [x] **Task 96.1.2:** Implement `readFileSync` (UTF-8 and Buffer).
- [x] **Task 96.1.3:** Implement `writeFileSync`.
- [x] **Task 96.1.4:** Implement `existsSync`, `unlinkSync`, `mkdirSync`, `rmdirSync`.
- [x] **Task 96.1.5:** Implement `statSync` and `readdirSync`.

### Milestone 96.2: Asynchronous IO (libuv)
- [x] **Task 96.2.1:** Implement `readFile` using `uv_fs_read` (via `uv_queue_work`).
- [x] **Task 96.2.2:** Implement `writeFile` using `uv_fs_write` (via `uv_queue_work`).
- [x] **Task 96.2.3:** Implement Promises API (`fs.promises`) for core methods.
- [x] **Task 96.2.4:** Implement `fs.promises.open`, `fs.promises.close`, `fs.promises.read`, `fs.promises.write`.

### Milestone 96.3: File Handles & Streams
- [x] **Task 96.3.1:** Implement `fs.openSync`, `fs.closeSync`, `fs.readSync`, `fs.writeSync`.
- [x] **Task 96.3.2:** Implement `fs.createReadStream`.
- [x] **Task 96.3.3:** Implement `fs.createWriteStream`.

### Milestone 96.4: Extended Metadata & Permissions
- [x] **Task 96.4.1:** Implement `fs.access` and `fs.accessSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp` (signatures), `IRGenerator_Expressions_Calls_Builtin_FS.cpp` (codegen), `src/runtime/src/node/fs.cpp` (impl using `uv_fs_access`).
- [x] **Task 96.4.2:** Implement `fs.chmod`, `fs.chown`, `fs.utimes` (and Sync variants).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_chmod`, `uv_fs_chown`, `uv_fs_utime`).
- [x] **Task 96.4.3:** Implement `fs.statfs` and `fs.statfsSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_statfs`).
- [x] **Task 96.4.4:** Implement full `fs.Stats` object fields.
    - **Plan:** Update `add_stats_methods` in `src/runtime/src/node/fs.cpp` to include `uid`, `gid`, `rdev`, `blksize`, `ino`, `size`, `blocks`, `atime`, `mtime`, `ctime`, `birthtime`.
- [x] **Task 96.4.5:** Implement `fs.constants`.
    - **Plan:** Define constants (F_OK, R_OK, etc.) in `Analyzer_StdLib_FS.cpp` as read-only properties of `fs`.

### Milestone 96.5: Links & Symlinks
- [x] **Task 96.5.1:** Implement `fs.link` and `fs.linkSync` (hard links).
- [x] **Task 96.5.2:** Implement `fs.symlink` and `fs.symlinkSync`.
- [x] **Task 96.5.3:** Implement `fs.readlink` and `fs.readlinkSync`.
- [x] **Task 96.5.4:** Implement `fs.lstat` and `fs.lstatSync`.
- [x] **Task 96.5.5:** Implement `fs.realpath` and `fs.realpathSync`.

### Milestone 96.6: File Manipulation & Copying
- [x] **Task 96.6.1:** Implement `fs.copyFile` and `fs.copyFileSync`.
- [x] **Task 96.6.2:** Implement `fs.cp` and `fs.cpSync` (recursive copy).
- [x] **Task 96.6.3:** Implement `fs.rename` and `fs.renameSync`.
- [x] **Task 96.6.4:** Implement `fs.truncate` and `fs.truncateSync`.
- [x] **Task 96.6.5:** Implement `fs.appendFile` and `fs.appendFileSync`.
- [x] **Task 96.6.6:** Implement `fs.rm` and `fs.rmSync` (recursive delete).
- [x] **Task 96.6.7:** Implement `fs.mkdtemp` and `fs.mkdtempSync`.

### Milestone 96.7: Directory Operations & Watching
- [ ] **Task 96.7.1:** Implement `fs.opendir` and `fs.opendirSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_opendir`).
- [ ] **Task 96.7.2:** Implement `fs.Dir` and `fs.Dirent` classes.
    - **Plan:** Define classes in `Analyzer_StdLib_FS.cpp`. Implement methods in `src/runtime/src/node/fs.cpp` (wrapping `uv_dir_t`).
- [ ] **Task 96.7.3:** Implement `fs.watch` and `fs.watchFile`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_event_t` and `uv_fs_poll_t`).
- [ ] **Task 96.7.4:** Implement `fs.unwatchFile`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (stop poll handle).

### Milestone 96.8: Advanced File Descriptors
- [ ] **Task 96.8.1:** Implement `fs.fchmod`, `fs.fchown`, `fs.futimes` (and Sync variants).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_fchmod`, `uv_fs_fchown`, `uv_fs_futime`).
- [ ] **Task 96.8.2:** Implement `fs.fstat` and `fs.fstatSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_fstat`).
- [ ] **Task 96.8.3:** Implement `fs.fsync` and `fs.fdatasync` (and Sync variants).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_fsync`, `uv_fs_fdatasync`).
- [ ] **Task 96.8.4:** Implement `fs.ftruncate` and `fs.ftruncateSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_ftruncate`).
- [ ] **Task 96.8.5:** Implement `fs.readv` and `fs.writev` (and Sync variants).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_read`/`write` with iov).

## Action Items
- [ ] **Current:** Implement `fs.opendir` and `fs.Dir` / `fs.Dirent` classes (Milestone 96.7).

## Reflections & Improvements

### Lessons Learned
1.  **LLVM Dominance in Async SM**: We encountered "Instruction does not dominate all uses" errors because instructions were being generated in the entry block of the State Machine function but used within state blocks. Since state blocks are reached via a `switch` (non-linear control flow), the entry block is the only safe place for constants, but any logic that depends on the current state must be carefully isolated.
2.  **Builtin Dispatch Type Safety**: The optimized FS dispatch in the compiler expects raw `TsString*` for string-returning methods (like `readlinkSync`). Returning a boxed `TsValue*` from the runtime causes a type mismatch that leads to empty results or crashes. We must ensure runtime signatures match compiler expectations exactly.
3.  **Promise Mapping Synchronization**: It is easy to forget to add new FS methods to the `ts_fs_get_promises` map. This leads to "undefined" errors at runtime even if the underlying async implementation is correct.
4.  **Async Exception Handling**: `setjmp`/`longjmp` is fundamentally incompatible with the stack-less async state machine because the jump target (the `jmp_buf` on the stack) becomes invalid once the function yields and its stack frame is popped. We must use explicit branching and store exception state in the heap-allocated `Frame`.
5.  **Exception State Management**: In async functions, the `pendingExc` state must be explicitly cleared when entering a `catch` block. Failure to do so causes the `finally` block (or the function's exit path) to rethrow the exception, even if it was successfully handled, leading to "Uncaught exception: undefined" errors.

### Future Improvements
1.  **Declarative Builtin Definitions**: Move towards a more declarative way of defining builtins that automatically generates both the compiler dispatch and the runtime registration, reducing the chance of signature mismatches.
2.  **Enhanced Async Debugging**: Add more granular logging to the `ts_async_resume` and `ts_async_await` runtime functions to help track down "Uncaught exception" crashes in complex async flows.
3.  **Automated FS Tests**: Create a standardized test suite for all FS methods that covers both Sync and Promise variants to catch missing mappings early.
4.  **Robust Recursive Operations**: The current implementations of `fs.rm` and `fs.cp` are basic. Future work should include a more robust recursive directory walker to handle deep trees and edge cases (like symlink loops) more gracefully.
