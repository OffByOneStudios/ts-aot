# PERF-003: PackedDouble Array Fast Paths

**Status:** Complete
**Category:** Performance
**Priority:** High
**Depends On:** PERF-002 Phase 5 (PackedSmi complete)
**Achieved Speedup:** 5x improvement (50ms → 10ms)

## Overview

Implement unboxed storage for PackedDouble arrays to close the 4.4x performance gap with Node.js on double array operations.

## Final Results

| Array Type | ts-aot (before) | ts-aot (after) | Node.js | Result |
|------------|----------------|----------------|---------|--------|
| Integer (PackedSmi) | 11ms | 12ms | 11ms | **Parity** |
| Double (PackedDouble) | 50ms | **10ms** | 11ms | **1.1x faster!** |

## Changes Made

### 1. Runtime: Fixed indexOf/lastIndexOf for PackedDouble

**Issue:** When searching in a PackedDouble array, the runtime was trying to dereference the search value as a pointer, causing a crash when the double's bit pattern (like `0x400c000000000000` for 3.5) looked like a valid address.

**Fix:** Check `isDouble` FIRST before attempting to dereference the search value. For PackedDouble arrays, the codegen passes raw IEEE 754 bits directly, not boxed values.

Files modified: [src/runtime/src/TsArray.cpp](src/runtime/src/TsArray.cpp)

### 2. Codegen: Fixed double index conversion

**Issue:** When accessing array elements with a `double` typed index (e.g., `arr[i]` where `i` is a loop counter), the codegen was using the slow `ts_array_get_dynamic` path because the index was a double.

**Fix:** Convert double indices to int64 with `CreateFPToSI` for numeric array access, allowing the fast specialized path to be used.

Files modified: [src/compiler/codegen/IRGenerator_Expressions_Access.cpp](src/compiler/codegen/IRGenerator_Expressions_Access.cpp)

## Before State

After PERF-002 Phase 5, PackedSmi arrays store raw int64 values and are **1.2x faster than Node.js**. However, PackedDouble arrays still box their values:

| Array Type | ts-aot | Node.js | Gap |
|------------|--------|---------|-----|
| Integer (PackedSmi) | 11ms | 13ms | **1.2x faster** |
| Double (PackedDouble) | 48ms | 11ms | **4.4x slower** |

## Root Cause Analysis

PackedDouble arrays are slow because:
1. **Runtime boxing**: `ts_array_push` calls `ts_value_make_double()` even for PackedDouble
2. **Memory layout**: Elements are stored as `TsValue*` pointers (8 bytes) pointing to heap-allocated boxes
3. **Cache misses**: Each element access requires pointer dereference to reach the actual double

## Proposed Solution

Store raw IEEE 754 doubles (8 bytes each) directly in the array's `elements` buffer, similar to how PackedSmi stores raw int64 values.

### Memory Layout Comparison

**Current (Boxed):**
```
TsArray {
  elements: [TsValue*, TsValue*, TsValue*, ...]  // 8-byte pointers
}
TsValue {
  type: DOUBLE_VAL  // 1 byte
  double_val: 3.14  // 8 bytes
}
```

**Proposed (Unboxed):**
```
TsArray {
  elementKind: PackedDouble
  elements: [3.14, 2.71, 1.41, ...]  // 8-byte IEEE 754 doubles directly
}
```

## Implementation Plan

### Phase 1: Runtime Changes

#### 1.1 ts_array_push for PackedDouble

```cpp
// In TsArray.cpp
void TsArray::PushDouble(double value) {
    EnsureCapacity(length + 1);
    double* doubleElements = reinterpret_cast<double*>(elements);
    doubleElements[length] = value;
    length++;
}

extern "C" int64_t ts_array_push_specialized(void* arr, int64_t val) {
    TsArray* array = static_cast<TsArray*>(arr);

    if (array->elementKind == ElementKind::PackedDouble ||
        array->elementKind == ElementKind::HoleyDouble) {
        // val contains the bit pattern of a double
        double d;
        memcpy(&d, &val, sizeof(double));
        array->PushDouble(d);
        return array->length;
    }
    // ... existing PackedSmi logic
}
```

#### 1.2 ts_array_get for PackedDouble

```cpp
extern "C" int64_t ts_array_get_specialized(void* arr, int64_t index) {
    TsArray* array = static_cast<TsArray*>(arr);

    if (array->elementKind == ElementKind::PackedDouble ||
        array->elementKind == ElementKind::HoleyDouble) {
        double* doubleElements = reinterpret_cast<double*>(array->elements);
        double d = doubleElements[index];
        int64_t bits;
        memcpy(&bits, &d, sizeof(int64_t));
        return bits;  // Return bit pattern
    }
    // ... existing PackedSmi logic
}
```

#### 1.3 ts_array_set for PackedDouble

```cpp
extern "C" void ts_array_set_specialized(void* arr, int64_t index, int64_t val) {
    TsArray* array = static_cast<TsArray*>(arr);

    if (array->elementKind == ElementKind::PackedDouble ||
        array->elementKind == ElementKind::HoleyDouble) {
        double d;
        memcpy(&d, &val, sizeof(double));
        double* doubleElements = reinterpret_cast<double*>(array->elements);
        doubleElements[index] = d;
        return;
    }
    // ... existing PackedSmi logic
}
```

