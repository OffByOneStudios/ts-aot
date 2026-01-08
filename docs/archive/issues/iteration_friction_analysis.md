# Deep Dive: Why Features Required Excessive Iteration

**Date:** December 28, 2025  
**Context:** Analysis of HTTPS implementation (Milestone 100.5) which required ~50+ debug cycles
**Status:** ✅ All issues addressed (see Solutions Implemented below)

## Executive Summary

The ts-aot compiler had architectural issues that caused a **multiplicative debugging burden**. Each new feature required debugging at 4+ layers, and issues at one layer masked issues at other layers. The root causes were:

1. **No Contextual Typing** - Callback parameters were always `TypeKind::Any` ✅ FIXED
2. **Implicit Boxing/Unboxing** - No clear contract between compiler and runtime ✅ FIXED
3. **Virtual Inheritance Complexity** - Pointer layouts are unpredictable ✅ FIXED
4. **No Type Debugging Tools** - Couldn't easily see what types flow through ✅ FIXED

---

## Solutions Implemented

### 1. Contextual Typing (Analyzer_Expressions_Calls.cpp, Analyzer_Functions.cpp)
- Added `contextualTypeStack` to Analyzer for propagating expected callback types
- `getExpectedCallbackType()` returns expected callback signatures for http, https, net, fs APIs
- Arrow functions now use contextual type to infer parameter types when not explicitly annotated

### 2. Consistent Unboxing (TsObject.h, TsObject.cpp, Primitives.cpp)
- Added `ts_value_get_*` family of helpers: `ts_value_get_object`, `ts_value_get_int`, `ts_value_get_double`, `ts_value_get_bool`, `ts_value_get_string`
- All C API functions should use these helpers for consistent unboxing

### 3. Safe Casting Helpers (TsObject.h, TsReadable.h, TsWritable.h, TsDuplex.h, TsSocket.h, TsServer.h)
- Added `AsXxx()` virtual methods to all stream classes
- `AsEventEmitter()`, `AsReadable()`, `AsWritable()`, `AsDuplex()`, `AsSocket()`, `AsServer()`
- Eliminates the need for C-style casts with virtual inheritance

### 4. Documentation (`.github/DEVELOPMENT.md`)
- Comprehensive development guide covering:
  - Contextual typing mechanism and how to extend it
  - Boxing/unboxing conventions
  - Safe casting patterns
  - Debugging and diagnostics
  - Common pitfalls and how to avoid them

---

## Root Cause #1: Missing Contextual Typing

### The Problem

When you write:
```typescript
http.createServer((req, res) => {
    res.writeHead(200);  // res has type Any!
    res.end("Hello");
});
```

The analyzer sees the arrow function in isolation. It doesn't know that `http.createServer` expects a callback with signature `(IncomingMessage, ServerResponse) => void`. So `req` and `res` get `TypeKind::Any`.

### Evidence from Code

```cpp
// Analyzer_Functions.cpp line 395-410
void Analyzer::visitArrowFunction(ast::ArrowFunction* node) {
    auto funcType = std::make_shared<FunctionType>();
    
    for (auto& param : node->parameters) {
        std::shared_ptr<Type> paramType = parseType(param->type, symbols);  // param->type is null!
        funcType->paramTypes.push_back(paramType);  // pushes Any
    }
}
```

No contextual type is passed in. Compare to TypeScript's `checkExpressionWithContextualType()`.

### Impact

The codegen layer has to compensate with **heuristics**:

```cpp
// IRGenerator_Expressions_Calls_Builtin_HTTP.cpp line 30-36
if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
    // Infer type from method name being called!
    if (prop->name == "writeHead" || prop->name == "write" || prop->name == "end") {
        isServerResponse = true;  // HACK
    }
}
```

This is fragile and will break for:
- Custom method names
- Intermediate variables (`const writer = res; writer.end()`)
- Conditionals (`(cond ? res1 : res2).end()`)

### Solution

Implement contextual typing in the analyzer:

```cpp
void Analyzer::visitCallExpression(ast::CallExpression* node) {
    // Get the callee's function type
    auto calleeType = ... 
    
    for (size_t i = 0; i < node->arguments.size(); i++) {
        auto arg = node->arguments[i].get();
        
        // Push contextual type for arrow functions
        if (auto arrow = dynamic_cast<ast::ArrowFunction*>(arg)) {
            if (i < calleeType->paramTypes.size()) {
                auto expectedFnType = calleeType->paramTypes[i];
                if (expectedFnType->kind == TypeKind::Function) {
                    pushContextualType(expectedFnType);
                    visit(arrow);
                    popContextualType();
                    continue;
                }
            }
        }
        visit(arg);
    }
}
```

---

## Root Cause #2: Implicit Boxing/Unboxing

### The Problem

The runtime uses `TsValue*` for boxed values and raw pointers for unboxed. But:
- There's no clear contract about what's boxed
- The compiler tracks this with `boxedValues` set, which is fragile
- The runtime has to guess via magic checks

### Evidence

