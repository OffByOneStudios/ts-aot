# ts-aot Examples

This directory contains benchmarks and production-ready examples for the TypeScript AOT compiler.

## Directory Structure

```
examples/
├── benchmarks/           # Performance benchmarks
│   ├── harness/         # Benchmark utilities
│   ├── startup/         # Startup time benchmarks
│   ├── compute/         # CPU-intensive benchmarks
│   ├── io/              # I/O benchmarks
│   └── memory/          # Memory allocation benchmarks
├── production/          # Production-ready templates
│   ├── api-server/      # REST API server
│   ├── cli-app/         # Command-line application
│   ├── worker/          # Background job processor
│   └── data-processor/  # ETL data pipeline
└── README.md
```

## Benchmarks

The benchmark suite compares ts-aot compiled executables against Node.js.

### Running Benchmarks

```powershell
# Run all benchmarks
.\scripts\run_benchmarks.ps1

# Run specific category
.\scripts\run_benchmarks.ps1 -Category startup
.\scripts\run_benchmarks.ps1 -Category compute

# Run with more iterations
.\scripts\run_benchmarks.ps1 -Runs 50
```

### Benchmark Categories

#### Startup Benchmarks (`benchmarks/startup/`)

Measures cold start performance - where AOT shines brightest.

| Benchmark | Description | Expected Speedup |
|-----------|-------------|------------------|
| cold_start | Minimal program, measures runtime overhead | 10-50x |
| cli_tool | Simple CLI with arg parsing | 5-20x |

#### Compute Benchmarks (`benchmarks/compute/`)

Measures raw computational performance.

| Benchmark | Description | Expected Speedup |
|-----------|-------------|------------------|
| fibonacci | Recursive function calls | 0.8-1.5x |
| sorting | Array sorting algorithms | 0.9-1.3x |
| json_parse | JSON parsing/stringify | 0.8-1.2x |
| regex | Regular expression matching | 0.9-1.2x |

#### I/O Benchmarks (`benchmarks/io/`)

Measures file and network I/O performance.

| Benchmark | Description | Expected Speedup |
|-----------|-------------|------------------|
| file_copy | File read/write throughput | 1.0-1.3x |
| http_hello | HTTP server req/sec | 1.0-1.3x |
| tcp_echo | TCP socket throughput | 1.0-1.2x |

#### Memory Benchmarks (`benchmarks/memory/`)

Measures allocation and GC performance.

| Benchmark | Description |
|-----------|-------------|
| allocation | Object allocation stress |
| gc_stress | GC behavior under load |

### Using the Benchmark Harness

```typescript
import { benchmark, BenchmarkSuite } from './harness/benchmark';

// Single benchmark
const result = benchmark('my-test', () => {
    // Code to benchmark
}, { iterations: 100, warmup: 10 });

// Benchmark suite
const suite = new BenchmarkSuite('My Suite');
suite.add('test1', () => { /* ... */ });
suite.add('test2', () => { /* ... */ });
suite.run();
suite.printSummary();
```

## Production Examples

### API Server (`production/api-server/`)

A REST API server demonstrating:
- HTTP request handling
- JSON request/response
- Router with URL parameters
- Middleware pattern
- Error handling
- Graceful shutdown

```bash
# Build
ts-aot examples/production/api-server/server.ts -o server.exe

# Run
./server.exe

# Test
curl http://localhost:3000/health
curl http://localhost:3000/api/users
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"Test","email":"test@example.com"}' \
     http://localhost:3000/api/users
```

### CLI Application (`production/cli-app/`)

A command-line application demonstrating:
- Argument parsing
- Command pattern
- File system operations
- Colored console output
- Exit codes

```bash
# Build
ts-aot examples/production/cli-app/main.ts -o cli.exe

# Run
./cli.exe --help
./cli.exe init myproject
./cli.exe build --verbose
```

### Background Worker (`production/worker/`)

