# Epic 62: High-Throughput HTTP Microservice

**Status:** Completed
**Goal:** Demonstrate the stability and throughput of the AOT-compiled networking stack.

## Concept
A simple JSON API server that mimics a microservice. It exercises the networking stack, JSON serialization, and concurrency model.

## Implementation Details

### The TypeScript Code
```typescript
import * as http from 'http';

const server = http.createServer((req, res) => {
    if (req.url === '/json') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ message: 'Hello, World!', timestamp: Date.now() }));
    } else {
        res.writeHead(404);
        res.end();
    }
});

server.listen(8080);
```

### The AOT Advantage
1.  **Memory Footprint:** AOT binaries should consume significantly less RAM than a Node.js process (which includes the V8 engine, JIT compiler, and profiler).
2.  **Tail Latency:** Without the JIT compiler running in the background or deoptimizing code, 99th percentile latency should be more stable.
3.  **Throughput:** Efficient `llhttp` and `libuv` bindings (Epic 56) should rival Node.js performance.

## Tasks

### Milestone 1: Runtime Infrastructure (C++)
- [x] Implement `TsHttpServer` class in `src/runtime/src/TsHttp.cpp`.
- [x] Implement `TsIncomingMessage` and `TsServerResponse` classes.
- [x] Implement `http.createServer` and `server.listen` using `libuv` and `llhttp`.
- [x] Support basic headers and body writing in `ServerResponse`.

### Milestone 2: Compiler Support (Workaround)
- [x] Define `http` module types in `src/compiler/analysis/Analyzer_StdLib.cpp`.
- [x] Add `http` to built-in modules in `src/compiler/analysis/Analyzer_Helpers.cpp`.
- [x] Ensure `IRGenerator` correctly maps `http` calls to runtime functions.

### Milestone 3: Example & Verification
- [x] Create `examples/http-server/http-server.ts`.
- [x] Implement a basic JSON API with routing.
- [x] Verify the server handles multiple concurrent requests.

### Milestone 4: Benchmarking
- [x] Stress test using custom `aiohttp` load generator (due to lack of `wrk`).
- [x] Compare throughput (RPS) and latency (P99) against Node.js.
- [x] Measure memory usage (RSS) under load.

## Benchmarking Results

| Metric | AOT Server | Node.js (v22.x) |
| :--- | :--- | :--- |
| **Requests/sec** | **15,144** | 13,890 |
| **Avg Latency** | **3.30 ms** | 3.60 ms |
| **P99 Latency** | **4.51 ms** | 6.15 ms |

*Note: Benchmarks were run with concurrency=50 over 10 seconds on Windows using a custom `aiohttp` load generator.*