```cpp
// Runtime - EventEmitter.cpp (before fix)
void ts_event_emitter_on(void* emitter, const char16_t* event, void* callback) {
    // Is emitter a TsValue* or TsEventEmitter*? WHO KNOWS!
    TsValue* val = (TsValue*)emitter;
    void* rawPtr = emitter;
    if ((uint8_t)val->type <= 10 && val->type == ValueType::OBJECT_PTR) {
        rawPtr = val->ptr_val;  // Try to unbox
    }
}
```

This pattern is repeated **everywhere** and is error-prone.

### Impact

We had multiple bugs:
1. Double-boxing (boxing an already-boxed value)
2. Not tracking boxed values in `boxedValues` set
3. Runtime magic checks failing due to memory alignment

### Solution

Option A: **Type-safe C API** - Different functions for boxed vs unboxed:
```cpp
void ts_event_emitter_on_boxed(TsValue* emitter, ...);
void ts_event_emitter_on_raw(TsEventEmitter* emitter, ...);
```

Option B: **Always box at the boundary** - All C API functions receive `TsValue*`:
```cpp
// Compiler always boxes, runtime always unboxes
void ts_event_emitter_on(TsValue* emitter, TsValue* event, TsValue* callback);
```

Option C: **Tagged pointers** - Use low bits to indicate boxed/unboxed:
```cpp
inline bool is_boxed(void* p) { return ((uintptr_t)p & 1) == 1; }
inline void* box_tag(void* p) { return (void*)((uintptr_t)p | 1); }
```

---

## Root Cause #3: Virtual Inheritance Complexity

### The Problem

The stream class hierarchy uses virtual inheritance:
```cpp
class TsReadable : public virtual TsEventEmitter { ... };
class TsWritable : public virtual TsEventEmitter { ... };
class TsDuplex : public TsReadable, public TsWritable { ... };
```

With virtual inheritance, `static_cast` and pointer arithmetic **DO NOT WORK**. You must use `dynamic_cast`.

### Evidence

```cpp
// WRONG - will crash or corrupt memory
TsWritable* w = (TsWritable*)ptr;  // ptr is TsDuplex*

// CORRECT
TsWritable* w = dynamic_cast<TsWritable*>((TsObject*)ptr);
```

### Impact

We had pointer corruption bugs where:
- Cast to base class gave wrong address
- Magic value checks read garbage memory
- RTTI was disabled in some builds

### Solution

1. **Never use C-style casts** for stream classes
2. **Add AsXxx() helpers**:
```cpp
class TsObject {
    virtual TsEventEmitter* AsEventEmitter() { return nullptr; }
    virtual TsWritable* AsWritable() { return nullptr; }
};
```
3. **Enable RTTI** in all builds (currently done)
4. **Consider removing virtual inheritance** - use composition instead

---

## Root Cause #4: No Type Debugging Tools

### The Problem

When something goes wrong, we can't see what types the analyzer inferred or what LLVM IR was generated without adding debug prints.

### Evidence

We added 96+ `fprintf` statements during HTTPS debugging. These were:
- Ad-hoc and scattered
- Not structured
- Had to be manually removed

### Solution

1. **Type dump command**:
```bash
ts-aot --dump-types foo.ts
# Output:
# foo.ts:5:3 req : Any (expected: IncomingMessage)
# foo.ts:5:8 res : Any (expected: ServerResponse)
```

2. **IR annotation mode**:
```bash
ts-aot --annotate-ir foo.ts -o foo.ll
# Generates LLVM IR with comments showing TypeScript types
```

3. **Runtime tracing**:
```bash
TSAOT_TRACE=boxing,calls ./foo.exe
# Logs all boxing/unboxing and C API calls
```

---

## Quantified Impact

| Issue | Cycles Wasted | Example |
|-------|---------------|---------|
| TypeKind::Any for callbacks | ~15 | `res.writeHead()` not recognized |
| Boxing confusion | ~10 | Double-boxed port, wrong unboxing |
| Virtual inheritance | ~8 | EventEmitter cast failures |
| Missing debug tools | ~12 | Adding/removing fprintf |
| **Total** | **~45 cycles** | Out of ~50 total |

---

## Recommended Prioritization

### High Impact, Medium Effort
1. **Contextual Typing for Callbacks** - Eliminates the biggest source of `Any` types
2. **Consistent Boxing Convention** - Pick one approach and apply everywhere

### Medium Impact, Low Effort
3. **AsXxx() helpers for casts** - Already partially done
4. **--dump-types flag** - Simple to add

### High Impact, High Effort
5. **Remove virtual inheritance** - Major refactor but eliminates a class of bugs

---

## Appendix: Files Requiring the Most Attention

| File | Lines | Issues |
|------|-------|--------|
| `IRGenerator_Expressions_Calls_Builtin.cpp` | 1644 | Giant switch statement, many boxing sites |
| `IRGenerator_Expressions_Calls_Builtin_FS.cpp` | 1427 | Duplicated patterns from above |
| `IRGenerator_Core.cpp` | 1252 | `boxedValues` tracking scattered |
| `IRGenerator_Classes.cpp` | 897 | Virtual inheritance handling |
| `Analyzer_Functions.cpp` | 538 | Missing contextual typing |
