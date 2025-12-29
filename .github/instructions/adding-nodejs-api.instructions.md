# How to Add a New Node.js API

This guide explains the 3-layer architecture for adding any new Node.js API to ts-aot.

## Architecture Overview

```
TypeScript Code → Analyzer → IRGenerator → Runtime
     (types)       (LLVM IR)     (C++ impl)
```

Each API needs changes in **3 layers**:

| Layer | File Pattern | Purpose |
|-------|--------------|---------|
| **Analysis** | `src/compiler/analysis/Analyzer_StdLib_<Module>.cpp` | Register types and method signatures |
| **Codegen** | `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_<Module>.cpp` | Generate LLVM IR that calls runtime |
| **Runtime** | `src/runtime/src/*.cpp` + `src/runtime/include/*.h` | Implement the actual functionality |

---

## Step 1: Register Types in Analyzer

**File:** `src/compiler/analysis/Analyzer_StdLib_<Module>.cpp`

**Purpose:** Tell the type system what methods/properties exist and their signatures.

### Template: Register a Module Object

```cpp
#include "Analyzer.h"

namespace ts {

void Analyzer::registerMyModule() {
    auto myModule = std::make_shared<ObjectType>();

    // Function: myModule.doSomething(arg: string): number
    auto doSomethingType = std::make_shared<FunctionType>();
    doSomethingType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    doSomethingType->returnType = std::make_shared<Type>(TypeKind::Int);
    myModule->fields["doSomething"] = doSomethingType;

    // Property: myModule.version (string)
    myModule->fields["version"] = std::make_shared<Type>(TypeKind::String);

    symbols.define("myModule", myModule);
}

} // namespace ts
```

### Template: Register a Class

```cpp
void Analyzer::registerMyClass() {
    auto myClass = std::make_shared<ClassType>("MyClass");
    
    // Instance property
    myClass->fields["length"] = std::make_shared<Type>(TypeKind::Int);
    
    // Instance method
    auto toStringMethod = std::make_shared<FunctionType>();
    toStringMethod->returnType = std::make_shared<Type>(TypeKind::String);
    myClass->methods["toString"] = toStringMethod;
    
    // Register the class type
    symbols.defineType("MyClass", myClass);
    
    // Static methods go on a separate ObjectType
    auto myClassStatic = std::make_shared<ObjectType>();
    
    auto createMethod = std::make_shared<FunctionType>();
    createMethod->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    createMethod->returnType = myClass;
    myClassStatic->fields["create"] = createMethod;
    
    symbols.define("MyClass", myClassStatic);
}
```

### Don't Forget!

Call your register function from `Analyzer::registerBuiltins()` in `Analyzer_StdLib.cpp`:

```cpp
void Analyzer::registerBuiltins() {
    // ... existing registrations ...
    registerMyModule();  // Add this line
}
```

---

## Step 2: Generate IR in Codegen

**File:** `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin_<Module>.cpp`

**Purpose:** When the compiler sees `myModule.doSomething(x)`, generate a call to `ts_my_module_do_something(x)`.

### Template: Builtin Call Handler

```cpp
#include "IRGenerator.h"
#include "spdlog/spdlog.h"

namespace ts {

bool IRGenerator::tryGenerateMyModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto id = std::dynamic_pointer_cast<ast::Identifier>(prop->expression);
    if (!id || id->name != "myModule") return false;
    
    const std::string& methodName = prop->name;
    
    if (methodName == "doSomething") {
        // Get argument
        llvm::Value* arg = visitExpression(node->arguments[0].get());
        
        // Declare runtime function
        auto ft = llvm::FunctionType::get(
            builder->getInt64Ty(),    // return type
            { builder->getPtrTy() },  // arg types (string is ptr)
            false
        );
        auto fn = module->getOrInsertFunction("ts_my_module_do_something", ft);
        
        // Create call
        return builder->CreateCall(ft, fn.getCallee(), { arg });
    }
    
    return nullptr;  // Not handled
}

} // namespace ts
```

### Don't Forget!

1. **Declare in header:** Add `bool tryGenerateMyModuleCall(...)` to `IRGenerator.h`

2. **Call from dispatcher:** In `IRGenerator_Expressions_Calls_Builtin.cpp`:
```cpp
bool IRGenerator::tryGenerateBuiltinCall(...) {
    // ... existing handlers ...
    if (auto result = tryGenerateMyModuleCall(node, prop)) return result;
}
```

---

## Step 3: Implement in Runtime

**Files:** 
- `src/runtime/include/TsMyModule.h` (header)
- `src/runtime/src/TsMyModule.cpp` (implementation)

### Template: Runtime Function

```cpp
// In TsMyModule.cpp

#include "TsMyModule.h"
#include "TsString.h"
#include "GC.h"

extern "C" {

int64_t ts_my_module_do_something(void* strArg) {
    // Unbox if needed
    TsString* str = (TsString*)ts_value_get_object((TsValue*)strArg);
    if (!str) str = (TsString*)strArg;
    
    // Implement logic
    const char* cstr = str->ToUtf8();
    int64_t result = strlen(cstr);
    
    return result;
}

} // extern "C"
```

### Don't Forget!

Add source file to `src/runtime/CMakeLists.txt`:
```cmake
add_library(tsruntime STATIC
    # ... existing files ...
    src/TsMyModule.cpp
)
```

---

## Common Type Mappings

| TypeScript Type | Analyzer Type | LLVM Type | Runtime C Type |
|-----------------|---------------|-----------|----------------|
| `number` (int) | `TypeKind::Int` | `getInt64Ty()` | `int64_t` |
| `number` (float) | `TypeKind::Double` | `getDoubleTy()` | `double` |
| `string` | `TypeKind::String` | `getPtrTy()` | `TsString*` |
| `boolean` | `TypeKind::Boolean` | `getInt1Ty()` | `bool` |
| `void` | `TypeKind::Void` | `getVoidTy()` | `void` |
| `any` / object | `TypeKind::Any` | `getPtrTy()` | `void*` |
| `Buffer` | `ClassType("Buffer")` | `getPtrTy()` | `TsBuffer*` |
| `Promise<T>` | `ClassType("Promise")` | `getPtrTy()` | `TsPromise*` |

---

## Checklist Before Committing

- [ ] Analyzer: Types registered in `Analyzer_StdLib_<Module>.cpp`
- [ ] Analyzer: `register<Module>()` called from `registerBuiltins()`
- [ ] Codegen: Handler in `IRGenerator_Expressions_Calls_Builtin_<Module>.cpp`
- [ ] Codegen: Handler declared in `IRGenerator.h`
- [ ] Codegen: Handler called from `tryGenerateBuiltinCall()`
- [ ] Runtime: `extern "C"` functions implemented
- [ ] Runtime: Source file added to `CMakeLists.txt`
- [ ] Build: `cmake --build build --target ts-aot --config Release` succeeds
- [ ] Test: Create `examples/test_<feature>.ts` and verify output

---

## Debugging Tips

1. **Type errors:** Run with `--dump-types` to see inferred types
2. **Codegen issues:** Run with `--dump-ir` to see generated LLVM IR
3. **Runtime crashes:** Check for:
   - Missing unboxing (`ts_value_get_object`)
   - Wrong casts (use `dynamic_cast` for stream classes)
   - Memory allocation (use `ts_alloc`, not `new`)
