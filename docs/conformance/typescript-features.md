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
| `tuple` | ⚠️ | Basic works, rest elements missing |
| `enum` | ❌ | |
| `unknown` | 🔬 | Type-only |
| `any` | ✅ | Compiles to boxed TsValue |
| `void` | ✅ | |
| `null` | ✅ | |
| `undefined` | ✅ | |
| `never` | 🔬 | Type-only |
| `object` | ✅ | |
| `bigint` | ❌ | |
| `symbol` | ⚠️ | Basic support |

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
| Indexable types | ⚠️ | Basic support |
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
| Static blocks | ❌ | |
| Abstract classes | ✅ | |
| Abstract methods | ✅ | |
| Class expressions | ✅ | Named and anonymous |
| `extends` (inheritance) | ✅ | |
| `implements` | ✅ | |
| `super` calls | ✅ | |
| `this` type | ⚠️ | |
| Index signatures in classes | ❌ | |

## 6. Functions

| Feature | Status | Notes |
|---------|--------|-------|
| Function declaration | ✅ | |
| Function expression | ✅ | |
| Arrow function | ✅ | |
| Optional parameters | ✅ | |
| Default parameters | ✅ | |
| Rest parameters | ✅ | |
| `this` parameter | ⚠️ | |
| Overloads | ⚠️ | Declaration only |
| Generic functions | ✅ | Monomorphized |
| Call signatures | ✅ | |
| Construct signatures | ✅ | |

## 7. Generics

| Feature | Status | Notes |
|---------|--------|-------|
| Generic functions | ✅ | Monomorphized |
| Generic classes | ✅ | Monomorphized |
| Generic interfaces | 🔬 | Type-only |
| Generic constraints (`extends`) | ⚠️ | Basic support |
| `keyof` constraint | ❌ | |
| Default type parameters | ❌ | |
| Generic parameter variance | 🔬 | Type-only |

## 8. Type Manipulation

| Feature | Status | Notes |
|---------|--------|-------|
| `keyof` operator | ❌ | |
| `typeof` operator | ✅ | Runtime typeof |
| Indexed access types | ❌ | |
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
| `export * as ns from` | ❌ | |
| `import type` | 🔬 | Type-only |
| `export type` | 🔬 | Type-only |
| Dynamic `import()` | ❌ | |
| `require()` (CommonJS) | ✅ | |
| `module.exports` | ⚠️ | |
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
| Numeric enums | ❌ | |
| String enums | ❌ | |
| Heterogeneous enums | ❌ | |
| Computed members | ❌ | |
| `const` enums | ❌ | |
| Ambient enums | ❌ | |
| Reverse mappings | ❌ | |

## 13. Type Narrowing

| Feature | Status | Notes |
|---------|--------|-------|
| `typeof` guards | ✅ | |
| Truthiness narrowing | ✅ | |
| Equality narrowing | ✅ | |
| `in` operator narrowing | ⚠️ | |
| `instanceof` narrowing | ⚠️ | |
| Control flow analysis | ⚠️ | Basic |
| Type predicates (`is`) | ❌ | |
| Discriminated unions | ❌ | |
| `never` type exhaustiveness | ❌ | |
| `asserts` keyword | ❌ | |

## 14. Union and Intersection Types

| Feature | Status | Notes |
|---------|--------|-------|
| Union types (`\|`) | ⚠️ | Basic support |
| Intersection types (`&`) | ❌ | |
| Type guards | ⚠️ | |
| Discriminated unions | ❌ | |

## 15. Literal Types

| Feature | Status | Notes |
|---------|--------|-------|
| String literals | 🔬 | Type-only |
| Numeric literals | 🔬 | Type-only |
| Boolean literals | 🔬 | Type-only |
| `as const` assertions | ❌ | |

## 16. Decorators

| Feature | Status | Notes |
|---------|--------|-------|
| Class decorators | ❌ | |
| Method decorators | ❌ | |
| Accessor decorators | ❌ | |
| Property decorators | ❌ | |
| Parameter decorators | ❌ | |
| Decorator factories | ❌ | |

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
| `Symbol.iterator` | ⚠️ | |
| Iterable protocol | ⚠️ | |
| Generator functions (`function*`) | ❌ | |
| `yield` expression | ❌ | |
| `yield*` delegation | ❌ | |
| Async generators | ❌ | |
| `for await...of` | ❌ | |

## 20. Mixins

| Feature | Status | Notes |
|---------|--------|-------|
| Mixin classes | ❌ | |
| Constrained mixins | ❌ | |

## 21. Triple-Slash Directives

| Feature | Status | Notes |
|---------|--------|-------|
| `/// <reference path="..." />` | ❌ | |
| `/// <reference types="..." />` | ❌ | |
| `/// <reference lib="..." />` | ❌ | |
| `/// <reference no-default-lib="true" />` | ❌ | |

## 22. Type Assertions

| Feature | Status | Notes |
|---------|--------|-------|
| `as` syntax | ✅ | |
| Angle-bracket syntax | ✅ | |
| `as const` | ❌ | |
| Non-null assertion (`!`) | ⚠️ | |

---

## Summary

| Category | Implemented | Partial | Not Implemented | Type-Only |
|----------|-------------|---------|-----------------|-----------|
| Basic Types | 10 | 2 | 2 | 2 |
| Variable Declarations | 6 | 0 | 0 | 0 |
| Interfaces | 4 | 2 | 0 | 4 |
| Type Aliases | 0 | 0 | 4 | 2 |
| Classes | 16 | 1 | 2 | 1 |
| Functions | 9 | 2 | 0 | 0 |
| Generics | 2 | 1 | 2 | 2 |
| Type Manipulation | 1 | 0 | 6 | 0 |
| Utility Types | 0 | 0 | 0 | 17 |
| Modules | 6 | 1 | 2 | 2 |
| Namespaces | 0 | 0 | 3 | 0 |
| Enums | 0 | 0 | 7 | 0 |
| Type Narrowing | 3 | 3 | 4 | 0 |
| Union/Intersection | 0 | 2 | 2 | 0 |
| Literal Types | 0 | 0 | 1 | 3 |
| Decorators | 0 | 0 | 6 | 0 |
| Declaration Merging | 0 | 0 | 6 | 1 |
| JSX | 0 | 0 | 4 | 0 |
| Iterators/Generators | 1 | 2 | 5 | 0 |
| Mixins | 0 | 0 | 2 | 0 |
| Triple-Slash | 0 | 0 | 4 | 0 |
| Type Assertions | 2 | 1 | 1 | 0 |
| **TOTAL** | **60** | **17** | **63** | **34** |

**Conformance: 60/140 runtime features (43%)**

Note: 34 features are type-only (erased at compile time) and don't require runtime support.
