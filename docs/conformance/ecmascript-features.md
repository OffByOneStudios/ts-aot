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
| Strict mode (`"use strict"`) | ⚠️ | Parsed, not enforced |
| Trailing commas in objects/arrays | ✅ | |
| Multiline strings (backslash) | ✅ | |
| Reserved words as property names | ✅ | |

### Array Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `Array.isArray()` | ✅ | |
| `Array.prototype.indexOf()` | ✅ | |
| `Array.prototype.lastIndexOf()` | ✅ | |
| `Array.prototype.every()` | ⚠️ | Known bug |
| `Array.prototype.some()` | ⚠️ | Known bug |
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
| `Object.entries()` | ⚠️ | Known bug |
| `Object.create()` | ❌ | |
| `Object.defineProperty()` | ❌ | |
| `Object.defineProperties()` | ❌ | |
| `Object.getOwnPropertyDescriptor()` | ❌ | |
| `Object.getOwnPropertyNames()` | ❌ | |
| `Object.getPrototypeOf()` | ❌ | |
| `Object.freeze()` | ❌ | |
| `Object.seal()` | ❌ | |
| `Object.preventExtensions()` | ❌ | |
| `Object.isFrozen()` | ❌ | |
| `Object.isSealed()` | ❌ | |
| `Object.isExtensible()` | ❌ | |

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
| `String.prototype.replace()` | ⚠️ | Basic support |

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
| `Function.prototype.bind()` | ❌ | |
| Property getters/setters | ❌ | |

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
| Tagged templates | ❌ | |
| `String.raw` | ❌ | |

### Destructuring
| Feature | Status | Notes |
|---------|--------|-------|
| Array destructuring | ❌ | Compilation error |
| Object destructuring | ❌ | Compilation error |
| Default values | ❌ | |
| Rest elements | ❌ | |
| Nested destructuring | ❌ | |
| Parameter destructuring | ❌ | |

### Spread Operator
| Feature | Status | Notes |
|---------|--------|-------|
| Array spread `[...arr]` | ⚠️ | Basic support |
| Object spread `{...obj}` | ❌ | Compilation error |
| Function call spread `fn(...args)` | ⚠️ | |

### Rest Parameters
| Feature | Status | Notes |
|---------|--------|-------|
| Rest parameters `...args` | ✅ | |

### Default Parameters
| Feature | Status | Notes |
|---------|--------|-------|
| Default parameter values | ✅ | |
| Expressions as defaults | ⚠️ | |

### Enhanced Object Literals
| Feature | Status | Notes |
|---------|--------|-------|
| Shorthand properties | ✅ | |
| Shorthand methods | ⚠️ | Known bug |
| Computed property names | ✅ | |

### Classes
| Feature | Status | Notes |
|---------|--------|-------|
| Class declarations | ✅ | |
| Class expressions | ❌ | |
| Constructor method | ✅ | |
| Instance methods | ✅ | |
| Static methods | ✅ | |
| `extends` keyword | ✅ | |
| `super` keyword | ✅ | |
| Getters and setters | ❌ | |

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
| Generator functions | ❌ | |
| `yield` expression | ❌ | |
| `yield*` delegation | ❌ | |
| Generator methods | ❌ | |

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
| `Symbol()` | ⚠️ | Basic support |
| `Symbol.for()` | ❌ | |
| `Symbol.keyFor()` | ❌ | |
| Well-known symbols | ⚠️ | `Symbol.iterator` only |

### Collections
| Feature | Status | Notes |
|---------|--------|-------|
| `Map` | ✅ | |
| `Set` | ✅ | |
| `WeakMap` | ❌ | |
| `WeakSet` | ❌ | |
| `Map.prototype.get/set/has/delete` | ✅ | |
| `Set.prototype.add/has/delete` | ✅ | |
| `Map.prototype.forEach` | ✅ | |
| `Set.prototype.forEach` | ✅ | |

### Typed Arrays
| Feature | Status | Notes |
|---------|--------|-------|
| `ArrayBuffer` | ⚠️ | Via Buffer |
| `DataView` | ❌ | |
| `Int8Array` | ❌ | |
| `Uint8Array` | ⚠️ | Via Buffer |
| `Int16Array` | ❌ | |
| `Uint16Array` | ❌ | |
| `Int32Array` | ❌ | |
| `Uint32Array` | ❌ | |
| `Float32Array` | ❌ | |
| `Float64Array` | ❌ | |

