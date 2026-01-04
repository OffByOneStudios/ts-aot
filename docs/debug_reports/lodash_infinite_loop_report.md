# Lodash Infinite Loop Debug Report

## Problem Statement

I'm building `ts-aot`, an ahead-of-time compiler for TypeScript that compiles to native code via LLVM. When running the compiled lodash library, the program hangs in an infinite loop. The loop appears to be repeatedly accessing a property called `.undefined` on TsFunction objects.

## Test Case

```typescript
// lodash_simple.ts
console.log("Before require");
const _ = require("lodash");
console.log("After require");
console.log("Done!");
```

**Expected output:**
```
Before require
After require
Done!
```

**Actual behavior:** The program hangs after "Before require" and never completes. When debug logging was enabled, we saw thousands of `.undefined` property accesses on TsFunction objects.

## Architecture Overview

### Compiler
- TypeScript → LLVM IR → Native Code
- Lodash is parsed as untyped JavaScript (all types inferred as `any`)
- Uses CommonJS module system (`require()`)

### Runtime
- GC-based memory management (Boehm GC via `ts_alloc`)
- Dynamic typing with `TsValue` boxed values:
  ```cpp
  struct TsValue {
      ValueType type;  // enum: UNDEFINED, NUMBER_INT, NUMBER_DBL, BOOLEAN, STRING_PTR, OBJECT_PTR, etc.
      union {
          int64_t i_val;
          double d_val;
          bool b_val;
          void* ptr_val;
      };
  };
  ```
- Objects are stored as `TsMap` (hash map of string keys → TsValue)
- Functions are stored as `TsFunction`:
  ```cpp
  struct TsFunction {
      void* funcPtr;       // Pointer to compiled function
      void* context;       // Closure context
      FunctionType type;   // COMPILED or NATIVE
      uint32_t magic;      // 0x46554E43 ("FUNC") for type identification
      TsMap* properties;   // Function can have properties (like _.chunk)
  };
  ```

## Key Code Snippets

### 1. Property Access (ts_object_get_property)

This function is called whenever we access a property on an object:

```cpp
TsValue* ts_object_get_property(void* obj, const char* keyStr) {
    if (!obj) {
        return ts_value_make_undefined();
    }
    
    // Try to detect TsValue* at the start
    uint8_t firstByte = *(uint8_t*)obj;
    if (firstByte <= 10) {  // Could be a valid ValueType
        TsValue* maybeVal = (TsValue*)obj;
        if (maybeVal->type == ValueType::UNDEFINED) {
            return ts_value_make_undefined();
        }
        if ((maybeVal->type == ValueType::OBJECT_PTR || maybeVal->type == ValueType::ARRAY_PTR || 
             maybeVal->type == ValueType::PROMISE_PTR) && maybeVal->ptr_val) {
            obj = maybeVal->ptr_val;
        }
    }
    
    // Check magic bytes to identify object type
    uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
    
    // Check for TsFunction (magic at offset 16)
    if (magic16 == 0x46554E43) { // TsFunction::MAGIC ("FUNC")
        TsFunction* func = (TsFunction*)obj;
        if (func->properties) {
            TsValue k;
            k.type = ValueType::STRING_PTR;
            k.ptr_val = TsString::Create(keyStr);
            TsValue val = func->properties->Get(k);
            if (val.type != ValueType::UNDEFINED) {
                TsValue* res = (TsValue*)ts_alloc(sizeof(TsValue));
                *res = val;
                return res;
            }
        }
        
        // Handle special function properties
        if (strcmp(keyStr, "prototype") == 0) { /* ... create prototype lazily ... */ }
        if (strcmp(keyStr, "length") == 0) { return ts_value_make_int(0); }
        if (strcmp(keyStr, "name") == 0) { return ts_value_make_string(TsString::Create("")); }
        
        // Unknown property on function -> return undefined
        return ts_value_make_undefined();
    }
    
    // ... other type checks ...
}
```

### 2. Object.keys / for-in loops (ts_object_keys)

For-in loops compile to code that calls `ts_object_keys()` to get all enumerable keys:

```cpp
TsValue* ts_object_keys(TsValue* obj) {
    if (!obj) return ts_value_make_array(TsArray::Create(0));
    
    void* rawPtr = ts_value_get_object(obj);
    if (!rawPtr) rawPtr = obj;
    
    // Check TsMap::magic at offset 24
    uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
    if (magic == 0x4D415053) { // TsMap::MAGIC
        return ts_value_make_array(ts_map_keys(rawPtr));
    }
    
    // Not a map - return empty array
    return ts_value_make_array(TsArray::Create(0));
}
```

