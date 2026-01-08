# Phase 8: Advanced Type System & Robustness

**Status:** In Progress
**Goal:** Implement advanced type system features and improve error handling and utility support.

## Epics

### [Epic 31: Union & Intersection Types](./epic_31_union_intersection.md) (Completed)
Support complex type relationships.
- **Key Features:**
    - [x] Union Types (`string | number`).
    - [x] Intersection Types (`A & B`).
    - [x] Type narrowing and basic compatibility rules.
    - [x] `as` type assertions.

### [Epic 32: Type Guards](./epic_32_type_guards.md) (Next)
Enable runtime type checking and narrowing.
- **Key Features:**
    - `typeof` operator.
    - `instanceof` operator.
    - User-defined type guards (`is` keyword).

### [Epic 33: Error Handling (try/catch)](./epic_33_error_handling.md)
Implement exception handling.
- **Key Features:**
    - `try`, `catch`, `finally` blocks.
    - `throw` statement.
    - Integration with C++ exceptions or setjmp/longjmp.

### [Epic 34: Destructuring & Spread](./epic_34_destructuring_spread.md) (Completed)
Support modern syntax for objects and arrays.
- **Key Features:**
    - [x] Object destructuring (`const { x, y } = point`).
    - [x] Array destructuring (`const [first, second] = arr`).
    - [x] Spread operator (`...`).

### [Epic 35: Standard Library Expansion](./epic_35_stdlib_expansion.md) (Completed)
Implement remaining core built-in objects.
- **Key Features:**
    - [x] `Date` object.
    - [x] `RegExp` support.
    - [x] `JSON` (parse/stringify).
    - [x] `Math` and `IO` expansion.
    - [x] `String` and `Array` enhancements.

## Success Criteria
- Can handle variables with multiple possible types (Unions).
- Code can safely narrow types using `typeof` or `instanceof`.
- Exceptions can be thrown and caught across function boundaries.
- Modern syntax like destructuring works as expected.
