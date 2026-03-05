---
layout: ../../layouts/DocsLayout.astro
title: Conformance Dashboard
description: ts-aot feature coverage for TypeScript, ECMAScript, and Node.js APIs.
---

## Overview

ts-aot tracks conformance across three dimensions: TypeScript language features, ECMAScript specification, and Node.js built-in APIs.

## TypeScript Features -- 99%

**118 out of 119 runtime features implemented.** 42 additional features are type-only (erased at compile time).

| Category | Implemented | Type-Only |
|----------|-------------|-----------|
| Basic Types | 14 | 2 |
| Variable Declarations | 6 | 0 |
| Interfaces | 6 | 4 |
| Classes | 19 | 1 |
| Functions | 11 | 0 |
| Generics | 5 | 2 |
| Modules | 9 | 2 |
| Enums | 7 | 0 |
| Type Narrowing | 9 (+1 partial) | 0 |
| Union/Intersection | 4 | 0 |
| Decorators | 6 | 0 |
| JSX | 3 | 1 |
| Iterators/Generators | 8 | 0 |
| Type Assertions | 4 | 0 |

**Partial:** `asserts` keyword (syntax compiles, runtime narrowing not supported).

**N/A:** Namespaces (legacy pattern), mixins (requires dynamic class construction).

## ECMAScript Features -- 96%

**221 out of 230 features implemented** across ES5 through ES2024.

| ES Version | Implemented | Total | Coverage |
|------------|-------------|-------|----------|
| ES5 | 47 | 47 | 100% |
| ES2015 (ES6) | 108 | 112 | 96% |
| ES2016 | 2 | 2 | 100% |
| ES2017 | 8 | 9 | 89% |
| ES2018 | 8 | 8 | 100% |
| ES2019 | 9 | 9 | 100% |
| ES2020 | 9 | 10 | 90% |
| ES2021 | 6 | 6 | 100% |
| ES2022 | 10 | 10 | 100% |
| ES2023 | 8 | 8 | 100% |
| ES2024 | 6 | 9 | 67% |

**Not yet implemented:** Shared memory/atomics, `Iterator.from()`, RegExp `/v` flag, growable SharedArrayBuffer, `Atomics.waitAsync()`.

## Node.js APIs -- 99%

**1,031 out of 1,040 APIs implemented** across 32 built-in modules.

| Module | Coverage | Module | Coverage |
|--------|----------|--------|----------|
| assert | 100% | http | 100% |
| async_hooks | 100% | http2 | 100% |
| buffer | 100% | https | 100% |
| child_process | 100% | net | 100% |
| cluster | 100% | os | 100% |
| console | 100% | path | 100% |
| crypto | 100% | process | 100% |
| dgram | 100% | querystring | 100% |
| dns | 100% | readline | 100% |
| events | 100% | stream | 100% |
| fs | 100% | string_decoder | 100% |
| inspector | 100% | timers | 100% |
| module | 100% | tls | 100% |
| perf_hooks | 100% | url | 100% |
| zlib | 100% | util | 100% |
| tty | 100% | | |

**N/A modules:** `domain` (deprecated), `repl` (AOT incompatible), `v8` (no V8), `vm` (AOT incompatible), `wasi` (not planned), `worker_threads` (use cluster instead).

## Test Suites

| Suite | Pass Rate | Description |
|-------|-----------|-------------|
| Golden IR | 264/264 (100%) | Compiler regression tests |
| Node.js API | 286/296 (96.6%) | Node.js API conformance |
| npm Packages | 11/11 (100%) | Real-world package compilation |

### Verified npm Packages

dotenv, escape-string-regexp, eventemitter3, fast-deep-equal, is-number, is-plain-object, ms, nanoid, picomatch, semver, uuid.

## Detailed Matrices

Full feature-by-feature tracking is maintained in the repository:

- [TypeScript Features](https://github.com/OffByOneStudios/ts-aot/blob/master/docs/conformance/typescript-features.md)
- [ECMAScript Features](https://github.com/OffByOneStudios/ts-aot/blob/master/docs/conformance/ecmascript-features.md)
- [Node.js APIs](https://github.com/OffByOneStudios/ts-aot/blob/master/docs/conformance/nodejs-features.md)
