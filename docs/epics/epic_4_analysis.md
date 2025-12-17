# Epic 4: Type Analysis & Monomorphization

**Status:** Planned
**Parent:** [Meta Epic](./meta_epic.md)

## Overview
The goal of this epic is to implement the optimization strategy to specialize generic/dynamic types. We will treat `any` and generic types as templates and generate specialized native functions for unique type signatures discovered at compile-time. This "Monomorphization" process allows us to generate efficient native code for TypeScript's dynamic features.

## Milestones

### Milestone 4.1: Type System & Symbol Table
Define the internal type system and implement a symbol table to track variable types and scopes.
- **Goal:** We can represent TS types (number, string, boolean, etc.) in C++ and look up variable definitions in nested scopes.

### Milestone 4.2: Analysis Pass (Scan)
Implement a pass that traverses the AST to identify function declarations and calls.
- **Goal:** The compiler knows about all defined functions and can resolve variable references to their declarations.

### Milestone 4.3: Monomorphization (Specialize)
Implement the logic to generate specialized function signatures based on call sites.
- **Goal:** If `add(a, b)` is called with integers, we generate a plan to compile `add_int_int`.

## Action Items

### Task 4.1: Type Definitions
- [x] Create `src/compiler/analysis/Type.h`.
- [x] Define `TypeKind` enum (Int, Double, Boolean, String, Void, Any, Function).
- [x] Define `Type` struct/class to hold type information.

### Task 4.2: Symbol Table
- [x] Create `src/compiler/analysis/SymbolTable.h` and `src/compiler/analysis/SymbolTable.cpp`.
- [x] Implement scope management (`enterScope`, `exitScope`).
- [x] Implement symbol definition and lookup (`define`, `lookup`).

### Task 4.3: Analysis Pass
- [x] Create `src/compiler/analysis/Analyzer.h` and `src/compiler/analysis/Analyzer.cpp`.
- [x] Implement AST traversal to populate the symbol table.
- [x] Verify that variables are defined before use.

### Task 4.4: Call Graph & Usage Tracking
- [x] Update `Analyzer` to track function calls and argument types.
- [x] Store usage information (e.g., "function `add` called with (int, int)").

### Task 4.5: Specialization Logic
- [ ] Implement a `Monomorphizer` class that takes the usage info and generates specialized function signatures.
- [ ] Create new `FunctionDeclaration` nodes (or similar) for the specialized versions.
