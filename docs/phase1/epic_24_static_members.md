# Epic 24: Static Members

**Goal:** Support the `static` keyword for class properties and methods.

## Status
- **Status:** Completed
- **Date:** 2024-05-22

## Milestones

### 24.1: AST & Parser Support
- [x] Update `AstNodes.h` to include `isStatic` in `PropertyDefinition` and `MethodDefinition`.
- [x] Update `AstLoader.cpp` to parse `static` modifier from JSON.

### 24.2: Type System & Analysis
- [x] Update `ClassType` to store static fields and methods.
- [x] Update `Analyzer` to resolve static member access (e.g., `Class.member`).
- [x] Enforce that static members are not accessible on instances.
- [x] Enforce that instance members are not accessible on the class.

### 24.3: Code Generation
- [x] Implement codegen for static fields (global variables).
- [x] Implement codegen for static methods (global functions without `this`).
- [x] Update `Monomorphizer` to handle static member references.

### 24.4: Verification
- [x] Create `tests/integration/static_members.ts`.
- [x] Verify correct behavior and initialization of static fields.

## Technical Notes
- Static fields are implemented as global variables mangled with the class name (`ClassName_static_FieldName`).
- Static methods are implemented as global functions mangled with the class name (`ClassName_static_MethodName`) and do not receive a `this` parameter.
- Constant initializers (NumericLiteral, BooleanLiteral) are supported for static fields.
