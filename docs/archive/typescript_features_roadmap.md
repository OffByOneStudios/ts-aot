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
| `switch` | ✅ | Phase 5 | |
| `for..of` | ✅ | Phase 5 | Iterators. |
| `for..in` | 📅 | Phase 5 | Object key iteration. |
| `try` / `catch` / `finally` | ✅ | Phase 8 | Exception handling (setjmp/longjmp). |
| `throw` | ✅ | Phase 8 | |

## 3. Functions

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Function Declaration | ✅ | Phase 4 | `function foo() {}`. |
| Function Call | ✅ | Phase 1 | |
| Recursion | ✅ | Phase 4 | |
| Arrow Functions | ✅ | Phase 5 | `() => {}`. |
| Template Literals | ✅ | Phase 5 | `` `Hello ${name}` ``. |
| Function Expressions | 📅 | Phase 5 | `const foo = function() {}`. |
| Optional Parameters | 📅 | Phase 6 | `function foo(x?: number)`. |
| Default Parameters | 📅 | Phase 6 | `function foo(x = 1)`. |
| Rest Parameters | 📅 | Phase 6 | `function foo(...args)`. |
| Overloads | ✅ | Phase 7 | Multiple signatures. |

## 4. Objects & Classes

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Object Literals | ✅ | Phase 4 | `{ x: 1, y: 2 }`. |
| Property Access | ✅ | Phase 4 | `obj.prop`. |
| `Map` | ⚠️ | Phase 3 | Basic `get`/`set`/`has`. |
| Classes | ✅ | Phase 6 | Basic support (fields, methods, ctors). |
| Inheritance | ✅ | Phase 7 | `extends`. |
| Access Modifiers | ✅ | Phase 7 | `public`, `private`, `protected`. |
| `this` keyword | ✅ | Phase 6 | Basic support (methods). |
| Interfaces | ✅ | Phase 7 | Structural typing. |
| Abstract Classes | ✅ | Phase 7 | `abstract` keyword. |
| Generics | 📅 | Phase 9 | `class Box<T>`. |

## 5. Advanced Features

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| Union Types | ✅ | Phase 8 | `string | number`. |
| Intersection Types | ✅ | Phase 8 | `A & B`. |
| Type Aliases | 📅 | Phase 5 | `type ID = string`. |
| Type Guards | ✅ | Phase 8 | `if (typeof x === 'string')`. |
| Destructuring | ✅ | Phase 8 | `const { x } = obj`. |
| Spread / Rest | ✅ | Phase 8 | `...obj`, `...arr`. |
| Modules (`import`/`export`) | 📅 | Phase 9 | Multi-file compilation. |
| Async / Await | ✅ | Phase 10 | Promises and Event Loop integration. |

## 6. Standard Library

| Feature | Status | Phase | Notes |
| :--- | :---: | :---: | :--- |
| `console.log` | ✅ | Phase 1 | |
| `Math` | ✅ | Phase 8 | `min`, `floor`, `max`, `abs`, `random`, `sqrt`, `pow`, `PI`. |
| `fs` | ✅ | Phase 8 | `readFileSync`, `writeFileSync`, `link`, `symlink`, `readlink`, `realpath`, `lstat`. |
| `crypto` | ⚠️ | Phase 4 | `md5`. Need `sha256`, etc. |
| `Date` | ✅ | Phase 8 | `now`, `getTime`, `getFullYear`, etc. |
| `RegExp` | ✅ | Phase 8 | `test`, `exec`. |
| `JSON` | ✅ | Phase 8 | `parse`, `stringify`. |

## Proposed Future Phases

### Phase 5: Syntactic Sugar & Iterators
- **Goal:** Make code more expressive.
- **Features:** `switch`, `for..of`, Arrow Functions, Template Literals, Type Aliases, Tuples, Enums.

### Phase 6: Classes & Objects (Basic)
- **Goal:** Implement basic class support.
- **Features:** Classes, Constructors, Methods, Fields, `this`.

### Phase 7: Advanced OOP
- **Goal:** Complete object-oriented programming features.
- **Status:** ✅ COMPLETED
- **Features:** Inheritance, Interfaces, Access Modifiers, Abstract Classes, Method Overloading.

### Phase 8: Advanced Type System & Robustness
- **Goal:** Implement advanced type system features and improve error handling.
- **Status:** ⚠️ PARTIAL (Union/Intersection, Type Guards, try/catch done)
- **Features:** Union/Intersection Types, Type Guards, `try/catch`, Destructuring, `Date`, `RegExp`, `JSON`.

### Phase 10: Async / Await & Event Loop
- **Goal:** Non-blocking I/O.
- **Status:** ✅ COMPLETED
- **Features:** `Promise`, `async`/`await`, `setTimeout`, `fs.promises`, `fetch`.

### Phase 11: Language Parity & Generics
- **Goal:** Reach full feature parity with core TypeScript.
- **Status:** 📅 PLANNED
- **Features:** Generics, Modules (ESM), Tuples, Enums, Optional/Default/Rest Parameters, `unknown`, `never`, `for..in`.
