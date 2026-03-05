---
layout: ../../layouts/DocsLayout.astro
title: Limitations
description: What ts-aot doesn't support and where Node.js is a better fit.
---

## Platform

- **Windows x64 only** for now. Linux and macOS are planned but not yet supported.
- Compiled binaries are native Windows executables (PE format).

## Language Limitations

### No `eval()` or `new Function()`

AOT compilation is fundamentally incompatible with runtime code evaluation. Code like this will not work:

```typescript
eval("console.log('hello')");           // Not supported
const fn = new Function("x", "return x + 1");  // Not supported
```

### No Dynamic `require()`

Only static `require()` with string literals works:

```typescript
const fs = require('fs');                    // OK
const mod = require('./lib/' + name);        // Not supported
const mod = require(dynamicVariable);        // Not supported
```

Dynamic `import()` works only with string literal specifiers (resolved at compile time).

### Entry Point

All programs must define a `user_main()` function. There is no implicit top-level execution:

```typescript
// This won't work as expected:
console.log("hello");

// Use this instead:
function user_main(): number {
    console.log("hello");
    return 0;
}
```

### `asserts` Keyword

The `asserts` keyword compiles (syntax is accepted) but does not perform runtime type narrowing.

## Performance Characteristics

### Where ts-aot is Faster

| Workload | Speedup |
|----------|---------|
| Cold start (minimal program) | ~2x |
| Recursive computation (fib) | ~1.7x |
| Short-lived CLI tools | Significant |
| Predictable latency | No JIT variance |

### Where Node.js is Faster

| Workload | Node.js Advantage |
|----------|-------------------|
| Array map/filter/reduce | 3-15x (V8's optimized array internals) |
| String concatenation | Significant (V8's rope optimization) |
| Template literal heavy code | ~15x |
| Long-running compute | JIT specializes hot paths |

V8's JIT compiler excels at optimizing hot loops over time, especially for array and string operations. ts-aot's advantage is in startup time and predictable performance.

## Binary Size

Compiled executables are larger than their Node.js source:

- A minimal "hello world" compiles to ~5-10MB (includes the runtime, GC, ICU subset, and libuv).
- Using `--bundle-icu` adds ~29MB for the full ICU data file (enables all locales without an external file).

This is the trade-off for self-contained binaries with no external runtime dependencies.

## Unsupported Node.js Modules

| Module | Reason |
|--------|--------|
| `vm` | Requires runtime code evaluation |
| `repl` | Interactive evaluation loop |
| `v8` | V8-specific internals |
| `worker_threads` | Architectural incompatibility (use `cluster` instead) |
| `wasi` | WebAssembly -- not planned |
| `domain` | Deprecated |

## npm Compatibility

ts-aot can compile many npm packages, but not all. Packages that work:

- Pure JavaScript/TypeScript logic
- Packages using supported Node.js APIs
- Packages with `.js` entry points

Packages that won't work:

- Native addons (`.node` files)
- Packages that use `eval()` or `new Function()`
- Packages relying on `vm` module
- Packages with dynamic `require()` based on runtime values

### Verified Working Packages

dotenv, escape-string-regexp, eventemitter3, fast-deep-equal, is-number, is-plain-object, ms, nanoid, picomatch, semver, uuid.

## TypeScript Differences

ts-aot is not a drop-in replacement for `tsc` + Node.js. Key differences:

- **Monomorphization** instead of type erasure -- generic functions are specialized per type
- **NaN-boxing** for values -- all JS values fit in 64 bits
- **Flat objects** with inline slots instead of hidden classes
- **Custom GC** instead of V8's Orinoco collector
- **ICU regex** instead of V8's Irregexp
