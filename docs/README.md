# ts-aot Development Roadmap

**ts-aot** is an ahead-of-time compiler for TypeScript that produces native executables via LLVM.

This document outlines our three strategic pillars and the concrete work required for each.

---

## 1. Conformance

**Goal:** Achieve measurable compatibility with TypeScript, JavaScript (ECMAScript), and Node.js APIs.

### 1.1 TypeScript Language Conformance

**Validation:** Run the official TypeScript test suite (`tests/cases/conformance/`).

| Category | Status | Notes |
|----------|--------|-------|
| Basic types (number, string, boolean) | Done | |
| Arrays and tuples | Partial | Destructuring incomplete |
| Objects and interfaces | Partial | Optional chaining `?.` missing |
| Classes (inheritance, static, abstract) | Done | |
| Generics | Partial | Constraints, conditional types missing |
| Union/intersection types | Partial | Type narrowing incomplete |
| Enums | Not started | |
| Decorators | Not started | Stage 3 decorators |
| Namespaces | Not started | |
| Module systems (ESM, CJS) | Partial | `import`/`export` works, `require()` works |

**Action Items:**
- [ ] Download and integrate TypeScript conformance tests
- [ ] Build test harness to run `.ts` files and compare output
- [ ] Generate conformance percentage report
- [ ] Prioritize by usage frequency (generics, enums, decorators)

### 1.2 ECMAScript Conformance

