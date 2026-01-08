# Epic 101: Buffer & Primitives

**Status:** ✅ COMPLETE
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Enhance the `Buffer` implementation to match Node.js parity and improve performance of binary data handling.

## Existing Implementation

The following are **already implemented** in the runtime:

| Feature | Location | Notes |
|---------|----------|-------|
| `TsBuffer` class | `src/runtime/include/TsBuffer.h` | Core buffer with data/length |
| `Buffer.alloc(size)` | `ts_buffer_alloc()` | Creates zero-filled buffer |
| `Buffer.allocUnsafe(size)` | `ts_buffer_alloc_unsafe()` | Creates uninitialized buffer |
| `Buffer.from(data, encoding?)` | `ts_buffer_from_string()` | Creates from string/array with encoding |
| `Buffer.concat(list)` | `ts_buffer_concat()` | Concatenates array of buffers |
| `Buffer.isBuffer(obj)` | `ts_buffer_is_buffer()` | Type check for buffer |
| `buffer.length` | `ts_buffer_length()` | Property access |
| `buffer.byteLength` | `ts_buffer_byte_length()` | TypedArray-like property |
| `buffer.byteOffset` | `ts_buffer_byte_offset()` | TypedArray-like property |
| `buffer.buffer` | `ts_buffer_get_array_buffer()` | Returns ArrayBuffer (self) |
| `buffer[index]` | `ts_buffer_get/set()` | Indexing |
| `buffer.toString(encoding?)` | `ts_buffer_to_string()` | Supports utf8, hex, base64, base64url |
| `buffer.slice(start, end)` | `ts_buffer_slice()` | Creates copy of portion |
| `buffer.subarray(start, end)` | `ts_buffer_subarray()` | Creates copy of portion |
| `buffer.copy(target, ...)` | `ts_buffer_copy()` | Copies bytes to target buffer |
| `buffer.fill(value, ...)` | `ts_buffer_fill()` | Fills buffer with value |
| `buffer.compare(other)` | `ts_buffer_compare()` | Compares two buffers |
| `buffer.equals(other)` | `ts_buffer_equals()` | Checks if buffers are equal |
| TypedArray basics | `TsTypedArray`, `TsDataView` | Uint8Array, Uint32Array, Float64Array |

**Analyzer:** `src/compiler/analysis/Analyzer_StdLib_Buffer.cpp`
**Codegen:** `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_Buffer.cpp`

## Milestones

### Milestone 101.1: Buffer API Parity ✅ COMPLETE
- [x] **Task 101.1.1:** Implement `Buffer.from(array|string|buffer)`. *(Exists)*
- [x] **Task 101.1.2:** Implement `Buffer.alloc(size)`. *(Exists)*
- [x] **Task 101.1.3:** Implement `Buffer.allocUnsafe(size)`.
- [x] **Task 101.1.4:** Implement `Buffer.concat(list)`.
- [x] **Task 101.1.5:** Implement `buffer.copy(target, targetStart, sourceStart, sourceEnd)`.
- [x] **Task 101.1.6:** Implement `buffer.slice(start, end)`.
- [x] **Task 101.1.7:** Implement `buffer.subarray(start, end)`.
- [x] **Task 101.1.8:** Implement `buffer.fill(value, offset, end)`.
- [x] **Task 101.1.9:** Implement `buffer.compare(other)`.
- [x] **Task 101.1.10:** Implement `buffer.equals(other)`.
- [x] **Task 101.1.11:** Implement `Buffer.isBuffer(obj)`.

### Milestone 101.2: Encodings ✅ COMPLETE
- [x] **Task 101.2.1:** Support `hex` encoding/decoding.
- [x] **Task 101.2.2:** Support `base64` and `base64url` encoding/decoding.
- [x] **Task 101.2.3:** UTF-8 handling (default encoding).

### Milestone 101.3: TypedArray Integration ✅ COMPLETE
- [x] **Task 101.3.1:** Ensure `Buffer` behaves like `Uint8Array` (byteLength, byteOffset properties).
- [x] **Task 101.3.2:** Implement `buffer.buffer` (ArrayBuffer) access.

## Action Items
- [x] **Complete:** Milestone 101.1 - Buffer API Parity.
- [x] **Complete:** Milestone 101.2 - Encodings.
- [x] **Complete:** Milestone 101.3 - TypedArray Integration.

## Test Files
- `examples/buffer_api_test.ts` - 29 tests for API parity
- `examples/buffer_encoding_test.ts` - 13 tests for encoding support
- `examples/buffer_typed_array_test.ts` - 6 tests for TypedArray-like properties
