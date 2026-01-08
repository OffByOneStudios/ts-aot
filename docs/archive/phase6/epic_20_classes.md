# Epic 20: Classes & Objects

**Goal:** Implement support for TypeScript classes, including property definitions, methods, constructors, and instantiation.

**Dependencies:**
- Phase 6 Meta Epic
- [Design: Classes & VTables](./design_classes.md)

## Milestones

### Milestone 20.1: AST & Parser Support
- [x] Add `ClassDeclaration` AST node.
- [x] Add `MethodDefinition` AST node (for methods and constructors).
- [x] Add `PropertyDefinition` AST node.
- [x] Update `NewExpression` to support class instantiation.
- [x] Update Parser to handle `class` keyword and body.

### Milestone 20.2: Semantic Analysis
- [x] Symbol Table: Register class names.
- [x] Type Checker: Validate class members.
- [x] Type Checker: Validate `new` expressions against constructors.

### Milestone 20.3: Code Generation (Basic)
- [x] Generate LLVM Struct type for the class (fields + vptr).
- [x] Generate VTable global constant for the class.
- [x] Generate Constructor function (allocates memory, sets vptr, initializes fields).
- [x] Generate Method functions (with `this` pointer).

### Milestone 20.4: Usage
- [x] Implement `NewExpression` codegen (call constructor).
- [x] Implement Method Call codegen (load vptr -> load function -> call).
- [x] Implement Field Access codegen (GEP into struct).

## Action Items
- [x] **Task 1:** Define AST nodes for Classes in `src/compiler/ast/AST.h`.
- [x] **Task 2:** Implement parsing logic in `src/compiler/parser/Parser.cpp`.
- [x] **Task 3:** Add AST dump support in `src/compiler/ast/AST.cpp`.
- [x] **Task 4:** Create a basic test case `tests/integration/classes_basic.ts`.
- [x] **Task 5:** Update `SymbolTable` to support Class types.
- [x] **Task 6:** Implement `Analyzer::visitClassDeclaration` to register the class and its members.
- [x] **Task 7:** Implement `Analyzer::visitNewExpression` to validate constructor calls.
- [x] **Task 8:** Implement `Analyzer::visitPropertyAccessExpression` to validate field/method access.
- [x] **Task 9:** Implement `IRGenerator::visitClassDeclaration` (Generate LLVM Structs).
- [x] **Task 10:** Implement `IRGenerator::visitNewExpression` (Allocation & Initialization).
- [x] **Task 11:** Implement `IRGenerator::visitPropertyAccessExpression` (Field Access).