**Validation:** Run [Test262](https://github.com/tc39/test262), the official ECMAScript test suite.

| Category | Status | Notes |
|----------|--------|-------|
| Primitives and operators | Done | |
| Control flow | Done | |
| Functions (arrow, rest, default params) | Done | |
| Closures and scoping | Done | Cell-based capture |
| Promises | Done | |
| async/await | Done | |
| Iterators/Generators | Not started | `function*`, `yield` |
| Proxy/Reflect | Not started | |
| Symbol | Partial | |
| WeakMap/WeakSet | Not started | |
| RegExp | Partial | Basic matching only |

**Action Items:**
- [ ] Set up Test262 harness with our compiler
- [ ] Start with "language" subset (skip Annex B, intl)
- [ ] Track pass rate over time
- [ ] Focus on ES2020+ features (nullish coalescing, optional chaining)

### 1.3 Node.js API Conformance

**Validation:** Run our Node.js test suite and compare against Node.js behavior.

| Module | Status | Coverage |
|--------|--------|----------|
| fs | Done | sync/async, streams, dirs |
| path | Done | Full POSIX/Windows |
| http/https | Done | Server, client, fetch |
| events | Done | EventEmitter full API |
| buffer | Done | alloc, from, toString |
| stream | Partial | Readable, Writable, Duplex |
| crypto | Partial | MD5 only |
| url | Done | URL class |
| util | Partial | format, inspect, types |
| process | Partial | argv, cwd, env, platform |
| timers | Done | setTimeout, setInterval |
| net | Partial | TCP client/server |
| child_process | Not started | |
| os | Not started | |
| zlib | Not started | |

**Current Test Suite:** 42 tests (28 TypeScript + 14 JavaScript), 100% passing

**Action Items:**
- [ ] Add missing module stubs with "not implemented" errors
- [ ] Expand test coverage per module (aim for Node.js test parity)
- [ ] Create API coverage report (functions implemented vs total)
- [ ] Implement high-priority missing: `child_process`, `os`, `zlib`

### 1.4 Regression Prevention

**Current Infrastructure:**
- Golden IR tests (`tests/golden_ir/`) - 90 tests
- Node.js API tests (`tests/node/`) - 42 tests

**Action Items:**
- [ ] Add GitHub Actions CI to run all tests on PR
- [ ] Block merges on test failures
- [ ] Add code coverage tracking
- [ ] Create "known failures" tracking (XFAIL tests)

---

## 2. Performance

**Goal:** Match or exceed Node.js (V8) performance for compute-bound workloads.

### 2.1 Benchmarking Infrastructure

**Action Items:**
- [ ] Create `benchmarks/` directory with standardized tests
- [ ] Implement benchmark runner with warmup, iterations, statistics
- [ ] Compare against Node.js on same machine
- [ ] Track performance over time (detect regressions)

**Proposed Benchmarks:**
| Benchmark | Category | Measures |
|-----------|----------|----------|
| fibonacci(40) | CPU | Function call overhead, recursion |
| array-sum-1M | CPU | Array iteration, numeric ops |
| string-concat-100K | Memory | String allocation, GC pressure |
| json-parse-large | I/O | Parser performance |
| http-hello-world | I/O | Request throughput |
| richards | CPU | Classic VM benchmark |
| deltablue | CPU | Constraint solver |

### 2.2 Parser Optimizations

**Current:** We use `dump_ast.js` (Node.js + TypeScript compiler) to parse.

**Problems:**
- Cold start overhead (~500ms)
- JSON serialization/deserialization cost
- Can't optimize for our specific needs

**Options:**
| Approach | Effort | Benefit |
|----------|--------|---------|
| Cache parsed AST | Low | Avoid re-parsing unchanged files |
| Use SWC parser (Rust) | Medium | 10-20x faster than tsc |
| Use oxc parser (Rust) | Medium | Fastest TS parser available |
| Write custom C++ parser | High | Full control, no dependencies |

**Action Items:**
- [ ] Benchmark current parse times for various file sizes
- [ ] Evaluate SWC/oxc integration feasibility
- [ ] Implement AST caching for incremental compilation

### 2.3 IR Generation Optimizations

**Current Issues:**
- Boxing/unboxing overhead for `any` types
- Runtime calls for simple operations
- No inlining of small runtime functions

**Optimization Opportunities:**
| Optimization | Impact | Difficulty |
|--------------|--------|------------|
| Inline `ts_value_make_int` etc. | High | Low |
| Unbox known-type variables | High | Medium |
| Specialize array methods for typed arrays | High | Medium |
| Monomorphize hot paths | Medium | High |
| Escape analysis for stack allocation | Medium | Medium |

**Action Items:**
- [ ] Profile generated code to find hotspots
- [ ] Implement IR-level inlining for boxing functions
- [ ] Add type-specialized array method codegen
- [ ] Track code size vs performance tradeoffs

### 2.4 LLVM Optimization Passes

**Current:** We use standard LLVM `-O2` optimization.

**Custom Pass Opportunities:**
| Pass | Purpose |
|------|---------|
| TsBoxElimination | Remove redundant box/unbox pairs |
| TsDevirtualization | Inline known method calls |
| TsGCOptimization | Reduce GC allocation frequency |
| TsBoundsCheckElimination | Remove proven-safe array bounds checks |

**Action Items:**
- [ ] Study LLVM pass infrastructure
- [ ] Implement TsBoxElimination pass
- [ ] Measure impact on benchmarks
- [ ] Consider using LLVM's PGO (Profile-Guided Optimization)

### 2.5 Runtime Optimizations

**Current Issues:**
- Boehm GC not optimal for JS allocation patterns
- Hash map implementation may have overhead
- String operations could use SIMD

**Action Items:**
- [ ] Profile runtime hotspots
- [ ] Evaluate alternative GC (generational, concurrent)
- [ ] Optimize TsMap for common access patterns
- [ ] Add SIMD string operations where beneficial

---

## 3. Developer Experience

**Goal:** Make ts-aot easy to use, debug, and integrate.

### 3.1 Documentation

**Current State:**
- `CLAUDE.md` - AI assistant instructions
- `.github/DEVELOPMENT.md` - Internal dev guide
- No public-facing documentation

**Action Items:**
- [ ] Create `docs/getting-started.md` - Installation and first program
- [ ] Create `docs/language-support.md` - What TypeScript features work
- [ ] Create `docs/nodejs-api.md` - Available Node.js modules
- [ ] Create `docs/building.md` - How to build from source
- [ ] Generate API reference from runtime headers

### 3.2 Landing Page / Website

**Purpose:** Public-facing site explaining the project.

**Content:**
- What is ts-aot?
- Why compile TypeScript to native?
- Performance comparisons
- Getting started guide
- API documentation
- Blog/changelog

**Action Items:**
- [ ] Choose static site generator (Astro, Next.js, plain HTML)
- [ ] Design simple landing page
- [ ] Host on GitHub Pages
- [ ] Add benchmarks with charts

### 3.3 Debug Adapter Protocol (DAP)

**Purpose:** Enable debugging in VS Code and other editors.

**Requirements:**
- Generate DWARF debug info in compiled binaries
- Implement DAP server that speaks to debugger
- Map source locations to native addresses
- Support breakpoints, stepping, variable inspection

**Action Items:**
- [ ] Emit LLVM debug metadata during codegen
- [ ] Verify `lldb` can read source locations
- [ ] Research DAP protocol specification
- [ ] Implement minimal DAP server (launch, breakpoints, step)
- [ ] Create VS Code extension for ts-aot debugging

### 3.4 Bundling and Distribution

**Current:** User must build from source with CMake + vcpkg.

**Goals:**
- Pre-built binaries for Windows, macOS, Linux
- Single executable distribution
- npm package for easy installation

**Action Items:**
- [ ] Set up GitHub Actions for multi-platform builds
- [ ] Create release workflow with binary uploads
- [ ] Consider static linking runtime
- [ ] Create npm wrapper package (`npx ts-aot`)
- [ ] Explore single-file bundling (embed runtime in compiler)

### 3.5 Editor Integration

**VS Code Extension Features:**
- Syntax highlighting (use existing TS grammar)
- Build task integration
- Error diagnostics from compiler
- Debug launch configuration
- Code lens for performance hints

**Action Items:**
- [ ] Create minimal VS Code extension
- [ ] Add problem matcher for compiler errors
- [ ] Integrate with DAP when available
- [ ] Consider LSP server for advanced features

---

## Priority Matrix

| Initiative | Impact | Effort | Priority |
|------------|--------|--------|----------|
| TypeScript conformance tests | High | Medium | P1 |
| GitHub Actions CI | High | Low | P1 |
| Benchmarking infrastructure | High | Low | P1 |
| Getting started docs | High | Low | P1 |
| Pre-built binaries | High | Medium | P1 |
| Test262 integration | High | High | P2 |
| Box elimination pass | High | Medium | P2 |
| DAP implementation | Medium | High | P2 |
| Alternative parser (SWC) | Medium | Medium | P2 |
| Landing page | Medium | Low | P2 |
| VS Code extension | Medium | Medium | P3 |
| Custom GC | High | Very High | P3 |
| LSP server | Medium | High | P3 |

---

## Metrics Dashboard (Proposed)

```
┌─────────────────────────────────────────────────────────────┐
│                    ts-aot Status Dashboard                   │
├─────────────────────────────────────────────────────────────┤
│ CONFORMANCE                                                  │
│   TypeScript:  ████████░░░░░░░░░░░░  42%  (1,234/2,891)     │
│   ECMAScript:  ██████░░░░░░░░░░░░░░  31%  (4,521/14,523)    │
│   Node.js:     ████████████░░░░░░░░  62%  (87/140 APIs)     │
│                                                              │
│ PERFORMANCE (vs Node.js v22)                                │
│   fibonacci:   ████████████████████  1.2x faster            │
│   array-sum:   ████████████░░░░░░░░  0.8x (slower)          │
│   http-rps:    ██████████████░░░░░░  1.1x faster            │
│                                                              │
│ TESTS                                                        │
│   Golden IR:   90/90 passing (100%)                         │
│   Node.js:     42/42 passing (100%)                         │
│   Last run:    2025-01-07 23:30 UTC                         │
└─────────────────────────────────────────────────────────────┘
```

---

## Next Steps

1. **This Week:**
   - Set up GitHub Actions CI for existing tests
   - Create `docs/getting-started.md`
   - Create `benchmarks/` with 3 initial benchmarks

2. **This Month:**
   - Download and integrate TypeScript conformance tests
   - Implement box elimination optimization
   - Set up multi-platform binary builds

3. **This Quarter:**
   - Achieve 50% TypeScript conformance
   - Implement basic DAP support
   - Launch landing page
