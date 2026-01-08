# Epic 94: Modern Syntax & Operators

**Status:** Planned
**Parent:** [Phase 17 Meta Epic](./meta_epic.md)

## Overview
Implement modern TypeScript/JavaScript operators and syntax features.

## Milestones

### Milestone 94.1: Short-circuiting Operators
- [x] **Optional Chaining:** Implement `?.` for properties, calls, and element access.
- [x] **Nullish Coalescing:** Implement `??` and `??=`.

### Milestone 94.2: Template Literals
- [x] **Tagged Templates:** Implement tagged template literal calls.

### Milestone 94.3: Destructuring
- [x] **Advanced Destructuring:** Support nested patterns and rest elements in arrays/objects.

## Detailed Plan: Advanced Destructuring (Milestone 94.3)

### 1. Runtime Support
- [x] **Object Rest (`...rest`):**
    - Implement `ts_object_rest(TsValue* obj, TsArray* excludedKeys)` in `src/runtime/src/TsObject.cpp`.
    - This function should iterate over `obj`'s keys and copy those NOT in `excludedKeys` to a new object.
    - Expose in `src/runtime/include/TsObject.h`.

### 2. IRGenerator Updates
- [x] **Fix Array Rest:**
    - In `visitArrayBindingPattern` (inside `generateDestructuring`), `ts_array_slice` is currently called with missing arguments (2 args instead of 3).
    - Update call to pass `length` (or a sentinel) as the 3rd argument.
- [x] **Implement Object Rest:**
    - In `visitObjectBindingPattern`, detect `BindingElement` with `isSpread` (or `dotDotDotToken`).
    - Collect all property names processed *before* the rest element.
    - Generate code to create a `TsArray` of these names (strings).
    - Call `ts_object_rest(value, excludedKeys)`.
    - Bind the result to the rest variable.
- [x] **Nested Patterns:**
    - Verify recursion works for both Array and Object patterns (seems mostly there, but double check).

### 3. Analyzer Updates
- [x] **Type Inference:**
    - Ensure `visitObjectBindingPattern` and `visitArrayBindingPattern` in `Analyzer` correctly infer types for rest elements.
    - Array Rest: `T[]` -> `T[]`.
    - Object Rest: `T` -> `Omit<T, "prop1" | "prop2">` (or just a new object type with remaining fields).

## Action Items
- [x] **Task 94.1:** Implement `OptionalExpression` in `IRGenerator`.
- [x] **Task 94.2:** Implement `BinaryExpression` logic for `??`.
- [x] **Task 94.3:** Verify with `ts-aot examples/modern_syntax_test.ts`.
- [x] **Task 94.4:** Implement Runtime support for Object Rest.
- [x] **Task 94.5:** Fix Array Rest in IRGenerator.
- [x] **Task 94.6:** Implement Object Rest in IRGenerator.
- [x] **Task 94.7:** Verify with `examples/destructuring_test.ts`.
