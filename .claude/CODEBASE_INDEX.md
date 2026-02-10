# ts-aot Codebase Index

Quick reference guide for navigating the TypeScript AOT compiler codebase.

**Last Updated:** 2026-01-06
**Current Phase:** Phase 19 - Ecosystem Validation
**Status:** Epic 106 (Golden IR Tests) completed - 93/120 tests passing

---

## Quick Stats

- **Language:** C++20
- **Build:** CMake + vcpkg
- **LLVM:** Version 18 (opaque pointers)
- **GC:** Boehm (bdwgc)
- **Async:** libuv event loop
- **Strings:** ICU Unicode
- **Lines of Code:** ~50k+ C++ (estimated)
- **Test Coverage:** 93 golden IR tests, ad-hoc examples

---

## Directory Structure at a Glance

```
ts-aoc/
├── src/
│   ├── compiler/          # Host compiler (C++20)
│   │   ├── ast/          # AST definitions & loading (3 files)
│   │   ├── analysis/     # Type inference (27 files)
│   │   └── codegen/      # LLVM IR generation (25+ files)
│   └── runtime/          # Target runtime library
│       ├── include/      # Headers (50+ files)
│       └── src/          # Implementation (40+ files)
├── tests/
│   └── golden_ir/        # Regression tests (93 tests)
│       ├── typescript/   # Typed code (63 tests, 70% pass)
│       └── javascript/   # Dynamic code (30 tests, 100% pass!)
├── examples/             # Ad-hoc test programs
├── docs/
│   └── phase19/          # Current phase documentation
├── scripts/
│   └── dump_ast.js       # TS → JSON parser
└── build/                # CMake output (gitignored)
```

---

## Compilation Pipeline

```
┌─────────────┐
│ file.ts     │
└──────┬──────┘
       ↓
┌─────────────────────────────────┐
│ dump_ast.js (Node.js)           │  Parse TypeScript
│ → file.ts.ast.json              │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ AstLoader::load()               │  JSON → AST
│ → ast::Program*                 │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ Analyzer::analyze()             │  Type Inference
│ → Symbol table + types          │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ Monomorphizer                   │  Generic specialization
│ → Instantiations list           │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ ASTToHIR::lower()               │  HIR
│ → HIR module                    │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ HIR Passes                      │  Optimize HIR
│ → Optimized HIR module          │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ HIRToLLVM::lower()              │  LLVM IR
│ → .ll file                      │
└──────┬──────────────────────────┘
       ↓
┌─────────────────────────────────┐
│ LLVM Linker                     │  Link + optimize
│ + tsruntime.lib                 │
│ → file.exe                      │
└─────────────────────────────────┘
```

---

## Key Classes & Files

### Compiler Core

| Class/File | Location | Purpose |
|------------|----------|---------|
| `Driver` | `src/compiler/Driver.cpp` | Orchestrates compilation flow |
| `AstLoader` | `src/compiler/ast/AstLoader.cpp` | JSON → AST conversion |
| `Analyzer` | `src/compiler/analysis/Analyzer.h` | Type inference engine |
| `Type` | `src/compiler/analysis/Type.h` | Type system (25+ kinds) |
| `SymbolTable` | `src/compiler/analysis/SymbolTable.h` | Scope & symbol tracking |
| `ASTToHIR` | `src/compiler/hir/ASTToHIR.cpp` | AST to HIR lowering |
| `HIRToLLVM` | `src/compiler/hir/HIRToLLVM.cpp` | HIR to LLVM IR lowering |
| `LoweringRegistry` | `src/compiler/hir/LoweringRegistry.cpp` | Builtin call specifications |

### Runtime Core

| Class | Header | Implementation | Purpose |
|-------|--------|----------------|---------|
| `TsObject` | `TsObject.h` | `TsObject.cpp` | Base class, property access |
| `TsValue` | `TsObject.h` | `TsObject.cpp` | Tagged union for boxing |
| `TsString` | `TsString.h` | `TsString.cpp` | ICU-based strings |
| `TsArray` | `TsArray.h` | `TsArray.cpp` | Dynamic arrays |
| `TsMap` | `TsMap.h` | `TsMap.cpp` | Hash maps |
| `TsSet` | `TsSet.h` | `TsSet.cpp` | Sets |
| `TsBuffer` | `TsBuffer.h` | `TsBuffer.cpp` | Binary buffers |
| `TsEventEmitter` | `TsEventEmitter.h` | `node/EventEmitter.cpp` | Event system |
| `TsSocket` | `TsSocket.h` | `node/Socket.cpp` | TCP networking |
| `TsReadable/Writable` | Stream headers | `node/*.cpp` | Stream interfaces |

