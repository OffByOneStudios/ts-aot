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

## Expected Performance Characteristics

### AOT Advantages

1. **Cold Start**: 10-50x faster startup (no JIT warmup)
2. **Short-lived Processes**: CLIs, lambdas, scripts
3. **Predictable Performance**: No JIT tier compilation
4. **Memory Footprint**: Often smaller than Node.js

### Where Node.js May Be Faster

1. **Long-running Compute**: JIT can optimize hot paths
2. **Highly Dynamic Code**: Heavy use of `any` types
3. **Very Large Applications**: Complex optimization takes time

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
