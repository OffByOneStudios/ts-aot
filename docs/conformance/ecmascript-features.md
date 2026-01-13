# ECMAScript Features Conformance

This document tracks ts-aot's conformance with ECMAScript (JavaScript) language features.

**Sources:**
- [ECMA-262 Specification](https://tc39.es/ecma262/)
- [ES6 Features Overview](https://exploringjs.com/es6/ch_overviews.html)
- [ECMAScript Features Cheatsheet](https://github.com/sudheerj/ECMAScript-features)

**Legend:**
- ✅ Implemented
- ⚠️ Partial
- ❌ Not implemented

---

## ES5 (2009) - Baseline

### Syntax
| Feature | Status | Notes |
|---------|--------|-------|
| Strict mode (`"use strict"`) | ✅ | Duplicate params, reserved names, duplicate props |
| Trailing commas in objects/arrays | ✅ | |
| Multiline strings (backslash) | ✅ | |
| Reserved words as property names | ✅ | |

### Array Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `Array.isArray()` | ✅ | |
| `Array.prototype.indexOf()` | ✅ | |
| `Array.prototype.lastIndexOf()` | ✅ | |
| `Array.prototype.every()` | ✅ | |
| `Array.prototype.some()` | ✅ | |
| `Array.prototype.forEach()` | ✅ | |
| `Array.prototype.map()` | ✅ | |
| `Array.prototype.filter()` | ✅ | |
| `Array.prototype.reduce()` | ✅ | |
| `Array.prototype.reduceRight()` | ✅ | |

### Object Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `Object.keys()` | ✅ | |
| `Object.values()` | ✅ | |
| `Object.entries()` | ✅ | |
| `Object.create()` | ⚠️ | Copies properties (no prototype chain) |
| `Object.defineProperty()` | ✅ | Supports value/get/set descriptors |
| `Object.defineProperties()` | ✅ | Supports value/get/set descriptors |
| `Object.getOwnPropertyDescriptor()` | ✅ | |
| `Object.getOwnPropertyNames()` | ✅ | |
| `Object.getPrototypeOf()` | ⚠️ | Returns undefined (no prototype chain) |
| `Object.freeze()` | ✅ | |
| `Object.seal()` | ✅ | |
| `Object.preventExtensions()` | ✅ | |
| `Object.isFrozen()` | ✅ | |
| `Object.isSealed()` | ✅ | |
| `Object.isExtensible()` | ✅ | |

### String Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `String.prototype.trim()` | ✅ | |
| `String.prototype.charAt()` | ✅ | |
| `String.prototype.charCodeAt()` | ✅ | |
| `String.prototype.indexOf()` | ✅ | |
| `String.prototype.lastIndexOf()` | ✅ | |
| `String.prototype.split()` | ✅ | |
| `String.prototype.substring()` | ✅ | |
| `String.prototype.slice()` | ✅ | |
| `String.prototype.toLowerCase()` | ✅ | |
| `String.prototype.toUpperCase()` | ✅ | |
| `String.prototype.replace()` | ✅ | String and regex replacement |

### JSON
| Feature | Status | Notes |
|---------|--------|-------|
| `JSON.parse()` | ✅ | |
| `JSON.stringify()` | ✅ | |

### Other
| Feature | Status | Notes |
|---------|--------|-------|
| `Date.now()` | ✅ | |
| `Date.prototype.toISOString()` | ✅ | |
| `Date.prototype.toJSON()` | ✅ | |
| `Function.prototype.bind()` | ✅ | Partial application and `this` binding work |
| Property getters/setters | ✅ | Object literals supported |

---

## ES6 / ES2015

### Variable Declarations
| Feature | Status | Notes |
|---------|--------|-------|
| `let` declarations | ✅ | |
| `const` declarations | ✅ | |
| Block scoping | ✅ | |
| Temporal dead zone | ✅ | |

### Arrow Functions
| Feature | Status | Notes |
|---------|--------|-------|
| Arrow function syntax | ✅ | |
| Lexical `this` binding | ✅ | |
| Implicit return | ✅ | |
| No `arguments` object | ✅ | |

### Template Literals
| Feature | Status | Notes |
|---------|--------|-------|
| Basic template literals | ✅ | |
| Expression interpolation | ✅ | |
| Multi-line strings | ✅ | |
| Tagged templates | ✅ | User-defined tag functions work |
| `String.raw` | ✅ | |

### Destructuring
| Feature | Status | Notes |
|---------|--------|-------|
| Array destructuring | ✅ | |
| Object destructuring | ✅ | |
| Default values | ✅ | |
| Rest elements | ✅ | Array rest only |
| Nested destructuring | ✅ | Objects, arrays, and mixed nesting |
| Parameter destructuring | ✅ | Object and array patterns in function params |

### Spread Operator
| Feature | Status | Notes |
|---------|--------|-------|
| Array spread `[...arr]` | ⚠️ | Basic support |
| Object spread `{...obj}` | ✅ | |
| Function call spread `fn(...args)` | ⚠️ | |

### Rest Parameters
| Feature | Status | Notes |
|---------|--------|-------|
| Rest parameters `...args` | ✅ | |

### Default Parameters
| Feature | Status | Notes |
|---------|--------|-------|
| Default parameter values | ✅ | |
| Expressions as defaults | ✅ | Function calls, arithmetic, previous params |

### Enhanced Object Literals
| Feature | Status | Notes |
|---------|--------|-------|
| Shorthand properties | ✅ | |
| Shorthand methods | ✅ | |
| Computed property names | ✅ | |

### Classes
| Feature | Status | Notes |
|---------|--------|-------|
| Class declarations | ✅ | |
| Class expressions | ✅ | Named and anonymous |
| Constructor method | ✅ | |
| Instance methods | ✅ | |
| Static methods | ✅ | |
| `extends` keyword | ✅ | |
| `super` keyword | ✅ | |
| Getters and setters | ✅ | VTable-based dispatch |

### Modules
| Feature | Status | Notes |
|---------|--------|-------|
| `import` declaration | ✅ | |
| `export` declaration | ✅ | |
| Default export | ✅ | |
| Named exports | ✅ | |
| Namespace imports (`* as`) | ✅ | |
| Re-exports | ✅ | |

### Iterators and for-of
| Feature | Status | Notes |
|---------|--------|-------|
| `for...of` loops | ✅ | |
| `Symbol.iterator` | ⚠️ | |
| Custom iterables | ❌ | |

### Generators
| Feature | Status | Notes |
|---------|--------|-------|
| Generator functions | ✅ | Basic function* and yield work |
| `yield` expression | ✅ | |
| `yield*` delegation | ✅ | Works with generators and arrays |
| Generator methods | ✅ | *method() syntax works |

### Promises
| Feature | Status | Notes |
|---------|--------|-------|
| `Promise` constructor | ✅ | |
| `Promise.resolve()` | ✅ | |
| `Promise.reject()` | ✅ | |
| `Promise.all()` | ✅ | |
| `Promise.race()` | ✅ | |
| `then()` | ✅ | |
| `catch()` | ✅ | |
| `finally()` | ✅ | |

### Symbols
| Feature | Status | Notes |
|---------|--------|-------|
| `Symbol()` | ✅ | Description, uniqueness, and as property keys |
| `Symbol.for()` | ✅ | Global symbol registry |
| `Symbol.keyFor()` | ✅ | Returns undefined for non-registered symbols |
| Well-known symbols | ⚠️ | `Symbol.iterator` only |

### Collections
| Feature | Status | Notes |
|---------|--------|-------|
| `Map` | ✅ | |
| `Set` | ✅ | |
| `WeakMap` | ✅ | Implemented as Map (no true weak semantics with Boehm GC) |
| `WeakSet` | ✅ | Implemented as Set (no true weak semantics with Boehm GC) |
| `Map.prototype.get/set/has/delete` | ✅ | |
| `Set.prototype.add/has/delete` | ✅ | |
| `Map.prototype.forEach` | ✅ | |
| `Set.prototype.forEach` | ✅ | |

### Typed Arrays
| Feature | Status | Notes |
|---------|--------|-------|
| `ArrayBuffer` | ⚠️ | Via Buffer |
| `DataView` | ✅ | All get/set methods for Int8/Uint8/Int16/Uint16/Int32/Uint32/Float32/Float64 |
| `Int8Array` | ✅ | |
| `Uint8Array` | ✅ | |
| `Uint8ClampedArray` | ✅ | Values clamped to 0-255 |
| `Int16Array` | ✅ | |
| `Uint16Array` | ✅ | |
| `Int32Array` | ✅ | |
| `Uint32Array` | ✅ | |
| `Float32Array` | ✅ | |
| `Float64Array` | ✅ | |

### Proxy and Reflect
| Feature | Status | Notes |
|---------|--------|-------|
| `Proxy` constructor | ✅ | |
| Proxy handlers | ⚠️ | get, set, has, deleteProperty, ownKeys, apply work; construct partial |
| `Reflect` methods | ⚠️ | get, set, has, deleteProperty, ownKeys, defineProperty, getOwnPropertyDescriptor, isExtensible, preventExtensions |
| Revocable proxies | ✅ | Proxy.revocable returns { proxy, revoke } correctly |

### Number
| Feature | Status | Notes |
|---------|--------|-------|
| `Number.isFinite()` | ✅ | |
| `Number.isNaN()` | ✅ | |
| `Number.isInteger()` | ✅ | |
| `Number.isSafeInteger()` | ✅ | |
| `Number.EPSILON` | ✅ | |
| `Number.MAX_SAFE_INTEGER` | ✅ | |
| `Number.MIN_SAFE_INTEGER` | ✅ | |
| Binary literals (`0b`) | ✅ | |
| Octal literals (`0o`) | ✅ | |

### Math
| Feature | Status | Notes |
|---------|--------|-------|
| `Math.abs()` | ✅ | |
| `Math.ceil()` | ✅ | |
| `Math.floor()` | ✅ | |
| `Math.round()` | ✅ | |
| `Math.min()` | ✅ | |
| `Math.max()` | ✅ | |
| `Math.pow()` | ✅ | |
| `Math.sqrt()` | ✅ | |
| `Math.random()` | ✅ | |
| `Math.sign()` | ✅ | |
| `Math.trunc()` | ✅ | |
| `Math.cbrt()` | ✅ | |
| `Math.log10()` | ✅ | |
| `Math.log2()` | ✅ | |
| `Math.expm1()` | ✅ | |
| `Math.log1p()` | ✅ | |
| `Math.hypot()` | ✅ | |
| `Math.fround()` | ✅ | |
| `Math.clz32()` | ✅ | |
| `Math.imul()` | ✅ | |

### String (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| `String.prototype.includes()` | ✅ | |
| `String.prototype.startsWith()` | ✅ | |
| `String.prototype.endsWith()` | ✅ | |
| `String.prototype.repeat()` | ✅ | |
| `String.fromCodePoint()` | ✅ | |
| `String.prototype.codePointAt()` | ✅ | |
| `String.prototype.normalize()` | ✅ | NFC, NFD, NFKC, NFKD forms |
| Unicode escapes (`\u{}`) | ✅ | Extended code points work |

### Array (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| `Array.from()` | ✅ | Supports arrays, mapFn |
| `Array.of()` | ✅ | |
| `Array.prototype.find()` | ✅ | |
| `Array.prototype.findIndex()` | ✅ | |
| `Array.prototype.fill()` | ✅ | |
| `Array.prototype.copyWithin()` | ✅ | |
| `Array.prototype.entries()` | ⚠️ | Returns array, not iterator |
| `Array.prototype.keys()` | ✅ | Returns array (iterator-like) |
| `Array.prototype.values()` | ✅ | Returns array (iterator-like) |

### Object (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| `Object.assign()` | ✅ | |
| `Object.is()` | ✅ | SameValue comparison (NaN=NaN, 0≠-0) |
| `Object.setPrototypeOf()` | ⚠️ | Stub - returns object unchanged (no prototype chain) |

### RegExp (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| Sticky flag (`y`) | ✅ | Supported via ICU |
| Unicode flag (`u`) | ✅ | ICU handles unicode mode |
| `RegExp.prototype.flags` | ✅ | |

---

## ES2016 (ES7)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.includes()` | ✅ | |
| Exponentiation operator (`**`) | ✅ | |

---

## ES2017 (ES8)

| Feature | Status | Notes |
|---------|--------|-------|
| `async` functions | ✅ | |
| `await` expression | ✅ | |
| `Object.values()` | ✅ | |
| `Object.entries()` | ✅ | |
| `String.prototype.padStart()` | ✅ | |
| `String.prototype.padEnd()` | ✅ | |
| `Object.getOwnPropertyDescriptors()` | ✅ | |
| Trailing commas in function params | ✅ | |
| Shared memory and atomics | ❌ | |

---

## ES2018 (ES9)

| Feature | Status | Notes |
|---------|--------|-------|
| Async iteration (`for await...of`) | ✅ | Works with arrays of promises |
| Rest/spread properties | ✅ | Object spread |
| `Promise.prototype.finally()` | ✅ | |
| RegExp named capture groups | ✅ | Indexed access and .groups property both work |
| RegExp lookbehind assertions | ✅ | ICU supports (?<=) and (?<!) |
| RegExp Unicode property escapes | ✅ | ICU supports \\p{} and \\P{} |
| RegExp `s` (dotAll) flag | ✅ | ICU regex supports DOTALL |

---

## ES2019 (ES10)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.flat()` | ✅ | |
| `Array.prototype.flatMap()` | ✅ | |
| `Object.fromEntries()` | ✅ | |
| `String.prototype.trimStart()` | ✅ | Also supports trimLeft alias |
| `String.prototype.trimEnd()` | ✅ | Also supports trimRight alias |
| Optional catch binding | ✅ | `catch { }` without parameter |
| `Symbol.prototype.description` | ✅ | ES2019 getter |
| Well-formed `JSON.stringify` | ✅ | |
| Revised `Function.prototype.toString` | ❌ | |

---

## ES2020 (ES11)

| Feature | Status | Notes |
|---------|--------|-------|
| `BigInt` | ✅ | Arithmetic, comparison, bitwise ops |
| Dynamic `import()` | ❌ | |
| Nullish coalescing (`??`) | ✅ | |
| Optional chaining (`?.`) | ✅ | Property access only |
| `Promise.allSettled()` | ✅ | |
| `String.prototype.matchAll()` | ✅ | Returns array of match results |
| `globalThis` | ✅ | Alias for global |
| `import.meta` | ❌ | |
| Export namespace (`export * as ns`) | ✅ | Functions and types work |

---

## ES2021 (ES12)

| Feature | Status | Notes |
|---------|--------|-------|
| `String.prototype.replaceAll()` | ✅ | |
| `Promise.any()` | ✅ | |
| `WeakRef` | ❌ | |
| `FinalizationRegistry` | ❌ | |
| Logical assignment (`&&=`, `\|\|=`, `??=`) | ✅ | Short-circuit semantics |
| Numeric separators (`1_000_000`) | ✅ | |

---

## ES2022 (ES13)

| Feature | Status | Notes |
|---------|--------|-------|
| Top-level `await` | ❌ | |
| Class fields (public) | ✅ | |
| Class fields (private `#`) | ✅ | Increment, compound assignment, direct assignment |
| Private methods | ✅ | |
| Static class blocks | ✅ | Top-level and local classes |
| `Array.prototype.at()` | ✅ | |
| `String.prototype.at()` | ✅ | Supports negative indices |
| `Object.hasOwn()` | ✅ | |
| RegExp match indices (`d` flag) | ✅ | hasIndices property and exec() indices array |
| Error `.cause` property | ✅ | Error constructor options and cause property |

---

## ES2023 (ES14)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.findLast()` | ✅ | |
| `Array.prototype.findLastIndex()` | ✅ | |
| `Array.prototype.toReversed()` | ✅ | |
| `Array.prototype.toSorted()` | ✅ | |
| `Array.prototype.toSpliced()` | ✅ | |
| `Array.prototype.with()` | ✅ | |
| Hashbang (`#!`) support | ✅ | TypeScript parser handles natively |
| Symbols as WeakMap keys | ✅ | WeakMap uses Map internally, supports symbols |

---

## ES2024 (ES15)

| Feature | Status | Notes |
|---------|--------|-------|
| `Object.groupBy()` | ✅ | Groups array elements by callback return value |
| `Map.groupBy()` | ✅ | Groups array elements, returns Map with key type preserved |
| `Promise.withResolvers()` | ✅ | Returns { promise, resolve, reject } |
| RegExp `/v` flag | ❌ | |
| Resizable `ArrayBuffer` | ❌ | |
| Growable `SharedArrayBuffer` | ❌ | |
| `String.prototype.isWellFormed()` | ✅ | Checks for lone surrogates |
| `String.prototype.toWellFormed()` | ✅ | Replaces lone surrogates with U+FFFD |
| `Atomics.waitAsync()` | ❌ | |

---

## Summary by ES Version

| Version | Implemented | Partial | Not Implemented | Total | % |
|---------|-------------|---------|-----------------|-------|---|
| ES5 | 45 | 2 | 0 | 47 | 96% |
| ES2015 | 101 | 11 | 0 | 112 | 90% |
| ES2016 | 2 | 0 | 0 | 2 | 100% |
| ES2017 | 8 | 0 | 1 | 9 | 89% |
| ES2018 | 7 | 0 | 1 | 8 | 88% |
| ES2019 | 8 | 0 | 1 | 9 | 89% |
| ES2020 | 7 | 0 | 3 | 10 | 70% |
| ES2021 | 4 | 0 | 2 | 6 | 67% |
| ES2022 | 9 | 0 | 1 | 10 | 90% |
| ES2023 | 8 | 0 | 0 | 8 | 100% |
| ES2024 | 5 | 0 | 4 | 9 | 56% |
| **TOTAL** | **201** | **13** | **16** | **230** | **87%** |

**Overall ECMAScript Conformance: 201/230 features (87%)**

---

## Priority Features (High Usage)

These features should be prioritized for implementation:

### Critical (Blocking Real-World Code)
1. ✅ Destructuring (array and object) - Implemented
2. ✅ Optional chaining (`?.`) - Property access implemented
3. ✅ Nullish coalescing (`??`) - Implemented
4. ✅ Object spread (`{...obj}`) - Implemented
5. ✅ `Object.assign()` - Implemented

### High Priority
6. ✅ Generator functions - Implemented (function*, yield, yield*)
7. ✅ `Array.from()` - Implemented
8. ✅ `Array.prototype.flat()` - Implemented
9. ✅ `WeakMap` / `WeakSet` - Implemented
10. ⚠️ `Proxy` / `Reflect` - Basic traps work (get, set, has, deleteProperty)

### Medium Priority
11. ✅ Private class fields (`#`) - Implemented
12. ❌ Dynamic `import()`
13. ✅ `BigInt` - Implemented
14. ❌ Top-level `await`
15. ✅ `for await...of` - Implemented