A background job processor demonstrating:
- Timer-based scheduling
- Job queue management
- Error recovery with retries
- Graceful shutdown
- Logging

```bash
# Build
ts-aot examples/production/worker/worker.ts -o worker.exe

# Run
./worker.exe
# Press Ctrl+C to stop
```

### Data Processor (`production/data-processor/`)

An ETL pipeline demonstrating:
- File I/O (JSON, CSV)
- Data transformation
- Pipeline composition
- Filtering and validation

```bash
# Build
ts-aot examples/production/data-processor/pipeline.ts -o processor.exe

# Run
./processor.exe input.json output.json
./processor.exe data.csv filtered.csv --csv -f status active -v
```

## Building Examples

All examples can be compiled using ts-aot:

```bash
# Build a single example
ts-aot examples/path/to/example.ts -o example.exe

# Build with debug symbols
ts-aot examples/path/to/example.ts -o example.exe -g
```

## Benchmark Results

Benchmarks run on Windows x64. Times in milliseconds (lower is better).

### Cold Start

| Runtime | Time (avg) | Speedup |
|---------|------------|---------|
| ts-aot  | 16ms       | **2.0x** |
| Node.js | 31ms       | 1.0x    |

ts-aot starts **2x faster** than Node.js for minimal programs.

### Compute: Fibonacci

| Benchmark | ts-aot | Node.js | Winner |
|-----------|--------|---------|--------|
| Recursive fib(35) | 36ms | 60ms | **ts-aot 1.7x** |
| Iterative fib(1000) x10000 | 8ms | 7ms | Node.js 1.1x |

ts-aot excels at recursive function calls.

### Array Operations (n=100,000)

| Operation | ts-aot | Node.js | Winner |
|-----------|--------|---------|--------|
| push | 1ms | 3ms | **ts-aot 3x** |
| map | 8ms | 1ms | Node.js 8x |
| filter | 15ms | 1ms | Node.js 15x |
| reduce | 3ms | 1ms | Node.js 3x |

V8's array optimizations outperform ts-aot on map/filter/reduce.

### String Operations (n=10,000)

| Operation | ts-aot | Node.js | Winner |
|-----------|--------|---------|--------|
| concat | 105ms | 0ms | Node.js |
| template literals | 15ms | 1ms | Node.js 15x |
| string methods x10 | 65ms | 3ms | Node.js 22x |

V8's string rope optimization gives Node.js a significant advantage.

### Summary

| Category | ts-aot Advantage |
|----------|------------------|
| **Cold Start** | 2x faster |
| **Recursive Compute** | 1.7x faster |
| **Array/String Ops** | Node.js faster (JIT optimizations) |

**Best use cases for ts-aot:**
- CLI tools and scripts (startup-dominated)
- Serverless functions (cold start matters)
- CPU-bound recursive algorithms
- Applications requiring predictable performance

## Performance Characteristics

### AOT Advantages

1. **Cold Start**: 2x faster startup (no JIT warmup)
2. **Short-lived Processes**: CLIs, lambdas, scripts
3. **Predictable Performance**: No JIT tier compilation
4. **Recursive Computation**: No trampoline overhead

### Where Node.js Is Faster

1. **Array Operations**: V8's optimized array internals
2. **String Operations**: V8's rope and inline caching
3. **Long-running Compute**: JIT can specialize hot paths

### Best Use Cases for ts-aot

- Command-line tools
- Serverless functions (AWS Lambda, etc.)
- Microservices with fast startup requirements
- Build tools and scripts
- Any application where startup time matters

## Contributing New Examples

When adding new examples:

1. Follow the existing directory structure
2. Include a `user_main()` function as entry point
3. Add TypeScript type annotations
4. Include comments explaining the example
5. Test with both ts-aot and Node.js
6. Update this README

## Notes

- Benchmarks may vary based on hardware and OS
- Run benchmarks multiple times for accurate results
- I/O benchmarks (http, tcp) require external load testing tools
- Memory benchmarks depend on GC behavior