---

## Finding Things Fast

### "Where is [feature] implemented?"

**TypeScript Feature:**
1. **Classes** → `analysis/Analyzer_Classes.cpp` → `hir/ASTToHIR.cpp` → `hir/HIRToLLVM.cpp`
2. **Functions** → `analysis/Analyzer_Functions.cpp` → `hir/ASTToHIR.cpp` → `hir/HIRToLLVM.cpp`
3. **Expressions** → `analysis/Analyzer_Expressions.cpp` → `hir/ASTToHIR.cpp` → `hir/HIRToLLVM.cpp`
4. **Type inference** → `analysis/Type.h` + `Analyzer_*.cpp`

**Node.js API:**
1. **Types** → `analysis/Analyzer_StdLib_<Module>.cpp`
2. **Lowering** → `hir/LoweringRegistry.cpp`
3. **Runtime** → `src/runtime/src/node/<module>.cpp` or `src/runtime/src/Ts<Type>.cpp`

**Examples:**
- **fs.readFileSync** → `Analyzer_StdLib_FS.cpp` (types) → `LoweringRegistry.cpp` (lowering) → `node/fs.cpp` (impl)
- **Array.map** → `Analyzer_Expressions_Calls.cpp` (typing) → `LoweringRegistry.cpp` (lowering) → `TsArray.cpp::Map()`
- **Classes** → `Analyzer_Classes.cpp` (analysis) → `ASTToHIR.cpp` + `HIRToLLVM.cpp` (codegen)

### "Where is [symbol] defined?"

Use the `/ctags-search` skill:
```
"Where is ts_array_get defined?"
```

Returns: `src/runtime/src/TsArray.cpp:145`

### "Why did [test] fail?"

Use the `/golden-ir-tests` skill:
```
"Run the golden tests for arrays"
```

Or manually:
```bash
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/
```

### "My exe crashed, what happened?"

Use the `/auto-debug` skill:
```
"Debug the crash in examples/test.exe"
```

Or manually:
```powershell
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe
```

---

## Node.js API Coverage

### ✅ Fully Implemented

| Module | Key APIs | Files |
|--------|----------|-------|
| **fs** | readFile, writeFile, stat, mkdir, readdir | `Analyzer_StdLib_FS.cpp`, `node/fs.cpp` |
| **path** | join, resolve, dirname, basename, extname | `Analyzer_StdLib_Path.cpp`, `node/path.cpp` |
| **events** | EventEmitter (on, once, emit, removeListener) | `Analyzer_StdLib_Events.cpp`, `node/EventEmitter.cpp` |
| **stream** | Readable, Writable, Duplex, pipe() | `Analyzer_StdLib_Stream.cpp`, `node/Writable.cpp`, etc. |
| **net** | Socket, Server, createConnection, createServer | `Analyzer_StdLib_Net.cpp`, `node/Socket.cpp` |
| **http** | Server, request, IncomingMessage, ServerResponse | `Analyzer_StdLib_HTTP.cpp`, `TsHttp.cpp`, `node/Socket.cpp` |
| **https** | TLS variants of http | Same as http + `node/SecureSocket.cpp` |
| **buffer** | Buffer.alloc, Buffer.from, read/write methods | `Analyzer_StdLib_Buffer.cpp`, `TsBuffer.cpp` |
| **process** | argv, cwd, exit, env, stdout, stderr | `Analyzer_StdLib_Process.cpp`, `Primitives.cpp` |

### ⚠️ Partially Implemented

| Module | Status | Files |
|--------|--------|-------|
| **util** | inspect, format, inherits | `Analyzer_StdLib_Util.cpp`, `TsUtil.cpp` |
| **url** | URL parsing, URLSearchParams | `TsURL.cpp` |
| **crypto** | Basic hashing (MD5, SHA1, HMAC) | `Crypto.cpp` |
| **TextEncoding** | TextEncoder, TextDecoder | `TsTextEncoding.cpp` |
| **JSON** | parse, stringify | `TsJSON.cpp` |

### ❌ Not Implemented

- **timers** (setTimeout, setInterval) - No async scheduling
- **child_process** - No subprocess spawning
- **cluster** - No worker threads
- **os** - No OS info
- **querystring** - Not needed yet
- **vm** - No sandboxing