### Proxy and Reflect
| Feature | Status | Notes |
|---------|--------|-------|
| `Proxy` constructor | ❌ | |
| Proxy handlers | ❌ | |
| `Reflect` methods | ❌ | |
| Revocable proxies | ❌ | |

### Number
| Feature | Status | Notes |
|---------|--------|-------|
| `Number.isFinite()` | ❌ | |
| `Number.isNaN()` | ❌ | |
| `Number.isInteger()` | ❌ | |
| `Number.isSafeInteger()` | ❌ | |
| `Number.EPSILON` | ❌ | |
| `Number.MAX_SAFE_INTEGER` | ❌ | |
| `Number.MIN_SAFE_INTEGER` | ❌ | |
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
| `Math.sign()` | ❌ | |
| `Math.trunc()` | ❌ | |
| `Math.cbrt()` | ❌ | |
| `Math.log10()` | ❌ | |
| `Math.log2()` | ❌ | |
| `Math.expm1()` | ❌ | |
| `Math.log1p()` | ❌ | |
| `Math.hypot()` | ❌ | |
| `Math.fround()` | ❌ | |
| `Math.clz32()` | ❌ | |
| `Math.imul()` | ❌ | |

### String (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| `String.prototype.includes()` | ✅ | |
| `String.prototype.startsWith()` | ✅ | |
| `String.prototype.endsWith()` | ✅ | |
| `String.prototype.repeat()` | ✅ | |
| `String.fromCodePoint()` | ❌ | |
| `String.prototype.codePointAt()` | ❌ | |
| `String.prototype.normalize()` | ❌ | |
| Unicode escapes (`\u{}`) | ⚠️ | |

### Array (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| `Array.from()` | ❌ | |
| `Array.of()` | ❌ | |
| `Array.prototype.find()` | ⚠️ | Known bug |
| `Array.prototype.findIndex()` | ✅ | |
| `Array.prototype.fill()` | ❌ | |
| `Array.prototype.copyWithin()` | ❌ | |
| `Array.prototype.entries()` | ❌ | |
| `Array.prototype.keys()` | ❌ | |
| `Array.prototype.values()` | ❌ | |

### RegExp (ES6)
| Feature | Status | Notes |
|---------|--------|-------|
| Sticky flag (`y`) | ❌ | |
| Unicode flag (`u`) | ❌ | |
| `RegExp.prototype.flags` | ❌ | |

---

## ES2016 (ES7)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.includes()` | ⚠️ | Known bug |
| Exponentiation operator (`**`) | ✅ | |

---

## ES2017 (ES8)

| Feature | Status | Notes |
|---------|--------|-------|
| `async` functions | ✅ | |
| `await` expression | ✅ | |
| `Object.values()` | ✅ | |
| `Object.entries()` | ⚠️ | Known bug |
| `String.prototype.padStart()` | ❌ | |
| `String.prototype.padEnd()` | ❌ | |
| `Object.getOwnPropertyDescriptors()` | ❌ | |
| Trailing commas in function params | ✅ | |
| Shared memory and atomics | ❌ | |

---

## ES2018 (ES9)

| Feature | Status | Notes |
|---------|--------|-------|
| Async iteration (`for await...of`) | ❌ | |
| Rest/spread properties | ❌ | Object spread |
| `Promise.prototype.finally()` | ✅ | |
| RegExp named capture groups | ❌ | |
| RegExp lookbehind assertions | ❌ | |
| RegExp Unicode property escapes | ❌ | |
| RegExp `s` (dotAll) flag | ❌ | |

---

## ES2019 (ES10)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.flat()` | ❌ | |
| `Array.prototype.flatMap()` | ❌ | |
| `Object.fromEntries()` | ❌ | |
| `String.prototype.trimStart()` | ❌ | |
| `String.prototype.trimEnd()` | ❌ | |
| Optional catch binding | ❌ | |
| `Symbol.prototype.description` | ❌ | |
| Well-formed `JSON.stringify` | ✅ | |
| Revised `Function.prototype.toString` | ❌ | |

---

## ES2020 (ES11)

