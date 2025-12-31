# Epic 105: Lodash Compilation Support

**Status:** In Progress
**Parent:** [Phase 19 Meta Epic](./meta_epic.md)

## Overview

Compile lodash functionality with ts-aot. Two-phase approach:
1. **Phase 1:** Create `ts-lodash` - a TypeScript implementation of core lodash functions
2. **Phase 2:** Implement JavaScript "slow path" to compile real lodash.js

## Current Progress

### ✅ Working Functions
- **Util:** `identity`, `constant`, `noop`, `times`, `range`
- **Array:** `head`, `first`, `last`, `tail`, `initial`, `take`, `takeRight`, `drop`, `dropRight`, `chunk`, `flatten`, `flattenDeep`, `uniq`, `uniqBy`, `difference`, `intersection`
- **Collection:** `map`, `reduce`, `reduceRight`, `every`, `some`, `forEach`, `forEachRight`, `filter`, `find`, `findIndex`, `sortBy`, `sortByDesc`, `groupBy`, `keyBy`
- **String:** `capitalize`, `camelCase`, `kebabCase`, `snakeCase`, `startCase`, `trim`, `trimStart`, `trimEnd`, `pad`, `padStart`, `padEnd`, `repeat`, `split`, `truncate`

### 🔧 Implemented but Blocked by Compiler Bugs
- **compact:** Needs truthiness check improvements

### ~~Compiler Issues Discovered~~ (FIXED)
1. ~~**Nested Loop Variable Scoping:**~~ **NOT A BUG** - tested and works correctly
2. ~~**Generic Callback Types:**~~ **FIXED** (commit 1439598) - `emitToBoolean()` now calls `ts_value_get_bool` for Boolean-typed pointers
3. ~~**Rest Parameter Array Type Inference:**~~ **FIXED** - `Set<T>[]` boxing/unboxing and for-of specialized element access
4. ~~**Generic Array Push Boxing:**~~ **FIXED** (commit e61b0b5) - `array.push()` now stores raw values for primitive types instead of boxing
5. ~~**Function Variable Calls:**~~ **FIXED** - Arrow functions and named function references now properly boxed; wrapper functions generated for `ts_call_N` compatibility
6. ~~**User-defined user_main:**~~ **FIXED** - Synthetic `__synthetic_user_main` now correctly calls user-defined `user_main` function
7. **Optional Parameters:** `undefined` checks need more work
8. ~~**Mutable Closures:**~~ **FIXED** - TsCell-based capture-by-reference now works for counter/memoize patterns

## Design Rationale

Lodash is written in **JavaScript**, not TypeScript. Since ts-aot is optimized for typed code, we use a two-phase approach:

| Phase | Approach | Optimization | Effort |
|-------|----------|--------------|--------|
| Phase 1 | TypeScript port (ts-lodash) | 🟢 Maximum | Medium |
| Phase 2 | JavaScript slow path | 🟡 Good with JSDoc | High |

Phase 1 gives us immediate value with fully-optimized code. Phase 2 enables compiling any npm JavaScript package.

---

## Phase 1: ts-lodash (TypeScript Utility Library)

**Goal:** Create a TypeScript port of the most-used lodash functions.

### Milestone 105.1: Array Utilities

- [x] **Task 105.1.1:** `chunk<T>(arr: T[], size: number): T[][]` - Split array into chunks ✅
- [🔧] **Task 105.1.2:** `compact<T>(arr: T[]): T[]` - Remove falsy values *(blocked: truthiness checks)*
- [x] **Task 105.1.3:** `flatten<T>(arr: T[][]): T[]` - Flatten one level ✅
- [x] **Task 105.1.4:** `flattenDeep<T>(arr: T[][]): T[]` - Flatten 2D array (recursive flatten blocked by Array.isArray) ✅
- [x] **Task 105.1.5:** `uniq<T>(arr: T[]): T[]` - Remove duplicates ✅
- [x] **Task 105.1.6:** `uniqBy<T>(arr: T[], fn: (x: T) => any): T[]` - Unique by key ✅
- [x] **Task 105.1.7:** `difference<T>(arr: T[], ...values: T[][]): T[]` - Set difference ✅
- [x] **Task 105.1.8:** `intersection<T>(...arrays: T[][]): T[]` - Set intersection ✅
- [x] **Task 105.1.9:** `take<T>(arr: T[], n: number): T[]` - Take first n elements ✅
- [x] **Task 105.1.10:** `drop<T>(arr: T[], n: number): T[]` - Drop first n elements ✅

