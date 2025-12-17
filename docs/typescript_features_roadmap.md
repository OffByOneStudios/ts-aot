# TypeScript Features Roadmap

This document tracks the implementation status of TypeScript language features in `ts-aot` and outlines the roadmap for future phases.

**Legend:**
- ✅ **Implemented**: Feature is fully supported.
- ⚠️ **Partial**: Feature is partially supported or has known limitations.
- 📅 **Planned**: Feature is assigned to a future phase.
- ❌ **Unsupported**: Feature is not currently planned.

## 1. Basic Types & Variables

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| `boolean` | ✅ | Phase 1 | |
| `number` (int/double) | ✅ | Phase 1 | Mapped to `int64_t` or `double`. |
| `string` | ✅ | Phase 2 | ICU-based `TsString`. |
| `void` | ✅ | Phase 1 | |
| `any` | ⚠️ | Phase 1 | Basic support, often falls back to specific types or errors. |
| `Array<T>` | ⚠️ | Phase 2 | `new Array(size)`, `[]`, `push`, `length`. Typed arrays partial. |
| `null` / `undefined` | ⚠️ | Future | Currently treated loosely or not strictly enforced. |
| `var` / `let` / `const` | ✅ | Phase 1 | Scoping rules simplified (all function/block scoped). |
| `tuple` | 📅 | Phase 5 | Fixed-length, mixed-type arrays. |
| `enum` | 📅 | Phase 5 | Numeric and String enums. |
| `unknown` | 📅 | Phase 6 | Safer `any`. |
| `never` | 📅 | Phase 6 | Unreachable code analysis. |

## 2. Control Flow

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| `if` / `else` | ✅ | Phase 1 | |
| `while` | ✅ | Phase 2 | |
| `for` (C-style) | ✅ | Phase 2 | `for (let i=0; i<n; i++)`. |
| `break` / `continue` | ✅ | Phase 2 | |
| `return` | ✅ | Phase 4 | |
| `switch` | 📅 | Phase 5 | |
| `for..of` | 📅 | Phase 5 | Iterators. |
| `for..in` | 📅 | Phase 5 | Object key iteration. |
| `try` / `catch` / `finally` | 📅 | Phase 7 | Exception handling (C++ exceptions or setjmp/longjmp). |
| `throw` | 📅 | Phase 7 | |

## 3. Functions

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Function Declaration | ✅ | Phase 4 | `function foo() {}`. |
| Function Call | ✅ | Phase 1 | |
| Recursion | ✅ | Phase 4 | |
| Arrow Functions | 📅 | Phase 5 | `() => {}`. |
| Function Expressions | 📅 | Phase 5 | `const foo = function() {}`. |
| Optional Parameters | 📅 | Phase 6 | `function foo(x?: number)`. |
| Default Parameters | 📅 | Phase 6 | `function foo(x = 1)`. |
| Rest Parameters | 📅 | Phase 6 | `function foo(...args)`. |
| Overloads | 📅 | Phase 6 | Multiple signatures. |

## 4. Objects & Classes

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Object Literals | ✅ | Phase 4 | `{ x: 1, y: 2 }`. |
| Property Access | ✅ | Phase 4 | `obj.prop`. |
| `Map` | ⚠️ | Phase 3 | Basic `get`/`set`/`has`. |
| Classes | 📅 | Phase 6 | `class Foo {}`. |
| Inheritance | 📅 | Phase 6 | `extends`. |
| Access Modifiers | 📅 | Phase 6 | `public`, `private`, `protected`. |
| `this` keyword | 📅 | Phase 6 | Context binding. |
| Interfaces | 📅 | Phase 6 | Structural typing. |
| Generics | 📅 | Phase 8 | `class Box<T>`. |

## 5. Advanced Features

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Union Types | 📅 | Phase 8 | `string | number`. |
| Intersection Types | 📅 | Phase 8 | `A & B`. |
| Type Aliases | 📅 | Phase 5 | `type ID = string`. |
| Type Guards | 📅 | Phase 8 | `if (typeof x === 'string')`. |
| Destructuring | 📅 | Phase 7 | `const { x } = obj`. |
| Spread / Rest | 📅 | Phase 7 | `...obj`, `...arr`. |
| Template Literals | 📅 | Phase 5 | `` `Hello ${name}` ``. |
| Modules (`import`/`export`) | 📅 | Phase 9 | Multi-file compilation. |
| Async / Await | 📅 | Phase 10 | Promises and Event Loop integration. |

## 6. Standard Library

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| `console.log` | ✅ | Phase 1 | |
| `Math` | ⚠️ | Phase 3 | `min`, `floor`. Need `max`, `abs`, `random`, etc. |
| `fs` | ⚠️ | Phase 3 | `readFileSync`. Need `writeFileSync`, etc. |
| `crypto` | ⚠️ | Phase 4 | `md5`. Need `sha256`, etc. |
| `Date` | 📅 | Phase 7 | |
| `RegExp` | 📅 | Phase 7 | |
| `JSON` | 📅 | Phase 7 | `parse`, `stringify`. |

## Proposed Future Phases

### Phase 5: Syntactic Sugar & Iterators
- **Goal:** Make code more expressive.
- **Features:** `switch`, `for..of`, Arrow Functions, Template Literals, Type Aliases, Tuples, Enums.

### Phase 6: Object-Oriented Programming
- **Goal:** Support class-based architecture.
- **Features:** Classes, Inheritance, Interfaces, `this`, Optional/Default/Rest params.

### Phase 7: Robustness & Utilities
- **Goal:** Error handling and standard library expansion.
- **Features:** `try/catch`, Destructuring, `Date`, `RegExp`, `JSON`.

### Phase 8: Advanced Type System
- **Goal:** Support complex type relationships.
- **Features:** Generics, Union/Intersection Types, Type Guards.

### Phase 9: Modules & Build
- **Goal:** Large scale project support.
- **Features:** `import`/`export`, Multi-file linking.

### Phase 10: Asynchronous Programming
- **Goal:** Non-blocking I/O.
- **Features:** `Promise`, `async`/`await`.
