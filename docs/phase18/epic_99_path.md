# Epic 99: Path & Utilities

**Status:** Completed
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the Node.js `path` module, providing utilities for working with file and directory paths.

## Milestones

### Milestone 99.1: Basic Path Manipulation
- [x] **Task 99.1.1:** Implement `path.join(...paths)`.
    - **Note:** Current implementation is limited to 2 arguments. Need to support variadic arguments.
- [x] **Task 99.1.2:** Implement `path.resolve(...paths)`.
- [x] **Task 99.1.3:** Implement `path.normalize(path)`.
- [x] **Task 99.1.4:** Implement `path.isAbsolute(path)`.

### Milestone 99.2: Path Components
- [x] **Task 99.2.1:** Implement `path.basename(path[, ext])`.
- [x] **Task 99.2.2:** Implement `path.dirname(path)`.
- [x] **Task 99.2.3:** Implement `path.extname(path)`.

### Milestone 99.3: Advanced Path Utilities
- [x] **Task 99.3.1:** Implement `path.relative(from, to)`.
- [x] **Task 99.3.2:** Implement `path.parse(path)` and `path.format(pathObject)`.
- [x] **Task 99.3.3:** Implement `path.toNamespacedPath(path)`.

### Milestone 99.4: Platform Specifics & Constants
- [x] **Task 99.4.1:** Implement `path.sep` and `path.delimiter`.
- [x] **Task 99.4.2:** Implement `path.win32` and `path.posix` objects.

## Action Items
- [x] **Current:** Define `path` module in `Analyzer_StdLib_Path.cpp`.

## Reflections & Improvements
- [x] **Task 99.5.1:** Ensure `path` methods handle both Windows and POSIX separators correctly based on the target platform.
