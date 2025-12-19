# Epic 56: Global Objects & Node.js Parity

**Status:** Not Started
**Goal:** Implement essential global objects and environment-specific APIs (fs, process, timers).

## Background
To support real-world TypeScript applications (and AoC solutions that read from files), we need a more complete set of global objects and a subset of the Node.js API.

## Action Items

### Timers
- [x] Implement `setTimeout(callback, delay, ...args)`.
- [x] Implement `setInterval(callback, delay, ...args)`.
- [x] Implement `clearTimeout(id)` and `clearInterval(id)`.
- [x] Integrate timers with the `libuv` event loop.

### File System (fs)
- [ ] Implement synchronous operations:
    - [ ] `fs.existsSync(path)`.
    - [ ] `fs.mkdirSync(path)` and `fs.rmdirSync(path)`.
    - [ ] `fs.unlinkSync(path)`.
    - [ ] `fs.statSync(path)` returning a basic `Stats` object.
- [ ] Implement asynchronous operations (`fs.promises`):
    - [x] `fs.promises.readFile(path)`.
    - [ ] `fs.promises.writeFile(path, data)`.
    - [ ] `fs.promises.mkdir(path)`.
    - [ ] `fs.promises.stat(path)`.
    - [x] Integrate with `libuv` thread pool (`uv_fs_t`) and return `TsPromise`.

### Process
- [ ] Implement `process.argv` (populated from `main` arguments).
- [ ] Implement `process.env` (populated from environment variables).
- [ ] Implement `process.exit(code)`.
- [ ] Implement `process.cwd()`.

### Networking & Buffers
- [ ] Add `llhttp` and `openssl` to `vcpkg.json`.
- [ ] Implement `URL` class for parsing and validation.
- [ ] Implement `Buffer` class for binary data handling (Uint8Array-like).
- [ ] Implement `fetch(url)` using `libuv` (TCP/DNS) and `llhttp` (parsing).
    - [ ] Async DNS resolution via `uv_getaddrinfo`.
    - [ ] TCP connection management via `uv_tcp_t`.
    - [ ] TLS/SSL integration (OpenSSL) for HTTPS support.
    - [ ] Response parsing with `llhttp` callbacks.
- [ ] Implement `Headers`, `Request`, and `Response` classes.

## Technical Architecture: Networking
To ensure high performance and future-proofing for an HTTP server, we will avoid high-level libraries like `libcurl` and build on the following stack:
1. **Event Loop:** `libuv` (already in use).
2. **Protocol Parsing:** `llhttp` (Node.js's parser).
3. **Security:** `OpenSSL` for TLS/SSL.
4. **Memory:** Boehm GC for all JS-visible objects (`Response`, `Headers`).

### HTTP Client Requirements
- **Connection Pooling:** (Future) Reuse TCP connections.
- **Streaming:** Support `response.body` as a stream.
- **Redirect Handling:** Follow 3xx status codes.

### HTTP Server Requirements (Future)
- **Socket Listening:** `uv_listen` on specified ports.
- **Request Routing:** Efficient mapping of paths to JS callbacks.
- **Keep-Alive:** Support persistent connections.

## Detailed Design: Buffer Class
The `Buffer` class provides a way to handle binary data in a manner compatible with Node.js, while being backed by the Boehm GC.

### Memory Layout
- **Pointer:** `uint8_t* data` (allocated via `ts_alloc`).
- **Length:** `size_t length`.
- **Encoding:** Default to UTF-8 for string conversions.

### Implementation Strategy
1. **C++ Runtime (`TsBuffer.h/cpp`):**
    - Define `class TsBuffer` inheriting from `TsObject`.
    - Use `icu::UnicodeString` for robust UTF-8 <-> UTF-16 conversions.
    - Implement `ts_buffer_alloc`, `ts_buffer_from_string`, etc., as `extern "C"` exports.
2. **Compiler Integration:**
    - Register `Buffer` as a global class in `Analyzer`.
    - Add specialized codegen for indexed access (`buf[i]`) to map to `ts_buffer_get/set`.

## Detailed Design: llhttp Wrapper
We will wrap `llhttp` to provide a streaming, event-driven parser that integrates with `libuv`.

### TsHttpParser Architecture
- **Parser Instance:** Each connection (client or server) owns a `TsHttpParser` instance.
- **Settings:** A static `llhttp_settings_t` structure defines the global callbacks.
- **Context:** The `llhttp_t.data` pointer will point back to the `TsHttpParser` instance to maintain state across chunks.

### Callback Workflow
1. **`on_headers_complete`:** 
    - Create the `Response` (client) or `Request` (server) object.
    - Parse the status code or HTTP method.
2. **`on_body`:** 
    - If the response is small, append to an internal `Buffer`.
    - If streaming is enabled, emit data to the JS `ReadableStream`.
3. **`on_message_complete`:** 
    - Finalize the object and trigger the `TsPromise` resolution.

## Verification Plan
- Create a "Node.js compatibility" test suite in `tests/integration/node_compat/`.
- Verify that `process.argv` correctly receives command-line arguments.