### 3. Lodash for-in usage (from lodash.js)

Lodash uses `for-in` loops extensively:

```javascript
// Line 2434 - arrayLikeKeys function
function arrayLikeKeys(value, inherited) {
    var isArr = isArray(value),
        isArg = !isArr && isArguments(value),
        // ...
        skipIndexes = isArr || isArg || isBuff || isType,
        result = skipIndexes ? baseTimes(value.length, String) : [],
        length = result.length;

    for (var key in value) {
        if ((inherited || hasOwnProperty.call(value, key)) &&
            !(skipIndexes && (
               key == 'length' ||
               // ...
            ))) {
          result.push(key);
        }
    }
    return result;
}

// Line 3529 - baseKeysIn
function baseKeysIn(object) {
    if (!isObject(object)) {
        return nativeKeysIn(object);
    }
    var isProto = isPrototype(object),
        result = [];

    for (var key in Object(object)) {
        if (!(key == 'constructor' && (isProto || !hasOwnProperty.call(object, key)))) {
            result.push(key);
        }
    }
    return result;
}
```

### 4. For-in IR Generation (from IRGenerator_Statements_Loops.cpp)

```cpp
// For TypeKind::Any objects (like lodash dynamic values)
} else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
    llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
    llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee keysFn = getRuntimeFunction("ts_object_keys", keysFt);
    llvm::Value* boxedKeys = createCall(keysFt, keysFn.getCallee(), { boxedObj });
    
    llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
    keys = createCall(getObjFt, getObjFn.getCallee(), { boxedKeys });
}
```

## Debug Observations

When debug logging was enabled in `ts_object_get_property`, we observed:
1. Thousands of calls with `keyStr = "undefined"`
2. The `obj` parameter was consistently a `TsFunction` (magic = 0x46554E43)
3. The loop never terminated

## Critical Lodash Code Patterns

### 1. Accessing Function.prototype (line 1463-1464)
```javascript
var arrayProto = Array.prototype,
    funcProto = Function.prototype,
    objectProto = Object.prototype;
```

### 2. Using prototype methods (line 1471-1473)
```javascript
var funcToString = funcProto.toString;
var hasOwnProperty = objectProto.hasOwnProperty;
```

### 3. Calling Function('return this')() (line 436)
```javascript
var root = freeGlobal || freeSelf || Function('return this')();
```
This uses the `Function` constructor to create and immediately call a function. Our runtime may not handle this correctly.

### 4. Using _.defaults with root.Object() (line 1449)
```javascript
context = context == null ? root : _.defaults(root.Object(), context, _.pick(root, contextProps));
```

## Minimal Reproduction Test Results

| Test | Result |
|------|--------|
| `Object.keys({a:1, b:2})` | ✅ Works - returns `['a', 'b']` |
| `Object.keys(myFunc)` | ✅ Works - returns `[]` (empty) |
| `for (var k in {a:1, b:2})` | ✅ Works - iterates correctly |
| `for (var k in myFunc)` | ❌ CRASHES with access violation (0xc0000005) |

The crash on `for (var k in myFunc)` is a key finding - it's not an infinite loop but a memory access violation.

## Hypotheses

### ✅ CONFIRMED: Hypothesis 1 - For-in on Function passes wrong pointer type

**Root Cause Found!** The for-in loop codegen has a bug for `TypeKind::Function`.

When iterating over a function with `for (var k in myFunc)`:

1. `myFunc` has `TypeKind::Function` (not `Any`, not `Object`)
2. The codegen falls through to the `else` branch:
   ```cpp
   } else {
       llvm::FunctionType* keysFt = llvm::FunctionType::get(...);
       llvm::FunctionCallee keysFn = getRuntimeFunction("ts_map_keys", keysFt);
       keys = createCall(keysFt, keysFn.getCallee(), { obj });  // BUG!
   }
   ```
3. It calls `ts_map_keys(obj)` where `obj` is a **boxed TsValue*** (from `ts_value_make_function`)
4. But `ts_map_keys` expects an **unboxed TsMap*** pointer!

**IR Evidence:**
```llvm
%10 = call ptr @ts_value_make_function(ptr @myFunc_boxed_wrapper_1, ptr null)
%11 = call ptr @ts_map_keys(ptr %10)   ; <-- WRONG: passing TsValue* to ts_map_keys!
```

