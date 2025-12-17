# Epic 25: Abstract Classes

**Goal:** Support the `abstract` keyword for classes and methods, ensuring that abstract classes cannot be instantiated and that subclasses implement all abstract methods.

## Status
- **Status:** Completed
- **Date:** 2024-05-22

## Milestones

### 25.1: AST & Parser Support
- [x] Update `AstNodes.h` to include `isAbstract` in `ClassDeclaration` and `MethodDefinition`.
- [x] Update `AstLoader.cpp` to parse `abstract` modifier from JSON.
- [x] Update `dump_ast.js` to export `isAbstract` for classes and methods.

### 25.2: Semantic Analysis
- [x] Update `ClassType` to store `isAbstract` flag.
- [x] Update `Analyzer` to prevent `new` on abstract classes.
- [x] Update `Analyzer` to ensure abstract methods have no body.
- [x] Update `Analyzer` to ensure concrete subclasses implement all inherited abstract methods.

### 25.3: Code Generation
- [x] Update `IRGenerator` to handle abstract methods in VTables.
- [x] Ensure that abstract methods do not generate function bodies.

### 25.4: Verification
- [x] Create `tests/integration/abstract_classes.ts` with positive and negative test cases.
- [x] Verify that illegal instantiation and missing implementations trigger compiler errors.

## Technical Notes
- Abstract classes serve as base classes and cannot be instantiated directly.
- Abstract methods define a signature but no implementation; concrete subclasses must override them.
- In the VTable, an abstract method that hasn't been overridden yet can be represented as a null pointer or a call to a runtime error function.
