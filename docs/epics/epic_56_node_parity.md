# Epic 56: Node Parity (File System & Process)

This epic focuses on expanding the Node.js-compatible API surface in `ts-aot`, specifically adding support for asynchronous file operations and process environment access.

## Milestones
- [x] Implement `fs.promises.writeFile`, `fs.promises.readFile`, `fs.promises.mkdir`, `fs.promises.stat` (Async)
- [x] Implement `fs.existsSync`, `fs.mkdirSync`, `fs.statSync`, `fs.unlinkSync`, `fs.rmdirSync` (Sync)
- [x] Implement `process.argv`, `process.cwd`, `process.env`, `process.exit`
- [x] Register new globals in Compiler Analyzer
- [x] Add Codegen support for new built-ins
- [x] Verify with integration tests

## Action Items
- [x] Add `ts_fs_writeFile_async`, `ts_fs_readFile_async`, `ts_fs_mkdir_async`, `ts_fs_stat_async` to `IO.cpp` using `libuv` thread pool.
- [x] Add `ts_fs_existsSync`, `ts_fs_mkdirSync`, `ts_fs_statSync`, `ts_fs_unlinkSync`, `ts_fs_rmdirSync` to `IO.cpp`.
- [x] Initialize `process.argv`, `process.cwd`, `process.env`, `process.exit` in `ts_main` (`Core.cpp`).
- [x] Update `Analyzer_StdLib.cpp` to register new `fs` and `process` members.
- [x] Update `IRGenerator_Expressions_Calls_Builtin.cpp` to handle new built-in calls.
- [x] Update `IRGenerator_Expressions_Access.cpp` to handle `process.argv` and `process.env`.
- [x] Create `tests/integration/node_parity_test.ts`.
- [x] Update `test_runner.py` to pass command-line arguments to the compiled app.
- [x] Run and verify tests.