---

## TypeScript Feature Status

### ✅ Well-Supported

**Core:**
- Variables (let, const, var)
- Functions (declaration, expression, arrow)
- Classes (fields, methods, constructors, static, inheritance)
- Control flow (if/else, for, while, switch)
- Operators (all arithmetic, logical, comparison)
- Closures (captured variables, cells)
- Generics (via monomorphization)

**Arrays:**
- Literals, push/pop/shift/unshift
- map, filter, reduce, forEach
- find, findIndex, indexOf, includes
- slice, concat, join, reverse, sort
- at, flat, flatMap

**Strings:**
- All basic methods (substring, slice, charAt, indexOf, etc.)
- Trim, repeat, pad, startsWith, includes
- Split, replace, match (regex)

**Objects:**
- Literals, property access (dot/bracket)
- Object.keys, Object.values, Object.entries
- Object.assign (shallow copy)
- Spread operator

### ⚠️ Partial Support

- **Async/Await** - Syntax works, some Promise operations missing
- **Error Handling** - throw works, try/catch NOT implemented
- **BigInt** - Type exists, operators incomplete
- **Symbol** - Basic support, iterator protocol missing
- **RegExp** - Basic matching, advanced features missing

### ❌ Not Implemented

- **try/catch/finally** - No exception handling
- **Decorators** (@decorator)
- **Generators** (function*, yield)
- **Async Iterators** (for await)
- **Proxy/Reflect** - No metaprogramming
- **TypedArray** - Uint8Array, etc.
- **DataView** - Binary data views

---

## Critical Patterns

### Memory Allocation (MANDATORY)

```cpp
// ❌ WRONG - not GC tracked
TsMyClass* obj = new TsMyClass();

// ✅ CORRECT - GC tracked
void* mem = ts_alloc(sizeof(TsMyClass));
TsMyClass* obj = new (mem) TsMyClass();
```

### Safe Casting (Virtual Inheritance)

```cpp
// ❌ WRONG - crashes with virtual inheritance
TsEventEmitter* e = (TsEventEmitter*)ptr;

// ✅ CORRECT - use dynamic_cast
TsEventEmitter* e = dynamic_cast<TsEventEmitter*>((TsObject*)ptr);

// ✅ BETTER - use AsXxx() helpers
TsEventEmitter* e = ((TsObject*)ptr)->AsEventEmitter();
```

### Boxing/Unboxing Pattern

```cpp
extern "C" void ts_my_function(void* param) {
    // ALWAYS unbox first
    void* rawPtr = ts_value_get_object((TsValue*)param);
    if (!rawPtr) rawPtr = param;  // Fallback if not boxed

    // ALWAYS use safe casting
    TsMyClass* obj = dynamic_cast<TsMyClass*>((TsObject*)rawPtr);
    if (!obj) return;

    // Now safe to use
}
```

### LLVM 18 Opaque Pointer Rules

```cpp
// ❌ WRONG
builder->CreateGEP(ptr, indices);              // Missing source type
builder->CreateLoad(ptr);                      // Missing value type
llvm::Type* ptrType = intType->getPointerTo(); // Removed API

// ✅ CORRECT
builder->CreateGEP(elementType, ptr, indices);
builder->CreateLoad(builder->getInt64Ty(), ptr);
llvm::Type* ptrType = builder->getPtrTy();
```

---

## Common Tasks

### Add a New Node.js API Function

**Example: Adding `fs.existsSync(path)`**

1. **Type registration** (`src/compiler/analysis/Analyzer_StdLib_FS.cpp`):
   ```cpp
   auto existsSyncFunc = std::make_shared<FunctionType>();
   existsSyncFunc->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
   existsSyncFunc->returnType = std::make_shared<Type>(TypeKind::Boolean);
   fsClass->fields["existsSync"] = existsSyncFunc;
   ```

2. **HIR lowering** (`src/compiler/hir/LoweringRegistry.cpp`):
   ```cpp
   reg.registerLowering("ts_fs_exists_sync",
       lowering("ts_fs_exists_sync")
           .returnsBool()
           .ptrArg()
           .build());
   ```

3. **Runtime implementation** (`src/runtime/src/node/fs.cpp`):
   ```cpp
   extern "C" bool ts_fs_exists_sync(void* pathPtr) {
       TsString* path = (TsString*)ts_value_get_string((TsValue*)pathPtr);
       if (!path) return false;

       struct stat buffer;
       return stat(path->ToUtf8(), &buffer) == 0;
   }
   ```

