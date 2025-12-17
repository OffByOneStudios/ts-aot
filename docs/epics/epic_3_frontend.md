# Epic 3: Compiler Frontend & AST

**Status:** Planned
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to implement the frontend of the compiler. Instead of writing a full TypeScript parser in C++, we will leverage the official TypeScript compiler (`tsc`) to parse the source code and dump a simplified JSON Abstract Syntax Tree (AST). The C++ compiler will then ingest this JSON and convert it into strongly-typed C++ structures ready for analysis and code generation.

## Milestones

### Milestone 3.1: AST Dumper (JavaScript)
Create a Node.js script that uses the TypeScript Compiler API to parse `.ts` files and output a JSON representation of the AST.
- **Goal:** Running `node scripts/dump_ast.js input.ts` produces a valid `input.json` containing the AST.

### Milestone 3.2: C++ AST Structures
Define the C++ data structures that represent the TypeScript AST.
- **Goal:** We have structs like `FunctionDecl`, `BinaryExpr`, `CallExpr` defined in headers.

### Milestone 3.3: AST Loader (JSON Deserialization)
Implement the logic to read the JSON file and populate the C++ AST structures.
- **Goal:** The compiler can load a JSON file and build a tree of C++ objects in memory.

### Milestone 3.4: Compiler CLI
Update the `ts-aot` executable to accept input files and orchestrate the compilation process.
- **Goal:** `ts-aot input.ts` (or `input.json`) runs the loader and prints a success message.

## Action Items

### Task 3.1: AST Dumper Script
- [x] Initialize `scripts/package.json` and install `typescript`.
- [x] Create `scripts/dump_ast.js`.
- [x] Implement AST traversal using `ts.createSourceFile`.
- [x] Output simplified JSON (Program -> Statements -> Expressions).

### Task 3.2: C++ AST Definitions
- [ ] Create `src/compiler/ast/AstNodes.h`.
- [ ] Define base `Node` struct and derived types (`FunctionDeclaration`, `VariableDeclaration`, `BinaryExpression`, `CallExpression`, `Literal`).
- [ ] Use `std::variant` or inheritance for node types.

### Task 3.3: AST Loader
- [ ] Create `src/compiler/ast/AstLoader.h` and `src/compiler/ast/AstLoader.cpp`.
- [ ] Implement `std::unique_ptr<Program> loadAst(const std::string& jsonPath)`.
- [ ] Use `nlohmann/json` to parse the file and map fields to C++ structs.

### Task 3.4: CLI Implementation
- [ ] Update `src/compiler/main.cpp`.
- [ ] Use `cxxopts` to parse command line arguments (`--output`, input file).
- [ ] Integrate `AstLoader` to load the input.
- [ ] Add a debug flag to print the loaded AST structure.
