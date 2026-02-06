# How to Add a New Node.js API

This guide explains the 3-layer architecture for adding any new Node.js API to ts-aot.

## Architecture Overview

```
TypeScript Code → Analyzer → ASTToHIR → HIR Passes → HIRToLLVM → Runtime
     (types)        (HIR)                    (LLVM IR)    (C++ impl)
```

Each API needs changes in **3 layers**:

| Layer | File Pattern | Purpose |
|-------|--------------|---------|
| **Analysis** | `src/compiler/analysis/Analyzer_StdLib_<Module>.cpp` | Register types and method signatures |
| **HIR Lowering** | `src/compiler/hir/LoweringRegistry.cpp` | Register HIR-to-LLVM call specifications |
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

## Step 2: Register in LoweringRegistry

**File:** `src/compiler/hir/LoweringRegistry.cpp`

**Purpose:** Builtins are lowered via a declarative registry that maps HIR call instructions to LLVM IR.

### Template: Register a Builtin

Add registrations in `LoweringRegistry::registerBuiltins()`:

```cpp
// In LoweringRegistry.cpp, inside registerBuiltins():

// Simple function: takes ptr arg, returns i64
reg.registerLowering("ts_my_module_do_something",
    lowering("ts_my_module_do_something")
        .returnsI64()           // Return type: i64
        .ptrArg()               // Arg 0: ptr (passed as-is)
        .build());

// Function with boxed arg: boxes the input before calling
reg.registerLowering("ts_my_module_process",
    lowering("ts_my_module_process")
        .returnsBoxed()         // Returns TsValue* (already boxed)
        .boxedArg()             // Arg 0: box to TsValue* before call
        .build());

// Void function
reg.registerLowering("ts_my_module_log",
    lowering("ts_my_module_log")
        .returnsVoid()          // No return value
        .ptrArg()               // Arg 0: ptr
        .build());

// Function with multiple args
reg.registerLowering("ts_my_module_combine",
    lowering("ts_my_module_combine")
        .returnsPtr()           // Returns raw ptr
        .ptrArg()               // Arg 0: ptr
        .i64Arg()               // Arg 1: i64
        .boxedArg()             // Arg 2: boxed value
        .build());
```

### Builder API Reference

**Return Types:**
| Method | LLVM Type | Use Case |
|--------|-----------|----------|
| `.returnsVoid()` | `void` | No return value |
| `.returnsPtr()` | `ptr` | Raw pointer return |
| `.returnsI64()` | `i64` | Integer return |
| `.returnsF64()` | `double` | Float return |
| `.returnsBool()` | `i1` | Boolean return |
| `.returnsBoxed()` | `ptr` | Returns TsValue* (marks as boxed) |

**Argument Types:**
| Method | LLVM Type | Conversion |
|--------|-----------|------------|
| `.ptrArg()` | `ptr` | Pass through |
| `.ptrArg(ArgConversion::Box)` | `ptr` | Box before call |
| `.boxedArg()` | `ptr` | Shorthand for `.ptrArg(ArgConversion::Box)` |
| `.i64Arg()` | `i64` | Pass through |
| `.f64Arg()` | `double` | Pass through |
| `.boolArg()` | `i1` | Pass through |

**Argument Conversions (ArgConversion enum):**
| Conversion | Effect |
|------------|--------|
| `None` | Pass value as-is |
| `Box` | Box to TsValue* |
| `Unbox` | Unbox from TsValue* |
| `ToI64` | Convert f64 → i64 |
| `ToF64` | Convert i64 → f64 |
| `ToI32` | Truncate to i32 |
| `ToBool` | Convert to i1 |
| `PtrToInt` | Cast ptr → i64 |
| `IntToPtr` | Cast i64 → ptr |

### When NOT to Use Registry

Keep inline in HIRToLLVM for:
- **Type-specific dispatch**: Console functions dispatch to `ts_console_log_int`, `ts_console_log_double`, etc. based on argument type
- **User-defined functions**: Looked up from function table
- **Complex multi-step lowerings**: Multiple LLVM instructions or conditional logic

---

## Step 3: Implement in Runtime (unchanged)

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
- [ ] HIR: Builtin registered in `LoweringRegistry::registerBuiltins()` in `src/compiler/hir/LoweringRegistry.cpp`
- [ ] HIR: Correct return type (`.returnsVoid()`, `.returnsI64()`, `.returnsBoxed()`, etc.)
- [ ] HIR: Correct argument types and conversions (`.ptrArg()`, `.boxedArg()`, `.i64Arg()`, etc.)
- [ ] Runtime: `extern "C"` functions implemented
- [ ] Runtime: Source file added to `CMakeLists.txt`
- [ ] Build: `cmake --build build --config Release` succeeds
- [ ] Test: Create `tmp/test_<feature>.ts` and verify output

---

## Debugging Tips

1. **Type errors:** Run with `--dump-types` to see inferred types
2. **Codegen issues:** Run with `--dump-ir` to see generated LLVM IR
3. **Runtime crashes:** Check for:
   - Missing unboxing (`ts_value_get_object`)
   - Wrong casts (use `dynamic_cast` for stream classes)
   - Memory allocation (use `ts_alloc`, not `new`)
