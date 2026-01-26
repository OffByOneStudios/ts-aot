# ts-aot

**TypeScript Ahead-of-Time Compiler** - Compile TypeScript directly to native executables via LLVM.

## Overview

ts-aot is an LLVM-based ahead-of-time compiler that compiles TypeScript directly to native machine code, producing standalone executables without requiring a runtime like Node.js or V8.

### Key Features

- **Native Executables**: Compile TypeScript to standalone Windows/Linux/macOS binaries
- **Node.js API Support**: Extensive compatibility with Node.js built-in modules
- **LLVM Optimization**: Benefit from LLVM's industrial-strength optimization passes
- **Type-Aware Codegen**: Use TypeScript's type information for better code generation

## Quick Start

```bash
# Compile a TypeScript file
build/src/compiler/Release/ts-aot.exe hello.ts -o hello.exe

# Run the executable
./hello.exe
```

## Conformance Status

| Category | Coverage | Notes |
|----------|----------|-------|
| TypeScript Features | 56% (78/140) | Classes, generics, async/await, modules |
| ECMAScript Features | 91% (209/230) | ES5-ES2024 features |
| Node.js APIs | 99% (1031/1040) | Full fs, http, net, crypto, streams |

See [docs/conformance/](docs/conformance/) for detailed feature matrices.

## Benchmarks

The benchmark suite validates compiler correctness and measures performance.

### Benchmark Results

| Category | Benchmark | Status | Notes |
|----------|-----------|--------|-------|
| **Compute** | fibonacci_simple | PASS | 315 ops/sec (recursive), 41K ops/sec (iterative) |
| **Compute** | sorting | PASS | QuickSort, MergeSort, BubbleSort algorithms |
| **Compute** | json_parse | PASS | JSON parse/stringify operations |
| **Compute** | regex | FAIL | Stack overflow on large text |
| **Startup** | cli_tool | PASS | Argument parsing with flags |
| **Startup** | cold_start | PASS | Minimal startup time test |
| **I/O** | file_copy | PASS | File read/write operations |
| **Memory** | allocation | PASS | Object allocation patterns |
| **Memory** | gc_stress | PASS | GC behavior under pressure |

### Running Benchmarks

```bash
# Compile and run individual benchmarks
build/src/compiler/Release/ts-aot.exe examples/benchmarks/compute/fibonacci_simple.ts -o fibonacci.exe
./fibonacci.exe

# Example output:
# Fibonacci Benchmark
# ==================
# fib_recursive(30): 3.17 ms avg, 315.89 ops/sec
# fib_iterative(10000): 23.90 us avg, 41844.86 ops/sec
# fib_memoized(500): 310.82 us avg, 3217.33 ops/sec
```

### Benchmark Categories

- **Compute**: CPU-bound operations (fibonacci, sorting, JSON, regex)
- **Startup**: Application initialization and argument parsing
- **I/O**: File system operations and network throughput
- **Memory**: Allocation patterns and garbage collection stress

## Building from Source

### Prerequisites

- **CMake** 3.20+
- **LLVM 18**
- **vcpkg** for dependencies
- **C++20** compatible compiler

### Build Steps

```bash
# Clone the repository
git clone https://github.com/cgrinker/ts-aoc.git
cd ts-aoc

# Configure and build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Project Structure

```
ts-aot/
├── src/
│   ├── compiler/          # Host compiler (TypeScript -> LLVM IR)
│   │   ├── analysis/      # Type inference and semantic analysis
│   │   ├── ast/           # AST loading and processing
│   │   └── codegen/       # LLVM IR generation
│   └── runtime/           # Target runtime library
│       ├── include/       # Runtime headers
│       └── src/           # Runtime implementation
├── examples/              # Example programs
│   └── benchmarks/        # Benchmark suite
├── tests/
│   ├── golden_ir/         # IR regression tests (136 tests)
│   └── node/              # Node.js API tests
└── docs/
    ├── conformance/       # Feature conformance matrices
    └── tickets/           # Implementation tickets
```

## Test Suite

```bash
# Run golden IR tests
python tests/golden_ir/runner.py

# Run Node.js API tests
python tests/node/run_tests.py
```

Current test status: **136/138 passed (98.6%)**

## Documentation

- [Development Guide](.github/DEVELOPMENT.md) - Contributing guidelines
- [TypeScript Conformance](docs/conformance/typescript-features.md) - Language feature support
- [ECMAScript Conformance](docs/conformance/ecmascript-features.md) - JavaScript feature support
- [Node.js Conformance](docs/conformance/nodejs-features.md) - API coverage
- [Roadmap](docs/README.md) - Development roadmap

## Architecture

ts-aot follows a traditional compiler architecture:

1. **Frontend**: Parse TypeScript using the TypeScript compiler API
2. **Analysis**: Infer types and perform semantic analysis
3. **Codegen**: Generate LLVM IR with type-aware optimizations
4. **Backend**: Use LLVM to produce optimized native code
5. **Runtime**: Link with a C++ runtime for Node.js API compatibility

### Key Technologies

- **LLVM 18**: Backend code generation and optimization
- **libuv**: Event loop and async I/O (same as Node.js)
- **ICU**: Unicode string handling
- **OpenSSL**: Cryptographic operations
- **Boehm GC**: Garbage collection

## License

[License information]

## Contributing

See [.github/DEVELOPMENT.md](.github/DEVELOPMENT.md) for development guidelines.
