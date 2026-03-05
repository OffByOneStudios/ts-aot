---
layout: ../../layouts/DocsLayout.astro
title: Getting Started
description: Install and use the ts-aot TypeScript ahead-of-time compiler.
---

## Overview

ts-aot compiles TypeScript and JavaScript directly to native machine code via LLVM, producing standalone executables without requiring Node.js or V8 at runtime.

The entire project -- compiler, runtime, extensions, tests, and this site -- is written by [Claude](https://claude.ai), Anthropic's AI, pair-programming with a human via [Claude Code](https://claude.ai/code).

## Installation

### From a Distribution

Download the latest release from [GitHub Releases](https://github.com/OffByOneStudios/ts-aot/releases), unzip, and add to your PATH.

```bash
# Unzip ts-aot-win-x64.zip, add to PATH, then:
ts-aot hello.ts -o hello.exe
./hello.exe
```

### From Source

```bash
git clone https://github.com/OffByOneStudios/ts-aot.git
cd ts-aoc

# Install dependencies via vcpkg
vcpkg install --triplet x64-windows-static-md

# Configure and build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Prerequisites (Building from Source)

- **CMake** 3.20+
- **LLVM 18** with development headers
- **vcpkg** for C++ dependencies
- **C++20** compiler (MSVC 2022 on Windows)

## Your First Program

Create a file `hello.ts`:

```typescript
function user_main(): number {
    console.log("Hello from ts-aot!");
    return 0;
}
```

Compile and run:

```bash
ts-aot hello.ts -o hello.exe
./hello.exe
# Output: Hello from ts-aot!
```

> **Note:** All programs must define a `user_main()` function as the entry point. The compiler generates a `main()` that calls it.

## A Real Example

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

This compiles to a single native binary that reads the filesystem -- no Node.js needed at runtime.

## Optimization Levels

```bash
ts-aot app.ts -o app.exe          # O0 (default, fastest compile)
ts-aot app.ts -o app.exe -O2      # O2 (good balance)
ts-aot app.ts -o app.exe -O3      # O3 (maximum optimization)
```

## npm Package Support

ts-aot can compile real npm packages. Tested and passing:

dotenv, escape-string-regexp, eventemitter3, fast-deep-equal, is-number, is-plain-object, ms, nanoid, picomatch, semver, uuid.

```typescript
import semver from 'semver';

function user_main(): number {
    console.log(semver.valid('1.2.3'));   // '1.2.3'
    console.log(semver.gt('2.0.0', '1.0.0'));  // true
    return 0;
}
```

## Next Steps

- [Architecture](/docs/architecture/) -- How the compiler works
- [CLI Reference](/docs/cli/) -- All compiler flags
- [Conformance](/docs/conformance/) -- Feature coverage details
- [Limitations](/docs/limitations/) -- What's not supported yet
