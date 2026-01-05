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
9. ~~**Function Hoisting in Nested Scopes:**~~ **FIXED** - Two-phase hoisting approach: Phase 1 creates null placeholders for all function names, Phase 2 fills them during normal statement processing. Added `ts_call_9` and `ts_call_10` for 9-10 argument function calls.
10. ~~**JavaScript Slow Path Analysis:**~~ **FIXED** - For JavaScript modules, `analyzeFunctionBody` was returning early without setting `inferredType` on AST nodes. This prevented codegen from recognizing `TypeKind::Any` for property assignment (`obj.b = 20`) and other operations. Fixed by visiting all statements even for UntypedJavaScript modules. Also fixed variable type inference to not update types from initializers in JavaScript mode.

### Boxing Refactor Fixes (commit 71c4b13, 8d37f63)

The following issues were discovered and fixed during the Unified Boxing Epic (Phase 19) that directly impact lodash support:

1. **JSON require() now works** ✅
   - Added runtime JSON parsing in `ts_require` for `.json` files
   - `require("./package.json")` now correctly parses and returns JSON object
   - Enables lodash's package.json resolution

2. **Dynamic array access on any-typed values** ✅
   - `ts_object_get_dynamic` now handles TsArray magic (0x41525259)
   - Numeric indices work: `arr[0]` returns correct element
   - Property access works: `arr.length` returns array length
   - Critical for JavaScript slow path where all arrays are `any`

3. **String type detection in arrays** ✅
   - `__ts_array_get_inline` now checks TsString magic (0x53545247)
   - JSON arrays containing strings return proper STRING_PTR type
   - Fixes display of string arrays from JSON

4. **Magic checks prevent crashes** ✅
   - Added magic validation to `__ts_map_find_bucket`, `__ts_map_get_value_at`, `__ts_map_set_at`
   - Prevents crashes when non-TsMap objects passed to map operations
   - Returns undefined gracefully instead of crashing

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

**Status:** ✅ Complete
**Estimated Effort:** 2-4 hours (minimal implementation)
**Actual Time:** ~2 hours

**Current State:**
- ✅ Basic infrastructure exists: `DIBuilder`, `DICompileUnit`, `DISubprogram` created
- ✅ Line location tracking via `emitLocation()` method
- ✅ `-g` flag implemented in CLI
- ✅ Type mapping helper (`createDebugType`) implemented
- ✅ Parameter debug info working
- ✅ Local variable debug info working
- ✅ DISubroutineType includes actual parameter types
- ✅ emitLocation() calls added to loops, switch, try/catch
- ✅ Tested with Visual Studio debugger

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

- [x] **Task 105.7.4:** Add `emitLocation()` calls throughout codegen (~30 min)
  - Added to loop statements (while, for, for-of)
  - Added to switch statement
  - Try statement already had it
  - Most statement visitors now have location tracking

- [x] **Task 105.7.5:** Test with Visual Studio debugger (~30 min)
  - Compiled test program with `-g` flag
  - Opened in Visual Studio debugger
  - Debug symbols working: can set breakpoints, inspect variables, see call stack
  - Ready to debug lodash crash

**Success Criteria:**
- ✅ Can set breakpoints on TypeScript source lines
- ✅ Can inspect local variables and parameters in debugger
- ✅ Can see function names in call stack
- ✅ Can step through TypeScript source code
- ⏳ Lodash crash shows actual source location (needs to be tested when lodash compiles)

**Benefits:**
- ✅ Replace marker-insertion debugging with proper debugger
- ✅ See actual crash locations and stack traces
- ✅ Inspect variable state at crash point
- ✅ Standard debugging workflow instead of printf-style debugging

