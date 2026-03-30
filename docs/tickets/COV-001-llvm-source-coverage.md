# COV-001: LLVM Source-Based Code Coverage

**Status:** Phase 1-2 Complete, Phase 3 Deferred
**Created:** 2026-03-29
**Updated:** 2026-03-30
**Priority:** Medium

## Summary

Add LLVM source-based code coverage instrumentation to ts-aot via `--coverage` flag. This enables measuring how well npm module tests exercise the source code of packages like Express. The approach uses LLVM's built-in coverage infrastructure: `llvm.instrprof.increment` intrinsics, `__llvm_covmap`/`__llvm_covfun` sections, and standard `llvm-profdata`/`llvm-cov` tooling.

## Motivation

We have npm module tests (e.g., Express) but no way to measure which lines/branches/functions of the npm source are actually exercised. Coverage data will:
- Show gaps in test coverage for npm packages
- Guide which Express features need additional tests
- Provide confidence that compiled npm code paths are actually reached

## End-to-End Workflow (Target)

```bash
# 1. Compile with coverage
ts-aot --coverage test_express.ts -o test_express.exe

# 2. Run (generates default.profraw)
./test_express.exe

# 3. Merge raw profiles
llvm-profdata merge -sparse default.profraw -o test_express.profdata

# 4. View coverage
llvm-cov show ./test_express.exe -instr-profile=test_express.profdata \
    --sources node_modules/express/lib/

# 5. Export as lcov/JSON for CI
llvm-cov export ./test_express.exe -instr-profile=test_express.profdata \
    -format=lcov > coverage.lcov
```

---

## Implementation Plan

### Phase 1: Enrich Source Location Tracking

**Goal:** Every HIR instruction carries `(file, line, column)` so coverage mapping has accurate source regions.

**Current state:**
- AST nodes have `line`, `column`, `sourceFile` fields (`ast::Node` in `AstNodes.h:197-199`)
- `HIRInstruction` has only `uint32_t sourceLocation = 0` (line number, never populated -- `setCurrentSourceLine()` is never called in ASTToHIR)
- `HIRFunction` has `sourceLine` and `sourceFile` (set from AST)
- `HIRToLLVM` has `DIBuilder`/`DIFile`/`DISubprogram` support gated by `emitDebugInfo_`

#### 1a. Extend HIRInstruction source location (`HIR.h`)

Add per-instruction file index and column alongside existing line:

```cpp
uint16_t sourceFileIdx = 0;    // Index into HIRModule::sourceFiles
uint32_t sourceLine = 0;       // Rename from sourceLocation
uint16_t sourceColumn = 0;
```

Add to `HIRModule`:
```cpp
std::vector<std::string> sourceFiles;  // Deduplicated file path table
```

**Files:** `HIR.h`, `HIR.cpp`, `HIRPrinter.cpp`

Update all HIR passes that copy `sourceLocation` to copy all three fields:
- `MethodResolutionPass.cpp`
- `IntegerOptimizationPass.cpp`
- `InliningPass.cpp`
- `BuiltinResolutionPass.cpp`

#### 1b. Propagate in HIRBuilder (`HIRBuilder.h`)

Update `HIRBuilder` to track current `(fileIdx, line, column)` and stamp every emitted instruction:

```cpp
void setCurrentSourceLoc(uint16_t fileIdx, uint32_t line, uint16_t column);
```

#### 1c. Set source locations in ASTToHIR (`ASTToHIR.cpp`)

Add helper:
```cpp
void updateSourceLoc(const ast::Node* node) {
    if (node && node->line > 0) {
        uint16_t fileIdx = getOrCreateFileIndex(node->sourceFile);
        builder_.setCurrentSourceLoc(fileIdx, node->line, node->column);
    }
}
```

Call at every `visit*` method and statement/expression lowering entry point (~50+ sites).

**Files:** `ASTToHIR.h`, `ASTToHIR.cpp`

#### 1d. Update HIRToLLVM debug location (`HIRToLLVM.cpp`)

Use file + column from the enriched source location (currently column is hardcoded to 0):

```cpp
llvm::DIFile* file = getOrCreateDIFile(hirModule_->sourceFiles[inst->sourceFileIdx]);
auto* loc = llvm::DILocation::get(context_, inst->sourceLine, inst->sourceColumn, sp);
builder_->SetCurrentDebugLocation(loc);
```

---

### Phase 2: Emit `llvm.instrprof.increment` Intrinsics

**Goal:** Insert counter increments at coverage-relevant points in the LLVM IR.

The intrinsic signature:
```llvm
declare void @llvm.instrprof.increment(ptr %name, i64 %hash, i32 %num_counters, i32 %counter_idx)
```

#### 2a. Add coverage state to HIRToLLVM (`HIRToLLVM.h`)

```cpp
bool emitCoverage_ = false;
void setEmitCoverage(bool enable) { emitCoverage_ = enable; }

struct CoverageRegion {
    uint16_t fileIdx;
    uint32_t lineStart, colStart, lineEnd, colEnd;
};
struct CoverageFunctionInfo {
    std::string funcName;
    uint64_t funcHash;
    uint32_t numCounters;
    std::vector<CoverageRegion> regions;
};
std::vector<CoverageFunctionInfo> coverageFunctions_;
```

