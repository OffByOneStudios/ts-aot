# Lodash Runtime Hang Investigation Report

## Executive Summary

**Problem:** Lodash hangs infinitely during module initialization  
**Root Cause:** While loops inside IIFEs enter infinite loops at runtime  
**Codegen Status:** IR generation is CORRECT  
**Issue Location:** Runtime execution or LLVM optimization

### Critical Test Case
```typescript
(function() {
    var i = 0;
    while (i < 5) { i++; }  // INFINITE LOOP
    console.log("done");     // NEVER REACHED
})();
```

**Status:** Hangs indefinitely ❌  
**Same code outside IIFE:** Works correctly ✅

## Problem Summary
Lodash compiles successfully but hangs indefinitely during module initialization, before any user code executes.

**Symptoms:**
- ✅ Compilation succeeds (exit code 0)
- ✅ IIFE pattern works in isolation: `var x = (function(){ return 42; })()`
- ✅ `.call(this)` works: `(function(){ return 42; }).call(this)`
- ❌ Lodash require hangs: `require("lodash")` never returns
- ❌ No output printed - hangs before "console.log" in main module

## Test Case
```typescript
// examples/test_require_simple.ts
console.log("Before require");  // NEVER PRINTS
const _ = require("lodash");
console.log("After require");
```

**Result:** Process hangs indefinitely, prints nothing.

## Lodash Structure
Lodash (node_modules/lodash/lodash.js, 17,210 lines) uses this pattern:
```javascript
;(function() {
  var undefined;
  var VERSION = '4.17.21';
  // ... 17,000 lines of function definitions and constants ...
  
  // Lots of regex patterns
  var reRegExpChar = /[\\^$.*+?()[\]{}|]/g;
  var reHasRegExpChar = RegExp(reRegExpChar.source);
  
  // Many while loops in utility functions
  while (++index < length) { /* ... */ }
  
  // Returns the lodash object
  return _;
}.call(this));
```

## Recent Fixes
1. **FunctionExpression return type inference** - Fixed analyzer to infer return types from return statements
2. **IIFE codegen** - Added support for calling FunctionExpression/ArrowFunction directly

These fixes allow simple IIFEs to work correctly.

## Possible Causes

### 1. Infinite Loop in Module Initialization (Most Likely)
**Evidence:**
- Hang occurs before any console output
- Lodash has extensive initialization code with:
  - Complex regex operations
  - Many while loops in utility functions
  - Recursive function definitions

**Hypothesis:** A while loop condition or recursive function is not terminating correctly during module init.

**Relevant Code Patterns:**
```javascript
// Line 509 and many others:
while (++index < length) {
  // If 'index' or 'length' is uninitialized/NaN, this could loop forever
}

// Line 947:
while (length--) {
  // If 'length' is not a number, could loop forever
}
```

### 2. Variable Initialization Issue
**Evidence:** Lodash starts with `var undefined;` (redefining undefined)

**Hypothesis:** Variable hoisting or initialization order could cause:
- Variables used before defined
- Uninitialized variables in loop conditions
- Type confusion (string vs number)

### 3. Regex Compilation Issue
**Evidence:** Lodash defines 40+ regex patterns at module scope:
```javascript
var reRegExpChar = /[\\^$.*+?()[\]{}|]/g;
var reHasRegExpChar = RegExp(reRegExpChar.source);
```

**Hypothesis:** Regex construction or compilation might hang.

### 4. Property Access on Uninitialized Object
**Evidence:** Lodash does extensive property access during init:
```javascript
freeModule && freeModule.exports
```

**Hypothesis:** If objects are not properly initialized, property access might trigger infinite recursion.

## Debugging Approach Needed

### Step 1: Isolate the Hang
Create minimal lodash reproduction:
```typescript
// Test 1: Just the IIFE wrapper
(function() {
  var VERSION = '4.17.21';
  console.log("Inside lodash IIFE");
  return {};
}.call(this));

// Test 2: Add first while loop
(function() {
  var index = 0, length = 5;
  while (++index < length) {
    // empty
  }
  console.log("While loop completed");
}.call(this));

// Test 3: Add regex
(function() {
  var re = /test/g;
  console.log("Regex created");
}.call(this));
```

### Step 2: Check IR Generation
Dump IR for simple while loop to verify condition checking:
```bash
ts-aot test_while.ts --dump-ir
```

Look for:
- Loop condition evaluation
- Variable increment operations
- Loop termination logic

### Step 3: Add Runtime Logging
Modify runtime or add instrumentation to track:
- Module initialization entry/exit
- Loop iterations (if infinite loop)
- Function call depth (if recursion)

## IR Analysis - CRITICAL FINDING

The IR for both cases (with and without IIFE) looks **CORRECT**:

### Non-IIFE (WORKS):
```llvm
whilecond:
  %i = load i64, ptr @i, align 8        ; Load global variable
  %cmptmp = icmp slt i64 %i, 5          ; Compare
  br i1 %cmptmp, label %whileloop, label %whileafter  ; Branch

whileloop:
  %addtmp5 = add i64 %i4, 1
  store i64 %addtmp5, ptr @i, align 8   ; Store to global
  br label %whilecond                    ; Loop back
```