**Runtime Prerequisites:**
- [x] `Array.slice(start?, end?)` - Extract array portions ✅
- [x] `Array.includes(value)` - Check if element exists ✅
- [x] `Array.indexOf(value)` - Find element index ✅
- [x] `Array.concat(...arrays)` - Concatenate arrays ✅

### Milestone 105.2: Collection Utilities

- [x] **Task 105.2.1:** `map<T, U>(arr: T[], fn: (x: T) => U): U[]` - Transform elements ✅
- [x] **Task 105.2.2:** `filter<T>(arr: T[], fn: (x: T) => boolean): T[]` - Filter elements ✅
- [x] **Task 105.2.3:** `reduce<T, U>(arr: T[], fn: (acc: U, x: T) => U, init: U): U` - Reduce ✅
- [x] **Task 105.2.4:** `find<T>(arr: T[], fn: (x: T) => boolean): T | undefined` - Find first match ✅
- [x] **Task 105.2.5:** `findIndex<T>(arr: T[], fn: (x: T) => boolean): number` - Find index ✅
- [x] **Task 105.2.6:** `every<T>(arr: T[], fn: (x: T) => boolean): boolean` - All match ✅
- [x] **Task 105.2.7:** `some<T>(arr: T[], fn: (x: T) => boolean): boolean` - Any match ✅
- [x] **Task 105.2.8:** `groupBy<T, K>(arr: T[], fn: (x: T) => K): Map<K, T[]>` - Group by key (returns Map instead of Record) ✅
- [x] **Task 105.2.9:** `keyBy<T, K>(arr: T[], fn: (x: T) => K): Map<K, T>` - Index by key (returns Map instead of Record) ✅
- [x] **Task 105.2.10:** `sortBy<T>(arr: T[], fn: (x: T) => number): T[]` - Sort by key ✅

**Runtime Prerequisites:**
- [x] `Array.sort(compareFn?)` - In-place sorting ✅
- [ ] `Array.findIndex(predicate)` - Native find index

### Milestone 105.3: Object Utilities

**Status:** In Progress - Object global implemented

- [ ] **Task 105.3.1:** `get<T>(obj: any, path: string, defaultValue?: T): T` - Deep property access
- [ ] **Task 105.3.2:** `set<T>(obj: T, path: string, value: any): T` - Deep property set
- [ ] **Task 105.3.3:** `pick<T>(obj: T, keys: string[]): Partial<T>` - Pick properties
- [ ] **Task 105.3.4:** `omit<T>(obj: T, keys: string[]): Partial<T>` - Omit properties
- [ ] **Task 105.3.5:** `merge<T>(target: T, ...sources: any[]): T` - Deep merge
- [ ] **Task 105.3.6:** `clone<T>(obj: T): T` - Shallow clone
- [ ] **Task 105.3.7:** `cloneDeep<T>(obj: T): T` - Deep clone
- [ ] **Task 105.3.8:** `isEqual(a: any, b: any): boolean` - Deep equality
- [ ] **Task 105.3.9:** `isEmpty(value: any): boolean` - Check if empty
- [ ] **Task 105.3.10:** `has(obj: any, path: string): boolean` - Check property exists

**Runtime Prerequisites:**
- [x] `Object` global - defined in Analyzer_StdLib.cpp ✅
- [x] `Object.keys(obj)` - Get object keys ✅
- [x] `Object.values(obj)` - Get object values ✅
- [x] `Object.entries(obj)` - Get key-value pairs ✅
- [x] `Object.assign(target, ...sources)` - Shallow merge ✅
- [x] `Object.hasOwn(obj, key)` - Check property exists ✅
- [ ] `typeof` operator - Runtime type checking

### Milestone 105.4: String Utilities

- [x] **Task 105.4.1:** `capitalize(str: string): string` - Uppercase first letter ✅
- [x] **Task 105.4.2:** `camelCase(str: string): string` - Convert to camelCase ✅
- [x] **Task 105.4.3:** `kebabCase(str: string): string` - Convert to kebab-case ✅
- [x] **Task 105.4.4:** `snakeCase(str: string): string` - Convert to snake_case ✅
- [x] **Task 105.4.5:** `startCase(str: string): string` - Convert to Start Case ✅
- [x] **Task 105.4.6:** `trim(str: string, chars?: string): string` - Trim characters ✅
- [x] **Task 105.4.7:** `pad(str: string, length: number, chars?: string): string` - Pad string ✅
- [x] **Task 105.4.8:** `repeat(str: string, n: number): string` - Repeat string ✅
- [x] **Task 105.4.9:** `split(str: string, sep: string, limit?: number): string[]` - Split string ✅
- [x] **Task 105.4.10:** `truncate(str: string, options?: {length: number}): string` - Truncate ✅