| Feature | Status | Notes |
|---------|--------|-------|
| `BigInt` | ❌ | |
| Dynamic `import()` | ❌ | |
| Nullish coalescing (`??`) | ❌ | |
| Optional chaining (`?.`) | ❌ | |
| `Promise.allSettled()` | ❌ | |
| `String.prototype.matchAll()` | ❌ | |
| `globalThis` | ⚠️ | |
| `import.meta` | ❌ | |
| Export namespace (`export * as ns`) | ❌ | |

---

## ES2021 (ES12)

| Feature | Status | Notes |
|---------|--------|-------|
| `String.prototype.replaceAll()` | ❌ | |
| `Promise.any()` | ✅ | |
| `WeakRef` | ❌ | |
| `FinalizationRegistry` | ❌ | |
| Logical assignment (`&&=`, `\|\|=`, `??=`) | ❌ | |
| Numeric separators (`1_000_000`) | ✅ | |

---

## ES2022 (ES13)

| Feature | Status | Notes |
|---------|--------|-------|
| Top-level `await` | ❌ | |
| Class fields (public) | ✅ | |
| Class fields (private `#`) | ❌ | |
| Private methods | ❌ | |
| Static class blocks | ❌ | |
| `Array.prototype.at()` | ❌ | |
| `String.prototype.at()` | ❌ | |
| `Object.hasOwn()` | ❌ | |
| RegExp match indices (`d` flag) | ❌ | |
| Error `.cause` property | ❌ | |

---

## ES2023 (ES14)

| Feature | Status | Notes |
|---------|--------|-------|
| `Array.prototype.findLast()` | ❌ | |
| `Array.prototype.findLastIndex()` | ❌ | |
| `Array.prototype.toReversed()` | ❌ | |
| `Array.prototype.toSorted()` | ❌ | |
| `Array.prototype.toSpliced()` | ❌ | |
| `Array.prototype.with()` | ❌ | |
| Hashbang (`#!`) support | ❌ | |
| Symbols as WeakMap keys | ❌ | |

---

## ES2024 (ES15)

| Feature | Status | Notes |
|---------|--------|-------|
| `Object.groupBy()` | ❌ | |
| `Map.groupBy()` | ❌ | |
| `Promise.withResolvers()` | ❌ | |
| RegExp `/v` flag | ❌ | |
| Resizable `ArrayBuffer` | ❌ | |
| Growable `SharedArrayBuffer` | ❌ | |
| `String.prototype.isWellFormed()` | ❌ | |
| `String.prototype.toWellFormed()` | ❌ | |
| `Atomics.waitAsync()` | ❌ | |

---

## Summary by ES Version

| Version | Implemented | Partial | Not Implemented | Total | % |
|---------|-------------|---------|-----------------|-------|---|
| ES5 | 22 | 3 | 19 | 44 | 50% |
| ES2015 | 48 | 13 | 47 | 108 | 44% |
| ES2016 | 1 | 1 | 0 | 2 | 50% |
| ES2017 | 4 | 1 | 4 | 9 | 44% |
| ES2018 | 1 | 0 | 7 | 8 | 13% |
| ES2019 | 1 | 0 | 8 | 9 | 11% |
| ES2020 | 0 | 1 | 9 | 10 | 0% |
| ES2021 | 2 | 0 | 4 | 6 | 33% |
| ES2022 | 1 | 0 | 9 | 10 | 10% |
| ES2023 | 0 | 0 | 8 | 8 | 0% |
| ES2024 | 0 | 0 | 9 | 9 | 0% |
| **TOTAL** | **80** | **19** | **124** | **223** | **36%** |

**Overall ECMAScript Conformance: 80/223 features (36%)**

---

## Priority Features (High Usage)

These features should be prioritized for implementation:

### Critical (Blocking Real-World Code)
1. ❌ Destructuring (array and object)
2. ❌ Optional chaining (`?.`)
3. ❌ Nullish coalescing (`??`)
4. ❌ Object spread (`{...obj}`)
5. ❌ `Object.assign()`

### High Priority
6. ❌ Generator functions
7. ❌ `Array.from()`
8. ❌ `Array.prototype.flat()`
9. ❌ `WeakMap` / `WeakSet`
10. ❌ `Proxy` / `Reflect`

### Medium Priority
11. ❌ Private class fields (`#`)
12. ❌ Dynamic `import()`
13. ❌ `BigInt`
14. ❌ Top-level `await`
15. ❌ `for await...of`
