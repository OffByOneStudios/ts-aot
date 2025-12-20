# Epic: Networking and Buffers

This epic covers the implementation of `Buffer`, `URL`, and `fetch` APIs in `ts-aot`.

## Milestones
- [x] Implement `Buffer` class (Runtime + Compiler)
- [x] Implement `URL` class (Runtime + Compiler)
- [x] Implement `fetch` API (Runtime + Compiler)
- [x] Support `Response.text()` and `Response.json()` (Async)
- [x] Verify with integration tests

## Action Items
- [x] Create `TsBuffer` in runtime with `alloc`, `from`, `toString`, `get`, `set`.
- [x] Create `TsURL` in runtime using `ada` or manual parsing.
- [x] Create `TsFetch` in runtime using `libuv`, `llhttp`, and `OpenSSL`.
- [x] Register `Buffer`, `URL`, `fetch`, `Response`, `Headers` in `Analyzer_StdLib.cpp`.
- [x] Add codegen support for `Buffer` and `URL` built-ins.
- [x] Fix `AsyncContext` and `ts_async_resume` to support complex async flows.
- [x] Align VTable signatures between compiler and runtime for `Response` methods.
- [x] Create `tests/integration/networking_test.ts`.
- [x] Verify all tests pass.
- [x] Support `fetch` options (POST, headers, body)
- [x] Support `Response` methods (`json()`, `text()`, `arrayBuffer()`)
- [ ] Support `Buffer` methods (`toString()`, `from()`, `alloc()`)

## Action Items
- [x] Create `TsBuffer.cpp` and `TsBuffer.h`
- [x] Create `TsURL.cpp` and `TsURL.h`
- [x] Create `TsFetch.cpp` and `TsFetch.h`
- [x] Update `Analyzer_StdLib.cpp` with new types
- [x] Update `IRGenerator_Expressions_Calls_Builtin.cpp` for `fetch`
- [x] Implement POST support in `TsFetch.cpp`
- [x] Fix compiler IR generation for `fetch` options
- [x] Implement `Response.json()` and `Response.text()`
- [x] Add integration tests for `fetch` POST