**Runtime Prerequisites:**
- [x] `String.toLowerCase()` - Convert to lowercase ✅
- [x] `String.toUpperCase()` - Convert to uppercase ✅
- [x] `String.trim()` - Trim whitespace ✅
- [x] `String.split(separator)` - Split string ✅
- [x] `String.replace(search, replacement)` - Replace substring ✅
- [x] `String.padStart(length, char)` - Pad start ✅
- [x] `String.padEnd(length, char)` - Pad end ✅
- [x] `String.repeat(count)` - Repeat string ✅

### Milestone 105.5: Function Utilities

**Status:** Complete - All core function utilities working with TsCell mutable closures

- [x] **Task 105.5.1:** `debounce<T>(fn: T, wait: number): T` - Debounce function ✅
- [x] **Task 105.5.2:** `throttle<T>(fn: T, wait: number): T` - Throttle function ✅
- [x] **Task 105.5.3:** `memoize<T>(fn: T): T` - Cache function results ✅
- [x] **Task 105.5.4:** `once<T>(fn: T): T` - Call only once ✅
- [x] **Task 105.5.5:** `negate<T>(fn: T): T` - Negate predicate ✅
- [ ] **Task 105.5.6:** `partial(fn, ...args)` - Partial application
- [ ] **Task 105.5.7:** `curry(fn)` - Currying

**Runtime Prerequisites:**
- [x] `setTimeout(fn, delay)` - Delayed execution ✅
- [x] `clearTimeout(id)` - Cancel timeout ✅
- [x] `Date.now()` - Current timestamp ✅
- [x] **Mutable closures** - TsCell-based capture-by-reference for counter/memoize patterns ✅

### Milestone 105.6: Utility Functions

- [x] **Task 105.6.1:** `range(start, end, step?)` - Generate number range ✅
- [x] **Task 105.6.2:** `times<T>(n: number, fn: (i: number) => T): T[]` - Call n times ✅
- [x] **Task 105.6.3:** `identity<T>(x: T): T` - Return input ✅
- [x] **Task 105.6.4:** `constant<T>(x: T): () => T` - Return constant function ✅
- [x] **Task 105.6.5:** `noop(): void` - Do nothing ✅
- [ ] **Task 105.6.6:** `uniqueId(prefix?: string): string` - Generate unique ID

---

## Phase 2: JavaScript Slow Path

**Goal:** Compile real lodash.js by implementing dynamic JavaScript support.

### Milestone 105.7: TsValue Operations

- [ ] **Task 105.7.1:** Implement `ts_value_add(a, b)` - Dynamic addition
- [ ] **Task 105.7.2:** Implement `ts_value_subtract(a, b)` - Dynamic subtraction
- [ ] **Task 105.7.3:** Implement `ts_value_multiply(a, b)` - Dynamic multiplication
- [ ] **Task 105.7.4:** Implement `ts_value_divide(a, b)` - Dynamic division
- [ ] **Task 105.7.5:** Implement `ts_value_eq(a, b)` - Loose equality (==)
- [ ] **Task 105.7.6:** Implement `ts_value_strict_eq(a, b)` - Strict equality (===)
- [ ] **Task 105.7.7:** Implement `ts_value_lt(a, b)` - Less than
- [ ] **Task 105.7.8:** Implement `ts_value_gt(a, b)` - Greater than
- [ ] **Task 105.7.9:** Implement `ts_value_to_bool(v)` - JavaScript truthiness
- [ ] **Task 105.7.10:** Implement `ts_value_typeof(v)` - typeof operator

### Milestone 105.8: Dynamic Property Access

- [ ] **Task 105.8.1:** Implement `ts_object_get_prop(obj, key)` - Get property by string
- [ ] **Task 105.8.2:** Implement `ts_object_set_prop(obj, key, value)` - Set property
- [ ] **Task 105.8.3:** Implement `ts_object_get_dynamic(obj, key)` - Get by TsValue key
- [ ] **Task 105.8.4:** Implement `ts_object_has_prop(obj, key)` - Check property exists
- [ ] **Task 105.8.5:** Implement `ts_object_delete_prop(obj, key)` - Delete property
- [ ] **Task 105.8.6:** Implement `ts_object_keys(obj)` - Get all keys
- [ ] **Task 105.8.7:** Implement `ts_array_get_dynamic(arr, index)` - Dynamic array access
- [ ] **Task 105.8.8:** Implement `ts_array_set_dynamic(arr, index, value)` - Dynamic array set

