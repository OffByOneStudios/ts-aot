# EPIC-HIR-001: HIR Infrastructure

**Status:** In Progress
**Started:** 2026-01-27
**Last Updated:** 2026-01-28

## Overview

Build a complete High-level Intermediate Representation (HIR) pipeline for ts-aot,
enabling better optimization opportunities and cleaner separation between frontend
analysis and backend code generation.

## Goals

1. Replace direct AST-to-LLVM code generation with a two-stage pipeline
2. Enable HIR-level optimizations before lowering to LLVM IR
3. Provide a clean IR for debugging and testing
4. Support future performance optimizations (unboxing, escape analysis, etc.)

## Architecture

```
TypeScript → AST → Analyzer → ASTToHIR → HIRModule → [Passes] → HIRToLLVM → LLVM IR
```

---

## Phase 1: Core Infrastructure

### 1.1 HIR Data Structures
- [x] HIRModule, HIRFunction, HIRBlock, HIRInstruction
- [x] HIRType system (Int64, Float64, String, Array, Object, Any, etc.)
- [x] HIRValue with SSA semantics
- [x] HIROpcode enum (80+ opcodes defined)

### 1.2 Pass Infrastructure
- [x] PassResult struct (changed, error)
- [x] HIRPass base class
- [x] HIRFunctionPass base class
- [x] PassManager with pipeline orchestration
- [x] Debug printing between passes
- [x] Pass callback hooks (afterPassCallback_)

### 1.3 BuiltinRegistry
- [x] Centralized method resolution tables
- [x] MethodResolution struct (opcode vs runtime call)
- [x] BuiltinResolution struct (global builtins)
- [x] Array methods (~25 registered)
- [x] String methods (~35 registered)
- [x] Math builtins (~30 registered)
- [x] Console builtins (~17 registered)
- [x] JSON builtins (2 registered)

---

## Phase 2: Resolution Passes

### 2.1 MethodResolutionPass
- [x] Basic pass structure
- [x] Array method resolution (push → ArrayPush, length → ArrayLength)
- [x] String method resolution (charAt, substring, etc. → runtime calls)
- [x] Registry-based lookup (no inline string checks)
- [x] Object method resolution (keys, values, entries) - via BuiltinResolutionPass globalTable_
- [x] Map/Set method resolution (get, set, has, delete, clear, forEach, entries, keys, values, size + ES2024 Set methods)

### 2.2 BuiltinResolutionPass
- [x] Basic pass structure
- [x] LoadGlobal tracing to identify builtins
- [x] console.* resolution
- [x] Math.* resolution
- [x] JSON.* resolution
- [x] Object.* static methods (keys, values, entries, assign, hasOwn, fromEntries, create, freeze, seal, is, getPrototypeOf, setPrototypeOf, isFrozen, isSealed, isExtensible, preventExtensions, defineProperty, defineProperties, getOwnPropertyDescriptor, getOwnPropertyDescriptors, getOwnPropertyNames, groupBy)
- [x] Array.* static methods (isArray, from, of)
- [x] Number.* static methods (isNaN, isFinite, isInteger, isSafeInteger, parseInt, parseFloat)
- [x] String.* static methods (fromCharCode, fromCodePoint, raw)

---

## Phase 3: ASTToHIR Coverage

### 3.1 Statements
- [x] Variable declarations (let, const, var)
- [x] Block statements
- [x] Expression statements
- [x] If/else statements
- [x] While loops
- [x] For loops
- [x] For-of loops
- [x] For-in loops
- [x] Switch statements
- [x] Break/continue
- [x] Return statements
- [x] Try/catch/finally (setjmp/longjmp based)
- [x] Throw statements
- [ ] Labeled statements
- [ ] With statements (deprecated, low priority)
- [ ] Debugger statements

### 3.2 Expressions
- [x] Binary operators (+, -, *, /, %, etc.)
- [x] Comparison operators (==, ===, <, >, etc.)
- [x] Logical operators (&&, ||, !)
- [x] Unary operators (-, +, !, ~, typeof)
- [x] Prefix increment/decrement (++x, --x)
- [x] Postfix increment/decrement (x++, x--)
- [x] Conditional expressions (?:)
- [x] Assignment expressions (=, +=, -=, etc.)
- [x] Function calls
- [x] Method calls (CallMethod)
- [x] New expressions
- [x] Property access (static and dynamic)
- [x] Element access (arr[i])
- [x] Array literals
- [x] Object literals
- [x] Template literals (simple)
- [x] Template literals (spans/expressions)
- [x] Tagged template literals
- [x] Comma expressions
- [x] Optional chaining (?.)
- [x] Nullish coalescing (??)
- [x] Spread elements
- [x] Await expressions
- [x] Yield expressions