### IIFE (HANGS):
```llvm
define internal ptr @func_expr_0(ptr %context) {
entry:
  %i = alloca i64, align 8              ; Local variable (alloca)
  store i64 0, ptr %i, align 8
  br label %whilecond

whilecond:
  %i1 = load i64, ptr %i, align 8       ; Load from local alloca
  %cmptmp = icmp slt i64 %i1, 5         ; Compare
  br i1 %cmptmp, label %whileloop, label %whileafter  ; Branch

whileloop:
  %i4 = load i64, ptr %i, align 8
  %addtmp5 = add i64 %i4, 1
  store i64 %addtmp5, ptr %i, align 8   ; Store to local alloca
  br label %whilecond                    ; Loop back

whileafter:
  ret ptr %1                             ; Return from function
}
```

**The IR is correct!** The problem is at **RUNTIME**, not codegen.

## Root Cause: Runtime Issue

Since the IR is correct but execution hangs, the problem is likely:

### Hypothesis 1: LLVM Optimization Bug (Most Likely)
The loop condition `icmp slt` and branch look correct, but:
- LLVM might be misoptimizing the loop
- Stack frame setup in function might be broken
- The comparison might be getting wrong values

### Hypothesis 2: Alloca Corruption
Variables are allocated with `alloca` in IIFE but as globals otherwise:
- Stack allocation might be corrupted
- Pointer arithmetic could be wrong
- Memory alignment issues

### Hypothesis 3: ts_call_0 Interaction
The IIFE is called via `ts_call_0`:
```llvm
%5 = call ptr @ts_value_make_function(ptr @func_expr_0, ptr null)
%6 = call ptr @ts_call_0(ptr %5)
```

Problem could be in how `ts_call_0` sets up the call frame or returns.

## C++ Runtime Code to Investigate

## C++ Runtime Code to Investigate

### 1. ts_call_0 Function
**File:** `src/runtime/function.cpp` or similar
**Check:**
- How it invokes the function pointer
- Stack frame setup
- Return value handling
- Context pointer management

### 2. ts_value_make_function
**File:** `src/runtime/value.cpp` or similar
**Check:**
- How TsFunction wraps the function pointer
- Context storage
- Function metadata

### 3. LLVM Optimization Level
**File:** Compiler driver
**Test:** Try compiling with `-O0` (no optimization) to see if it still hangs

## Debugging Steps

1. **Add printf to runtime:**
   ```cpp
   // In ts_call_0:
   printf("ts_call_0: Calling function %p\n", funcPtr);
   result = funcPtr(context);
   printf("ts_call_0: Returned from function\n");
   ```

2. **Add printf to generated IR:**
   Inject `ts_console_log_int` calls:
   ```llvm
   whilecond:
     call void @ts_console_log_int(i64 999)  ; Debug marker
     %i1 = load i64, ptr %i, align 8
     call void @ts_console_log_int(i64 %i1) ; Print i value
     %cmptmp = icmp slt i64 %i1, 5
     br i1 %cmptmp, label %whileloop, label %whileafter
   ```

3. **Test simpler loops:**
   - Do-while loop in IIFE
   - For loop in IIFE
   - Infinite loop with break

4. **Check optimization:**
   ```bash
   ts-aot test_while_iife.ts -O0  # No optimization
   ```

## IR Snippets to Examine

Need to compare working IIFE vs lodash IIFE:

**Working (examples/test_iife_simple.ts):**
```llvm
define ptr @func_expr_0(ptr %context) {
  ; Simple return
  ret ptr <value>
}
; Call via ts_call_0
```

**Lodash (node_modules/lodash/lodash.js):**
```llvm
; Expected: Module init function with 17k lines of IR
; Check for: Infinite loop patterns, missing terminators
```

## CRITICAL FINDING: While Loops Hang in IIFEs

### Test Results

**✅ WORKS - While loop outside IIFE:**
```typescript
var i = 0;
while (i < 5) { i++; }
console.log("done");  // Prints "done"
```

**❌ HANGS - While loop inside IIFE:**
```typescript
(function() {
    var i = 0;
    while (i < 5) { i++; }
    console.log("done");  // NEVER PRINTS - INFINITE LOOP
})();
```

**Root Cause:** The combination of IIFE + while loop causes infinite loops.

### Why This Breaks Lodash
Lodash structure:
```javascript
(function() {
  // Line 509 and many others:
  while (++index < length) { /* ... */ }
  return lodash;
}.call(this));
```

The IIFE wrapper contains dozens of while loops in utility functions, all of which would hang.

## Proposed Next Steps

1. **Examine IIFE codegen** - Check how variables are scoped in IIFE functions
2. **Check IR** - Compare while loop IR in IIFE vs non-IIFE context
3. **Variable binding** - Ensure loop variables in IIFEs are properly allocated
4. **Context pointer** - Check if IIFE context affects variable access
5. **Test for loops** - See if for loops also hang in IIFEs

## Questions for Analysis

1. Does a simple while loop work in an IIFE?
   ```typescript
   (function() {
     var i = 0;
     while (i < 5) { i++; }
     console.log("done");
   })();
   ```

2. Does lodash hang during parse/analysis or at runtime?
   - Compiler succeeds → Runtime hang

3. Is there a maximum module size limit that causes issues?
   - Lodash is 17k lines

4. Does lodash use any unsupported features?
   - `arguments` object?
   - `eval` or `Function` constructor?
   - Getter/setter properties?

## System Info
- Compiler: ts-aot (Release build)
- Runtime: ts-aot runtime (Boehm GC, libuv event loop)
- OS: Windows
- Lodash version: 4.17.21
