# Plan: Fix Nested Property Access on `any` Typed Objects

## Problem Statement

Accessing nested properties on `any` typed objects crashes:
```javascript
var obj = { a: { b: { c: 3 } } };
console.log(obj.a.b.c);  // CRASH
```

The crash occurs in `dynamic_cast` inside `ts_object_get_dynamic` when handling the second level of property access.

## Root Cause Analysis

### Crash Location
- **Crash**: Access violation at `mov eax,dword ptr [rdi+8]` with `rdi=0`
- **Function**: `ts_object_get_dynamic` at line 2418: `dynamic_cast<TsProxy*>((TsObject*)rawObj)`
- **Cause**: `rawObj` is not a valid TsObject* with proper RTTI

### Data Flow

1. **First access (`obj.a`)**:
   - `ts_object_get_dynamic(obj_boxed, "a")` called
   - `rawObj = ts_value_get_object(obj_boxed)` returns outer TsMap*
   - `TsMap::Get("a")` returns TsValue with `{type=OBJECT_PTR, ptr_val=inner_map_ptr}`
   - Return: heap-allocated TsValue* containing `{type=OBJECT_PTR, ptr_val=inner_map_ptr}`

2. **Second access (`(obj.a).b`)**:
   - `ts_object_get_dynamic(result_from_step_1, "b")` called
   - `ts_value_get_object(result_from_step_1)` is called
   - **HYPOTHESIS**: Returns wrong value, causing dynamic_cast to crash

### Hypothesis Investigation

The issue is likely in `ts_value_get_object` not correctly extracting the nested TsMap* from the intermediate TsValue*.

Looking at `ts_value_get_object`:
1. It checks if the input looks like a TsValue* (type <= 10 && bytes 1-3 are zero)
2. If so, extracts `ptr_val`
3. Otherwise, tries to interpret as raw object by checking magic numbers

The problem could be:
- The TsValue* from step 1 is not being recognized as a boxed value
- The `ptr_val` being extracted is incorrect
- The nested TsMap* is not a valid object

## Proposed Fix

### Option A: Add Debug Tracing (for investigation)

Add temporary debug output to `ts_object_get_dynamic` to trace exactly what values are being passed:

```cpp
// In ts_object_get_dynamic:
fprintf(stderr, "ts_object_get_dynamic: obj=%p\n", obj);
if (obj) {
    fprintf(stderr, "  type=%d ptr_val=%p\n", (int)obj->type, obj->ptr_val);
}
void* rawObj = ts_value_get_object(obj);
fprintf(stderr, "  rawObj=%p\n", rawObj);
if (rawObj) {
    uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
    fprintf(stderr, "  magic16=0x%08X\n", magic16);
}
```

### Option B: Fix Underlying Issue

Based on crash analysis, the most likely fix is in `ts_value_get_object` - ensure it correctly handles TsValue* that was created by `ts_object_get_dynamic`.

Key areas to check:
1. Verify padding bytes are actually zero in allocated TsValue*
2. Verify `ptr_val` is correctly set when copying TsValue to heap
3. Add null check before dynamic_cast

### Option C: Skip dynamic_cast for Known TsMap

Since TsProxy is a rare case, we can check TsMap magic **before** attempting dynamic_cast:

```cpp
void* rawObj = ts_value_get_object(obj);
if (!rawObj) return ts_value_make_undefined();

// Check if this is a TsMap first (most common case)
uint32_t magic16 = *(uint32_t*)((char*)rawObj + 16);
if (magic16 == 0x4D415053) {  // TsMap::MAGIC
    // It's a TsMap - skip proxy check, go directly to map operations
    TsMap* map = (TsMap*)rawObj;
    // ... rest of map handling
}

// Only do dynamic_cast if not a TsMap
TsProxy* proxy = dynamic_cast<TsProxy*>((TsObject*)rawObj);
if (proxy) {
    return proxy->get(key, nullptr);
}
```

## Recommended Approach

1. **Step 1**: Add debug tracing (Option A) to identify exact failure point
2. **Step 2**: Based on debug output, implement fix (likely Option B or C)
3. **Step 3**: Remove XFAIL marker from property_chain.js test
4. **Step 4**: Run full test suite to verify no regressions

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/src/TsObject.cpp` | Fix `ts_value_get_object` or add magic check before dynamic_cast |
| `tests/golden_ir/javascript/property_access/property_chain.js` | Remove XFAIL marker |

## Test Plan

1. Build runtime and compiler
2. Run property_chain.js test manually
3. Verify output is `3`
4. Run full golden IR test suite
5. Run full Node.js API test suite

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Fix breaks other property access | Medium | Run full test suite |
| Performance regression | Low | Magic check is O(1) |
| Memory safety issues | Medium | Use existing safe patterns |
