# Active Project State

**Last Updated:** 2025-12-26
**Current Phase:** Phase 18 (Node.js Compatibility)

## Current Focus
We are in **Phase 18: Node.js Compatibility**, focusing on implementing the core Node.js modules (`fs`, `path`, `events`, `buffer`, `http`, `crypto`). We have just completed **Milestone 96.8: Advanced File Descriptors**.

## Active Tasks
1.  **Epic 96:** The File System (fs) - Milestone 96.9 (Watching & Events).
2.  **Epic 97:** Networking (http/https).

## Recent Accomplishments
*   **Compiler Fix:** Resolved "Operand is null" error in Async State Machine by ensuring all variables (including `catch` variables and internal control flags) are correctly spilled to the async frame.
*   **Milestone 96.8: Advanced File Descriptors:** Implemented `sync`, `datasync`, `truncate`, `readv`, `writev`, `fstat`, `fchmod`, `fchown`, and `futimes` for `FileHandle` and `fs`.
*   **Compiler Fix:** Resolved property unboxing issues for `Stats.size` and other numeric properties.
*   **Runtime Fix:** Fixed `TsMap` property priority to ensure `Stats.size` is correctly retrieved before prototype helpers.
*   **Async Lifetime Fix:** Resolved `readv`/`writev` buffer lifetime issues by moving `uv_buf_t` descriptors to heap-allocated `FSPromiseWork`.
*   **Milestone 96.7:** Directory Operations & Watching (opendir, Dir, Dirent).
*   **Milestone 96.6:** File Manipulation & Copying (copyFile, cp, rename, truncate, appendFile, rm, mkdtemp).
*   **Milestone 96.5:** Links & Symlinks (link, symlink, readlink, realpath, lstat).
*   **Milestone 96.4:** Extended Metadata & Permissions (access, chmod, chown, utimes, statfs, Stats fields, constants).
*   **Milestone 96.3:** File Handles & Streams (openSync, closeSync, readSync, writeSync, createReadStream, createWriteStream).
*   **Milestone 96.2:** Asynchronous IO (readFile, writeFile, Promises API).
*   **Milestone 96.1:** Synchronous IO (readFileSync, writeFileSync, existsSync, unlinkSync, mkdirSync, rmdirSync, statSync, readdirSync).
