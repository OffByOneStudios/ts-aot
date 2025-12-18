# Epic 46: Asynchronous I/O

Expose asynchronous versions of existing APIs using `libuv` and the `AsyncContext` infrastructure.

## Milestones
- [x] Implement `fs.promises.readFile`.
- [x] Implement a basic `fetch` using `libuv` (or a wrapper).

## Action Items
- [x] Implement `ts_fs_read_file_async` in the runtime using `uv_fs_read`.
- [x] Register `fs.promises.readFile` in the `Analyzer`.
- [x] Implement `ts_fetch` in the runtime (possibly using a simple socket-based HTTP client or a library).
- [x] Register `fetch` in the `Analyzer`.
- [x] Verify with `tests/integration/async_io_test.ts`.
