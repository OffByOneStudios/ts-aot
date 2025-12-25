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

## Action Items
- [ ] **Current:** Verify full epic completion.
