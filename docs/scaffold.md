Here is a comprehensive technical specification report designed to be fed into an AI agent (or a human engineering team) to scaffold the project.

It defines the architecture, directory structure, dependencies, and implementation strategy we discussed.

Technical Specification: TypeScript AOT Compiler (ts-aot)
Date: December 16, 2025 Target Architecture: C++20 / LLVM Backend Build System: CMake + vcpkg

1. Project Overview
The goal is to build an Ahead-of-Time (AOT) compiler for TypeScript that compiles .ts files into native machine code (object files) and links them against a custom C++ runtime.

Key Architectural Decisions
Frontend: Leverage the official tsc compiler to generate a JSON AST. The C++ compiler will ingest this JSON, avoiding the need to write a custom TS parser.

Optimization Strategy: Monomorphization. Treat any and generic types as templates. Generate specialized native functions for unique type signatures discovered at compile-time.

Runtime Strategy: A "Thin Runtime" model.

Memory: Boehm GC (bdwgc) for automatic garbage collection.

Async: libuv for the Event Loop and I/O.

Object Model: Hybrid. Static C++ structs for typed classes; Tagged Unions for dynamic fallbacks.

2. Dependency Management (vcpkg.json)
The project relies on vcpkg for dependency resolution.

JSON

{
  "name": "ts-aot",
  "version-string": "0.1.0",
  "dependencies": [
    "llvm",           // Backend: IR generation and machine code output
    "nlohmann-json",  // Frontend: Parsing the AST JSON dump
    "fmt",            // Utilities: Logging and string formatting
    "cxxopts",        // CLI: Argument parsing
    "libuv",          // Runtime: Event Loop, Timer, I/O
    "bdwgc",          // Runtime: Garbage Collection
    "icu",            // Runtime: UTF-16 String support
    "catch2"          // Testing: Unit tests
  ]
}
3. Directory Structure
The project is bifurcated into the Compiler CLI (build tools) and the Runtime Library (linked dependency).

Plaintext

/ts-aot
├── vcpkg.json
├── CMakeLists.txt              # Root CMake configuration
├── scripts/
│   └── dump_ast.js             # Node.js script to run 'tsc' and output JSON AST
├── src/
│   ├── compiler/               # [ARTIFACT: ts-aot executable]
│   │   ├── main.cpp            # Entry point
│   │   ├── ast/
│   │   │   ├── AstLoader.cpp   # Loads JSON into C++ AST structs
│   │   │   └── AstNodes.h      # Struct definitions (FunctionDecl, BinaryExpr, etc.)
│   │   ├── analysis/
│   │   │   ├── TypeTracker.h   # Symbol table & type inference state
│   │   │   └── Monomorphizer.cpp # "Macro" engine: creates specializations
│   │   └── codegen/
│   │   │   ├── IRGenerator.cpp # LLVM IR Builder logic
│   │   │   └── ObjEmitter.cpp  # Writes .o files to disk
│   │
│   └── runtime/                # [ARTIFACT: libtsruntime.a]
│       ├── include/            # Public headers (included by LLVM-generated code)
│       │   ├── TsRuntime.h     # Main entry header
│       │   ├── TsString.h      # String implementation
│       │   ├── TsObject.h      # Object/Map implementation
│       │   └── GC.h            # GC wrapper macros
│       └── src/
│           ├── Core.cpp        # Entry point main() replacement
│           ├── EventLoop.cpp   # libuv integration
│           ├── Memory.cpp      # bdwgc setup
│           └── Primitives.cpp  # Implementation of number/string ops
│
└── tests/
    ├── unit/                   # C++ unit tests for Runtime
    └── integration/            # .ts files to compile and run
4. Build System Configuration (CMakeLists.txt)
Root Configuration
Standard: C++20

Module Path: Define CMAKE_MODULE_PATH to find vcpkg toolchains.

Target: tsruntime (Static Library)
Includes: src/runtime/include

Sources: src/runtime/src/*.cpp

Linkage: libuv::libuv, unofficial::bdwgc::bdwgc, ICU::uc.

Compile Definitions: TS_RUNTIME_EXPORT.

Target: ts-aot (Executable)
Includes: src/compiler

Sources: src/compiler/**/*.cpp

Linkage: LLVM::LLVM (components: core, support, codegen, irreader, x86codegen), nlohmann_json::nlohmann_json, fmt::fmt.

5. Implementation Guidelines
Phase 1: The Runtime (Foundation)
Before the compiler can generate code, the runtime must exist to support it.

Memory: Implement ts_alloc(size_t) wrapping GC_MALLOC.

Primitives: Create TsString. It must be immutable and likely ref-counted or GC-managed.

Console: Implement ts_console_log(TsString* str) wrapping printf.

Entry: Define int ts_main() which initializes the GC, starts the libuv loop, and calls the user's generated main.

Phase 2: The Compiler Frontend (AST)
AST Dumper: Create a small JS script using the TypeScript Compiler API (ts.createSourceFile) to traverse user code and dump a simplified JSON structure.

AST Loader: Use nlohmann/json to deserialize this into C++ structs (Program, FunctionDeclaration, Expression).

Phase 3: The Monomorphizer (The "Macro" System)
This is the core differentiator.

Pass 1 (Scan): Identify all function calls.

Pass 2 (Specialize):

If a function add(a: any, b: any) is called with (int, int), generate a symbol add_int_int.

If called with (string, string), generate add_str_str.

Pass 3 (Fallback): For unresolvable types (e.g., I/O results), generate a "boxed" version accepting TaggedValue.

Phase 4: Code Generation (LLVM)
Module Setup: Initialize llvm::Module and llvm::IRBuilder.

Function Gen: Iterate over the specialized functions from Phase 3.

Instruction Selection:

Static Math: Emit native add/sub instructions.

Runtime Calls: Emit call instructions to ts_alloc or ts_console_log.

Output: Use llvm::TargetMachine::emitCodeToMemory or file stream to write the object file.

6. Data Structures (Reference)
The Tagged Value (Runtime Fallback):

C++

// src/runtime/include/TsObject.h
enum class ValueType : uint8_t {
    UNDEFINED = 0,
    NUMBER_INT,
    NUMBER_DBL,
    BOOLEAN,
    STRING_PTR,
    OBJECT_PTR
};

struct TaggedValue {
    ValueType type;
    union {
        int64_t i_val;
        double d_val;
        bool b_val;
        void* ptr_val;
    };
};
The V-Table (Static Classes): For TS Classes, generate a C++ struct with a pointer to a function table.

C++

struct TsClassInstance {
    void* vtable; // Pointer to array of function pointers
    // ... fields follow ...
};