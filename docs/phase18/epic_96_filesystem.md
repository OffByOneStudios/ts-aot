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
- [ ] **Task 96.5.1:** Implement `fs.link` and `fs.linkSync` (hard links).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_link`).
- [ ] **Task 96.5.2:** Implement `fs.symlink` and `fs.symlinkSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_symlink`).
- [ ] **Task 96.5.3:** Implement `fs.readlink` and `fs.readlinkSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_readlink`).
- [ ] **Task 96.5.4:** Implement `fs.lstat` and `fs.lstatSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_lstat`).
- [ ] **Task 96.5.5:** Implement `fs.realpath` and `fs.realpathSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_realpath`).

### Milestone 96.6: File Manipulation & Copying
- [ ] **Task 96.6.1:** Implement `fs.copyFile` and `fs.copyFileSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_copyfile`).
- [ ] **Task 96.6.2:** Implement `fs.cp` and `fs.cpSync` (recursive copy).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using recursive logic).
- [ ] **Task 96.6.3:** Implement `fs.rename` and `fs.renameSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_rename`).
- [ ] **Task 96.6.4:** Implement `fs.truncate` and `fs.truncateSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_truncate`).
- [ ] **Task 96.6.5:** Implement `fs.appendFile` and `fs.appendFileSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `open` with append flag + `write`).
- [ ] **Task 96.6.6:** Implement `fs.rm` and `fs.rmSync` (recursive delete).
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using recursive logic).
- [ ] **Task 96.6.7:** Implement `fs.mkdtemp` and `fs.mkdtempSync`.
    - **Plan:** `Analyzer_StdLib_FS.cpp`, `IRGenerator_Expressions_Calls_Builtin_FS.cpp`, `src/runtime/src/node/fs.cpp` (impl using `uv_fs_mkdtemp`).

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
- [ ] **Current:** Start Milestone 96.5 (Links & Symlinks).
