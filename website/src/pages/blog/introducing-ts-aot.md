---
layout: ../../layouts/BlogLayout.astro
title: Introducing ts-aot
date: 2026-03-05
description: An LLVM-based ahead-of-time compiler for TypeScript. Why we built it and what it can do.
author: ts-aot team
---

What if you could compile TypeScript to a native executable? No Node.js runtime, no V8, no JIT warmup -- just a binary you can ship and run.

That's what ts-aot does. And it was built entirely by an AI.

## What is ts-aot?

ts-aot is an ahead-of-time compiler that takes TypeScript (or JavaScript) source code and produces standalone native executables via LLVM. The output is a single binary with no external runtime dependencies.

Every line of code -- the compiler, the runtime, the test suites, and this website -- was written by [Claude](https://claude.ai), Anthropic's AI assistant, pair-programming with a human collaborator via [Claude Code](https://claude.ai/code). The human provided direction on architecture and priorities; Claude wrote the implementation.

```bash
ts-aot app.ts -o app.exe
./app.exe
```

The compiler includes a hand-written C++ parser, a type-aware analyzer that drives optimized codegen, an SSA-based intermediate representation with 8 optimization passes, and LLVM 18 as the backend. All of it AI-generated.

## Why?

TypeScript is excellent for writing applications, but deploying them still means shipping Node.js or bundling a runtime. For CLI tools, serverless functions, and microservices, that overhead matters:

- **Cold start**: Node.js needs to parse JavaScript, build an AST, and JIT-compile it. A native binary just runs.
- **Distribution**: A single executable is simpler than a `node_modules/` tree.
- **Predictable performance**: No JIT tier transitions means consistent latency.

## The numbers

We track conformance rigorously:

- **99% TypeScript features** (118/119 runtime features)
- **96% ECMAScript features** (ES5 through ES2024)
- **99% Node.js APIs** (1,031/1,040 across 32 modules)

264 golden IR tests, 286 Node.js API tests, and 11 npm package integration tests all pass.

### Performance

ts-aot starts **2x faster** than Node.js for minimal programs and runs recursive computation **1.7x faster**. V8's JIT still wins on long-running array and string operations -- we're honest about that.

## How it works

The compiler pipeline:

1. **Parser** -- Hand-written C++ recursive descent (~4,300 LOC)
2. **Type Analyzer** -- Inference, monomorphization, module resolution
3. **HIR** -- SSA-based IR with optimization passes (constant folding, escape analysis, inlining, dead code elimination, etc.)
4. **LLVM IR** -- Type-driven code generation via LLVM 18
5. **Linking** -- Static linking with runtime (custom GC, ICU strings, libuv async I/O)

TypeScript types aren't just erased -- they drive code generation. A `number[]` compiles to optimized array operations, not boxed `any` values.

## What works

Real programs compile and run:

```typescript
import * as fs from 'fs';
import * as http from 'http';

function user_main(): number {
    const server = http.createServer((req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.end('Hello from ts-aot!');
    });
    server.listen(3000);
    console.log('Listening on :3000');
    return 0;
}
```

This compiles to a single binary that serves HTTP -- using the same `http` module API you'd use in Node.js.

npm packages work too: semver, uuid, nanoid, dotenv, picomatch, and more compile and pass their test suites.

## Built by Claude

ts-aot is an experiment in AI-assisted software engineering. The project comprises roughly 100,000 lines of C++20, including:

- A hand-written recursive descent parser (~4,300 LOC)
- A custom mark-sweep garbage collector with generational nursery
- 8 SSA-based optimization passes (constant folding, escape analysis, inlining, etc.)
- 32 Node.js API extension modules
- 560+ passing tests across three test suites
- NaN-boxing, flat inline-slot objects, and type-driven code generation

All of it was written by Claude through conversation -- the human collaborator set the direction and reviewed the output, but the code itself is AI-generated. The project serves as a concrete data point for what AI pair-programming can produce on a complex systems programming task.

This website is also Claude-generated: an Astro static site with zero JS frameworks, CSS-only theming, and pure `.astro` components.

## What's next

ts-aot is a working compiler with strong conformance numbers, but there's more to do:

- **Linux and macOS support** (currently Windows x64 only)
- **Remaining ES2024 features** (atomics, SharedArrayBuffer)
- **More npm package validation**
- **Performance work** on array/string operations

## Try it

Check out the [getting started guide](/docs/) or browse the source on [GitHub](https://github.com/OffByOneStudios/ts-aot).

```bash
ts-aot hello.ts -o hello && ./hello
```
