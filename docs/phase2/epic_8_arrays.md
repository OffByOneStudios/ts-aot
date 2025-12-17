# Epic 8: Arrays & Collections

**Status:** Planned
**Parent:** [Phase 2 Meta Epic](./meta_epic.md)

## Overview
Implement dynamic arrays to store lists of data.

## Milestones

### Milestone 8.1: Runtime Array
- **C++:** Create `TsArray` class template or void* based container managed by GC.
- **API:** `create`, `push`, `get`, `set`, `length`.

### Milestone 8.2: Array Literals & Types
- **AST:** Add `ArrayLiteralExpression` (`[1, 2, 3]`).
- **Type System:** Add `ArrayType` (`number[]`).
- **Analyzer:** Infer array element types.

### Milestone 8.3: Array Operations
- **AST:** Add `ElementAccessExpression` (`arr[i]`).
- **Codegen:** Generate calls to runtime array functions.
- **Methods:** Implement `push` and `pop` as built-ins or runtime methods.

## Action Items
- [x] Implement `TsArray` in Runtime.
- [x] Add `ArrayLiteral` to AST/Parser.
- [x] Implement Array type inference in Analyzer.
- [x] Implement `ElementAccess` in Codegen.
- [x] Add integration tests for array operations.
