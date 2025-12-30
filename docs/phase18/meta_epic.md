# Phase 18: Node.js Runtime Compatibility

**Status:** In Progress
**Goal:** Implement the standard library APIs that developers expect from a Node.js-compatible runtime.

## Verification Strategy
To ensure feature parity with Node.js LTS, we have established a [Compatibility Matrix](./compatibility_matrix.md).
1.  **Baseline:** Generated from Node.js v22 LTS using `scripts/analyze_node_compat.js`.
2.  **Tracking:** Each Epic will update the matrix, changing status from 🔴 (Missing) to 🟡 (Partial) or 🟢 (Implemented).
3.  **Compliance Tests:** A new test suite `tests/compliance/` will verify the existence and basic signature of these APIs.

## Implementation Strategy
To maintain compiler performance and code organization, each Node.js module (fs, path, http, etc.) must be implemented in dedicated source files rather than monolithic "Builtin" handlers.
- **Analysis:** Create `src/compiler/analysis/Analyzer_StdLib_<Module>.cpp`
- **Codegen:** Create `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_<Module>.cpp`

**📖 Developer Guide:** See `.github/instructions/adding-nodejs-api.instructions.md` for step-by-step instructions on adding new APIs.

## Core Objectives
1.  **File System:** Full `fs` module support (sync and async). ✅
2.  **Networking:** `net` and `http` modules (building on our existing prototype). 🟡
3.  **Streams:** The fundamental `stream` API (Readable, Writable, Duplex, Transform). ✅

## Milestones

### [Epic 96: The File System (fs)](./epic_96_filesystem.md) ✅ Complete
- [x] **Sync API:** `readFileSync`, `writeFileSync`, `statSync`, etc.
- [x] **Async API:** `readFile`, `writeFile` using `libuv` thread pool.
- [x] **File Handles:** `open`, `read`, `write`, `close`.
- [x] **Verification:** Update `compatibility_matrix.md` for `fs` module.

### [Epic 97: Streams & Events](./epic_97_streams_events.md) ✅ Complete
- [x] **EventEmitter:** A robust C++ implementation of `EventEmitter`.
- [x] **Stream Base:** Abstract base classes for Streams.
- [x] **Piping:** Efficient `src.pipe(dest)` implementation (using `sendfile` where possible).
- [x] **Verification:** Update `compatibility_matrix.md` for `events` and `stream`.

### [Epic 98: Module Resolution](./epic_98_module_resolution.md)
- [ ] **CommonJS Support:** `require()` implementation that searches `node_modules`.
- [ ] **ESM Support:** `import` implementation.
- [ ] **JSON Imports:** Support `require('./data.json')`.

### [Epic 99: Path & Utilities](./epic_99_path.md) ✅ Complete
- [x] **Path Manipulation:** `join`, `resolve`, `relative`, `normalize`.
- [x] **Path Components:** `basename`, `dirname`, `extname`, `parse`, `format`.
- [x] **Platform Specifics:** `sep`, `delimiter`, `win32`, `posix`.
- [x] **Verification:** Update `compatibility_matrix.md` for `path` module.

### [Epic 100: Networking (net & http)](./epic_100_networking.md) ✅ Complete
- [x] **TCP Sockets:** `net.Socket` and `net.Server` using `libuv`.
- [x] **HTTP Parser:** Integration with `llhttp` for `http.IncomingMessage`.
- [x] **HTTP Client:** `http.request` and `http.get`.
- [x] **HTTP Server:** `http.createServer` and `ServerResponse`.
- [x] **HTTPS:** `https.request`, `https.get`, `https.createServer` with OpenSSL.
- [x] **Utilities:** `net.isIP`, `http.METHODS`, `http.STATUS_CODES`, `SocketAddress`, `BlockList`.
- [x] **Agents:** Connection pooling with `http.Agent` and `https.Agent`.
- [x] **WebSocket:** Full RFC 6455 WebSocket implementation.

### [Epic 101: Buffer & Primitives](./epic_101_buffer.md) 🟡 Partial
**Existing:** `TsBuffer` class with `alloc()`, `from()`, length, indexing, `toString()`.
- [ ] **Buffer API:** `Buffer.concat`, `buffer.copy()`, `buffer.slice()`, `buffer.fill()`.
- [ ] **Encoding:** Support for `hex`, `base64`, `utf8` in Buffers.
- [ ] **TypedArrays:** Better integration between `Buffer` and `Uint8Array`.
- [ ] **Verification:** Update `compatibility_matrix.md` for `buffer`.

### [Epic 102: Process & Globals](./epic_102_process_globals.md) ✅ Complete
**Existing:** `process.argv`, `process.env`, `process.exit()`, `process.cwd()`, `setTimeout`, `console.log`.
- [x] **Process:** `process.nextTick`, `process.chdir()`, `process.platform`, `process.arch`.
- [x] **Standard I/O:** `process.stdout`, `process.stderr`, `process.stdin` as Streams.
- [x] **Timers:** `clearTimeout`, `setInterval`, `clearInterval`, `setImmediate`.
- [x] **Console:** `console.error`, `console.warn`, `console.time`, `console.trace`.
- [x] **Verification:** Update `compatibility_matrix.md` for `process` and `globals`.

### [Epic 103: Utilities (util & url)](./epic_103_utils.md) 🟡 Partial
**Existing:** `TsURL` class with basic parsing.
- [ ] **URL API:** `URL` class (WHATWG) and `URLSearchParams`.
- [ ] **Util:** `util.promisify`, `util.inspect`, `util.inherits`.
- [ ] **Text Encoding:** `TextEncoder` and `TextDecoder`.
- [ ] **Verification:** Update `compatibility_matrix.md` for `util` and `url`.