#### 1.4 indexOf/lastIndexOf/includes for PackedDouble

```cpp
extern "C" int64_t ts_array_indexOf_specialized(void* arr, int64_t searchVal, int64_t fromIndex) {
    TsArray* array = static_cast<TsArray*>(arr);

    if (array->elementKind == ElementKind::PackedDouble ||
        array->elementKind == ElementKind::HoleyDouble) {
        double searchDouble;
        memcpy(&searchDouble, &searchVal, sizeof(double));
        double* doubleElements = reinterpret_cast<double*>(array->elements);

        for (int64_t i = fromIndex; i < array->length; i++) {
            // IEEE 754 comparison (handles NaN correctly)
            if (doubleElements[i] == searchDouble) {
                return i;
            }
        }
        return -1;
    }
    // ... existing PackedSmi logic
}
```

### Phase 2: Codegen Verification

The codegen should already work correctly since we added element kind checks in PERF-002:

```cpp
// In IRGenerator_Expressions_Access.cpp - already implemented
if (arrayType->elementKind == ElementKind::PackedDouble ||
    arrayType->elementKind == ElementKind::HoleyDouble) {
    isSpecialized = true;
    llvmElemType = llvm::Type::getDoubleTy(*context);
}
```

**Verify:**
- [ ] Array element access uses `getelementptr double` for PackedDouble
- [ ] Load/store use `double` type, not `i64`
- [ ] indexOf/includes use `CreateBitCast` (not `CreateFPToSI`) for PackedDouble

### Phase 3: Array Creation

Ensure PackedDouble arrays are created with the correct element kind:

```cpp
// In ts_array_create_specialized
extern "C" void* ts_array_create_specialized(int64_t elementKind, int64_t initialCapacity) {
    TsArray* arr = TsArray::Create();
    arr->elementKind = static_cast<ElementKind>(elementKind);
    arr->EnsureCapacity(initialCapacity);
    return arr;
}
```

### Phase 4: Element Kind Transitions

Handle transitions when incompatible values are pushed:

```cpp
void TsArray::TransitionTo(ElementKind newKind) {
    if (newKind == elementKind) return;

    if (elementKind == ElementKind::PackedSmi && newKind == ElementKind::PackedDouble) {
        // Widen int64 values to double
        int64_t* intElements = reinterpret_cast<int64_t*>(elements);
        double* doubleElements = reinterpret_cast<double*>(ts_alloc(capacity * sizeof(double)));

        for (int64_t i = 0; i < length; i++) {
            doubleElements[i] = static_cast<double>(intElements[i]);
        }

        elements = reinterpret_cast<TsValue**>(doubleElements);
        elementKind = ElementKind::PackedDouble;
    }
    // ... other transitions
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `src/runtime/src/TsArray.cpp` | Add PackedDouble fast paths to push/get/set/indexOf |
| `src/runtime/include/TsArray.h` | Add PushDouble, GetDouble helper methods |
| `tests/golden_ir/typescript/passes/element_kind_inference.ts` | Add double array tests |

## Testing Strategy

### Unit Tests

```typescript
// tests/golden_ir/typescript/passes/packed_double_arrays.ts
function user_main(): number {
    // Test 1: Double array creation and access
    const doubles = [1.5, 2.5, 3.5, 4.5, 5.5];
    let sum = 0;
    for (let i = 0; i < doubles.length; i++) {
        sum += doubles[i];
    }
    console.log(sum);  // Should print 17.5

    // Test 2: indexOf with doubles
    console.log(doubles.indexOf(3.5));  // Should print 2
    console.log(doubles.indexOf(9.9));  // Should print -1

    // Test 3: Push doubles
    doubles.push(6.5);
    console.log(doubles.length);  // Should print 6

    return 0;
}
// OUTPUT: 17.5
// OUTPUT: 2
// OUTPUT: -1
// OUTPUT: 6
```

### Benchmark

```typescript
// tmp/bench_packed_double.ts
function benchmarkDoubleArray(): number {
    const iterations = 1000000;
    const arr: number[] = [];

    for (let i = 0; i < iterations; i++) {
        arr.push(i * 1.5);
    }

    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }

    return sum;
}
```

**Target:** Match or exceed Node.js performance (currently 11ms)

## Success Criteria

1. **Performance:** Double array benchmark completes in <15ms (vs current 48ms)
2. **Correctness:** All 146+ golden IR tests pass
3. **Edge Cases:** NaN, Infinity, -0 handled correctly
4. **Transitions:** PackedSmi → PackedDouble transition works

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| NaN comparison semantics | Use IEEE 754 comparison (`==` returns false for NaN) |
| -0 vs +0 distinction | Store raw bits, comparison handles correctly |
| Memory alignment | doubles are 8-byte aligned, same as pointers |
| Type confusion | Runtime checks elementKind before accessing |

## Timeline Estimate

- Phase 1 (Runtime): Core changes
- Phase 2 (Codegen): Verification
- Phase 3 (Creation): Array initialization
- Phase 4 (Transitions): Element kind migration
- Testing & Benchmarking

## Related

- PERF-002: Integer Optimization V8/JSC Parity (PackedSmi complete)
- [V8 Elements Kinds](https://v8.dev/blog/elements-kinds)