**The Fix:** For `TypeKind::Function`, either:
1. Return an empty array (functions have no enumerable properties by default)
2. Or use `ts_object_keys` which handles boxing correctly

### Hypothesis 2: Boxing/unboxing issue in for-in loop

The for-in loop may be receiving a boxed `TsValue*` but treating it as an unboxed pointer, or vice versa. This could cause the keys array to be misinterpreted.

### Hypothesis 3: Lodash's `Object(value)` wrapper

Lodash wraps values with `Object(value)` before iterating (line 3529):
```javascript
for (var key in Object(object)) { ... }
```

Our `Object` constructor might be returning something unexpected that causes infinite iteration.

### Hypothesis 4: Prototype chain iteration

The for-in loop should iterate over inherited properties too. If our prototype chain has a circular reference or keeps returning new keys, it could loop forever.

### Hypothesis 5: Iterator state corruption

If the for-in loop's index variable or keys array gets corrupted during iteration (perhaps by a closure capturing and modifying it), the termination condition might never be met.

## Questions for Review

1. **For-in semantics**: When iterating over a function with `for-in`, what keys should be returned? Just own properties, or also inherited properties from `Function.prototype`?

2. **Object() constructor**: What should `Object(func)` return when `func` is a function? The function itself, or a wrapper object?

3. **Key detection**: Is checking magic bytes at fixed offsets reliable for type identification? Could there be alignment/padding issues?

4. **Boxing consistency**: Are we consistently boxing/unboxing values before passing them to runtime functions? The `ts_object_keys` function tries to handle both boxed and unboxed inputs.

## Relevant Files

- `src/runtime/src/TsObject.cpp` - Property access and Object.keys implementation
- `src/compiler/codegen/IRGenerator_Statements_Loops.cpp` - For-in loop code generation
- `src/compiler/codegen/BoxingPolicy.cpp` - Boxing policy for runtime function calls
- `node_modules/lodash/lodash.js` - Lodash source (17,210 lines)

## Environment

- Windows 10/11
- LLVM 18 (opaque pointers)
- MSVC 2022
- C++20 standard

---

## Proposed Fixes

### Fix 1: Handle TypeKind::Function in for-in codegen

In `IRGenerator_Statements_Loops.cpp`, add handling for `TypeKind::Function`:

```cpp
} else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Function) {
    // Functions have no enumerable own properties by default
    // (unless they have explicit properties set like _.chunk)
    // Use ts_object_keys which handles boxed values correctly
    llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
    llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee keysFn = getRuntimeFunction("ts_object_keys", keysFt);
    llvm::Value* boxedKeys = createCall(keysFt, keysFn.getCallee(), { boxedObj });
    
    llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
    keys = createCall(getObjFt, getObjFn.getCallee(), { boxedKeys });
} else {
```

### Fix 2: Make ts_object_keys handle TsFunction

In `ts_object_keys`, add handling for TsFunction:

```cpp
TsValue* ts_object_keys(TsValue* obj) {
    if (!obj) return ts_value_make_array(TsArray::Create(0));
    
    void* rawPtr = ts_value_get_object(obj);
    if (!rawPtr) rawPtr = obj;
    
    // Check TsFunction magic at offset 16
    uint32_t funcMagic = *(uint32_t*)((char*)rawPtr + 16);
    if (funcMagic == 0x46554E43) { // TsFunction::MAGIC ("FUNC")
        TsFunction* func = (TsFunction*)rawPtr;
        if (func->properties) {
            return ts_value_make_array(ts_map_keys(func->properties));
        }
        return ts_value_make_array(TsArray::Create(0));
    }
    
    // Check TsMap magic at offset 24
    uint32_t magic = *(uint32_t*)((char*)rawPtr + 24);
    if (magic == 0x4D415053) { // TsMap::MAGIC
        return ts_value_make_array(ts_map_keys(rawPtr));
    }
    
    return ts_value_make_array(TsArray::Create(0));
}
```

### Fix 3: Make the else branch use ts_object_keys instead of ts_map_keys

The safest fix is to change the `else` branch to always use `ts_object_keys` with boxing, which handles all types correctly:

```cpp
} else {
    // Fallback: use ts_object_keys which handles boxing correctly
    llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
    llvm::FunctionType* keysFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee keysFn = getRuntimeFunction("ts_object_keys", keysFt);
    llvm::Value* boxedKeys = createCall(keysFt, keysFn.getCallee(), { boxedObj });
    
    llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
    llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
    keys = createCall(getObjFt, getObjFn.getCallee(), { boxedKeys });
}
```