### Milestone 105.9: Slow Path Codegen

- [ ] **Task 105.9.1:** Detect JavaScript files in ModuleResolver
- [ ] **Task 105.9.2:** Set "slow path mode" flag in Analyzer for JS modules
- [ ] **Task 105.9.3:** Type all JS variables as `TsValue*` (Any)
- [ ] **Task 105.9.4:** Generate `ts_value_*` calls for operators
- [ ] **Task 105.9.5:** Generate `ts_object_get_prop` for property access
- [ ] **Task 105.9.6:** Generate `ts_function_call` for dynamic calls
- [ ] **Task 105.9.7:** Box all return values as `TsValue*`
- [ ] **Task 105.9.8:** Emit warnings for untyped code paths

### Milestone 105.10: JSDoc Type Extraction

- [ ] **Task 105.10.1:** Parse JSDoc `@param {Type} name` annotations
- [ ] **Task 105.10.2:** Parse JSDoc `@returns {Type}` annotations
- [ ] **Task 105.10.3:** Parse JSDoc `@type {Type}` for variables
- [ ] **Task 105.10.4:** Convert JSDoc types to ts-aot Type system
- [ ] **Task 105.10.5:** Use extracted types to optimize slow path
- [ ] **Task 105.10.6:** Emit "typed JS" vs "untyped JS" in diagnostics

### Milestone 105.11: Compile Real Lodash

- [ ] **Task 105.11.1:** Parse lodash.js without errors
- [ ] **Task 105.11.2:** Compile lodash core functions
- [ ] **Task 105.11.3:** Run lodash test suite
- [ ] **Task 105.11.4:** Benchmark vs Node.js
- [ ] **Task 105.11.5:** Document performance comparison

---

## Directory Structure

```
examples/ts-lodash/
├── index.ts                    # Barrel export
├── package.json                # For npm-style imports
├── src/
│   ├── array/
│   │   ├── chunk.ts
│   │   ├── compact.ts
│   │   ├── flatten.ts
│   │   ├── uniq.ts
│   │   └── index.ts           # Array barrel
│   ├── collection/
│   │   ├── filter.ts
│   │   ├── find.ts
│   │   ├── groupBy.ts
│   │   ├── map.ts
│   │   ├── reduce.ts
│   │   ├── sortBy.ts
│   │   └── index.ts           # Collection barrel
│   ├── object/
│   │   ├── get.ts
│   │   ├── set.ts
│   │   ├── pick.ts
│   │   ├── omit.ts
│   │   ├── merge.ts
│   │   ├── clone.ts
│   │   ├── isEqual.ts
│   │   └── index.ts           # Object barrel
│   ├── function/
│   │   ├── debounce.ts
│   │   ├── throttle.ts
│   │   ├── memoize.ts
│   │   └── index.ts           # Function barrel
│   ├── string/
│   │   ├── capitalize.ts
│   │   ├── camelCase.ts
│   │   ├── kebabCase.ts
│   │   └── index.ts           # String barrel
│   └── util/
│       ├── range.ts
│       ├── times.ts
│       ├── identity.ts
│       └── index.ts           # Util barrel
└── tests/
    ├── array_test.ts
    ├── collection_test.ts
    ├── object_test.ts
    ├── string_test.ts
    └── function_test.ts
```

---

## Success Criteria

### Phase 1 Complete When:
- [ ] 40+ lodash functions implemented in TypeScript
- [ ] All functions compile with ts-aot
- [ ] Test suite passes
- [ ] Can be imported from other ts-aot projects

### Phase 2 Complete When:
- [ ] Can parse and compile lodash.js
- [ ] Lodash test suite passes when compiled
- [ ] Performance within 2x of Node.js for slow path
- [ ] JSDoc types improve performance to within 1.5x

---

## Dependencies

### Compiler Changes Needed
1. **typeof operator** - Returns string type name
2. **in operator** - Check property exists (`'key' in obj`)
3. **Spread operator** - For `...args` in functions
4. **Rest parameters** - For `function(...args)`
5. **Default parameters** - For `function(x = 0)`

### Runtime Changes Needed
(See individual milestone prerequisites above)

---

## References
- [Lodash Documentation](https://lodash.com/docs)
- [Lodash GitHub](https://github.com/lodash/lodash)
- [You Don't Need Lodash](https://github.com/you-dont-need/You-Dont-Need-Lodash-Underscore) - Native JS alternatives
