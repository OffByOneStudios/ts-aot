# Epic 101: Buffer & Primitives

**Status:** In Progress (Partial Implementation)
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Enhance the `Buffer` implementation to match Node.js parity and improve performance of binary data handling.

## Existing Implementation

The following are **already implemented** in the runtime:

| Feature | Location | Notes |
|---------|----------|-------|
| `TsBuffer` class | `src/runtime/include/TsBuffer.h` | Core buffer with data/length |
| `Buffer.alloc(size)` | `ts_buffer_alloc()` | Creates zero-filled buffer |
| `Buffer.from(data)` | `ts_buffer_from()` | Creates from string/array |
| `buffer.length` | `ts_buffer_length()` | Property access |
| `buffer[index]` | `ts_buffer_get/set()` | Indexing |
| `buffer.toString()` | `ts_buffer_to_string()` | UTF-8 conversion |
| TypedArray basics | `TsTypedArray`, `TsDataView` | Uint8Array, Uint32Array, Float64Array |

**Analyzer:** `src/compiler/analysis/Analyzer_StdLib_Buffer.cpp`
**Codegen:** `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Buffer.cpp`

## Milestones

### Milestone 101.1: Buffer API Parity
- [x] **Task 101.1.1:** Implement `Buffer.from(array|string|buffer)`. *(Exists)*
- [x] **Task 101.1.2:** Implement `Buffer.alloc(size)`. *(Exists)*
- [ ] **Task 101.1.3:** Implement `Buffer.allocUnsafe(size)`.
- [ ] **Task 101.1.4:** Implement `Buffer.concat(list)`.
- [ ] **Task 101.1.5:** Implement `buffer.copy(target, targetStart, sourceStart, sourceEnd)`.
- [ ] **Task 101.1.6:** Implement `buffer.slice(start, end)`. *(Partially exists)*
- [ ] **Task 101.1.7:** Implement `buffer.fill(value, offset, end)`.

### Milestone 101.2: Encodings
- [ ] **Task 101.2.1:** Support `hex` encoding/decoding.
- [ ] **Task 101.2.2:** Support `base64` and `base64url` encoding/decoding.
- [ ] **Task 101.2.3:** Optimize `utf8` handling.

### Milestone 101.3: TypedArray Integration
- [ ] **Task 101.3.1:** Ensure `Buffer` inherits from `Uint8Array` (or behaves as such).
- [ ] **Task 101.3.2:** Implement `buffer.buffer` (ArrayBuffer) access.

## Action Items
- [ ] **Planned:** Milestone 101.1 - Buffer API Parity.
