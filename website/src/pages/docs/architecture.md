---
layout: ../../layouts/DocsLayout.astro
title: Architecture
description: How the ts-aot compiler pipeline and runtime work.
---

## Compiler Pipeline

ts-aot uses a multi-stage compilation pipeline:

```
TypeScript Source
    |
    v
Native C++ Parser       -- Hand-written recursive descent (~4,300 LOC)
    |
    v
AST                     -- TypeScript-compatible abstract syntax tree
    |
    v
Type Analyzer           -- Type inference, monomorphization, module resolution
    |
    v
HIR (High-level IR)     -- SSA-based intermediate representation
    |   |
    |   v
    |  HIR Optimization Passes (8 passes)
    |
    v
LLVM IR                 -- Generated via LLVM 18 C++ API
    |
    v
LLVM Optimization       -- O0-O3 pipeline with runtime-aware annotations
    |
    v
Native Object Code      -- x86-64 machine code
    |
    v
LLD Linker              -- Link with runtime + extensions
    |
    v
Standalone Executable
```

### Parser

A hand-written C++ recursive descent parser (~4,300 lines) handles TypeScript and JavaScript syntax. It produces a TypeScript-compatible AST without depending on Node.js or the TypeScript compiler.

### Type Analyzer

The analyzer performs type inference, resolves modules, and monomorphizes generic functions. TypeScript types drive code generation -- typed code gets optimized paths while `any` falls back to boxed values.

### HIR Pipeline

The High-level Intermediate Representation uses SSA (Static Single Assignment) form with 8 optimization passes:

| Pass | Purpose |
|------|---------|
| TypePropagation | Propagate type information across SSA |
| IntegerOptimization | Specialize number operations to i64 |
| ConstantFolding | Evaluate compile-time constants |
| DeadCodeElimination | Remove unreachable code |
| Inlining | Inline small functions |
| EscapeAnalysis | Stack-allocate non-escaping objects |
| MethodResolution | Devirtualize method calls |
| BuiltinResolution | Lower built-in calls (Math, Array, etc.) |

### LLVM Backend

The HIR is lowered to LLVM IR using the LLVM 18 C++ API (opaque pointers). The full LLVM optimization pipeline (O0-O3) is available, with runtime-aware function annotations that help LLVM's passes.

## Runtime

Every compiled executable links against a C++ runtime library:

| Component | Description |
|-----------|-------------|
| **TsGC** | Custom mark-sweep GC with generational nursery (4MB), conservative stack scanning |
| **TsString** | ICU-based Unicode strings with NFC normalization |
| **TsArray** | Dynamic arrays with typed specialization |
| **TsMap / TsSet** | Hash-based collections |
| **TsFlatObject** | Shape-based objects with NaN-boxed inline slots and vtable dispatch |
| **TsClosure** | Closure capture with cell-based upvalues |
| **TsPromise** | Promise/async-await via libuv event loop |
| **TsRegExp** | ICU-based regular expressions |
| **TsBigInt** | Arbitrary-precision integers via libtommath |
| **TsProxy / TsReflect** | ES6 Proxy and Reflect |

### Memory Management

ts-aot uses a custom garbage collector (not Boehm GC):

- **Mark-sweep collector** with 16 size classes (8-4096 bytes)
- **Generational nursery** (4MB) with pin-based promotion
- **Conservative stack scanning** via setjmp register flushing
- **Escape analysis** enables stack allocation of short-lived objects
- **NaN-boxing** for compact value representation

### Extension System

Node.js APIs are implemented as C++ extension libraries with JSON contracts:

```
extensions/node/
├── fs/          # File system (sync + async)
├── http/        # HTTP server and client
├── net/         # TCP sockets
├── crypto/      # Hashing, HMAC, random, KDF
├── stream/      # Readable, Writable, Duplex, Transform
└── ...          # 32 modules total
```

Each module has an `.ext.json` contract defining function signatures, which enables the compiler to generate direct native calls instead of dynamic dispatch.

### Static Linking

All dependencies are statically linked:

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
