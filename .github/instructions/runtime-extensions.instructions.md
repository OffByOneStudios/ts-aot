# Runtime Extension Development Guide

When extending the ts-aot runtime (adding new Node.js APIs, native functions, etc.), follow these critical patterns to avoid memory corruption and crashes.

## Memory Layout

TsObject-derived classes have this layout:
- **Offset 0**: C++ vtable pointer (8 bytes) - managed by compiler
- **Offset 8**: Explicit `magic` member (used for runtime type checking)

TsMap specifically has magic `0x4D415053` at **offset 16** (not 8) due to the TsObject base class layout.

## Virtual Inheritance (CRITICAL)

The runtime uses virtual inheritance for Node.js stream classes:
```cpp
class TsReadable : public virtual TsEventEmitter { ... };
class TsWritable : public virtual TsEventEmitter { ... };
class TsDuplex : public TsReadable, public TsWritable { ... };
```

### Rules

1. **NEVER use pointer arithmetic** to cast between base and derived classes
2. **ALWAYS use `dynamic_cast<TargetClass*>`** for pointer conversions
3. With virtual inheritance, base class subobjects are NOT at predictable offsets

### Wrong
```cpp
// BROKEN - assumes TsEventEmitter* is at known offset
TsEventEmitter* e = (TsEventEmitter*)((char*)obj + 8);
```

### Correct
```cpp
// Works with virtual inheritance
TsEventEmitter* e = dynamic_cast<TsEventEmitter*>(obj);
if (!e) return; // Cast failed
```

## Boxed Values in C API

Many `extern "C"` functions receive `void*` that may be:
1. A raw object pointer (e.g., `TsEventEmitter*`)
2. A boxed `TsValue*` containing the object in `ptr_val`

### Unboxing Pattern

Always check and unbox before use:
```cpp
void ts_my_function(void* param) {
    TsValue* val = (TsValue*)param;
    void* rawPtr = param;
    
    // Check if it's a boxed TsValue (type enum is 0-10)
    if ((uint8_t)val->type <= 10 && val->type == ValueType::OBJECT_PTR) {
        rawPtr = val->ptr_val;  // Unbox
    }
    
    // Now use dynamic_cast to get the correct type
    TsEventEmitter* e = dynamic_cast<TsEventEmitter*>((TsObject*)rawPtr);
    if (!e) return;
    
    // Use e...
}
```

## Creating Error Objects

`ts_error_create(message)` returns an **already-boxed** `TsValue*`. Do NOT double-box:

### Wrong
```cpp
TsValue* err = ts_error_create("message");
TsValue* boxed = ts_value_make_object(err);  // WRONG - double-boxed!
```

### Correct
```cpp
TsValue* err = ts_error_create("message");
// Use err directly - it's already a TsValue*
emitter->Emit("error", 1, (void**)&err);
```

## Memory Allocation

- **NEVER** use `new`/`malloc` directly for GC-managed objects
- **ALWAYS** use `ts_alloc(size)` which wraps Boehm GC
- For placement new: `void* mem = ts_alloc(sizeof(MyClass)); new (mem) MyClass();`

## String Handling

- **NEVER** use `std::string` for runtime values
- **ALWAYS** use `TsString::Create(...)` for string objects
- Convert to C string with `->ToUtf8()` when needed

## Adding New Properties to Runtime Classes

When adding a property like `statusCode` to `TsIncomingMessage`:

1. Add the field to the header: `int statusCode = 0;`
2. Set it in the appropriate callback (e.g., `on_headers_complete`)
3. Handle it in `ts_object_get_property` using `dynamic_cast`:

```cpp
TsValue* ts_object_get_property(void* obj, void* key) {
    // ... existing code ...
    
    TsObject* obj_ptr = (TsObject*)obj;
    TsIncomingMessage* msg = dynamic_cast<TsIncomingMessage*>(obj_ptr);
    if (msg) {
        if (strcmp(keyStr, "statusCode") == 0) {
            return ts_value_make_int(msg->statusCode);
        }
    }
    
    // ... rest of property lookup ...
}
```

## Testing Your Extensions

1. Build runtime: `cmake --build build --target tsruntime --config Release`
2. Build compiler: `cmake --build build --target ts-aot --config Release`
3. Create a test file in `examples/`
4. Compile: `build/src/compiler/Release/ts-aot.exe examples/test.ts -o examples/test.exe`
5. Run: `.\examples\test.exe`
