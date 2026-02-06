# Code Snippets for Runtime Development

Copy these patterns directly. Do NOT modify the safety checks.

---

## 1. Extern "C" Function with Unboxing

```cpp
extern "C" void ts_my_function(void* param) {
    // ALWAYS unbox first - param may be TsValue* or raw pointer
    void* rawPtr = ts_value_get_object((TsValue*)param);
    if (!rawPtr) rawPtr = param;
    
    // ALWAYS use dynamic_cast - NEVER C-style cast
    TsMyClass* obj = dynamic_cast<TsMyClass*>((TsObject*)rawPtr);
    if (!obj) return;  // Cast failed - wrong type
    
    // Now safe to use obj
}
```

---

## 2. Create a New GC-Managed Object

```cpp
// In header: static TsMyClass* Create();

TsMyClass* TsMyClass::Create() {
    // ALWAYS use ts_alloc - NEVER new/malloc directly
    void* mem = ts_alloc(sizeof(TsMyClass));
    return new (mem) TsMyClass();  // Placement new
}
```

---

## 3. Create and Return a String

```cpp
extern "C" void* ts_get_some_string() {
    // ALWAYS use TsString::Create - NEVER std::string
    TsString* result = TsString::Create("hello world");
    return ts_value_make_string(result);  // Box for return
}
```

---

## 4. Get Property from Object

```cpp
// In ts_object_get_property() in TsObject.cpp:

TsMyClass* myObj = dynamic_cast<TsMyClass*>(obj_ptr);
if (myObj) {
    if (strcmp(keyStr, "myProperty") == 0) {
        return ts_value_make_int(myObj->myProperty);
    }
    if (strcmp(keyStr, "myStringProp") == 0) {
        return ts_value_make_string(myObj->myStringProp);
    }
}
```

---

## 5. Emit an Event

```cpp
void TsMyClass::EmitData(void* data) {
    // Get as TsEventEmitter - use AsEventEmitter(), not cast
    TsEventEmitter* emitter = this->AsEventEmitter();
    if (!emitter) return;
    
    void* args[] = { data };
    emitter->Emit("data", 1, args);
}
```

---

## 6. Emit an Error (Already Boxed)

```cpp
void TsMyClass::EmitError(const char* message) {
    TsEventEmitter* emitter = this->AsEventEmitter();
    if (!emitter) return;
    
    // ts_error_create returns ALREADY-BOXED TsValue*
    // Do NOT double-box with ts_value_make_object!
    TsValue* err = ts_error_create(message);
    void* args[] = { err };
    emitter->Emit("error", 1, args);
}
```

---

## 7. Add Callback Parameter Typing (Analyzer)

In `Analyzer_Expressions_Calls.cpp`, in `getExpectedCallbackType()`:

```cpp
// For myModule.myMethod((param1, param2) => { ... })
if (objName == "myModule" && methodName == "myMethod" && argIndex == 0) {
    auto param1Type = std::make_shared<ClassType>("MyParam1Class");
    auto param2Type = std::make_shared<ClassType>("MyParam2Class");
    auto voidType = std::make_shared<Type>(TypeKind::Void);
    return std::make_shared<FunctionType>(
        std::vector<std::shared_ptr<Type>>{param1Type, param2Type},
        voidType
    );
}
```

---

## 8. Cast Between Stream Types

```cpp
// From void* to TsSocket
void* rawPtr = ts_value_get_object((TsValue*)param);
if (!rawPtr) rawPtr = param;
TsSocket* socket = dynamic_cast<TsSocket*>((TsObject*)rawPtr);

// From TsSocket to TsEventEmitter
TsEventEmitter* emitter = socket->AsEventEmitter();

// From TsSocket to TsWritable  
TsWritable* writable = socket->AsWritable();

// From TsSocket to TsReadable
TsReadable* readable = socket->AsReadable();
```

---

## 10. libuv Callback with Context

```cpp
struct MyContext {
    TsMyClass* self;
    void* callback;
};

static void on_my_event(uv_handle_t* handle) {
    MyContext* ctx = (MyContext*)handle->data;
    if (!ctx || !ctx->self) return;
    
    // Emit event or call callback
    ctx->self->OnEvent();
}

void TsMyClass::StartAsync() {
    MyContext* ctx = (MyContext*)ts_alloc(sizeof(MyContext));
    ctx->self = this;
    ctx->callback = nullptr;

    handle->data = ctx;
    // Start libuv operation...
}
```

---

## 11. Register Builtin in LoweringRegistry (HIR Pipeline)

In `LoweringRegistry.cpp`, inside `registerBuiltins()`:

```cpp
// Basic pattern: runtime func name, return type, arg types
reg.registerLowering("ts_my_function",
    lowering("ts_my_function")
        .returnsI64()       // Return i64
        .ptrArg()           // Arg 0: ptr (passed as-is)
        .build());

// With boxed arg (auto-boxes before call)
reg.registerLowering("ts_my_boxed_func",
    lowering("ts_my_boxed_func")
        .returnsBoxed()     // Returns TsValue* (marked boxed)
        .ptrArg()           // Arg 0: object ptr
        .boxedArg()         // Arg 1: value (auto-box to TsValue*)
        .build());

// Void return
reg.registerLowering("ts_my_void_func",
    lowering("ts_my_void_func")
        .returnsVoid()
        .ptrArg()
        .ptrArg()
        .build());

// With type conversion
reg.registerLowering("ts_my_float_func",
    lowering("ts_my_float_func")
        .returnsF64()
        .f64Arg()           // Direct f64 arg
        .i64Arg(ArgConversion::ToF64)  // Convert i64 to f64
        .build());
```

**Builder Methods:**
- `.returnsVoid()`, `.returnsI64()`, `.returnsF64()`, `.returnsPtr()`, `.returnsBoxed()`, `.returnsBool()`
- `.ptrArg()`, `.i64Arg()`, `.f64Arg()`, `.boolArg()`, `.boxedArg()`
- `.variadic()` for vararg functions