### Add a Golden IR Test

Create `tests/golden_ir/typescript/feature/test_name.ts`:

```typescript
// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @expected_runtime_function
// CHECK-NOT: unwanted_pattern
// OUTPUT: Expected output text

function user_main(): number {
  // Test code here
  return 0;
}
```

Run:
```bash
python tests/golden_ir/runner.py tests/golden_ir/typescript/feature/test_name.ts
```

### Debug a Type Inference Issue

1. **Dump types**: `ts-aot file.ts --dump-types -o out.exe`
2. **Check analyzer**: Open `Analyzer_Expressions_*.cpp`
3. **Trace `lastType`**: Follow visit methods
4. **Verify type assignment**: Check `node->inferredType`

### Debug a Boxing Issue

1. **Check policy**: `BoxingPolicy::decide()` in `BoxingPolicy.cpp`
2. **Verify tracking**: `boxedValues.insert()` after boxing
3. **Check runtime**: Unboxing helpers in `TsObject.cpp`
4. **Trace IR**: `--dump-ir` to see box/unbox calls

---

## Build & Test Commands

### Build

```bash
# Full rebuild
cmake --build build --config Release

# Compiler only
cmake --build build --config Release --target ts-aot

# Runtime only
cmake --build build --config Release --target tsruntime
```

### Compile & Run

```bash
# Compile TypeScript to executable
build/src/compiler/Release/ts-aot.exe examples/test.ts -o examples/test.exe

# Run
examples/test.exe

# With diagnostics
ts-aot examples/test.ts --dump-types --dump-ir -o test.exe
```

### Test

```bash
# All golden tests
python tests/golden_ir/runner.py

# Category
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/

# Single test
python tests/golden_ir/runner.py tests/golden_ir/typescript/arrays/array_map.ts

# Verbose
python tests/golden_ir/runner.py --verbose tests/golden_ir/typescript/arrays/array_map.ts
```

### Debug

```powershell
# Crash analysis (use skill)
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe

# Symbol search (use skill)
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_get"
```

---

## Known Issues & Limitations

### Critical Blockers
- None currently

### High Priority
- **try/catch not implemented** - No exception handling
- **BigInt operators incomplete** - Type exists but can't use +, -, etc.
- **Timer APIs missing** - No setTimeout, setInterval


---

## Documentation Map

| Topic | File |
|-------|------|
| **Current status** | `.github/context/active_state.md` |
| **Architecture decisions** | `.github/context/architecture_decisions.md` |
| **Known issues** | `.github/context/known_issues.md` |
| **Development guide** | `.github/DEVELOPMENT.md` |
| **Quick reference** | `.github/instructions/quick-reference.md` |
| **Code snippets** | `.github/instructions/code-snippets.md` |
| **Runtime extensions** | `.github/instructions/runtime-extensions.instructions.md` |
| **Node.js APIs** | `.github/instructions/adding-nodejs-api.instructions.md` |
| **Current phase** | `docs/phase19/meta_epic.md` |
| **Latest epic** | `docs/phase19/epic_106_golden_ir_tests.md` |
| **Migration guide** | `MIGRATION.md` |
| **Claude setup** | `CLAUDE.md` |

---

## Skills Quick Reference

| Skill | When to Use | Trigger Terms |
|-------|-------------|---------------|
| `/auto-debug` | Executable crashes | "crash", "access violation", "debug" |
| `/golden-ir-tests` | Run test suite | "golden tests", "run tests", "regression" |
| `/ctags-search` | Find symbol definition | "where is defined", "find symbol" |

---

## Pro Tips

1. **Always read active state first**: `.github/context/active_state.md`
2. **Use skills for automation**: Don't invoke CDB or ctags directly
3. **Check rules before editing**: Runtime files trigger safety reminders
4. **Run golden tests after changes**: Catch regressions early
5. **Use `--dump-types` for debugging**: See what the analyzer inferred
6. **Check BoxingPolicy for boxing bugs**: Centralized decision logic
7. **Use AsXxx() for stream casts**: Never C-style cast virtual inheritance
8. **Track boxed values**: `boxedValues.insert()` after every box operation
9. **Build both targets**: `cmake --build build --config Release` builds compiler + runtime
10. **Reference by line number**: Use `file:line` format in discussions

---

**End of Index** - For detailed documentation, see files listed in Documentation Map section above.
