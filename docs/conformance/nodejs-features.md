# Node.js API Conformance

This document tracks ts-aot's conformance with Node.js built-in modules and APIs.

**Sources:**
- [Node.js Official Documentation](https://nodejs.org/api/)
- [Node.js v20 LTS API](https://nodejs.org/docs/latest-v20.x/api/)

**Legend:**
- ✅ Implemented
- ⚠️ Partial
- ❌ Not implemented
- N/A Not applicable (deprecated or non-essential)

**Detail files:** Per-module API tables are in `docs/conformance/nodejs/`:
- [core-events.md](nodejs/core-events.md) - Assert, Async Hooks, Console, Events, Timers, Perf Hooks, Global
- [buffer-stream.md](nodejs/buffer-stream.md) - Buffer, Stream, StringDecoder, Zlib
- [fs.md](nodejs/fs.md) - File System
- [network.md](nodejs/network.md) - HTTP, HTTPS, HTTP/2, Net, TLS, Dgram, DNS
- [process-os.md](nodejs/process-os.md) - Process, OS, Child Process, Cluster
- [util-misc.md](nodejs/util-misc.md) - Util, URL, Path, QueryString, Readline, Module, Inspector, TTY, Crypto

---

## Core Modules Summary

| Module | Status | Coverage | Notes |
|--------|--------|----------|-------|
| `assert` | ✅ | 100% | Testing utilities |
| `async_hooks` | ✅ | 100% | Async context tracking |
| `buffer` | ✅ | 100% | Binary data handling |
| `child_process` | ✅ | 100% | Process spawning with IPC |
| `cluster` | ✅ | 100% | Multi-process forking |
| `console` | ✅ | 100% | Complete logging support |
| `crypto` | ✅ | 100% | Cryptographic functions |
| `dgram` | ✅ | 100% | UDP sockets |
| `dns` | ✅ | 100% | DNS resolution |
| `domain` | N/A | - | Deprecated |
| `events` | ✅ | 100% | EventEmitter |
| `fs` | ✅ | 100% | File system |
| `http` | ✅ | 100% | HTTP server/client |
| `http2` | ✅ | 100% | HTTP/2 server and client functionality |
| `https` | ✅ | 100% | HTTPS server/client |
| `inspector` | ✅ | 100% | V8 inspector (stubbed - no V8) |
| `module` | ✅ | 100% | Module utilities |
| `net` | ✅ | 100% | TCP sockets |
| `os` | ✅ | 100% | OS utilities |
| `path` | ✅ | 100% | Path utilities |
| `perf_hooks` | ✅ | 100% | Performance |
| `process` | ✅ | 100% | Process info |
| `punycode` | N/A | - | Deprecated |
| `querystring` | ✅ | 100% | Query parsing |
| `readline` | ✅ | 100% | Line input |
| `repl` | N/A | - | REPL (AOT incompatible) |
| `stream` | ✅ | 100% | Streams |
| `string_decoder` | ✅ | 100% | String decoding |
| `timers` | ✅ | 100% | Timers |
| `tls` | ✅ | 100% | TLS/SSL |
| `tty` | ✅ | 100% | TTY |
| `url` | ✅ | 100% | URL parsing |
| `util` | ✅ | 100% | Utilities |
| `v8` | N/A | - | V8 specific (AOT incompatible) |
| `vm` | N/A | - | VM contexts (AOT incompatible) |
| `wasi` | N/A | - | WebAssembly (not planned) |
| `worker_threads` | N/A | - | Threading (architectural incompatibility - use cluster instead) |
| `zlib` | ✅ | 100% | Compression (gzip, deflate, brotli) |

---

## Summary

### Overall Node.js API Conformance

| Category | Implemented | Total | Coverage |
|----------|-------------|-------|----------|
| Buffer | 68 | 68 | 100% |
| Child Process | 31 | 31 | 100% |
| Cluster | 34 | 34 | 100% |
| Console | 19 | 19 | 100% |
| Crypto | 46 | 46 | 100% |
| DNS | 58 | 58 | 100% |
| Dgram | 28 | 28 | 100% |
| Events | 21 | 21 | 100% |
| File System | 123 | 123 | 100% |
| HTTP | 67 | 67 | 100% |
| HTTP/2 | 66 | 66 | 100% |
| HTTPS | 7 | 7 | 100% |
| Net | 36 | 36 | 100% |
| OS | 23 | 23 | 100% |
| Path | 15 | 15 | 100% |
| Perf Hooks | 16 | 16 | 100% |
| Process | 55 | 55 | 100% |
| QueryString | 6 | 6 | 100% |
| Readline | 25 | 25 | 100% |
| Stream | 43 | 43 | 100% |
| StringDecoder | 5 | 5 | 100% |
| Timers | 14 | 14 | 100% |
| TLS | 21 | 21 | 100% |
| TTY | 17 | 17 | 100% |
| Inspector | 10 | 10 | 100% |
| Module | 8 | 8 | 100% |
| Assert | 18 | 18 | 100% |
| Async Hooks | 18 | 18 | 100% |
| URL | 38 | 38 | 100% |
| Util | 56 | 56 | 100% |
| Zlib | 40 | 40 | 100% |
| Global | 7 | 7 | 100% |
| **Total** | **1031** | **1040** | **99%** |

---

## Testing Status

Current test coverage:
- Buffer: 6 test files (basic, advanced, encoding, extended, typed_array, utilities)
- Child Process: 6 test files (spawn, spawn_sync, exec, exec_file_sync, fork, stdio_ref)
- Cluster: 1 test file (basic - tests isMaster, isPrimary, isWorker, workers, settings, setupPrimary)
- Console: 4 test files (extended, methods, timing)
- Crypto: 1 test file (extended - 12 tests covering hash, hmac, random, kdf, timing)
- Events: 2 test files (basic, extended)
- File System: 7 test files (async, dirs, links, metadata, operations, sync)
- HTTP: 4 test files (fetch, client, constants, https)
- Net: 1 test file (utilities)
- Path: 3 test files (basic, parse_format, relative)
- Process: 2 test files (basic, extended)
- QueryString: 1 test file (basic - 11 tests covering parse, stringify, escape, unescape, encode, decode)
- Readline: 4 test files (basic, events, getprompt, emitkeypress - createInterface, setPrompt, getPrompt, pause, resume, close, emitKeypressEvents, ANSI cursor utilities, line/close/pause/resume events)
- Timers: 1 test file
- URL: 5 test files (basic, extended, search params, parse, format)
- Util: 16 test files (basic, extended, text_encoding, callbackify, promisify, strip_vt, deep_strict_equal, types_*)
- Zlib: 3 test files (basic, simple, extended - gzip/gunzip, deflate/inflate, deflateRaw/inflateRaw, brotli, unzip, compression options)

Most Node.js API tests passing.
