# Epic 22: Interfaces & Structural Typing

**Goal:** Support TypeScript `interface` declarations and structural typing.

## Milestones

### 22.1: AST & Parser Support
- [x] Add `InterfaceDeclaration` to `AstNodes.h`.
- [x] Update `dump_ast.js` to handle `ts.SyntaxKind.InterfaceDeclaration`.
- [x] Update `AstLoader.cpp` to parse interfaces from JSON.
- [x] Add `implementsInterfaces` to `ClassDeclaration`.

### 22.2: Semantic Analysis - Declaration
- [x] Implement `visitInterfaceDeclaration` in `Analyzer`.
- [x] Add `InterfaceType` to `Type.h`.
- [x] Support interface member resolution (properties and methods).
- [x] Support interface inheritance (`interface A extends B`).

### 22.3: Semantic Analysis - Implementation
- [x] Implement structural compatibility logic (`isAssignableTo`).
- [x] Validate that classes correctly implement their `implements` interfaces.
- [x] Support passing classes to functions expecting an interface.

### 22.4: Monomorphization & Codegen
- [x] Update `Monomorphizer` to handle interface-typed parameters by specializing for concrete classes.
- [x] Ensure `IRGenerator` uses the specialized class type for member access and method calls.
- [x] Fix mangling logic to include class names for object/interface parameters.

## Action Items
- [x] Define `InterfaceDeclaration` AST node.
- [x] Update `dump_ast.js` for interfaces.
- [x] Implement `InterfaceType` in `Type.h`.
- [x] Implement `Analyzer::visitInterfaceDeclaration`.
- [x] Implement `Analyzer::checkInterfaceImplementation`.
- [x] Update `Monomorphizer::generateMangledName` to handle classes.
- [x] Update `IRGenerator::visitCallExpression` to use `inferredType` for mangling.
- [x] Create integration test `tests/integration/interfaces.ts`.
- [x] Create integration test `tests/integration/interfaces_extending.ts`.
- [x] Verify all tests pass.
