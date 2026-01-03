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

**Status:** Mostly Complete - Core object utilities working

- [x] **Task 105.3.1:** `get<T>(obj: any, path: string, defaultValue?: T): T` - Deep property access ✅
- [ ] **Task 105.3.2:** `set<T>(obj: T, path: string, value: any): T` - Deep property set
- [x] **Task 105.3.3:** `pick<T>(obj: T, keys: string[]): Partial<T>` - Pick properties ✅
- [x] **Task 105.3.4:** `omit<T>(obj: T, keys: string[]): Partial<T>` - Omit properties ✅
- [ ] **Task 105.3.5:** `merge<T>(target: T, ...sources: any[]): T` - Deep merge
- [x] **Task 105.3.6:** `clone<T>(obj: T): T` - Shallow clone ✅
- [x] **Task 105.3.7:** `cloneDeep<T>(obj: T): T` - Deep clone ✅
- [x] **Task 105.3.8:** `isEqual(a: any, b: any): boolean` - Deep equality ✅
- [x] **Task 105.3.9:** `isEmpty(value: any): boolean` - Check if empty ✅
- [x] **Task 105.3.10:** `has(obj: any, path: string): boolean` - Check property exists ✅

**Runtime Prerequisites:**
- [x] `Object` global - defined in Analyzer_StdLib.cpp ✅
- [x] `Object.keys(obj)` - Get object keys ✅
- [x] `Object.values(obj)` - Get object values ✅
- [x] `Object.entries(obj)` - Get key-value pairs ✅
- [x] `Object.assign(target, ...sources)` - Shallow merge ✅
- [x] `Object.hasOwn(obj, key)` - Check property exists ✅
- [x] `typeof` operator - Runtime type checking ✅

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
- [x] **Task 105.5.6:** `partial(fn, ...args)` - Partial application ✅ (typed versions: partial1_2, partial2_3)
- [x] **Task 105.5.7:** `curry(fn)` - Currying ✅ (typed version: curry2; curry3 blocked by triple-nested closure bug)

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
- [x] **Task 105.6.6:** `uniqueId(prefix?: string): string` - Generate unique ID ✅

---

### Milestone 105.7: Debug Symbols for LLVM IR

**Goal:** Add DWARF/CodeView debug information to enable source-level debugging in Visual Studio/WinDbg.

**Status:** In Progress
**Estimated Effort:** 2-4 hours (minimal implementation)

**Current State:**
- ✅ Basic infrastructure exists: `DIBuilder`, `DICompileUnit`, `DISubprogram` created
- ✅ Line location tracking via `emitLocation()` method
- ✅ `-g` flag implemented in CLI
- ✅ Type mapping helper (`createDebugType`) implemented
- ✅ Parameter debug info working
- ✅ Local variable debug info working
- ✅ DISubroutineType includes actual parameter types
- ❌ Need more `emitLocation()` calls throughout codegen
- ❌ Need testing with Visual Studio debugger

**Tasks:**

- [x] **Task 105.7.1:** Add `DILocalVariable` for function parameters (~30 min)
  - In function body generation, create `DIParameterVariable` for each parameter
  - Insert `llvm.dbg.declare` intrinsic after parameter allocas
  
- [x] **Task 105.7.2:** Add `DILocalVariable` for local variables (~1 hour)
  - In `visitVariableDeclaration`, create `DIAutoVariable` for each variable
  - Insert `llvm.dbg.declare` intrinsic after alloca
  - Track variable lifetime in nested scopes

- [x] **Task 105.7.3:** Create basic type debug info (~30 min)
  - Map TypeScript primitives to LLVM debug types (i64, ptr, double, bool)
  - Replace empty `DISubroutineType` array with actual parameter types
  - Create `DIBasicType` nodes for common types

- [ ] **Task 105.7.4:** Add `emitLocation()` calls throughout codegen (~1 hour)
  - Add to all statement visitors (loops, switches, try/catch, etc.)
  - Add to key expression points (assignments, calls, property access)
  - Ensure consistent location tracking for generated code

- [ ] **Task 105.7.5:** Test with Visual Studio debugger (~1 hour)
  - Compile test program with `-g` flag
  - Load in Visual Studio debugger
  - Verify: breakpoints work, variables visible, call stack correct
  - Test with lodash crash to see actual crash location

