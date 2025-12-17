# Phase 6: Object-Oriented Programming

**Goal:** Implement class-based object-oriented programming features and advanced function capabilities to support more complex application architectures.

## Epics

### Epic 20: Classes & Objects
- **Goal:** Implement `class` syntax, constructors, and property access.
- **Features:**
    - Class Declaration (`class Foo {}`)
    - Constructors (`constructor() {}`)
    - Properties (`x: number`)
    - Methods (`bar() {}`)
    - `new` operator for classes.

### Epic 21: Inheritance & Polymorphism
- **Goal:** Support class inheritance and method overriding.
- **Features:**
    - `extends` keyword.
    - `super` calls (constructor and methods).
    - Method overriding.
    - `instanceof` operator.

### Epic 22: Access Modifiers & `this`
- **Goal:** Implement encapsulation and context binding.
- **Features:**
    - `public`, `private`, `protected` modifiers (compile-time check).
    - `this` keyword in methods and constructors.
    - `readonly` properties.

### Epic 23: Interfaces & Structural Typing
- **Goal:** Implement TypeScript's structural type system for objects.
- **Features:**
    - `interface` declaration.
    - `implements` clause for classes.
    - Structural compatibility checks (duck typing).

### Epic 24: Advanced Function Features
- **Goal:** Enhance function definitions with flexibility.
- **Features:**
    - Optional Parameters (`x?: number`).
    - Default Parameters (`x = 1`).
    - Rest Parameters (`...args`).
    - Function Overloads (multiple signatures).

### Epic 25: Advanced Types (Part 1)
- **Goal:** Introduce safer top and bottom types.
- **Features:**
    - `unknown` type.
    - `never` type.
    - Type assertions (`as Foo`).

## Technical Considerations
- **VTable:** Need to implement virtual tables for dynamic dispatch in inheritance.
- **Memory Layout:** Class instances will need a defined memory layout compatible with inheritance.
- **`this` Context:** Need to pass `this` pointer implicitly to methods.
- **Structural Typing:** Analysis phase needs to check shape compatibility, not just nominal type equality.