**Implementation Details:**
- Added `createDebugType()` helper in [IRGenerator_Core.cpp](../../src/compiler/codegen/IRGenerator_Core.cpp#L222-L257)
- Parameter debug info added in [IRGenerator_Functions.cpp](../../src/compiler/codegen/IRGenerator_Functions.cpp#L464-L489)
- Local variable debug info added in [IRGenerator_Core.cpp](../../src/compiler/codegen/IRGenerator_Core.cpp#L658-L667)
- Location tracking added to loops in [IRGenerator_Statements_Loops.cpp](../../src/compiler/codegen/IRGenerator_Statements_Loops.cpp)
- Location tracking added to switch in [IRGenerator_Statements.cpp](../../src/compiler/codegen/IRGenerator_Statements.cpp)
- Test program: [debug_test.ts](../../examples/debug_test.ts) - simple function with parameters and locals
- Verified: debug IR contains `DISubprogram`, `DILocalVariable`, `DIBasicType` metadata
- Tested: Visual Studio debugger opens and can debug the executable

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

---

### Milestone 105.11.5: Incremental Lodash Pattern Testing ⚠️ NEW

**Goal:** Before attempting full lodash, verify each JavaScript pattern used by lodash works in isolation.

**Rationale:** 
- Lodash uses many advanced JavaScript patterns (IIFEs, closures, prototype chains, etc.)
- Full lodash is 17,220 lines - impossible to debug as a unit
- Must verify each pattern independently before combining
- Prevent regressions in other language features

**Test Strategy:**
1. Create minimal test file for each pattern
2. Compile and run to verify correctness
3. Run existing test suite to catch regressions
4. Only proceed to next pattern after current one passes

**Regression Test Suite:**
```powershell
# Run before and after each lodash fix
.\scripts\run_regression_tests.ps1
# Minimum tests to pass:
# - test_inline_ops.exe (Map/Array/Object operations)
# - map_set_test.exe (collections)
# - json_import_test.exe (ES6 imports)
# - test_require_json.exe (CommonJS require)
# - object_test.exe (Object.keys/values/entries)
```

#### Pattern Tests (in order of complexity):

- [ ] **Test 105.11.5.1:** Simple Object Literals
  ```javascript
  // tests/js_patterns/object_literal.js
  var obj = { a: 1, b: 2, c: 3 };
  console.log(obj.a, obj.b, obj.c);
  ```

- [ ] **Test 105.11.5.2:** Object Property Assignment
  ```javascript
  // tests/js_patterns/object_assign.js
  var obj = {};
  obj.x = 10;
  obj.y = 20;
  obj['z'] = 30;
  console.log(obj.x, obj.y, obj.z);
  ```

- [ ] **Test 105.11.5.3:** Array Operations
  ```javascript
  // tests/js_patterns/array_ops.js
  var arr = [1, 2, 3];
  arr.push(4);
  console.log(arr.length, arr[0], arr[3]);
  for (var i = 0; i < arr.length; i++) {
    console.log(arr[i]);
  }
  ```

- [ ] **Test 105.11.5.4:** Basic Function Definition & Call
  ```javascript
  // tests/js_patterns/basic_function.js
  function add(a, b) { return a + b; }
  var result = add(2, 3);
  console.log(result); // 5
  ```

- [ ] **Test 105.11.5.5:** Function Stored in Object
  ```javascript
  // tests/js_patterns/object_method.js
  var obj = {
    value: 42,
    getValue: function() { return this.value; }
  };
  console.log(obj.getValue()); // 42
  ```

- [ ] **Test 105.11.5.6:** Simple IIFE (Immediately Invoked Function Expression)
  ```javascript
  // tests/js_patterns/iife_simple.js
  var result = (function() {
    return 42;
  })();
  console.log(result); // 42
  ```

- [ ] **Test 105.11.5.7:** IIFE with .call(this)
  ```javascript
  // tests/js_patterns/iife_call.js
  var result = (function() {
    return "hello";
  }).call(this);
  console.log(result); // "hello"
  ```

- [ ] **Test 105.11.5.8:** IIFE with Closure
  ```javascript
  // tests/js_patterns/iife_closure.js
  var counter = (function() {
    var count = 0;
    return function() {
      count++;
      return count;
    };
  })();
  console.log(counter()); // 1
  console.log(counter()); // 2
  ```

- [ ] **Test 105.11.5.9:** Multiple Object Properties (Lodash typedArrayTags pattern)
  ```javascript
  // tests/js_patterns/multi_props.js
  var tags = {};
  tags['[object Float32Array]'] = true;
  tags['[object Float64Array]'] = true;
  tags['[object Int8Array]'] = true;
  tags['[object Int16Array]'] = true;
  console.log(tags['[object Float32Array]']); // true
  ```

- [ ] **Test 105.11.5.10:** While Loop in IIFE
  ```javascript
  // tests/js_patterns/iife_while.js
  var result = (function() {
    var i = 0;
    while (i < 10) { i++; }
    return i;
  })();
  console.log(result); // 10
  ```

- [ ] **Test 105.11.5.11:** For-in Loop on Object
  ```javascript
  // tests/js_patterns/for_in.js
  var obj = { a: 1, b: 2, c: 3 };
  for (var key in obj) {
    console.log(key, obj[key]);
  }
  ```

- [ ] **Test 105.11.5.12:** Function with Many Parameters (Lodash uses up to 10)
  ```javascript
  // tests/js_patterns/many_params.js
  function fn(a,b,c,d,e,f,g,h,i,j) {
    return a+b+c+d+e+f+g+h+i+j;
  }
  console.log(fn(1,2,3,4,5,6,7,8,9,10)); // 55
  ```

- [ ] **Test 105.11.5.13:** Nested Function Definitions
  ```javascript
  // tests/js_patterns/nested_functions.js
  function outer() {
    function inner() { return 42; }
    return inner();
  }
  console.log(outer()); // 42
  ```

- [ ] **Test 105.11.5.14:** Function Hoisting
  ```javascript
  // tests/js_patterns/function_hoisting.js
  console.log(hoisted()); // "works"
  function hoisted() { return "works"; }
  ```

- [ ] **Test 105.11.5.15:** Constructor Function with Prototype
  ```javascript
  // tests/js_patterns/constructor.js
  function Person(name) { this.name = name; }
  Person.prototype.greet = function() { return "Hello " + this.name; };
  var p = new Person("World");
  console.log(p.greet()); // "Hello World"
  ```

- [ ] **Test 105.11.5.16:** Object.keys() Iteration
  ```javascript
  // tests/js_patterns/object_keys.js
  var obj = { a: 1, b: 2, c: 3 };
  var keys = Object.keys(obj);
  for (var i = 0; i < keys.length; i++) {
    console.log(keys[i]);
  }
  ```

- [ ] **Test 105.11.5.17:** Array.push in Loop (Lodash mixin pattern)
  ```javascript
  // tests/js_patterns/array_push_loop.js
  var methods = [];
  var names = ['chunk', 'compact', 'drop', 'take'];
  for (var i = 0; i < names.length; i++) {
    methods.push(names[i]);
  }
  console.log(methods.length); // 4
  ```

- [ ] **Test 105.11.5.18:** typeof Checks
  ```javascript
  // tests/js_patterns/typeof.js
  console.log(typeof undefined);  // "undefined"
  console.log(typeof null);       // "object"
  console.log(typeof 42);         // "number"
  console.log(typeof "hello");    // "string"
  console.log(typeof {});         // "object"
  console.log(typeof function(){}); // "function"
  ```

- [ ] **Test 105.11.5.19:** Ternary with Truthiness
  ```javascript
  // tests/js_patterns/ternary.js
  var a = null;
  var b = a ? "truthy" : "falsy";
  console.log(b); // "falsy"
  var c = 1;
  var d = c ? "truthy" : "falsy";
  console.log(d); // "truthy"
  ```

- [ ] **Test 105.11.5.20:** Large Object with 50+ Properties
  ```javascript
  // tests/js_patterns/large_object.js
  var obj = {};
  for (var i = 0; i < 50; i++) {
    obj['prop' + i] = i;
  }
  console.log(Object.keys(obj).length); // 50
  console.log(obj.prop25); // 25
  ```

- [ ] **Test 105.11.5.21:** Lodash runInContext() Simulation (Mini Version)
  ```javascript
  // tests/js_patterns/run_in_context.js
  // Simulates the pattern that hangs in full lodash
  var _ = (function() {
    var lodash = {};
    var methodNames = ['chunk', 'compact', 'drop', 'filter', 'find'];
    for (var i = 0; i < methodNames.length; i++) {
      lodash[methodNames[i]] = (function(name) {
        return function() { return name; };
      })(methodNames[i]);
    }
    return lodash;
  })();
  console.log(_.chunk()); // "chunk"
  console.log(_.filter()); // "filter"
  ```

**Execution Order:**
1. Run tests 105.11.5.1 through 105.11.5.10 first (basic patterns)
2. Run regression suite after each
3. If all pass, run tests 105.11.5.11 through 105.11.5.21 (advanced patterns)
4. Only attempt full lodash (105.12) after ALL pattern tests pass

---

### Milestone 105.12: Compile Real Lodash

**Status:** In Progress - Incremental debugging with checkpoints

**Prerequisites:** ⚠️ All tests in Milestone 105.11.5 must pass first!

**Approach:**
Lodash is 17,210 lines with 603+ function definitions. The file is too large to debug as a whole. We're using an incremental checkpoint approach:
1. Create `examples/lodash_debug.js` with console.log checkpoints
2. Add `process.exit(0)` after each checkpoint
3. Move the exit point forward after each successful test
4. Identify the exact function/pattern that causes the hang

**Test Command:**
```powershell
build/src/compiler/Release/ts-aot.exe examples/lodash_debug_test.ts -o examples/lodash_debug_test.exe
if ($?) { .\examples\lodash_debug_test.exe }
```

**Checkpoint Progress:**
- [x] **CHECKPOINT 0:** Simple IIFE works ✅
  - Test: `(function() { console.log("hi"); }.call(this));`
  - Result: Works - basic IIFE execution is fine
  
- [x] **DISCOVERY:** Simple while loops in IIFEs work! ✅
  - Test: `(function() { var i=0; while(i<5){i++;} return i; })()`
  - Result: Works correctly, returns 5
  - Test: Pre-increment in while: `while(++i<5)` also works
  - Test: IIFE with `.call(this)` and while loop also works
  - **Conclusion:** The bug is NOT "while loops in IIFEs" in general
  
- [x] **ROOT CAUSE IDENTIFIED** 🎯🔥
  - **NOT a while loop bug!**
  - **ACTUAL ISSUE:** Boehm GC infinite collection cycle
  - Stack trace shows: `GC_mark_from → GC_mark_some_inner → GC_stopped_mark → GC_try_to_collect_inner`
  - Triggered during: `ts_object_get_property` called from lodash IIFE
  - **Problem:** Lodash allocates SO MANY objects during initialization that it exhausts GC heap
  - GC enters infinite mark-and-sweep cycles unable to reclaim enough memory
  - See: [hang_stack_trace.txt](../../hang_stack_trace.txt)

**Root Cause Analysis:**
1. Lodash initialization creates massive object graph (17K lines of code)
2. Every property access allocates memory via `ts_object_get_property`
3. GC heap fills up faster than collection can complete
4. GC enters thrashing state: continuously collecting but never freeing enough
5. Program appears to hang (actually stuck in GC)

**Potential Fixes:**
1. **Increase GC heap size** - Give Boehm GC more memory
2. **Optimize object allocation** - Reduce allocations in `ts_object_get_property`
3. **Cache property lookups** - Avoid repeated allocations for same properties
4. **Use object pooling** - Reuse TsString objects for property names
5. **Disable GC temporarily** - Turn off GC during module init, run after

**Next Steps:**
1. ✅ Increase Boehm GC initial heap size
2. 🔄 Test if lodash loads successfully
3. 🔄 If still fails, optimize property access allocations

**Debug Commands Used:**
```powershell
# Attach to hung process and get stack trace
.\attach_and_break.ps1
```

- [ ] **CHECKPOINT 1:** IIFE entry (line ~10)
  - Test: Does the IIFE even start executing?
  - Location: Right after `;(function() {`
  
- [ ] **CHECKPOINT 2:** Constants defined (line ~305)
  - Test: Can we initialize all the const variables?
  - Location: After `templateCounter = -1`
  
- [ ] **CHECKPOINT 3:** typedArrayTags initialized (line ~323)
  - Test: Can we set up the typedArrayTags object?
  - Location: After multi-line assignment to typedArrayTags
  - **Known Issue:** This is where our test currently exits - the first complex object initialization
  
- [ ] **CHECKPOINT 4:** cloneableTags initialized (line ~340)
  - Test: Second complex object initialization
  - Location: After cloneableTags setup
  
- [ ] **CHECKPOINT 5:** Helper functions defined (line ~500)
  - Test: Can we define the first batch of helper functions (apply, arrayAggregator, etc.)?
  - Location: After ~25 internal helper functions
  
- [ ] **CHECKPOINT 6:** Hash/Cache classes (line ~2400)
  - Test: Hash, ListCache, MapCache, SetCache, Stack classes
  - Location: After all cache data structures are defined
  
- [ ] **CHECKPOINT 7:** Main lodash constructor (line ~1700)
  - Test: lodash(), baseLodash(), LodashWrapper classes
  - Location: After core constructors
  
- [ ] **CHECKPOINT 8:** Base utility functions (line ~8000)
  - Test: First half of base* helper functions
  - Location: Midpoint of internal functions
  
- [ ] **CHECKPOINT 9:** Public API methods (line ~16600)
  - Test: Wrapped value methods added to lodash
  - Location: After "Add methods that return wrapped values"
  
- [ ] **CHECKPOINT 10:** Module export (line ~17200)
  - Test: Final module.exports and .call(this)
  - Location: Just before closing IIFE
  
**Expected Failure Point:**
Based on previous analysis, we expect to fail at the first while loop inside the IIFE. This will likely be in:
- Helper functions that iterate (arrayEach, arrayFilter, etc.)
- Cache operations that use while loops
- String processing functions

**Progress (2026-01-03):**
- ✅ Fixed LLVM verification error (`add ptr` with pointer operands)
- ✅ Added safety fallbacks for pointer arithmetic in binary expressions
- ✅ Lodash now compiles successfully to native code
- ✅ **Fixed IIFE closure capture bug** (commit a5fd35f)
  - CommonJS modules use IIFE pattern that was failing to capture outer scope
  - Fixed: Analyzer now propagates parent scope to IIFE function expressions
  - Test: `examples/iife_test.ts` and `examples/test_iife_multivar.ts` pass
- ✅ **Fixed null closure context bug** (commit 3698c2c)
  - Direct call optimization was bypassing function wrapper and passing null context
  - Added `functionsWithClosures` set to track functions with captures
  - Modified direct call optimization to skip functions with closures
  - These functions now use ts_call_N path which properly extracts context
  - Previously crashed with null pointer dereference at `mov rax, [rcx]` with `rcx=0`
- ✅ **Fixed function magic check** (commit 32a0276)
  - Added `ts_extract_function()` helper with magic check (0x46554E43 "FUNC")
  - Updated all `ts_call_N` functions to verify object is TsFunction before casting
  - Prevents crashes when non-function objects (TsMap, TsArray, etc.) are called
  - Simple tests work: object property assignment, function calls all pass
- ✅ **BREAKTHROUGH: Identified GC Thrashing as Root Cause** (NOT a crash!)
  - Program doesn't crash - it HANGS during initialization
  - Automated debugging (attach_and_break.ps1) revealed stack trace stuck in GC collection cycle
  - Root cause: Lodash creates massive object graph (~17K lines of code)
  - Every property access via `ts_object_get_prop` triggers string allocation
  - Default GC heap exhausts faster than collection completes → infinite mark-and-sweep
  - This was **NOT a while loop bug** - all while loop patterns tested separately and work fine

**Root Cause Analysis (COMPLETE):**
✅ **Problem:** GC Heap Exhaustion During Massive Object Initialization
- Lodash's `runInContext()` function (end of file) creates hundreds of methods
- Each property access converts numeric keys to strings (`TsString::FromDouble/FromInt`)
- Stack trace shows: `ts_object_get_prop → TsString::FromDouble → GC_collect_a_little_inner`
- GC enters thrashing state where collections never complete

✅ **Fixes Applied:**
1. **GC Configuration** (Memory.cpp):
   - Expanded initial heap: `GC_expand_hp(512 * 1024 * 1024)` // 512MB
   - Increased max heap: `GC_set_max_heap_size(4GB)`
   - Reduced collection frequency: `GC_set_free_space_divisor(1)` // Minimal GC
   
2. **String Interning** (TsString.cpp):
   - Added cache for numeric strings 0-999 in `TsString::FromInt()`
   - Prevents repeated allocation of common property keys
   - Uses `std::unordered_map<int64_t, TsString*>` for O(1) lookup

✅ **Results:**
- Lodash 17,000 lines: **WORKS** - loads in 0.11 seconds ✅
- Lodash 17,100 lines: **WORKS** - loads in 0.11 seconds ✅  
- Lodash 17,220 lines (full): **HANGS** after 30+ seconds ❌
- Bisection shows issue in final 120 lines containing `runInContext()` call
- This function builds the entire lodash API object with ~200+ methods

**Debug Steps:**
1. ✅ Used auto-debug skill to analyze hang (not a crash!)
2. ✅ Created attach_and_break.ps1 to capture stack traces from hung process
3. ✅ Identified GC thrashing via stack trace showing `GC_collect_a_little_inner`
4. ✅ Configured GC: 512MB initial heap, 4GB max, minimal collection frequency
5. ✅ Added string interning for numeric keys (0-999)
6. ✅ Bisected lodash: 17,100 lines work, full 17,220 hangs
7. ✅ Identified `runInContext()` as the bottleneck (creates 200+ methods)

**Next Steps: Performance Optimization Pass (Milestone 105.13)**
- 🔄 Implement comprehensive string interning system
- 🔄 Optimize property access to reduce allocations  
- 🔄 Add object pooling for frequently-allocated types
- 🔄 Profile memory usage during lodash load
- Goal: Get full lodash (17,220 lines) loading in <5 seconds

- [x] **Task 105.12.1:** Debug lodash runtime crash - Part 1: Null Context ✅
  - [x] Get stack trace from CDB debugger ✅
  - [x] Identified null pointer in closure context extraction ✅
  - [x] Fixed direct call optimization to preserve closure context ✅
  - [x] Commit 3698c2c created ✅
  
- [x] **Task 105.12.1b:** Debug lodash runtime crash - Part 2: Function Type Safety ✅
  - [x] Added ts_extract_function helper with magic check ✅
  - [x] Updated all ts_call_N functions to use helper ✅
  - [x] Verified simple tests pass ✅
  - [x] Commit 32a0276 created ✅

- [x] **Task 105.12.1c:** Debug lodash hang - Part 3: GC Thrashing Analysis ✅
  - [x] Automated debugging with attach_and_break.ps1 ✅
  - [x] Stack trace reveals GC collection loop, not infinite while loop ✅
  - [x] Identified root cause: excessive allocations during property access ✅
  - [x] Configured GC for large heaps (512MB initial, 4GB max) ✅
  - [x] Added numeric string interning (0-999) ✅
  - [x] Bisected to find issue in runInContext() call ✅
  
### Milestone 105.13: Performance Optimization Pass

**Goal:** Optimize property access and memory allocation to support full lodash

**Strategy:**
1. **Comprehensive String Interning**: Cache all property name strings, not just numeric
2. **Reduce Property Access Allocations**: Optimize hot paths in `ts_object_get_prop`
3. **UTF-8 Buffer Reuse**: Avoid re-allocating `ToUtf8()` buffers
4. **Object Pooling**: Reuse frequently-allocated objects (TsValue, small strings)

- [ ] **Task 105.13.1:** Implement Global String Interning Pool
  - Add `TsStringCache` class with hash map for all strings
  - Intern property names at parse/compile time where possible
  - Add `TsString::GetInterned(const char*)` factory method
  - Expected impact: 50-70% reduction in string allocations

- [ ] **Task 105.13.2:** Optimize `ts_object_get_prop` Fast Path
  - Check if key is already a TsString (avoid conversion)
  - Special-case numeric indices for arrays (skip string conversion entirely)
  - Add fast path for small integer keys (0-999) using direct index
  - Expected impact: 30-40% faster property access

- [ ] **Task 105.13.3:** Lazy UTF-8 Buffer Allocation
  - Only allocate `utf8Buffer` when actually needed (not on every `ToUtf8()` call)
  - Cache UTF-8 bytes in TsString when length < 64 (fits in inline buffer)
  - Expected impact: 20-30% reduction in allocations

- [ ] **Task 105.13.4:** Property Name Compile-Time Optimization  
  - When compiler sees literal property access (`obj.foo`), emit direct lookup
  - Generate interned string constants at compile time
  - Bypass `ts_value_get_string()` conversion for known property names
  - Expected impact: 40-50% faster for static property access

- [ ] **Task 105.13.5:** Benchmark and Profile
  - Measure lodash load time after each optimization
  - Profile with Windows Performance Analyzer to find remaining bottlenecks
  - Target: Full lodash loads in <5 seconds (currently: hangs after 30s)
  
- [ ] **Task 105.13.6:** Document Performance Characteristics
  - Create performance guide for large libraries
  - Document memory usage patterns and GC tuning
  - Provide recommendations for optimal compilation

**Success Criteria:**
- ✅ Lodash 17,100 lines loads in <0.2s (currently: 0.11s)
- 🎯 Full lodash 17,220 lines loads in <5s (currently: hangs)
- 🎯 Property access 2-3x faster than baseline
- 🎯 Memory allocations reduced by 60%+

**Fallback Plan (if optimization insufficient):**
- Add `--lazy-init` flag to defer object initialization
- Implement lodash module as ES6 modules instead of IIFE
- Create "lite" lodash subset that works with current performance

- [ ] **Task 105.12.2:** `Function.prototype.call/apply/bind` support
  - Lodash UMD wrapper uses `.call(this)` to invoke the factory
  - Need runtime support for `ts_function_call_with_this`
  - Ensure proper `this` binding in slow-path JS

- [ ] **Task 105.12.3:** Prototype chain handling
  - Lodash sets up constructor/prototype relationships
  - Need proper `__proto__`/`prototype` support
  - `new` operator for constructor functions

- [ ] **Task 105.12.4:** Get lodash methods callable
  - Once initialization passes, test basic lodash calls
  - `_.chunk([1,2,3,4], 2)` should work
  - `_.map([1,2,3], x => x * 2)` should work

- [ ] **Task 105.12.5:** Run lodash test suite
- [ ] **Task 105.12.6:** Benchmark vs Node.js
- [ ] **Task 105.12.7:** Document performance comparison

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
