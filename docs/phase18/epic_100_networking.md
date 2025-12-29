# Epic 100: Networking (net & http)

**Status:** In Progress
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the core Node.js networking modules. This will allow `ts-aot` to run web servers and make HTTP requests.

## Milestones

### Milestone 100.1: TCP Sockets (net)
- [x] **Task 100.1.1:** Implement `net.Socket` wrapping `uv_tcp_t`.
- [x] **Task 100.1.2:** Implement `net.Server` and `listen()`.
- [x] **Task 100.1.3:** Support `connect()` and basic data transfer.
- [x] **Task 100.1.4:** Integrate with `EventEmitter` and `Duplex` stream.

### Milestone 100.2: HTTP Parsing
- [x] **Task 100.2.1:** Integrate `llhttp` (or similar) into the runtime.
- [x] **Task 100.2.2:** Implement `http.IncomingMessage` as a `Readable` stream.
- [x] **Task 100.2.3:** Implement header parsing and body handling.

### Milestone 100.3: HTTP Server
- [x] **Task 100.3.1:** Implement `http.Server` and `http.createServer`.
- [x] **Task 100.3.2:** Implement `http.ServerResponse` as a `Writable` stream.
- [x] **Task 100.3.3:** Support status codes, headers, and chunked encoding.

### Milestone 100.4: HTTP Client
- [x] **Task 100.4.1:** Implement `http.ClientRequest`.
- [x] **Task 100.4.2:** Implement `http.request` and `http.get`.
- [x] **Task 100.4.3:** Implement response events (`data`, `end`, `error`).
- [x] **Task 100.4.4:** Implement `res.statusCode` property access.
- [x] **Task 100.4.5:** Fix virtual inheritance pointer casting with `dynamic_cast`.

### Milestone 100.5: HTTPS Support (OpenSSL)
**Status:** Complete
**Prerequisite:** OpenSSL is already linked into tsruntime.lib

- [x] **Task 100.5.1:** Create `TsSecureSocket` class wrapping SSL_CTX and SSL objects.
- [x] **Task 100.5.2:** Implement SSL handshake integration with libuv.
- [x] **Task 100.5.3:** Implement `https.request` and `https.get` C API.
- [x] **Task 100.5.4:** Add compiler codegen for `https` module.
- [x] **Task 100.5.5:** Implement `https.Server` and `https.createServer`.
- [x] **Task 100.5.6:** Add certificate loading from Buffer (fs.readFileSync integration).

### Milestone 100.6: Networking Utilities & Constants
- [x] **Task 100.6.1:** Implement `net.createConnection` (alias for `net.connect`).
- [x] **Task 100.6.2:** Implement `net.isIP`, `net.isIPv4`, `net.isIPv6`.
- [x] **Task 100.6.3:** Implement `http.METHODS` and `http.STATUS_CODES` constants.
- [x] **Task 100.6.4:** Implement `http.maxHeaderSize` constant.
- [x] **Task 100.6.5:** Implement `http.validateHeaderName` and `http.validateHeaderValue`.
- [ ] **Task 100.6.6:** Implement `net.SocketAddress` class.
- [ ] **Task 100.6.7:** Implement `net.BlockList` class.
- [x] **Task 100.6.8:** Implement `net.getDefaultAutoSelectFamily` and `net.setDefaultAutoSelectFamily`.
- [x] **Task 100.6.9:** Implement `net.getDefaultAutoSelectFamilyAttemptTimeout` and `net.setDefaultAutoSelectFamilyAttemptTimeout`.

### Milestone 100.7: Connection Pooling (Agents)
- [ ] **Task 100.7.1:** Implement `http.Agent` base class.
- [ ] **Task 100.7.2:** Implement `https.Agent` class.
- [ ] **Task 100.7.3:** Implement `http.globalAgent` and `https.globalAgent`.
- [ ] **Task 100.7.4:** Support connection reuse in `ClientRequest`.
- [ ] **Task 100.7.5:** Implement `http.setMaxIdleHTTPParsers`.

### Milestone 100.8: HTTP Advanced Classes
- [ ] **Task 100.8.1:** Implement `http.OutgoingMessage` base class.
- [ ] **Task 100.8.2:** Implement `http.CloseEvent` class.
- [ ] **Task 100.8.3:** Implement `http.MessageEvent` class.

### Milestone 100.9: WebSocket Support
- [ ] **Task 100.9.1:** Implement `http.WebSocket` class.
- [ ] **Task 100.9.2:** Implement WebSocket handshake protocol.
- [ ] **Task 100.9.3:** Implement WebSocket frame encoding/decoding.

**Reference Implementation:** See `src/runtime/src/TsFetch.cpp` lines 50-130 for working OpenSSL + libuv pattern:
```cpp
// SSL context setup
SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_client_method());
SSL* ssl = SSL_new(ssl_ctx);
BIO* rbio = BIO_new(BIO_s_mem());  // Read from network
BIO* wbio = BIO_new(BIO_s_mem());  // Write to network
SSL_set_bio(ssl, rbio, wbio);
SSL_set_connect_state(ssl);  // Client mode

// In uv read callback: write to rbio, call SSL_read
// In uv write: call SSL_write, read from wbio, send via uv_write
```

## Action Items
- [x] **Completed:** Milestone 100.1 - TCP Sockets.
- [x] **Completed:** Milestone 100.2 - HTTP Parsing.
- [x] **Completed:** Milestone 100.3 - HTTP Server.
- [x] **Completed:** Milestone 100.4 - HTTP Client (Full).
- [x] **Completed:** Milestone 100.5 - HTTPS Support (Client & Server).
- [ ] **Planned:** Milestone 100.6 - Networking Utilities & Constants.
- [ ] **Planned:** Milestone 100.7 - Connection Pooling (Agents).
- [ ] **Planned:** Milestone 100.8 - HTTP Advanced Classes.
- [ ] **Planned:** Milestone 100.9 - WebSocket Support.
