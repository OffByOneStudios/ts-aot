# ts-aot Quick Reference Card

## File Locations

| Task | File(s) |
|------|---------|
| Register types for `foo` module | `src/compiler/analysis/Analyzer_StdLib_Foo.cpp` |
| Register builtin lowering | `src/compiler/hir/LoweringRegistry.cpp` |
| Implement `ts_foo_bar()` runtime | `src/runtime/src/TsFoo.cpp` + `src/runtime/include/TsFoo.h` |

## Type Patterns

```cpp
// Int
std::make_shared<Type>(TypeKind::Int)      // Analyzer
builder->getInt64Ty()                       // IRGen
int64_t                                     // Runtime

// String  
std::make_shared<Type>(TypeKind::String)   // Analyzer
builder->getPtrTy()                         // IRGen
TsString*                                   // Runtime

// Object/Class
std::make_shared<ClassType>("MyClass")     // Analyzer
builder->getPtrTy()                         // IRGen
TsMyClass*                                  // Runtime
```

## Runtime Safety Patterns

```cpp
// ALWAYS unbox void* params
void* raw = ts_value_get_object((TsValue*)param);
if (!raw) raw = param;

// ALWAYS use dynamic_cast for stream classes
TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)raw);

// ALWAYS use ts_alloc for GC objects
void* mem = ts_alloc(sizeof(TsMyClass));
TsMyClass* obj = new (mem) TsMyClass();

// ALWAYS use TsString::Create for strings
TsString* str = TsString::Create("hello");
```

## LoweringRegistry Patterns (HIR Pipeline)

```cpp
// In LoweringRegistry::registerBuiltins():

// Simple: ptr arg → i64 return
reg.registerLowering("ts_array_length",
    lowering("ts_array_length")
        .returnsI64()
        .ptrArg()
        .build());

// Boxed arg, boxed return
reg.registerLowering("ts_array_get",
    lowering("ts_array_get")
        .returnsBoxed()
        .ptrArg()       // array
        .i64Arg()       // index
        .build());

// Multiple args with boxing
reg.registerLowering("ts_array_push",
    lowering("ts_array_push")
        .returnsI64()
        .ptrArg()       // array
        .boxedArg()     // value (auto-box)
        .build());

// Void return
reg.registerLowering("ts_object_set_property",
    lowering("ts_object_set_property")
        .returnsVoid()
        .ptrArg()       // object
        .ptrArg()       // key
        .boxedArg()     // value
        .build());
```

## Build & Test Commands

```powershell
# Build compiler
cmake --build build --target ts-aot --config Release

# Build runtime
cmake --build build --target tsruntime --config Release

# Compile and run test (use tmp/ for ad-hoc testing, NOT examples/)
build/src/compiler/Release/ts-aot.exe tmp/test.ts -o tmp/test.exe
./tmp/test.exe

# Debug type inference
build/src/compiler/Release/ts-aot.exe tmp/test.ts --dump-types

# Debug generated IR
build/src/compiler/Release/ts-aot.exe tmp/test.ts --dump-ir

# Debug HIR output
build/src/compiler/Release/ts-aot.exe tmp/test.ts --dump-hir
```
