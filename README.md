# ts-aot

**TypeScript Ahead-of-Time Compiler** - Compile TypeScript directly to native executables via LLVM.

## Overview

ts-aot is an LLVM-based ahead-of-time compiler that compiles TypeScript and JavaScript directly to native machine code, producing standalone executables without requiring Node.js or V8 at runtime.

### Key Features

- **Native Executables**: Compile `.ts`, `.tsx`, `.js`, `.jsx` to standalone binaries
- **Node.js API Compatibility**: 32 built-in modules (fs, http, net, crypto, streams, etc.)
- **LLVM Optimization**: Full O0-O3 optimization pipeline with runtime-aware function annotations
- **Type-Aware Codegen**: TypeScript types drive optimized code generation (NaN-boxed values, flat objects, integer specialization)
- **Custom GC**: Mark-sweep collector with generational nursery, escape analysis for stack allocation
- **npm Package Support**: Compile real npm packages (semver, uuid, nanoid, picomatch, etc.)
- **Portable Distribution**: Single zip, add to PATH, no installer needed

## Quick Start

### From a Distribution

```bash
# Unzip ts-aot-win-x64.zip, add to PATH, then:
ts-aot hello.ts -o hello.exe
./hello.exe
```

### From Source

```bash
# Build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Compile and run
build/src/compiler/Release/ts-aot.exe hello.ts -o hello.exe
./hello.exe
```

### Example

```typescript
import * as fs from 'fs';
import * as path from 'path';

function user_main(): number {
    const files = fs.readdirSync('.');
    const tsFiles = files.filter((f: string) => f.endsWith('.ts'));
    console.log(`Found ${tsFiles.length} TypeScript files`);
    for (const f of tsFiles) {
        const stat = fs.statSync(f);
        console.log(`  ${f} (${stat.size} bytes)`);
    }
    return 0;
}
```

## Conformance Status

| Category | Coverage | Details |
|----------|----------|---------|
| TypeScript Features | 99% (118/119 runtime) | Classes, generics, async/await, decorators, iterators |
| ECMAScript Features | 96% (221/230) | ES5 through ES2024 |
| Node.js APIs | 99% (1031/1040) | 32 modules: fs, http, net, crypto, streams, etc. |

### Test Suites

| Suite | Status | Description |
|-------|--------|-------------|
| Golden IR | 264/264 (100%) | Compiler regression tests (TypeScript + JavaScript) |
| Node.js API | 286/296 (96.6%) | Node.js API conformance tests |
| npm Packages | 11/11 (100%) | Real-world npm package compilation |

Passing npm packages: dotenv, escape-string-regexp, eventemitter3, fast-deep-equal, is-number, is-plain-object, ms, nanoid, picomatch, semver, uuid.

See [docs/conformance/](docs/conformance/) for detailed feature matrices.

## Compiler Flags

```
ts-aot <input.ts> [options]

Options:
  -o <file>         Output executable path
  -O0, -O1, -O2, -O3  Optimization level (default: O0)
  -g                Generate debug symbols (.pdb)
  --dump-ir         Print LLVM IR to stdout
  --dump-hir        Print HIR (intermediate representation)
  --dump-types      Print inferred types
  --bundle-icu      Embed ICU data in executable (~29MB larger, no external files)
  --gc-statepoints  Enable precise GC roots via LLVM statepoints
  --legacy-parser   Use Node.js-based parser instead of native C++ parser
```

## Architecture

```
TypeScript/JavaScript Source
        |
        v
  [Native C++ Parser]  -- Hand-written recursive descent (~4300 LOC)
        |
        v
  [AST]  -- TypeScript-compatible AST
        |
        v
  [Type Analyzer]  -- Type inference, monomorphization, module resolution
        |
        v
  [HIR (High-level IR)]  -- SSA-based intermediate representation
        |   |
        |   v
        |  [HIR Optimization Passes]
        |   - TypePropagation     - Propagate type info across SSA
        |   - IntegerOptimization - Specialize number ops to i64
        |   - ConstantFolding     - Evaluate compile-time constants
        |   - DeadCodeElimination - Remove unreachable code
        |   - Inlining            - Inline small functions
        |   - EscapeAnalysis      - Stack-allocate non-escaping objects
        |   - MethodResolution    - Devirtualize method calls
        |   - BuiltinResolution   - Lower built-in calls (Math, Array, etc.)
        |
        v
  [LLVM IR]  -- Generated via LLVM 18 C++ API
        |
        v
  [LLVM Optimization Pipeline]  -- O0-O3 with runtime-aware annotations
        |
        v
  [Native Object Code]  -- x86-64 machine code
        |
        v
  [LLD Linker]  -- Link with runtime + extensions into executable
        |
        v
  [Standalone Executable]
```

### Runtime

The runtime is a C++ library linked into every compiled executable:

| Component | Description |
|-----------|-------------|
| TsGC | Custom mark-sweep GC with generational nursery (4MB), conservative stack scanning |
| TsString | ICU-based Unicode strings |
| TsArray | Dynamic arrays with typed specialization |
| TsMap / TsSet | Hash-based collections |
| TsObject | Property access with flat inline-slot optimization |
| TsFlatObject | Shape-based objects with NaN-boxed inline slots and vtable dispatch |
| TsClosure | Closure capture with cell-based upvalues |
| TsPromise | Promise/async-await via libuv event loop |
| TsRegExp | ICU-based regular expressions |
| TsBigInt | Arbitrary-precision integers via libtommath |
| TsProxy / TsReflect | ES6 Proxy and Reflect |
| TsJSON | JSON.parse / JSON.stringify |
| TsSymbol | Symbol primitives with global registry |

### Extension System

Node.js APIs are implemented as C++ extension libraries with JSON contracts:

```
extensions/node/
├── fs/          # File system (sync + async)
├── http/        # HTTP server and client
├── net/         # TCP sockets
├── crypto/      # Hashing, HMAC, random, KDF
├── stream/      # Readable, Writable, Duplex, Transform
├── ...          # 32 modules total
└── each module has:
    ├── src/*.cpp       # C++ implementation
    └── *.ext.json      # Type contract (functions, classes, constants)
```

The `.ext.json` contracts define function signatures, enabling the compiler to generate direct native calls instead of dynamic dispatch.

## Project Structure

```
ts-aot/
├── src/
│   ├── compiler/              # Host compiler
│   │   ├── parser/            # Native C++ recursive descent parser
│   │   ├── ast/               # AST types and processing
│   │   ├── analysis/          # Type inference, monomorphization, module resolution
│   │   ├── hir/               # HIR pipeline
│   │   │   ├── passes/        # 8 optimization passes
│   │   │   └── handlers/      # Built-in function handlers (Math, Array, Map, etc.)
│   │   ├── extensions/        # Extension contract loader
│   │   └── codegen/           # LLVM IR generation and linking
│   └── runtime/               # Target runtime library
│       ├── include/           # Runtime headers (TsNanBox.h, etc.)
│       └── src/               # Runtime implementation (~35 source files)
├── extensions/
│   └── node/                  # 32 Node.js API extension modules
├── tests/
│   ├── golden_ir/             # 264 compiler regression tests
│   │   ├── typescript/        # Typed code tests
│   │   └── javascript/        # Dynamic/slow-path tests
│   ├── node/                  # 296 Node.js API tests
│   └── npm/                   # 11 npm package integration tests
├── scripts/
│   ├── package.ps1            # Build portable distribution zip
│   └── ...                    # Benchmark and utility scripts
├── docs/
│   ├── conformance/           # Feature conformance matrices
│   └── tickets/               # Implementation tickets
└── tmp/                       # Temporary test/debug files
```

## Building from Source

### Prerequisites

- **CMake** 3.20+
- **LLVM 18** (with development headers)
- **vcpkg** for dependencies
- **C++20** compiler (MSVC 2022 on Windows)

### Build Steps

```bash
git clone https://github.com/cgrinker/ts-aoc.git
cd ts-aoc

# Install dependencies
vcpkg install --triplet x64-windows-static-md

# Configure and build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Creating a Distribution

```powershell
# Create a portable zip (~112 MB without debug symbols)
.\scripts\package.ps1 -NoPdb

# With debug symbols (~500 MB)
.\scripts\package.ps1

# With embedded ICU data (larger executables, no external icudt74l.dat needed)
.\scripts\package.ps1 -BundleIcu
```

The zip contains everything needed: compiler, ICU data, runtime libraries, extension libraries, and extension contracts. Unzip and add to PATH.

### Static Linking

ts-aot statically links all dependencies (vcpkg triplet `x64-windows-static-md`). Compiled executables only depend on standard Windows system DLLs.

| Library | Purpose |
|---------|---------|
| libuv | Event loop and async I/O |
| ICU | Unicode string handling |
| OpenSSL | TLS/crypto |
| llhttp | HTTP parsing |
| c-ares | Async DNS |
| nghttp2 | HTTP/2 |
| zlib + brotli | Compression |
| libsodium | Cryptographic primitives |
| libtommath | BigInt support |
| spdlog + fmt | Logging |

## Running Tests

```bash
# Golden IR regression tests (compiler correctness)
python tests/golden_ir/runner.py tests/golden_ir

# Node.js API tests
python tests/node/run_tests.py

# npm package integration tests
python tests/npm/runner.py
```

## Documentation

- [Development Guide](.github/DEVELOPMENT.md) - Build system, debugging, workflow
- [TypeScript Conformance](docs/conformance/typescript-features.md) - 118/119 runtime features
- [ECMAScript Conformance](docs/conformance/ecmascript-features.md) - ES5-ES2024 coverage
- [Node.js Conformance](docs/conformance/nodejs-features.md) - 32 module API tables

## License

[License information]
