---
paths: src/runtime/**
priority: critical
---

# Runtime Safety Rules

These rules are **MANDATORY** for all code in `src/runtime/`. Violations will cause memory corruption, crashes, or undefined behavior.

## ⛔ STOP AND CHECK - Before ANY Runtime Edit ⛔

Before editing ANY file in `src/runtime/`, verify your code uses these patterns:

## Memory Allocation

**❌ WRONG:**
```cpp
TsMyClass* obj = new TsMyClass();           // Direct new - not GC'd
TsMyClass* obj = (TsMyClass*)malloc(size);  // Direct malloc - not GC'd
```

**✅ CORRECT:**
```cpp
void* mem = ts_alloc(sizeof(TsMyClass));
TsMyClass* obj = new (mem) TsMyClass();  // Placement new
```

**Why:** The runtime uses Boehm GC. Objects allocated with `new`/`malloc` won't be tracked by the GC and will leak memory.

## String Creation

**❌ WRONG:**
```cpp
std::string str = "hello";
std::string* str = new std::string("hello");
```

**✅ CORRECT:**
```cpp
TsString* str = TsString::Create("hello");
```

**Why:** Runtime strings must be ICU-based for Unicode correctness and GC tracking.

## Casting with Virtual Inheritance

**❌ WRONG:**
```cpp
TsEventEmitter* e = (TsEventEmitter*)ptr;              // C-style cast
TsWritable* w = static_cast<TsWritable*>(ptr);         // static_cast
TsSocket* s = reinterpret_cast<TsSocket*>(ptr);        // reinterpret_cast
```

**✅ CORRECT:**
```cpp
// Option 1: Use AsXxx() helpers (preferred)
TsEventEmitter* e = ((TsObject*)ptr)->AsEventEmitter();
TsWritable* w = ((TsObject*)ptr)->AsWritable();

// Option 2: Use dynamic_cast
TsSocket* s = dynamic_cast<TsSocket*>((TsObject*)ptr);
```

**Why:** Stream classes use virtual inheritance (`TsReadable : public virtual TsEventEmitter`). Pointer layouts are unpredictable. C-style casts and `static_cast` will corrupt pointers.

## Boxing & Unboxing

### Unboxing Parameters

**❌ WRONG:**
```cpp
extern "C" void ts_my_function(void* param) {
    TsMyClass* obj = (TsMyClass*)param;  // Assume raw pointer
    // ...
}
```

**✅ CORRECT:**
```cpp
extern "C" void ts_my_function(void* param) {
    // ALWAYS unbox first - param may be TsValue* or raw pointer
    void* rawPtr = ts_value_get_object((TsValue*)param);
    if (!rawPtr) rawPtr = param;  // Fallback if not boxed

    // ALWAYS use dynamic_cast for virtual inheritance classes
    TsMyClass* obj = dynamic_cast<TsMyClass*>((TsObject*)rawPtr);
    if (!obj) return;  // Handle error

    // Now safe to use obj
}
```

**Why:** C API functions receive `void*` that may be boxed `TsValue*` or raw pointers. Must unbox consistently.

### Available Unboxing Helpers

```cpp
void* ts_value_get_object(TsValue* val);    // Extract object pointer
int64_t ts_value_get_int(TsValue* val);     // Extract integer
double ts_value_get_double(TsValue* val);   // Extract double
bool ts_value_get_bool(TsValue* val);       // Extract boolean
void* ts_value_get_string(TsValue* val);    // Extract string
```

### Error Creation (Already Boxed)

**❌ WRONG:**
```cpp
TsValue* err = ts_error_create(message);
lastValue = ts_value_make_object(err);  // Double-boxed!
```

**✅ CORRECT:**
```cpp
TsValue* err = ts_error_create(message);  // Already boxed
emitter->Emit("error", 1, (void**)&err);
```

**Why:** `ts_error_create` returns an already-boxed `TsValue*`. Don't box it again.

## Event Emission

**❌ WRONG:**
```cpp
void TsMyClass::EmitData(void* data) {
    TsEventEmitter* emitter = (TsEventEmitter*)this;  // C-style cast
    emitter->Emit("data", 1, &data);
}
```

**✅ CORRECT:**
```cpp
void TsMyClass::EmitData(void* data) {
    TsEventEmitter* emitter = this->AsEventEmitter();
    if (!emitter) return;

    void* args[] = { data };
    emitter->Emit("data", 1, args);
}
```

## Console Output

**❌ WRONG:**
```cpp
printf("Value: %d\n", value);
std::cout << "Value: " << value << std::endl;
fprintf(stdout, "Value: %d\n", value);
```

**✅ CORRECT:**
```cpp
ts_console_log("Value: %d", value);
// Or for debugging:
SPDLOG_INFO("Value: {}", value);
```

**Why:** Runtime must route through proper I/O system (libuv).

## libuv Callbacks with Context

**❌ WRONG:**
```cpp
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    TsSocket* socket = (TsSocket*)stream;  // Wrong! stream != socket
    // ...
}
```

**✅ CORRECT:**
```cpp
struct ReadContext {
    TsSocket* socket;
    void* callback;
};

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    ReadContext* ctx = (ReadContext*)stream->data;
    if (!ctx || !ctx->socket) return;

    ctx->socket->OnRead(nread, buf);
}

void TsSocket::StartReading() {
    ReadContext* ctx = (ReadContext*)ts_alloc(sizeof(ReadContext));
    ctx->socket = this;
    ctx->callback = nullptr;

    tcpHandle->data = ctx;
    uv_read_start((uv_stream_t*)tcpHandle, on_alloc, on_read);
}
```

**Why:** libuv handles are not the same as TsSocket objects. Must use context struct.

## Safe Casting Helpers

Every stream class provides these:

```cpp
class TsObject {
public:
    virtual TsEventEmitter* AsEventEmitter() { return nullptr; }
    virtual TsReadable* AsReadable() { return nullptr; }
    virtual TsWritable* AsWritable() { return nullptr; }
    virtual TsDuplex* AsDuplex() { return nullptr; }
    virtual TsSocket* AsSocket() { return nullptr; }
    virtual TsServer* AsServer() { return nullptr; }
};
```

Override these in derived classes:

```cpp
class TsSocket : public TsDuplex {
public:
    TsSocket* AsSocket() override { return this; }
};
```

## Property Access Pattern

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

## Common Pitfalls

### 1. Forgetting to Check Cast Results

```cpp
// BAD
TsSocket* s = dynamic_cast<TsSocket*>(obj);
s->DoSomething();  // CRASH if cast failed!

// GOOD
TsSocket* s = dynamic_cast<TsSocket*>(obj);
if (!s) return;  // Or handle error
s->DoSomething();
```

### 2. Using std::string for Runtime Values

```cpp
// BAD
std::string path = "/tmp/file.txt";
openFile(path);

// GOOD
TsString* path = TsString::Create("/tmp/file.txt");
openFile(path);
```

### 3. Mixing Heap Types

```cpp
// BAD
char* buffer = new char[1024];        // Not GC'd
TsBuffer* tsBuffer = (TsBuffer*)buffer;  // Type mismatch!

// GOOD
void* mem = ts_alloc(sizeof(TsBuffer));
TsBuffer* tsBuffer = new (mem) TsBuffer();
tsBuffer->data = (uint8_t*)ts_alloc(1024);
```

## Debugging Tips

### Check Object Magic

```cpp
uint32_t magic = *(uint32_t*)((char*)ptr + 8);
fprintf(stderr, "Object magic: 0x%08X\n", magic);
```

### Verify GC Allocation

```cpp
SPDLOG_DEBUG("Allocated object at {} with size {}", ptr, sizeof(TsMyClass));
```

### Trace Boxing

```cpp
SPDLOG_DEBUG("Value {} boxed={}", (void*)value, boxedValues.count(value));
```

## Quick Reference Table

| Task | Runtime Function |
|------|------------------|
| Allocate GC object | `ts_alloc(size)` |
| Create string | `TsString::Create(str)` |
| Unbox object | `ts_value_get_object(val)` |
| Unbox int | `ts_value_get_int(val)` |
| Unbox double | `ts_value_get_double(val)` |
| Unbox bool | `ts_value_get_bool(val)` |
| Box object | `ts_value_make_object(ptr)` |
| Box int | `ts_value_make_int(i)` |
| Box double | `ts_value_make_double(d)` |
| Box bool | `ts_value_make_bool(b)` |
| Box string | `ts_value_make_string(str)` |
| Create error | `ts_error_create(msg)` (already boxed!) |
| Cast to EventEmitter | `obj->AsEventEmitter()` |
| Cast to Readable | `obj->AsReadable()` |
| Cast to Writable | `obj->AsWritable()` |
| Console log | `ts_console_log(fmt, ...)` |

## Summary

**NEVER:**
- Use `new` or `malloc` directly
- Use `std::string` for runtime values
- Use C-style casts with virtual inheritance
- Assume `void*` is unboxed
- Double-box error objects
- Access `void*` params without unboxing

**ALWAYS:**
- Use `ts_alloc` + placement new
- Use `TsString::Create`
- Use `AsXxx()` helpers or `dynamic_cast`
- Unbox with `ts_value_get_*` helpers
- Check cast results before use
- Use `ts_console_log` for output
