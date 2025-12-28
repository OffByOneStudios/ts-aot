# Epic 100: Networking (net & http)

**Status:** Planned
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement the core Node.js networking modules. This will allow `ts-aot` to run web servers and make HTTP requests.

## Milestones

### Milestone 100.1: TCP Sockets (net)
- [ ] **Task 100.1.1:** Implement `net.Socket` wrapping `uv_tcp_t`.
- [ ] **Task 100.1.2:** Implement `net.Server` and `listen()`.
- [ ] **Task 100.1.3:** Support `connect()` and basic data transfer.
- [ ] **Task 100.1.4:** Integrate with `EventEmitter` and `Duplex` stream.

### Milestone 100.2: HTTP Parsing
- [ ] **Task 100.2.1:** Integrate `llhttp` (or similar) into the runtime.
- [ ] **Task 100.2.2:** Implement `http.IncomingMessage` as a `Readable` stream.
- [ ] **Task 100.2.3:** Implement header parsing and body handling.

### Milestone 100.3: HTTP Server
- [ ] **Task 100.3.1:** Implement `http.Server` and `http.createServer`.
- [ ] **Task 100.3.2:** Implement `http.ServerResponse` as a `Writable` stream.
- [ ] **Task 100.3.3:** Support status codes, headers, and chunked encoding.

### Milestone 100.4: HTTP Client
- [ ] **Task 100.4.1:** Implement `http.ClientRequest`.
- [ ] **Task 100.4.2:** Implement `http.request` and `http.get`.
- [ ] **Task 100.4.3:** Support HTTPS (via `mbedtls` or `openssl`).

## Action Items
- [ ] **Planned:** Milestone 100.1 - TCP Sockets.
