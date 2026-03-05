---
layout: ../../layouts/DocsLayout.astro
title: CLI Reference
description: ts-aot compiler command-line flags and options.
---

## Usage

```bash
ts-aot <input.ts> [options]
```

The input file can be `.ts`, `.tsx`, `.js`, or `.jsx`.

## Options

### Output

| Flag | Description |
|------|-------------|
| `-o <file>` | Output executable path. Required. |

### Optimization

| Flag | Description |
|------|-------------|
| `-O0` | No optimization (default). Fastest compilation. |
| `-O1` | Basic optimizations. |
| `-O2` | Standard optimizations. Good balance of speed and compile time. |
| `-O3` | Maximum optimization. Best runtime performance. |

### Debug

| Flag | Description |
|------|-------------|
| `-g` | Generate debug symbols (.pdb on Windows). |
| `--dump-ir` | Print generated LLVM IR to stdout. |
| `--dump-hir` | Print HIR (intermediate representation) to stdout. |
| `--dump-types` | Print inferred types to stdout. |

### Advanced

| Flag | Description |
|------|-------------|
| `--bundle-icu` | Embed ICU data in executable (~29MB larger, no external `icudt74l.dat` needed). |
| `--gc-statepoints` | Enable precise GC roots via LLVM statepoints (experimental). |
| `--legacy-parser` | Use Node.js-based parser instead of native C++ parser. |

## Examples

```bash
# Basic compilation
ts-aot app.ts -o app.exe

# Optimized build
ts-aot app.ts -o app.exe -O2

# Debug build with symbols
ts-aot app.ts -o app.exe -g

# Inspect generated IR
ts-aot app.ts --dump-ir -o /dev/null

# Check type inference
ts-aot app.ts --dump-types -o /dev/null

# Self-contained executable (no external ICU data file)
ts-aot app.ts -o app.exe --bundle-icu
```

## Entry Point

All programs must define a `user_main()` function:

```typescript
function user_main(): number {
    // Your program here
    return 0;  // Exit code
}
```

The compiler generates a `main()` function that initializes the runtime (GC, libuv event loop, ICU) and calls `user_main()`.

## Supported File Types

| Extension | Handling |
|-----------|----------|
| `.ts` | TypeScript with full type analysis |
| `.tsx` | TypeScript + JSX |
| `.js` | JavaScript (dynamic/slow path) |
| `.jsx` | JavaScript + JSX |

TypeScript files get optimized codegen paths (typed arrays, integer specialization, flat objects). JavaScript files still compile but fall back to boxed `any` values for untyped operations.
