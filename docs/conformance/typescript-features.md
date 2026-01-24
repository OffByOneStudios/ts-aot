# TypeScript Language Features Conformance

This document tracks ts-aot's conformance with TypeScript language features.

**Source:** [TypeScript Handbook](https://www.typescriptlang.org/docs/handbook/intro.html)

**Legend:**
- ✅ Implemented
- ⚠️ Partial
- ❌ Not implemented
- 🔬 Not applicable (type-only, erased at compile time)

---

## 1. Basic Types

| Feature | Status | Notes |
|---------|--------|-------|
| `boolean` | ✅ | |
| `number` | ✅ | |
| `string` | ✅ | |
| `array` (T[] and Array<T>) | ✅ | |
| `tuple` | ✅ | Basic, rest elements, and optional elements |
| `enum` | ✅ | Numeric, string, and heterogeneous enums |
| `unknown` | 🔬 | Type-only |
| `any` | ✅ | Compiles to boxed TsValue |
| `void` | ✅ | |
| `null` | ✅ | |
| `undefined` | ✅ | |
| `never` | 🔬 | Type-only |
| `object` | ✅ | |
| `bigint` | ✅ | Arbitrary precision integers |
| `symbol` | ✅ | Description, uniqueness, Symbol.for/keyFor, as property keys |

## 2. Variable Declarations

| Feature | Status | Notes |
|---------|--------|-------|
| `var` | ✅ | |
| `let` | ✅ | |
| `const` | ✅ | |
| Block scoping | ✅ | |
| Temporal dead zone | ✅ | |
| Re-declaration rules | ✅ | |

## 3. Interfaces

| Feature | Status | Notes |
|---------|--------|-------|
| Basic interface | 🔬 | Type-only, structural |
| Optional properties (`?`) | ✅ | |
| Readonly properties | 🔬 | Type-only |
| Excess property checks | 🔬 | Type-only |
| Function types | ✅ | |
| Indexable types | ✅ | Classes and interfaces with [key: string]: T |
| Class implementing interface | ✅ | |
| Interface extending interface | 🔬 | Type-only |
| Hybrid types | ⚠️ | |

## 4. Type Aliases

| Feature | Status | Notes |
|---------|--------|-------|
| Basic type alias | 🔬 | Type-only |
| Generic type alias | 🔬 | Type-only |
| Conditional types | ❌ | |
| Mapped types | ❌ | |
| Template literal types | ❌ | |

## 5. Classes

| Feature | Status | Notes |
|---------|--------|-------|
| Class declaration | ✅ | |
| Constructor | ✅ | |
| Properties | ✅ | |
| Methods | ✅ | |
| Getters/Setters | ✅ | Object literals and classes |
| `public` modifier | ✅ | |
| `private` modifier | ✅ | |
| `protected` modifier | ✅ | |
| `readonly` modifier | 🔬 | Type-only |
| Parameter properties | ✅ | |
| Static members | ✅ | |
| Static blocks | ✅ | Top-level and local classes |
| Abstract classes | ✅ | |
| Abstract methods | ✅ | |
| Class expressions | ✅ | Named and anonymous |
| `extends` (inheritance) | ✅ | |
| `implements` | ✅ | |
| `super` calls | ✅ | |
| `this` type | ✅ | Method chaining with `this` return type |
| Index signatures in classes | ✅ | Dynamic properties via TsMap |

## 6. Functions

| Feature | Status | Notes |
|---------|--------|-------|
| Function declaration | ✅ | |
| Function expression | ✅ | |
| Arrow function | ✅ | |
| Optional parameters | ✅ | |
| Default parameters | ✅ | |
| Rest parameters | ✅ | |
| `this` parameter | ✅ | Explicit this param type enforced |
| Overloads | ✅ | Function and method overloads work |
| Generic functions | ✅ | Monomorphized |
| Call signatures | ✅ | |
| Construct signatures | ✅ | |

## 7. Generics

| Feature | Status | Notes |
|---------|--------|-------|
| Generic functions | ✅ | Monomorphized |
| Generic classes | ✅ | Monomorphized |
| Generic interfaces | 🔬 | Type-only |
| Generic constraints (`extends`) | ✅ | Interface and type parameter constraints |
| `keyof` constraint | ✅ | Works with generic functions |
| Default type parameters | ✅ | Parsed and used when type args not provided |
| Generic parameter variance | 🔬 | Type-only |

## 8. Type Manipulation

| Feature | Status | Notes |
|---------|--------|-------|
| `keyof` operator | ✅ | Object property access by dynamic key |
| `typeof` operator | ✅ | Runtime typeof |
| Indexed access types | ✅ | T['prop'] resolves to property type |
| Conditional types | ❌ | |
| Mapped types | ❌ | |
| Template literal types | ❌ | |
| `infer` keyword | ❌ | |

## 9. Utility Types

| Feature | Status | Notes |
|---------|--------|-------|
| `Partial<T>` | 🔬 | Type-only |
| `Required<T>` | 🔬 | Type-only |
| `Readonly<T>` | 🔬 | Type-only |
| `Record<K,V>` | 🔬 | Type-only |
| `Pick<T,K>` | 🔬 | Type-only |
| `Omit<T,K>` | 🔬 | Type-only |
| `Exclude<T,U>` | 🔬 | Type-only |
| `Extract<T,U>` | 🔬 | Type-only |
| `NonNullable<T>` | 🔬 | Type-only |
| `Parameters<T>` | 🔬 | Type-only |
| `ConstructorParameters<T>` | 🔬 | Type-only |
| `ReturnType<T>` | 🔬 | Type-only |
| `InstanceType<T>` | 🔬 | Type-only |
| `ThisParameterType<T>` | 🔬 | Type-only |
| `OmitThisParameter<T>` | 🔬 | Type-only |
| `ThisType<T>` | 🔬 | Type-only |
| `Awaited<T>` | 🔬 | Type-only |

## 10. Modules

| Feature | Status | Notes |
|---------|--------|-------|
| `import` statement | ✅ | |
| `export` statement | ✅ | |
| Default exports | ✅ | |
| Named exports | ✅ | |
| `export * from` (re-export) | ✅ | |
| `export * as ns from` | ✅ | Functions and types work |
| `import type` | 🔬 | Type-only |
| `export type` | 🔬 | Type-only |
| Dynamic `import()` | ❌ | |
| `require()` (CommonJS) | ✅ | |
| `module.exports` | ✅ | Property setting and reassignment |
| JSON imports | ✅ | Compile-time embedding |

## 11. Namespaces

| Feature | Status | Notes |
|---------|--------|-------|
| `namespace` declaration | ❌ | |
| Namespace merging | ❌ | |
| Ambient namespaces | ❌ | |

## 12. Enums

| Feature | Status | Notes |
|---------|--------|-------|
| Numeric enums | ✅ | Auto-increment and explicit values |
| String enums | ✅ | |
| Heterogeneous enums | ✅ | Mixed numeric and string values |
| Computed members | ✅ | Arithmetic, string.length, Math.floor/ceil/round/trunc/abs |
| `const` enums | ✅ | Inlined at compile time |
| Ambient enums | ✅ | declare enum with explicit values |
| Reverse mappings | ✅ | `Color[0]` returns member name |

## 13. Type Narrowing

| Feature | Status | Notes |
|---------|--------|-------|
| `typeof` guards | ✅ | |
| Truthiness narrowing | ✅ | |
| Equality narrowing | ✅ | |
| `in` operator narrowing | ✅ | Object property checks work |
| `instanceof` narrowing | ✅ | Class and Array checks |
| Control flow analysis | ✅ | typeof, truthiness, null check narrowing |
| Type predicates (`is`) | ✅ | Returns boolean at runtime |
| Discriminated unions | ✅ | Switch/if on string literal property works |
| `never` type exhaustiveness | ✅ | Works in switch with default case |
| `asserts` keyword | ⚠️ | Syntax compiles, runtime narrowing not supported |

## 14. Union and Intersection Types

| Feature | Status | Notes |
|---------|--------|-------|
| Union types (`\|`) | ✅ | typeof, Array.isArray, in narrowing |
| Intersection types (`&`) | ✅ | Object literals merge properties |
| Type guards | ✅ | typeof, in, Array.isArray |
| Discriminated unions | ✅ | Switch/if on string literal property works |

## 15. Literal Types

| Feature | Status | Notes |
|---------|--------|-------|
| String literals | 🔬 | Type-only |
| Numeric literals | 🔬 | Type-only |
| Boolean literals | 🔬 | Type-only |
| `as const` assertions | ✅ | Pass-through at runtime |

## 16. Decorators

| Feature | Status | Notes |
|---------|--------|-------|
| Class decorators | ✅ | Decorator called at class definition time |
| Method decorators | ✅ | Receives target, propertyKey, PropertyDescriptor |
| Accessor decorators | ✅ | Receives target, propertyKey, PropertyDescriptor with get/set |
| Property decorators | ✅ | Receives target and propertyKey (no descriptor) |
| Parameter decorators | ❌ | |
| Decorator factories | ✅ | @decorator(args) factory pattern supported |

## 17. Declaration Merging

| Feature | Status | Notes |
|---------|--------|-------|
| Interface merging | 🔬 | Type-only |
| Namespace merging | ❌ | |
| Class/namespace merging | ❌ | |
| Function/namespace merging | ❌ | |
| Enum/namespace merging | ❌ | |
| Module augmentation | ❌ | |
| Global augmentation | ❌ | |

## 18. JSX

| Feature | Status | Notes |
|---------|--------|-------|
| JSX elements | ❌ | |
| JSX expressions | ❌ | |
| JSX fragments | ❌ | |
| JSX type checking | ❌ | |

## 19. Iterators and Generators

| Feature | Status | Notes |
|---------|--------|-------|
| `for...of` loops | ✅ | |
| `Symbol.iterator` | ✅ | Well-known symbols as property keys |
| Iterable protocol | ✅ | Custom iterables with [Symbol.iterator] work |
| Generator functions (`function*`) | ✅ | Basic support |
| `yield` expression | ✅ | |
| `yield*` delegation | ✅ | Works with generators and arrays |
| Async generators | ✅ | async function* and yield work |
| `for await...of` | ✅ | Works with async generators and Promise arrays |

## 20. Mixins

| Feature | Status | Notes |
|---------|--------|-------|
| Mixin classes | ❌ | |
| Constrained mixins | ❌ | |

## 21. Triple-Slash Directives

| Feature | Status | Notes |
|---------|--------|-------|
| `/// <reference path="..." />` | ✅ | Parsed and stored in AST |
| `/// <reference types="..." />` | ✅ | Parsed and stored in AST |
| `/// <reference lib="..." />` | ✅ | Parsed and stored in AST |
| `/// <reference no-default-lib="true" />` | ❌ | |

## 22. Type Assertions

| Feature | Status | Notes |
|---------|--------|-------|
| `as` syntax | ✅ | |
| Angle-bracket syntax | ✅ | |
| `as const` | ✅ | Pass-through at runtime, type-level assertion |
| Non-null assertion (`!`) | ✅ | Pass-through expression |

---

## Summary

| Category | Implemented | Partial | Not Implemented | Type-Only |
|----------|-------------|---------|-----------------|-----------|
| Basic Types | 14 | 0 | 0 | 2 |
| Variable Declarations | 6 | 0 | 0 | 0 |
| Interfaces | 5 | 1 | 0 | 4 |
| Type Aliases | 0 | 0 | 4 | 2 |
| Classes | 19 | 0 | 0 | 1 |
| Functions | 11 | 0 | 0 | 0 |
| Generics | 5 | 0 | 0 | 2 |
| Type Manipulation | 3 | 0 | 4 | 0 |
| Utility Types | 0 | 0 | 0 | 17 |
| Modules | 8 | 0 | 1 | 2 |
| Namespaces | 0 | 0 | 3 | 0 |
| Enums | 7 | 0 | 0 | 0 |
| Type Narrowing | 9 | 1 | 0 | 0 |
| Union/Intersection | 4 | 0 | 0 | 0 |
| Literal Types | 1 | 0 | 0 | 3 |
| Decorators | 5 | 0 | 1 | 0 |
| Declaration Merging | 0 | 0 | 6 | 1 |
| JSX | 0 | 0 | 4 | 0 |
| Iterators/Generators | 8 | 0 | 0 | 0 |
| Mixins | 0 | 0 | 2 | 0 |
| Triple-Slash | 3 | 0 | 1 | 0 |
| Type Assertions | 4 | 0 | 0 | 0 |
| **TOTAL** | **112** | **2** | **26** | **34** |

**Conformance: 112/140 runtime features (80%)**

Note: 34 features are type-only (erased at compile time) and don't require runtime support.