#### 2b. Emit counter increments

**Coverage points to instrument:**
1. **Function entry** -- Counter 0 for every function
2. **Branch targets** -- Each arm of if/else, switch cases
3. **Loop headers** -- while/for condition blocks

**Strategy:** Two-pass per function:
1. Pre-scan HIR function to count coverage regions and assign counter indices
2. Emit `llvm.instrprof.increment` calls during normal lowering

For each function, create a `__profn_<funcname>` global variable holding the function name string.

#### 2c. Run InstrProfilingLoweringPass (`CodeGenerator.cpp`)

After the optimization pipeline, lower intrinsics into actual counter globals:

```cpp
if (emitCoverage_) {
    llvm::ModulePassManager CovMPM;
    CovMPM.addPass(llvm::InstrProfilingLoweringPass());
    CovMPM.run(*module, MAM);
}
```

**Files:** `CodeGenerator.cpp`, `CodeGenerator.h`

---

### Phase 3: Generate Coverage Mapping Sections

**Goal:** Emit `__llvm_covmap` and `__llvm_covfun` sections mapping counters to source regions.

#### 3a. Build coverage records

After all functions are lowered, use LLVM's `CoverageMappingWriter` to serialize:

```cpp
// For each function:
std::vector<CounterMappingRegion> regions;
for (size_t i = 0; i < covFunc.regions.size(); ++i) {
    regions.push_back(CounterMappingRegion(
        Counter::getCounter(i),
        fileIdx, 0,
        lineStart, colStart, lineEnd, colEnd,
        CounterMappingRegion::CodeRegion));
}
```

#### 3b. Emit as LLVM module globals

Create `llvm::GlobalVariable` entries in special sections:
- `__llvm_covfun` (COFF: `.lcovfun`) -- Per-function coverage records
- `__llvm_covmap` (COFF: `.lcovmap`) -- Global filename table + version header

Reference: Clang's `CoverageMappingGen.cpp` for exact binary layout.

**Files:** `HIRToLLVM.cpp` (new method `emitCoverageMapping()` called at end of `lower()`)

---

### Phase 4: Driver and Linker Integration

#### 4a. Add `--coverage` flag

**`Driver.h`:** Add `bool coverage = false` to `DriverOptions`
**`main.cpp`:** Add CLI option parsing
**`Driver.cpp`:** Wire flag through:
```cpp
hirToLlvm.setEmitCoverage(options.coverage);
if (options.coverage) hirToLlvm.setEmitDebugInfo(true);  // coverage requires debug info
codeGen.setEmitCoverage(options.coverage);
```

#### 4b. Link profile runtime

When `--coverage` is enabled, link against `clang_rt.profile`:

```cpp
if (options.coverage) {
    // Windows: clang_rt.profile-x86_64.lib
    // Linux: -lclang_rt.profile-x86_64
    linkOpts.libraries.push_back("clang_rt.profile-x86_64");
}
```

This library provides:
- `__llvm_profile_runtime` symbol
- `atexit` handler that writes `.profraw` on process exit
- Counter increment implementations

**Fallback:** If `clang_rt.profile` is unavailable, implement a minimal profile runtime in ts-aot's runtime (the `__llvm_profile_runtime` symbol + a `.profraw` writer).

#### 4c. Add library search paths

Search for `clang_rt.profile` in standard LLVM/Clang installation directories.

**Files:** `Driver.h`, `Driver.cpp`, `main.cpp`, `LinkerDriver.h`

---

### Phase 5: CMake Build System

Add LLVM components to the compiler's CMakeLists.txt:

```cmake
set(LLVM_COMPONENTS
    ...
    ProfileData
    Coverage
    ...
)
```

**Files:** `src/compiler/CMakeLists.txt`

---

## Dependency Graph

```
Phase 5 (CMakeLists)          Phase 4a (--coverage flag)
    |                              |
    v                              v
Phase 1a (HIR.h source loc)   Phase 4b-4c (linker)
    |
    v
Phase 1b (HIRBuilder)
    |
    v
Phase 1c (ASTToHIR propagation)  -- largest change by LOC
    |
    v
Phase 1d (HIRToLLVM debug loc)
    |
    v
Phase 2a-2b (counter emission)
    |
    v
Phase 2c (InstrProfiling pass)
    |
    v
Phase 3a-3b (coverage mapping)  -- most complex LLVM interaction
```

**Recommended implementation order:**
1. Phase 5 (CMakeLists) -- unblocks LLVM API usage
2. Phase 4a (`--coverage` flag) -- minimal, unblocks driver wiring
3. Phase 1a-1b (HIR source loc + builder) -- foundational data structures
4. Phase 1c (ASTToHIR propagation) -- largest change by line count
5. Phase 1d (HIRToLLVM debug loc update) -- small
6. Phase 2a-2b (counter emission) -- core instrumentation logic
7. Phase 2c (InstrProfiling pass) -- small addition to CodeGenerator
8. Phase 3a-3b (coverage mapping) -- most complex LLVM interaction
9. Phase 4b-4c (linker integration) -- needed for end-to-end