**Success Criteria:**
- Can set breakpoints on TypeScript source lines ✅ (line info generated)
- Can inspect local variables and parameters in debugger ✅ (DILocalVariable generated)
- Can see function names in call stack ✅ (DISubprogram generated)
- Can step through TypeScript source code ⏳ (needs more emitLocation calls)
- Lodash crash shows actual source location instead of assembly ⏳ (needs testing)

**Benefits:**
- Replace marker-insertion debugging with proper debugger
- See actual crash locations and stack traces
- Inspect variable state at crash point
- Standard debugging workflow instead of printf-style debugging

**Implementation Details:**
- Added `createDebugType()` helper in [IRGenerator_Core.cpp](../../src/compiler/codegen/IRGenerator_Core.cpp#L222-L257)
- Parameter debug info added in [IRGenerator_Functions.cpp](../../src/compiler/codegen/IRGenerator_Functions.cpp#L464-L489)
- Local variable debug info added in [IRGenerator_Core.cpp](../../src/compiler/codegen/IRGenerator_Core.cpp#L658-L667)
- Test program: [debug_test.ts](../../examples/debug_test.ts) - simple function with parameters and locals
- Verified: debug IR contains `DISubprogram`, `DILocalVariable`, `DIBasicType` metadata

---

## Phase 2: JavaScript Slow Path

**Goal:** Compile real lodash.js by implementing dynamic JavaScript support.

### ⚠️ CRITICAL: Boxing Policy & Runtime Registration ⚠️

To avoid "ad hoc boxing" and ensure the compiler generates correct code, **ALL** new runtime functions must be registered in `src/compiler/codegen/BoxingPolicy.cpp`.

**Rules:**
1.  **Registry:** Add entry to `BoxingPolicy::CORE_RUNTIME_ARG_BOXING`.
2.  **Signature:** Slow path functions MUST take `TsValue*` (boxed) arguments and return `TsValue*`.
3.  **Policy:** Use `true` for all arguments in the boxing policy map.
    ```cpp
    // Example in BoxingPolicy.cpp
    {"ts_value_add", {true, true}}, // (TsValue* a, TsValue* b) -> TsValue*
    ```
4.  **Implementation:**
    *   **Input:** Receive `TsValue*`. Check `type` tag.
    *   **Logic:** Handle type coercion (e.g., number + string = string).
    *   **Output:** Return `TsValue*` using `ts_value_make_*`.

### Milestone 105.8: TsValue Operations

**Boxing Policy:** All arguments `true` (boxed). Returns `TsValue*`.

- [x] **Task 105.8.1:** `ts_value_add(a, b)` - Dynamic addition ✅
- [x] **Task 105.8.2:** `ts_value_subtract(a, b)` - Dynamic subtraction ✅
- [x] **Task 105.8.3:** `ts_value_multiply(a, b)` - Dynamic multiplication ✅
- [x] **Task 105.8.4:** `ts_value_divide(a, b)` - Dynamic division ✅
- [x] **Task 105.8.5:** `ts_value_eq(a, b)` - Loose equality (==)
- [x] **Task 105.8.6:** `ts_value_strict_eq(a, b)` - Strict equality (===) ✅
- [x] **Task 105.8.7:** `ts_value_lt(a, b)` - Less than ✅
- [x] **Task 105.8.8:** `ts_value_gt(a, b)` - Greater than ✅
- [x] **Task 105.8.9:** `ts_value_to_bool(v)` - JavaScript truthiness ✅
- [x] **Task 105.8.10:** `ts_value_typeof(v)` - typeof operator ✅

### Milestone 105.9: Dynamic Property Access

**Boxing Policy:** All arguments `true` (boxed). Returns `TsValue*`.

- [x] **Task 105.9.1:** `ts_object_get_prop(obj, key)` - Get property by string ✅
- [x] **Task 105.9.2:** `ts_object_set_prop(obj, key, value)` - Set property ✅
- [x] **Task 105.9.3:** `ts_object_get_dynamic(obj, key)` - Get by TsValue key
- [x] **Task 105.9.4:** `ts_object_has_prop(obj, key)` - Check property exists ✅
- [x] **Task 105.9.5:** `ts_object_delete_prop(obj, key)` - Delete property ✅
- [x] **Task 105.9.6:** `ts_object_keys(obj)` - Get all keys ✅
- [x] **Task 105.9.7:** `ts_array_get_dynamic(arr, index)` - Dynamic array access
- [x] **Task 105.9.8:** `ts_array_set_dynamic(arr, index, value)` - Dynamic array set

### Milestone 105.10: Slow Path Codegen

- [x] **Task 105.10.1:** Detect JavaScript files in ModuleResolver ✅
- [x] **Task 105.10.2:** Set "slow path mode" flag in Analyzer for JS modules ✅
- [x] **Task 105.10.3:** Type all JS variables as `TsValue*` (Any) ✅
- [x] **Task 105.10.4:** Generate `ts_value_*` calls for operators ✅
- [x] **Task 105.10.5:** Generate `ts_object_get_prop` for property access ✅
- [x] **Task 105.10.6:** Generate `ts_function_call` for dynamic calls ✅
- [x] **Task 105.10.7:** Box all return values as `TsValue*` ✅
- [ ] **Task 105.10.8:** Emit warnings for untyped code paths

### Milestone 105.11: Missing Language Features (Blockers)

- [x] **Task 105.11.1:** `in` operator support (`'key' in obj`) ✅
- [x] **Task 105.11.2:** Object spread in literals (`{ ...obj }`) ✅
- [ ] **Task 105.11.3:** `arguments` object support (legacy JS)
- [x] **Task 105.11.4:** Fix default parameters for `undefined` arguments (type + runtime)  
  - Analyzer/codegen now agree on specializations when callers pass `undefined` into defaulted params; wrappers preserve boxed defaulted primitives so runtime defaulting works. Integration test `tests/integration/default_params_undefined.ts` passes. Removed noisy `ts_value_is_undefined` / `ts_require` printf spam in the runtime.

### Milestone 105.12: Compile Real Lodash

- **Progress (2026-01-02):** Installed npm `lodash`, added smoke test `examples/lodash_npm_test.ts`, and taught `ts_require` to resolve `node_modules` with `package.json` mains. Untyped JS now auto-defines missing globals as `any`, so lodash parses. Runtime still returns empty exports because the lodash bootstrap IIFE isn't executing correctly (`Function.prototype.call`/`this` handling in slow-path JS); current `ts-aot` run exits non-zero with `boxValue` slow-path warnings.
- **Plan to finish 105.12:**
  1) **CJS wrapper execution semantics:** ensure `ts_require` executes the module factory with `{module, exports, require}` and returns `module.exports`; verify the lodash factory actually runs.  
  2) **Function.prototype.call/apply/bind in slow-path JS:** implement runtime + codegen so `.call/.apply` preserve `this` and argument spreading (lodash UMD wraps the factory with `.call(this)`).  
  3) **Module/global `this` binding:** align slow-path module entry so top-level `this` maps to `globalThis`/exports as lodash expects.  
  4) **Smoke test target:** get `examples/lodash_npm_test.ts` printing the chunk/merge/shuffle outputs (non-null exports) as success criteria; trim any slow-path `boxValue` warnings observed during that run.  
  5) **Regression guard:** add a minimal test/script to prevent regressions once the smoke test passes.
- [ ] **Task 105.12.1:** Parse lodash.js without errors
- [ ] **Task 105.12.2:** Compile lodash core functions
- [ ] **Task 105.12.3:** Run lodash test suite
- [ ] **Task 105.12.4:** Benchmark vs Node.js
- [ ] **Task 105.12.5:** Document performance comparison

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
1. **typeof operator** - Returns string type name ✅
2. **in operator** - Check property exists (`'key' in obj`) (See 105.10.1)
3. **Spread operator** - For `...args` in functions ✅ (Object spread missing)
4. **Rest parameters** - For `function(...args)` ✅
5. **Default parameters** - For `function(x = 0)` (Needs fix for `undefined`)

### Runtime Changes Needed
(See individual milestone prerequisites above)

---

## References
- [Lodash Documentation](https://lodash.com/docs)
- [Lodash GitHub](https://github.com/lodash/lodash)
- [You Don't Need Lodash](https://github.com/you-dont-need/You-Dont-Need-Lodash-Underscore) - Native JS alternatives
