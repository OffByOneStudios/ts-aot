# Epic 56: Global Objects & Node.js Parity

**Status:** Not Started
**Goal:** Implement essential global objects and environment-specific APIs (fs, process, timers).

## Background
To support real-world TypeScript applications (and AoC solutions that read from files), we need a more complete set of global objects and a subset of the Node.js API.

## Action Items

### Timers
- [ ] Implement `setTimeout(callback, delay, ...args)`.
- [ ] Implement `setInterval(callback, delay, ...args)`.
- [ ] Implement `clearTimeout(id)` and `clearInterval(id)`.
- [ ] Integrate timers with the `libuv` event loop.

### File System (fs)
- [ ] Implement `fs.existsSync(path)`.
- [ ] Implement `fs.mkdirSync(path)` and `fs.rmdirSync(path)`.
- [ ] Implement `fs.unlinkSync(path)`.
- [ ] Implement `fs.statSync(path)` returning a basic `Stats` object.

### Process
- [ ] Implement `process.argv` (populated from `main` arguments).
- [ ] Implement `process.env` (populated from environment variables).
- [ ] Implement `process.exit(code)`.
- [ ] Implement `process.cwd()`.

### Networking & Buffers
- [ ] Complete the `fetch(url)` implementation in `IO.cpp` using `libcurl` or similar.
- [ ] Implement a basic `Buffer` class for binary data handling.

## Verification Plan
- Create a "Node.js compatibility" test suite in `tests/integration/node_compat/`.
- Verify that `process.argv` correctly receives command-line arguments.