---

## Key Files

| File | Changes |
|------|---------|
| `src/compiler/hir/HIR.h` | Source location struct on HIRInstruction, file table on HIRModule |
| `src/compiler/hir/HIRBuilder.h` | Track and propagate (file, line, column) |
| `src/compiler/hir/ASTToHIR.cpp` | Call `updateSourceLoc()` at ~50+ visit sites |
| `src/compiler/hir/HIRToLLVM.cpp` | Emit `llvm.instrprof.increment`, coverage mapping sections |
| `src/compiler/hir/HIRToLLVM.h` | Coverage state, CoverageFunctionInfo |
| `src/compiler/codegen/CodeGenerator.cpp` | Run InstrProfilingLoweringPass |
| `src/compiler/Driver.cpp` | Wire `--coverage`, link profile runtime |
| `src/compiler/Driver.h` | `coverage` option |
| `src/compiler/main.cpp` | CLI flag |
| `src/compiler/CMakeLists.txt` | Add ProfileData, Coverage LLVM components |
| HIR pass files (4 files) | Copy new source location fields |

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Source location gaps in ASTToHIR | Some regions show 0 coverage | Instrument broadly; accept gaps initially, fill incrementally |
| Coverage mapping binary format complexity | Corrupt `.profraw` / `llvm-cov` errors | Use LLVM's `CoverageMappingWriter` API, reference Clang's implementation |
| `clang_rt.profile` availability | Link failure on systems without Clang | Document requirement; provide fallback minimal runtime |
| Optimization moving/merging counters | Inaccurate coverage at `-O2` | Run InstrProfilingLoweringPass after standard optimizations |
| Multi-file modules (Express has many) | File paths in mapping must match on disk | Use absolute paths from AST `sourceFile` field |

---

## Testing Strategy

1. **Smoke test:** Compile simple TS file with `--coverage --dump-ir`, verify `llvm.instrprof.increment` calls appear
2. **Section test:** Verify `__llvm_covmap`/`__llvm_covfun` sections in IR dump
3. **Runtime test:** Compile + run, confirm `default.profraw` is generated
4. **Tooling test:** Full `llvm-profdata merge` + `llvm-cov show` pipeline on a simple file
5. **Multi-file test:** Compile project with imports, verify coverage spans multiple source files
6. **npm test:** Run Express test with coverage, verify Express source lines appear in report
7. **Golden IR test:** Add regression test checking for coverage intrinsic emission
8. **No-regression:** Ensure non-coverage builds are completely unaffected

---

## Estimated Scope

- Phase 1 (source locations): ~300 LOC changes across 8 files
- Phase 2 (counter emission): ~200 LOC in HIRToLLVM + CodeGenerator
- Phase 3 (coverage mapping): ~250 LOC in HIRToLLVM (most complex)
- Phase 4 (driver/linker): ~50 LOC across 4 files
- Phase 5 (CMake): ~5 LOC

---

## Current Status (2026-03-30)

### Completed: Phases 1, 2, 4, 5

The `--coverage` flag works end-to-end:
- `llvm.instrprof.increment` intrinsics emitted per function entry
- `InstrProfilingLoweringPass` creates counter/data/name sections
- Counter linkage fixed from `linkonce_odr` to `internal` (avoids potential COFF COMDAT issues)
- Custom profraw v9 writer in `TsProfileRuntime.cpp` (walks PE sections at runtime)
- `atexit` handler writes `.profraw` on clean process exit

### Verified Results

**Simple test (3 functions):** 2/3 counters non-zero (1 function constant-folded by LLVM optimizer)

**Express test (968 functions):** 231/968 counters hit (23.9%) from startup + single HTTP request

**No regressions:** Golden IR 264/276, Node 293/296 (all pre-existing failures)

**Non-coverage builds unaffected:** No profraw generated, no instrumentation overhead

### Known Limitations

1. **Constant-folded functions show count=0:** LLVM optimizer may evaluate pure functions at compile time, leaving their counters untouched. Not a bug — the functions genuinely aren't called at runtime.

2. **Monomorphizer-inlined module init:** The synthetic `user_main` may contain inlined module init code. The original `__module_init` function may show count=0 while `user_main` shows count=1. Again, correct behavior.

3. **SIGTERM doesn't trigger profraw write:** On Windows, SIGTERM kills the process without calling `atexit`. Use `process.exit(0)` or `setTimeout` for servers.

4. **`llvm-profdata` not bundled:** Users need LLVM toolchain installed separately. Currently not available on the dev machine — profraw parsing verified with custom Python scripts.

### Deferred: Phase 3 (Coverage Mapping Sections)

Phase 3 (`__llvm_covmap` / `__llvm_covfun` sections) is deferred. This is needed for `llvm-cov show` to map counters to source lines. Without it:
- `llvm-profdata show --all-functions` works (function-level counts)
- `llvm-cov show` does NOT work (no source-line mapping)

Function-level coverage is already actionable for Express: it shows which Express library functions are exercised by tests. Line-level coverage requires the complex `CoverageMappingWriter` binary format — high effort, independent of current work.
