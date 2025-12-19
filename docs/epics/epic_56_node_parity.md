# Epic 56: Node Parity (File System & Process)

This epic focuses on expanding the Node.js-compatible API surface in `ts-aot`, specifically adding support for asynchronous file operations and process environment access.

## Milestones
- [x] Implement `fs.promises.writeFile` (Async)
- [x] Implement `fs.existsSync` (Sync)
- [x] Implement `process.argv`
- [x] Register new globals in Compiler Analyzer
- [x] Add Codegen support for new built-ins
- [x] Verify with integration tests

## Action Items
- [x] Add `ts_fs_writeFile_async` to `IO.cpp` using `libuv` thread pool.
- [x] Add `ts_fs_existsSync` to `IO.cpp`.
- [x] Initialize `process.argv` in `ts_main` (`Core.cpp`).
- [x] Update `Analyzer_StdLib.cpp` to register `fs.promises.writeFile`, `fs.existsSync`, and `process.argv`.
- [x] Update `IRGenerator_Expressions_Calls_Builtin.cpp` to handle `fs.promises.writeFile` and `fs.existsSync`.
- [x] Update `IRGenerator_Expressions_Access.cpp` to handle `process.argv`.
- [x] Create `tests/integration/node_parity_test.ts`.
- [x] Update `test_runner.py` to pass command-line arguments to the compiled app.
- [x] Run and verify tests.
