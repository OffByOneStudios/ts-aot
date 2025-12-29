# ts-aot Development Guidelines

This document provides essential guidelines for developing and contributing to the TypeScript AOT compiler.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Type System & Contextual Typing](#type-system--contextual-typing)
3. [Boxing & Unboxing Convention](#boxing--unboxing-convention)
4. [Safe Casting with Virtual Inheritance](#safe-casting-with-virtual-inheritance)
5. [Debugging & Diagnostics](#debugging--diagnostics)
6. [Common Pitfalls](#common-pitfalls)

---

## Architecture Overview

The compiler has two main components:

```
src/compiler/     - The host compiler (runs on dev machine)
src/runtime/      - The target runtime library (linked into generated code)
```

### Compilation Flow

1. **Parse**: TypeScript → AST via `dump_ast.js`
2. **Analyze**: Type inference (`Analyzer*.cpp`)
3. **Codegen**: AST → LLVM IR (`IRGenerator*.cpp`)
4. **Link**: LLVM IR + `tsruntime.lib` → Executable

---

## Type System & Contextual Typing

### Problem: Untyped Callback Parameters

TypeScript infers callback parameter types from context:

```typescript
http.createServer((req, res) => {
    res.writeHead(200);  // TypeScript knows res is ServerResponse
});
```

Without contextual typing, `req` and `res` would be `Any`, breaking codegen.

### Solution: Contextual Type Stack

The analyzer maintains a stack of expected types:

```cpp
// In Analyzer.h
std::vector<std::shared_ptr<Type>> contextualTypeStack;

void pushContextualType(std::shared_ptr<Type> type);
void popContextualType();
std::shared_ptr<Type> getContextualType() const;
```

When visiting a call expression with callback arguments:

```cpp
// In visitCallExpression
if (isArrowOrFn) {
    auto expectedCb = getExpectedCallbackType(objName, methodName, argIndex);
    if (expectedCb) {
        pushContextualType(expectedCb);
        visit(arg.get());
        popContextualType();
    }
}
```

Arrow functions then use this context:

```cpp
// In visitArrowFunction
auto contextualType = getContextualType();
if (contextualType && contextualType->kind == TypeKind::Function) {
    auto contextualFuncType = std::static_pointer_cast<FunctionType>(contextualType);
    // Use contextual param types for untyped parameters
    if (paramType->kind == TypeKind::Any && i < contextualFuncType->paramTypes.size()) {
        paramType = contextualFuncType->paramTypes[i];
    }
}
```

### Adding New Contextual Types

To add contextual typing for a new API in `Analyzer_Expressions_Calls.cpp`:

```cpp
static std::shared_ptr<FunctionType> getExpectedCallbackType(
    const std::string& objName, 
    const std::string& methodName, 
    size_t argIndex
) {
    // Add your API here
    if (objName == "myModule" && methodName == "myMethod" && argIndex == 0) {
        auto cbType = std::make_shared<FunctionType>();
        cbType->paramTypes.push_back(std::make_shared<ClassType>("MyParamType"));
        cbType->returnType = std::make_shared<Type>(TypeKind::Void);
        return cbType;
    }
    return nullptr;
}
```

---

## Boxing & Unboxing Convention

### The Problem

Runtime functions receive `void*` that may be:
1. A raw object pointer (e.g., `TsEventEmitter*`)
2. A boxed `TsValue*` (with `type` field and `ptr_val`)

### The Solution: Consistent Unboxing

**Always unbox at the start of a C API function:**

```cpp
extern "C" void ts_my_function(void* param) {
    // Standard unboxing pattern
    void* rawPtr = ts_value_get_object((TsValue*)param);
    if (!rawPtr) rawPtr = param;  // Fallback if not boxed
    
    TsMyClass* obj = dynamic_cast<TsMyClass*>((TsObject*)rawPtr);
    if (!obj) return;  // Handle error
    
    // Now use obj safely
}
```

### Unboxing Helpers

Located in `TsObject.h` / `TsObject.cpp`:

```cpp
// In TsObject.h
extern "C" {
    void* ts_value_get_object(TsValue* val);  // Extract object pointer
    int64_t ts_value_get_int(TsValue* val);   // Extract integer
    double ts_value_get_double(TsValue* val); // Extract double
    bool ts_value_get_bool(TsValue* val);     // Extract boolean
    void* ts_value_get_string(TsValue* val);  // Extract string
}
```

### Boxing in Codegen

Track boxed values in `IRGenerator`:

```cpp
// After creating a boxed value
llvm::Value* boxed = boxValue(lastValue, type);
boxedValues.insert(boxed);  // Track it!
```

Check before unboxing:

```cpp
if (boxedValues.count(value)) {
    // Value is boxed, emit unbox call
}
```

---

## Safe Casting with Virtual Inheritance

### The Problem

The stream class hierarchy uses virtual inheritance:

```cpp
class TsReadable : public virtual TsEventEmitter { };
class TsWritable : public virtual TsEventEmitter { };
class TsDuplex : public TsReadable, public TsWritable { };
```

With virtual inheritance, pointer layouts are **unpredictable**. C-style casts and `static_cast` will crash or corrupt memory.

### The Solution: AsXxx() Helpers

Every class in the stream hierarchy provides safe casting methods:

```cpp
// In TsObject.h
class TsObject {
public:
    virtual TsEventEmitter* AsEventEmitter() { return nullptr; }
    virtual TsReadable* AsReadable() { return nullptr; }
    virtual TsWritable* AsWritable() { return nullptr; }
    virtual TsDuplex* AsDuplex() { return nullptr; }
    virtual TsSocket* AsSocket() { return nullptr; }
    virtual TsServer* AsServer() { return nullptr; }
};

// In TsSocket.h
class TsSocket : public TsDuplex {
    virtual TsSocket* AsSocket() override { return this; }
};
```

### Usage

```cpp
// WRONG - will crash with virtual inheritance
TsWritable* w = (TsWritable*)somePtr;

// CORRECT - use dynamic_cast
TsWritable* w = dynamic_cast<TsWritable*>((TsObject*)somePtr);

// BETTER - use AsXxx() helper
TsWritable* w = ((TsObject*)somePtr)->AsWritable();
```

---

## Debugging & Diagnostics

### --dump-types Flag

Dump inferred types for all expressions:

```bash
ts-aot --dump-types myfile.ts
```

### Debug Logging

Use spdlog for structured logging:

```cpp
#include <spdlog/spdlog.h>

SPDLOG_INFO("Contextual typing: parameter {} gets type {}", i, paramType->toString());
SPDLOG_DEBUG("tryGenerateHTTPCall: prop->name={}", prop->name);
SPDLOG_ERROR("Failed to resolve symbol: {}", symbolName);
```

Log levels:
- `SPDLOG_TRACE`: Very verbose, for step-by-step tracing
- `SPDLOG_DEBUG`: Development debugging
- `SPDLOG_INFO`: Notable events
- `SPDLOG_ERROR`: Errors

### Common Debug Patterns

**Type debugging:**
```cpp
if (node->inferredType) {
    SPDLOG_DEBUG("Expression type: kind={}, name={}", 
        (int)node->inferredType->kind,
        node->inferredType->toString());
}
```

**Boxing debugging:**
```cpp
SPDLOG_DEBUG("Value {} boxed={}", (void*)value, boxedValues.count(value));
```

**Runtime magic check debugging:**
```cpp
uint32_t magic = *(uint32_t*)((char*)ptr + 8);
fprintf(stderr, "Object magic: 0x%08X\n", magic);
```

---

## Common Pitfalls

### 1. Forgetting to Track Boxed Values

```cpp
// WRONG
lastValue = createCall(ft, fn.getCallee(), args);
// Missing: boxedValues.insert(lastValue);
```

### 2. Using C-style Casts with Virtual Inheritance

```cpp
// WRONG
TsEventEmitter* e = (TsEventEmitter*)somePtr;

// CORRECT
TsEventEmitter* e = dynamic_cast<TsEventEmitter*>((TsObject*)somePtr);
// OR
TsEventEmitter* e = ((TsObject*)somePtr)->AsEventEmitter();
```

### 3. LLVM Type Mismatches

```cpp
// WRONG - args don't match FunctionType
llvm::FunctionType* ft = llvm::FunctionType::get(
    builder->getPtrTy(), 
    { builder->getInt64Ty() },  // Expected: i64
    false
);
createCall(ft, fn, { ptrValue });  // Passed: ptr - CRASH!

// CORRECT
createCall(ft, fn, { castValue(ptrValue, builder->getInt64Ty()) });
```

### 4. Missing Return Type in Class Fields

When adding a field to a class type in the analyzer, ensure the runtime has a corresponding getter:

```cpp
// In Analyzer_StdLib_*.cpp
incomingMessageClass->fields["statusCode"] = std::make_shared<Type>(TypeKind::Int);

// Must have in runtime (TsHttp.cpp):
void* ts_incoming_message_statusCode(void* ctx, void* msg) {
    return ts_value_make_int(((TsIncomingMessage*)msg)->statusCode);
}
```

### 5. Double-Boxing Errors

```cpp
// ts_error_create already returns a boxed TsValue*
// WRONG
TsValue* err = ts_error_create(msg);
lastValue = boxValue(err, errorType);  // Double-boxed!

// CORRECT
lastValue = ts_error_create(msg);  // Already boxed
```

---

## Quick Reference: Adding a New Node.js API

1. **Analyzer**: Define types in `Analyzer_StdLib_*.cpp`
   - Add class fields, methods, and return types
   
2. **Contextual Typing**: Add callback signatures in `getExpectedCallbackType()`
   
3. **Codegen**: Handle calls in `IRGenerator_Expressions_Calls_Builtin_*.cpp`
   - Use correct boxing/unboxing
   - Track boxed values
   
4. **Runtime**: Implement C API in `src/runtime/src/`
   - Use standard unboxing pattern
   - Use `dynamic_cast` for stream classes
   
5. **Test**: Create example in `examples/` and verify end-to-end
