# Epic 62: High-Throughput HTTP Microservice

**Status:** Planned
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
- [ ] Create `examples/http-server/`.
- [ ] Implement a basic routing logic.
- [ ] Use `JSON.stringify` (Epic 54) for response generation.
- [ ] Stress test using `wrk` or `autocannon`.

## Benchmarking
- **Throughput:** Requests per second.
- **Latency:** P50, P99, P99.9 latency.
- **Memory:** RSS usage under load.
