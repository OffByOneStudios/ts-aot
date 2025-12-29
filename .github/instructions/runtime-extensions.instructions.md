# Runtime Extension Development Guide

**⚠️ READ THIS BEFORE EDITING ANY RUNTIME CODE ⚠️**

When extending the ts-aot runtime (adding new Node.js APIs, native functions, etc.), follow these critical patterns to avoid memory corruption and crashes.

## Pre-Edit Checklist

Before making ANY changes, verify:

- [ ] Am I using `ts_alloc()` for all GC-managed allocations? (NOT `new`/`malloc`)
- [ ] Am I using `TsString::Create()` for strings? (NOT `std::string`)
- [ ] Am I using `AsXxx()` or `dynamic_cast` for casting? (NOT C-style casts)
- [ ] Am I using `ts_value_get_*()` helpers for unboxing? 
- [ ] Am I calling the correct config (Release or Debug) that matches the runtime?

## Quick Reference - Copy These Patterns

### Pattern 1: Safe Object Casting
```cpp
// ✅ CORRECT - use AsXxx() helpers
TsEventEmitter* emitter = ((TsObject*)ptr)->AsEventEmitter();
if (!emitter) return;  // Always null-check!

// ✅ ALSO CORRECT - use dynamic_cast  
TsSocket* socket = dynamic_cast<TsSocket*>((TsObject*)ptr);
if (!socket) return;

// ❌ WRONG - NEVER do this
TsEventEmitter* e = (TsEventEmitter*)ptr;  // BROKEN with virtual inheritance!
```

### Pattern 2: Unboxing void* Parameters
```cpp
// ✅ CORRECT - always unbox first
void* rawPtr = ts_value_get_object((TsValue*)param);
if (!rawPtr) rawPtr = param;  // Fallback if not boxed
TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)rawPtr);

// ❌ WRONG - assuming param is the raw pointer
TsSocket* s = (TsSocket*)param;  // May be a TsValue*, not raw!
```

### Pattern 3: Memory Allocation
```cpp
// ✅ CORRECT - use ts_alloc for GC-managed objects
void* mem = ts_alloc(sizeof(TsMyClass));
TsMyClass* obj = new (mem) TsMyClass();

// ❌ WRONG - raw new is NOT GC-managed
TsMyClass* obj = new TsMyClass();  // WILL LEAK or crash!
```

### Pattern 4: String Creation
```cpp
// ✅ CORRECT - use TsString::Create
TsString* str = TsString::Create("hello");

// ❌ WRONG - std::string is not compatible
std::string s = "hello";  // Cannot pass to runtime APIs!
```

### Pattern 5: Error Creation (Already Boxed)
```cpp
// ✅ CORRECT - ts_error_create returns boxed TsValue*
TsValue* err = ts_error_create("Something failed");
callback(err);  // Use directly

// ❌ WRONG - double-boxing
TsValue* err = ts_error_create("message");
TsValue* boxed = ts_value_make_object(err);  // DOUBLE-BOXED!
```

---

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
