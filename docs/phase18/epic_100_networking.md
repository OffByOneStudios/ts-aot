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
- [ ] **Task 100.4.3:** Support HTTPS (via `mbedtls` or `openssl`).

## Action Items
- [x] **Completed:** Milestone 100.1 - TCP Sockets.
- [x] **Completed:** Milestone 100.2 - HTTP Parsing.
- [x] **Completed:** Milestone 100.3 - HTTP Server.
- [x] **Completed:** Milestone 100.4 - HTTP Client (Basic).
