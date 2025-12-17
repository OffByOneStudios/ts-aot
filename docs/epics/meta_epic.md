# Meta Epic: TypeScript AOT Compiler (ts-aot)

This document outlines the high-level epics required to deliver the `ts-aot` project, based on the technical specification in `docs/scaffold.md`.

## Epic 1: Project Infrastructure & Build System
**Goal:** Establish the repository structure, dependency management, and build configuration.
- Set up `vcpkg.json` with dependencies (LLVM, nlohmann-json, fmt, libuv, bdwgc, etc.).
- Create root `CMakeLists.txt` and configure subdirectories.
- Configure CI/CD pipelines (future).

## Epic 2: Runtime Library (libtsruntime)
**Goal:** Implement the "Thin Runtime" required to support compiled TypeScript code.
- **Memory Management:** Implement `ts_alloc` wrapping Boehm GC (`bdwgc`).
- **Primitives:** Implement `TsString` (immutable, UTF-16/ICU) and `TsObject`.
- **Event Loop:** Integrate `libuv` for async operations.
- **Entry Point:** Define `ts_main()` to initialize GC and loop.
- **Console:** Implement `ts_console_log`.

## Epic 3: Compiler Frontend & AST
**Goal:** Ingest TypeScript source code and convert it into an internal C++ representation.
- **AST Dumper:** Create `scripts/dump_ast.js` using the TypeScript Compiler API to output JSON.
- **AST Loader:** Implement C++ JSON deserialization (`nlohmann/json`) into `AstNodes` structs.
- **CLI:** Create the `ts-aot` executable entry point with argument parsing (`cxxopts`).

## Epic 4: Type Analysis & Monomorphization
**Goal:** Implement the optimization strategy to specialize generic/dynamic types.
- **Symbol Table:** Implement `TypeTracker` to track variable types and scopes.
- **Pass 1 (Scan):** Identify function calls and usage patterns.
- **Pass 2 (Specialize):** Generate specialized function signatures (e.g., `add_int_int`) for specific type arguments.
- **Pass 3 (Fallback):** Implement fallback mechanisms for dynamic types using `TaggedValue`.

## Epic 5: Code Generation (LLVM)
**Goal:** Translate the specialized AST into native machine code.
- **IR Generation:** Initialize `llvm::Module` and `llvm::IRBuilder`.
- **Instruction Selection:** Map AST nodes to LLVM IR (math ops, function calls).
- **Runtime Linking:** Emit calls to runtime functions (`ts_alloc`, `ts_console_log`).
- **Object Emission:** Write `.o` files using `llvm::TargetMachine`.

## Epic 6: Testing & Integration
**Goal:** Ensure correctness through unit and integration tests.
- **Unit Tests:** Set up `Catch2` for C++ runtime components.
- **Integration Tests:** Create a suite of `.ts` files to compile and execute, verifying output against `ts-node` or `node`.