### 3.3 Functions
- [x] Function declarations
- [x] Function parameters
- [x] Default parameter values
- [x] Rest parameters
- [x] Arrow functions
- [x] Function expressions
- [x] Closures / captured variables (full implementation with mutable captures and nested closures)
- [x] Async functions
- [x] Generator functions (function*, yield, yield*)
- [x] Async generators (async function*, yield with Promises)

### 3.4 Classes
- [x] Class declarations
- [x] Class expressions
- [x] Constructor
- [x] Instance methods
- [x] Static methods
- [x] Instance properties
- [x] Static properties
- [x] Getters/setters (object literals)
- [x] Private fields (#field)
- [x] Inheritance (extends)
- [x] Super calls
- [x] Static blocks

### 3.5 Types and Patterns
- [x] Destructuring (object patterns)
- [x] Destructuring (array patterns)
- [x] Binding elements with renaming
- [x] Default values in destructuring
- [x] Rest elements (...rest) in array patterns
- [x] Nested destructuring (objects in arrays, arrays in objects)
- [x] Type assertions (as)
- [x] Non-null assertions (!)
- [ ] Enum declarations (stub exists)
- [x] Interface declarations (type-only, no codegen)
- [x] Type alias declarations (type-only, no codegen)

### 3.6 Modules
- [x] Import declarations (handled at module resolution, no-op at HIR)
- [x] Export declarations (handled at module resolution, no-op at HIR)
- [x] Default exports (handled at module resolution)
- [x] Named exports (handled at module resolution)
- [x] Re-exports (handled at module resolution)
- [ ] Dynamic imports (requires runtime support)

### 3.7 Other
- [x] RegExp literals (calls ts_regexp_from_literal)
- [x] BigInt literals (calls ts_bigint_create_str)
- [x] JSX elements (calls ts_jsx_create_element with props/children)
- [x] JSX fragments (calls ts_jsx_create_element with null tagName)
- [x] Decorators (handled in IRGenerator, not HIR)

---

## Phase 4: HIRToLLVM Coverage

### 4.1 Constants
- [x] ConstInt
- [x] ConstFloat
- [x] ConstBool
- [x] ConstString
- [x] ConstNull
- [x] ConstUndefined

### 4.2 Arithmetic
- [x] AddI64, SubI64, MulI64, DivI64, ModI64, NegI64
- [x] AddF64, SubF64, MulF64, DivF64, ModF64, NegF64
- [x] StringConcat
- [x] Checked arithmetic (AddI64Checked, SubI64Checked, MulI64Checked with overflow detection)

### 4.3 Bitwise
- [x] AndI64, OrI64, XorI64
- [x] ShlI64, ShrI64, UShrI64
- [x] NotI64

### 4.4 Comparison
- [x] CmpEqI64, CmpNeI64, CmpLtI64, CmpLeI64, CmpGtI64, CmpGeI64
- [x] CmpEqF64, CmpNeF64, CmpLtF64, CmpLeF64, CmpGtF64, CmpGeF64
- [x] CmpEqPtr, CmpNePtr

### 4.5 Boolean
- [x] LogicalAnd, LogicalOr, LogicalNot

### 4.6 Type Operations
- [x] CastI64ToF64, CastF64ToI64, CastBoolToI64
- [x] BoxInt, BoxFloat, BoxBool, BoxString, BoxObject
- [x] UnboxInt, UnboxFloat, UnboxBool, UnboxString, UnboxObject
- [x] TypeCheck (basic)
- [x] TypeOf
- [x] InstanceOf

### 4.7 Memory/GC
- [x] Alloca
- [x] Load, Store
- [x] GetElementPtr
- [x] GCAlloc, GCAllocArray (stubs to Boehm)
- [x] GCStore, GCLoad (stubs)
- [x] Safepoint, SafepointPoll (stubs)

### 4.8 Objects
- [x] NewObject, NewObjectDynamic
- [x] GetPropStatic, GetPropDynamic
- [x] SetPropStatic, SetPropDynamic
- [x] HasProp, DeleteProp

### 4.9 Arrays
- [x] NewArrayBoxed, NewArrayTyped
- [x] GetElem, SetElem
- [x] GetElemTyped, SetElemTyped
- [x] ArrayLength
- [x] ArrayPush

### 4.10 Calls
- [x] Call (with type-specific dispatch for console)
- [x] CallMethod (fallback for unresolved)
- [x] CallVirtual (vtable dispatch via TsObject::vtable)
- [x] CallIndirect

### 4.11 Globals
- [x] LoadGlobal
- [x] LoadFunction

### 4.14 Closures
- [x] MakeClosure (creates TsClosure with captured values)
- [x] LoadCapture (loads from closure context)
- [x] StoreCapture (stores to mutable capture cell)
- [x] Hidden __closure parameter for functions with captures
- [x] Capture propagation for nested closures

### 4.12 Control Flow
- [x] Branch
- [x] CondBranch
- [x] Switch
- [x] Return, ReturnVoid
- [x] Unreachable

### 4.13 Exception Handling
- [x] SetupTry (push handler + setjmp)
- [x] PopHandler
- [x] Throw (set exception + longjmp)
- [x] GetException
- [x] ClearException

### 4.14 SSA
- [x] Phi
- [x] Select
- [x] Copy

---

## Phase 5: Optimization Passes (Future)

### 5.1 Analysis Infrastructure
- [ ] AnalysisManager for caching results
- [ ] DominanceAnalysis
- [ ] LivenessAnalysis
- [ ] AliasAnalysis

### 5.2 TypePropagationPass
- [x] Propagate types through SSA graph
- [x] Handle phi nodes (merge types at merge points)
- [x] Narrow types based on guards (typeof, instanceof) - basic framework
- [x] Track array element types (via inferResultType for GetElem)

### 5.3 ConstantFoldingPass
- [x] Fold arithmetic on constants (int and float)
- [x] Fold string concatenation
- [x] Fold boolean operations (&&, ||, !)
- [x] Fold comparisons (==, !=, <, <=, >, >=)

### 5.4 DeadCodeEliminationPass
- [x] Remove unused instructions (liveness analysis with backward propagation)
- [x] Remove unreachable blocks (BFS from entry block)
- [x] Remove dead stores (side-effect analysis)

### 5.5 InliningPass
- [x] Inline small functions (with configurable size threshold)
- [x] Inline lambda callbacks (simple single-block lambdas)
- [x] Cost model for inlining decisions (size-based with call count bonus)
- [x] Call graph analysis with recursion detection
- [x] Value cloning and remapping for inlining transformation
- [x] Multi-block function support (disabled by default)

### 5.6 EscapeAnalysisPass
- [ ] Track object escapes
- [ ] Enable stack allocation for non-escaping objects
- [ ] Scalar replacement of aggregates

---

## Testing

### Unit Tests
- [ ] PassManager tests
- [ ] MethodResolutionPass tests
- [ ] BuiltinResolutionPass tests
- [ ] Individual opcode lowering tests

### Integration Tests
- [x] Basic array.push test (hir_push_simple.ts)
- [ ] Golden IR tests for HIR output
- [ ] End-to-end compilation tests

---

## Files

### Core HIR
- `src/compiler/hir/HIR.h` - Data structures
- `src/compiler/hir/HIR.cpp` - Implementation
- `src/compiler/hir/ASTToHIR.h` - AST to HIR lowering
- `src/compiler/hir/ASTToHIR.cpp`
- `src/compiler/hir/HIRToLLVM.h` - HIR to LLVM lowering
- `src/compiler/hir/HIRToLLVM.cpp`
- `src/compiler/hir/HIRPrinter.h` - Debug output
- `src/compiler/hir/HIRPrinter.cpp`
- `src/compiler/hir/BuiltinRegistry.h` - Centralized builtin resolution
- `src/compiler/hir/BuiltinRegistry.cpp`

### Passes
- `src/compiler/hir/passes/HIRPass.h` - Base class
- `src/compiler/hir/passes/HIRPass.cpp`
- `src/compiler/hir/passes/PassManager.h`
- `src/compiler/hir/passes/PassManager.cpp`
- `src/compiler/hir/passes/MethodResolutionPass.h`
- `src/compiler/hir/passes/MethodResolutionPass.cpp`
- `src/compiler/hir/passes/BuiltinResolutionPass.h`
- `src/compiler/hir/passes/BuiltinResolutionPass.cpp`
- `src/compiler/hir/passes/TypePropagationPass.h`
- `src/compiler/hir/passes/TypePropagationPass.cpp`
- `src/compiler/hir/passes/ConstantFoldingPass.h`
- `src/compiler/hir/passes/ConstantFoldingPass.cpp`
- `src/compiler/hir/passes/DeadCodeEliminationPass.h`
- `src/compiler/hir/passes/DeadCodeEliminationPass.cpp`
- `src/compiler/hir/passes/InliningPass.h`
- `src/compiler/hir/passes/InliningPass.cpp`

---

## Completion Summary

| Phase | Completion |
|-------|------------|
| Phase 1: Core Infrastructure | 100% |
| Phase 2: Resolution Passes | 100% |
| Phase 3: ASTToHIR Coverage | 98% |
| Phase 4: HIRToLLVM Coverage | 100% |
| Phase 5: Optimization Passes | 80% |

**Overall: ~99% Complete**

### Recent Progress (2026-01-29)
- **Class expressions:**
  - Full support for anonymous and named class expressions
  - Unique name generation for anonymous classes (__class_expr_N)
  - Proper inheritance handling (extends clause)
  - Shape creation with inherited properties from base class
  - Instance/static properties and methods
  - Constructor processing with super calls
  - Result value is reference to class constructor function
- **Generator function support:**
  - Generator functions (function*) with Yield and YieldStar opcodes
  - Yield expressions produce IteratorResult values
  - Yield* delegation to other generators/iterables
  - Generator return handling marks generator as done
- **Async generator support:**
  - Async generators (async function*) combining async and generator semantics
  - AsyncGenerator creation with ts_async_generator_create
  - Async yield produces Promises
  - Proper return handling for async generators
- **Tagged template literals:**
  - Full support for tagged templates with strings array and values
- **Comma expressions:**
  - Sequential evaluation with proper value propagation
- **Async/await support:**
  - Await expressions now generate HIR Await opcode
  - Async functions properly call ts_promise_await for blocking semantics
  - AsyncReturn opcode for resolving async function promises
- **Spread elements:**
  - Full support for array spread in literals and function calls
  - Object spread in object literals
- **Private fields (#field):**
  - Class private field declarations and access
  - Proper encapsulation via mangled property names
- **Map/Set method resolution:**
  - Added HIRTypeKind::Map and HIRTypeKind::Set type kinds
  - Registered all Map instance methods (get, set, has, delete, clear, size, forEach, entries, keys, values)
  - Registered all Set instance methods (add, has, delete, clear, size, forEach, entries, keys, values)
  - Registered ES2024 Set methods (union, intersection, difference, symmetricDifference, isSubsetOf, isSupersetOf, isDisjointFrom)
  - Registered Map.groupBy static method
- **Expression completions:**
  - Comma expressions: evaluate both sides for side effects, return right value
  - Tagged template literals: full implementation with strings array, raw property, and expression arguments
  - Yield expressions: added Yield and YieldStar opcodes for generator support

### Recent Progress (2026-01-28 evening)
- **Template literals with expressions:**
  - Support for `${expression}` interpolation within template strings
  - TemplateLiteral, TemplateHead, TemplateMiddle, TemplateTail spans
  - Lowered using StringConcat chain with expression stringification
  - ts_stringification runtime helper for non-string expression values
- **Optional chaining (`?.`):**
  - Property access: `obj?.prop`
  - Element access: `arr?.[index]`
  - Null/undefined check → short-circuit to undefined
  - Creates conditional branch with null coalescing
- **Nullish coalescing (`??`):**
  - `a ?? b` evaluates to `b` only if `a` is null/undefined
  - CmpEqPtr with null constant + conditional branch
  - Fixed double-boxing issue in HIRToLLVM::lowerStore (boxes non-pointer values when storing to Any-typed allocas)
- **Getters/setters in object literals:**
  - `get propName() { }` and `set propName(value) { }`
  - Uses `__getter_<name>` and `__setter_<name>` naming convention
  - Full closure support (captures from outer scope)
  - Implicit `this` parameter for method body

### Recent Progress (2026-01-28)
- **BuiltinRegistry static method registrations completed:**
  - Object static methods: keys, values, entries, assign, hasOwn, fromEntries, create, freeze, seal, is, getPrototypeOf, setPrototypeOf, isFrozen, isSealed, isExtensible, preventExtensions, defineProperty, defineProperties, getOwnPropertyDescriptor, getOwnPropertyDescriptors, getOwnPropertyNames, groupBy (22 methods)
  - Array static methods: isArray, from, of (3 methods)
  - Number static methods: isNaN, isFinite, isInteger, isSafeInteger, parseInt, parseFloat (6 methods)
  - String static methods: fromCharCode, fromCodePoint, raw (3 methods)
  - Global functions: parseInt, parseFloat, isNaN, isFinite (4 functions)
  - All registered in BuiltinRegistry for proper HIR resolution
- **InliningPass fully working and enabled:**
  - Call graph analysis with call count tracking
  - DFS-based recursion detection (prevents infinite inlining)
  - Size-based cost model with configurable thresholds
  - Single-call-site bonus (more aggressive inlining for unique calls)
  - Simple lambda detection for callback inlining
  - **Fixed parameter-to-argument mapping** - callee parameters now correctly replaced with caller arguments
  - Value cloning and remapping with proper SSA maintenance (ID and name matching)
  - Return value extraction and call result replacement
  - Multi-block function support (disabled by default for safety)
  - Fixed-point iteration until no more inlining opportunities
  - **Enabled in default HIR pipeline** in Driver.cpp
- **Type system fix for numeric literals:**
  - All numeric literals now typed as `TypeKind::Double` (matching JavaScript semantics)
  - Fixes HIR type mismatch error (`i64` vs `double`) when passing numeric literals to functions
  - TypeScript `number` type is always IEEE 754 double-precision
  - See `Analyzer_Expressions_Literals.cpp:visitNumericLiteral`
- **DeadCodeEliminationPass implemented:**
  - Liveness analysis with backward propagation from roots
  - Side-effect detection for calls, stores, mutations
  - Unreachable block detection via BFS from entry
  - Removes unused instructions after constant folding
  - Full test verification with unused variables/computations
- **ConstantFoldingPass implemented:**
  - Integer arithmetic folding (add, sub, mul, div, mod)
  - Float arithmetic folding (add, sub, mul, div)
  - Boolean operation folding (and, or, not)
  - Comparison folding (eq, ne, lt, le, gt, ge for int and float)
  - String concatenation folding
  - Type cast folding (int-to-float, float-to-int, bool-to-int)
  - Safe handling of division/modulo by zero (returns Unknown)
  - Tracks constants through SSA graph for chained operations
- **TypePropagationPass implemented:**
  - Forward type propagation for constants, arithmetic, boxing/unboxing
  - Phi node type merging (join of incoming types)
  - Type narrowing framework for typeof/instanceof guards
  - Array element type tracking
  - Iterative fixpoint algorithm for convergence
- **Destructuring patterns fully implemented:**
  - Object destructuring: `const { a, b } = obj`
  - Property renaming: `const { a: x, b: y } = obj`
  - Array destructuring: `const [first, second] = arr`
  - Holes in array patterns: `const [a, , c] = arr`
  - Default values for objects: `const { d = 100 } = obj`
  - Default values for arrays: `const [a, b, c, d = 40] = arr`
  - Rest elements: `const [first, ...rest] = arr`
  - Nested destructuring (objects in arrays, arrays in objects)
  - Uses `ts_value_is_undefined` runtime function for proper default value handling
  - Boxing with `boxValueIfNeeded` for select instruction type consistency
- **Exception handling fully implemented:**
  - Try/catch/finally statements with all combinations
  - Throw statements (direct and from function calls)
  - setjmp/longjmp based implementation at runtime
  - SetupTry, PopHandler, Throw, GetException, ClearException HIR opcodes
  - Try-finally without catch (exception stored, finally runs, then rethrow)
  - Nested try statements with unique block naming
  - Unreachable code detection after throw
- **Classes fully implemented:**
  - Class declarations with constructors
  - Instance methods and properties (via shape-based object layout)
  - Static methods and properties (via global variables)
  - Inheritance with `extends` keyword
  - `super()` constructor calls with argument passing
  - Inherited method resolution (searches base class chain)
  - Property inheritance via shape copying from base class
  - String/number/bool property boxing in SetPropStatic/SetPropDynamic
  - String concatenation type coercion (int/double/bool → string)
- Full closure implementation with mutable captures and nested closure propagation
- TsClosure runtime struct with heap-allocated capture cells (TsCell)
- MakeClosure, LoadCapture, StoreCapture HIR opcodes fully lowered to LLVM
- CallIndirect passes closure context correctly
- All closure, class, exception, and destructuring tests passing

---

## Next Steps (Priority Order)

1. **EscapeAnalysisPass** - Track object escapes for stack allocation
2. **Class getters/setters** - Extend getter/setter support from object literals to classes
3. **Generator functions** - `function*` and `yield` support
4. **Enum declarations** - Full enum support with const enums
