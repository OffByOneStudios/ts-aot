# Epic 79: Runtime Feature Parity

**Status:** Completed
**Goal:** Implement the minimum set of standard library features required to run advanced benchmarks (SHA-256, JSON, etc.).

## Background
Our current runtime is sufficient for simple tasks but lacks the `TypedArray` and `Number` methods used in high-performance TypeScript code. To accurately benchmark against Node.js, we need these features.

## Action Items

### Milestone 1: TypedArrays & DataView
- [x] Register `Uint8Array`, `Uint32Array`, and `DataView` in `Analyzer_StdLib.cpp`.
- [x] Implement `ts_typed_array_create` and `ts_data_view_create` in the runtime.
- [x] Add support for `new Uint32Array(size)` in `IRGenerator_Classes.cpp`.
- [x] Implement element access (`arr[i]`) for TypedArrays in `IRGenerator_Expressions_Access.cpp`.

### Milestone 2: Number Methods
- [x] Add `toString(radix)` and `toFixed(digits)` to `Analyzer_Expressions_Access.cpp`.
- [x] Implement `ts_number_to_string` and `ts_number_to_fixed` in `src/runtime/src/TsString.cpp`.
- [x] Handle these calls in `IRGenerator_Expressions_Calls_Builtin.cpp`.

### Milestone 3: Buffer & String Enhancements
- [x] Implement `Buffer.alloc()` and `Buffer.from()`.
- [x] Add `String.prototype.split` with RegExp support.

## Verification Plan
- [x] **SHA-256 Benchmark:** Successfully compile and run `examples/sha256.ts`.
- [x] **Unit Tests:** Add tests for each new runtime feature.
- [x] **Number Tests:** Verified `toString(radix)` and `toFixed()` with `examples/number_test.ts`.
- [x] **Buffer/String Tests:** Verified `Buffer` and `split(RegExp)` with `examples/buffer_string_test.ts`.
