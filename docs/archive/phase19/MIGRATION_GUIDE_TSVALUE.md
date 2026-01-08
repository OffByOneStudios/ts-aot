# TsValue API Migration Guide

This document describes the migration from pointer-based `TsValue*` APIs to value-based `TsValue` APIs.

## Overview

The ts-aot runtime is transitioning from heap-allocated `TsValue*` pointers to stack-allocated `TsValue` values passed by value. This eliminates heap allocation overhead and improves performance.

## TsValue Structure

```cpp
struct TsValue {
    uint8_t type;    // ValueType enum (0-10)
    uint8_t _pad[7]; // Padding for alignment
    union {
        int64_t i_val;
        double d_val;
        bool b_val;
        void* ptr_val;
    };
};
// Total: 16 bytes (fits in 2 registers on x64)
```

## ValueType Enum

```cpp
enum ValueType : uint8_t {
    UNDEFINED = 0,
    NUMBER_INT = 1,
    NUMBER_DBL = 2,
    BOOLEAN = 3,
    STRING_PTR = 4,
    OBJECT_PTR = 5,
    PROMISE_PTR = 6,
    ARRAY_PTR = 7,
    BIGINT_PTR = 8,
    SYMBOL_PTR = 9,
    FUNCTION_PTR = 10
};
```

## API Migration Table

### Map APIs

| Old API (Pointer-based) | New API (Value-based) |
|------------------------|----------------------|
| `TsValue* ts_map_get(void* map, TsValue* key)` | `TsValue ts_map_get_v(void* map, TsValue key)` |
| `void ts_map_set(void* map, TsValue* key, TsValue* value)` | `void ts_map_set_v(void* map, TsValue key, TsValue value)` |
| `bool ts_map_has(void* map, TsValue* key)` | `bool ts_map_has_v(void* map, TsValue key)` |
| `bool ts_map_delete(void* map, TsValue* key)` | `bool ts_map_delete_v(void* map, TsValue key)` |

### Array APIs

| Old API (Pointer-based) | New API (Value-based) |
|------------------------|----------------------|
| `TsValue* ts_array_get_as_value(void* arr, int64_t index)` | `TsValue ts_array_get_v(void* arr, int64_t index)` |
| `void ts_array_set(void* arr, int64_t index, TsValue* value)` | `void ts_array_set_v(void* arr, int64_t index, TsValue value)` |

### Object Property APIs

| Old API (Pointer-based) | New API (Value-based) |
|------------------------|----------------------|
| `TsValue* ts_object_get_prop(TsValue* obj, TsValue* key)` | `TsValue ts_object_get_prop_v(TsValue obj, TsValue key)` |
| `TsValue* ts_object_set_prop(TsValue* obj, TsValue* key, TsValue* value)` | `TsValue ts_object_set_prop_v(TsValue obj, TsValue key, TsValue value)` |

## Inline Boxing (Codegen)

Instead of calling runtime functions like `ts_value_make_string()`, the codegen can emit inline LLVM IR:

```cpp
// Stack allocate TsValue
llvm::Value* boxed = builder->CreateAlloca(tsValueType);

// Store type field (offset 0)
llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxed, 0);
builder->CreateStore(builder->getInt8((uint8_t)type), typePtr);

// Store value field (offset 8)
llvm::Value* valPtr = builder->CreateStructGEP(tsValueType, boxed, 1);
builder->CreateStore(rawValue, valPtr);
```

## Inline Unboxing (Codegen)

```cpp
// Get value field (offset 8, the union)
llvm::Value* valPtr = builder->CreateStructGEP(tsValueType, boxed, 1);
llvm::Value* rawValue = builder->CreateLoad(expectedType, valPtr);
```

## Type Checking (Codegen)

```cpp
// Get type field (offset 0)
llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxed, 0);
llvm::Value* actualType = builder->CreateLoad(builder->getInt8Ty(), typePtr);

// Compare with expected type
llvm::Value* isMatch = builder->CreateICmpEQ(actualType, builder->getInt8(expectedType));
```

## Benefits

1. **No Heap Allocation**: Values passed on stack/registers, no GC pressure
2. **Cache Friendly**: 16 bytes fits in cache line, no pointer chasing
3. **Type Safety**: Type field always present, no magic number probing needed
4. **LLVM Optimization**: Stack allocations can be optimized away (mem2reg)

## Deprecation Timeline

1. **Phase 1** (Current): Add `_v` variants alongside existing APIs
2. **Phase 2**: Update codegen to prefer `_v` variants
3. **Phase 3**: Add deprecation warnings to old APIs
4. **Phase 4**: Remove deprecated APIs after full migration

## Notes

- The `_v` APIs are safe to call from LLVM-generated code
- Old pointer-based APIs remain for backwards compatibility
- Internal runtime code should migrate to `_v` variants incrementally
