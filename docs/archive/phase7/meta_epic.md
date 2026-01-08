# Phase 7: Advanced OOP

**Status:** COMPLETED
**Goal:** Complete the object-oriented programming features of the compiler, building upon the basic class support from Phase 6.

## Epics

### [Epic 21: Inheritance](./epic_21_inheritance.md) - COMPLETED
Implement class inheritance using `extends`.
- **Key Features:**
    - Subclassing (`class Dog extends Animal`).
    - `super` calls (constructors and methods).
    - Method overriding.
    - VTable extension (copy parent VTable, append new methods, overwrite overridden ones).

### [Epic 22: Interfaces](./epic_22_interfaces.md) - COMPLETED
Implement structural typing and interfaces.
- **Key Features:**
    - `interface` declaration.
    - `implements` clause (compile-time check).
    - Structural compatibility checks in Type Checker.
    - No runtime impact (purely compile-time).

### [Epic 23: Access Modifiers](./epic_23_access_modifiers.md) - COMPLETED
Implement visibility control for class members.
- **Key Features:**
    - `public` (default).
    - `private` (class-private).
    - `protected` (class and subclass).
    - Compile-time enforcement in Analyzer.

### [Epic 24: Advanced Class Features](./epic_24_advanced_classes.md) - COMPLETED
Implement remaining class features.
- **Key Features:**
    - Static members (`static`)
    - Readonly properties (`readonly`)
    - Getters and Setters (`get`, `set`)
    - Parameter properties (`constructor(public x: number)`)

### [Epic 25: Abstract Classes](./epic_25_abstract_classes.md) - COMPLETED
Support the `abstract` keyword for classes and methods.
- **Key Features:**
    - `abstract class` declaration.
    - `abstract` methods (no body).
    - Instantiation prevention.
    - Implementation enforcement in subclasses.

### [Epic 26: Method Overloading (Basic)](./epic_26_method_overloading.md) - IN COMPLETED
Support basic method overloading in classes.
- **Key Features:**
    - Multiple method signatures.
    - Correct overload resolution based on argument types.

## Success Criteria
- Can compile and run code using inheritance and polymorphism.
- Interfaces correctly enforce structure without generating code.
- Access modifiers prevent unauthorized access at compile time.
