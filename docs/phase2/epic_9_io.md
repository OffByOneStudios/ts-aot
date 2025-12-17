# Epic 9: File I/O & System Interop

**Status:** Planned
**Parent:** [Phase 2 Meta Epic](./meta_epic.md)

## Overview
Enable reading input data from files and parsing strings into numbers.

## Milestones

### Milestone 9.1: File System
- **Runtime:** Implement `ts_fs_read_file_sync(path)` using C++ std::filesystem or C stdio.
- **Binding:** Expose as `fs.readFileSync`.

### Milestone 9.2: Parsing
- **Runtime:** Implement `ts_parse_int(str)` and `ts_parse_float(str)`.
- **Binding:** Expose as global `parseInt` and `parseFloat`.

## Action Items
- [ ] Implement `fs.readFileSync` in Runtime.
- [ ] Implement `parseInt` in Runtime.
- [ ] Add integration test: Read a file containing numbers and sum them.
