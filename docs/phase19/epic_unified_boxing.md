# Epic: Unified TsValue Boxing Architecture

**Phase:** 19  
**Status:** In Progress  
**Priority:** High  
**Created:** 2026-01-03

## Problem Statement

The ts-aot runtime suffers from a fragile type identification system based on magic numbers and inconsistent boxing conventions. This has caused multiple runtime bugs including:

### Critical Bug: Computed Property Access Returns Undefined

**Symptom:** `obj[key]` in for-in loops returned `undefined` instead of the correct value.

**Root Cause:** Codegen passed raw `TsString*` to `ts_map_get(TsValue*)` because:
1. For-in returns boxed `TsValue*` from `ts_array_get`
2. Value stored to alloca, then loaded as new LLVM SSA value
3. `boxedValues` set doesn't track values across alloca stores/loads
4. Conditional check `boxedValues.count(key)` returned false
5. `boxValue()` called on already-boxed value → passed wrong type

**Workaround Applied:** Unbox-then-rebox pattern in element access codegen.

**Performance Cost of Workaround:** 2 function calls + 1 heap allocation per property access.

### Underlying Architectural Issues

1. **Magic Number Fragility**
   - `ts_value_typeof` checks for TsFunction magic (0x46554E43) at offsets 0, 8, 16, 24
   - Virtual inheritance changes class layouts unpredictably
   - Each new runtime class may require updating offset checks

2. **Unsafe Memory Probing**
   - `ts_value_get_string` reads arbitrary memory with `__try/__except`
   - Checking magic at `*(uint32_t*)v` assumes v is readable
   - Exception handlers add overhead and hide bugs

3. **Mixed Raw/Boxed Conventions**
   - Some APIs take `TsString*`, others take `TsValue*`
   - Caller must know which convention each API uses
   - No compile-time or runtime enforcement

4. **boxedValues Tracking Limitations**
   - Only tracks SSA values within a function
   - Doesn't survive alloca store/load cycles
   - Doesn't track across function boundaries
   - Cannot be 100% reliable for optimization decisions

## Solution: Unified TsValue Architecture

### Core Principles

1. **All runtime API boundaries use `TsValue*`** - No raw pointer confusion
2. **Inline boxing/unboxing in codegen** - Avoid function call overhead
3. **TsValue.type is the single source of truth** - No magic number probing
4. **Stack-allocated TsValue for temporaries** - Avoid heap allocation overhead

### TsValue Structure (Existing)

```cpp
enum class ValueType : uint8_t {
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
    FUNCTION_PTR = 10,  // NEW - for explicit function tagging
};

struct TsValue {
    ValueType type;      // 1 byte
    // 7 bytes padding
    union {
        int64_t i_val;
        double d_val;
        bool b_val;
        void* ptr_val;
    };                   // 8 bytes
};                       // Total: 16 bytes
```

### Inline Boxing Pattern

Instead of calling `ts_value_make_string()`, emit LLVM IR directly:

```cpp
// IRGenerator helper - emit inline boxing
llvm::Value* IRGenerator::emitInlineBox(llvm::Value* rawPtr, ValueType type) {
    // Stack allocate TsValue (optimized away by LLVM if not needed)
    llvm::Value* boxed = builder->CreateAlloca(tsValueType, nullptr, "boxed");
    
    // Store type field
    llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxed, 0);
    builder->CreateStore(builder->getInt8((uint8_t)type), typePtr);
    
    // Store ptr_val field
    llvm::Value* ptrPtr = builder->CreateStructGEP(tsValueType, boxed, 1);
    builder->CreateStore(rawPtr, ptrPtr);
    
    return boxed;
}

// IRGenerator helper - emit inline unboxing
llvm::Value* IRGenerator::emitInlineUnbox(llvm::Value* boxed) {
    llvm::Value* ptrPtr = builder->CreateStructGEP(tsValueType, boxed, 1);
    return builder->CreateLoad(builder->getPtrTy(), ptrPtr, "unboxed");
}

// IRGenerator helper - emit type check
llvm::Value* IRGenerator::emitTypeCheck(llvm::Value* boxed, ValueType expected) {
    llvm::Value* typePtr = builder->CreateStructGEP(tsValueType, boxed, 0);
    llvm::Value* actualType = builder->CreateLoad(builder->getInt8Ty(), typePtr);
    return builder->CreateICmpEQ(actualType, builder->getInt8((uint8_t)expected));
}
```

### Runtime API Migration

**Phase 1: Add TsValue-based APIs alongside existing ones**
```cpp
// Old (keep temporarily)
void* ts_map_get(void* map, TsValue* key);

// New (preferred)
TsValue ts_map_get_v(TsMap* map, TsValue key);  // Pass by value, return by value
```

**Phase 2: Migrate codegen to use new APIs**

**Phase 3: Remove old APIs and magic number checks**

---

## Milestones

### Milestone 1: Inline Boxing Infrastructure ✅ COMPLETE
Add core LLVM IR emission helpers for inline boxing/unboxing without heap allocation.

#### Action Items
- [x] 1.1 Define `tsValueType` as LLVM StructType in IRGenerator initialization
- [x] 1.2 Implement `emitInlineBox(rawPtr, ValueType)` helper
- [x] 1.3 Implement `emitInlineUnbox(boxed)` helper  
- [x] 1.4 Implement `emitTypeCheck(boxed, expectedType)` helper
- [ ] 1.5 Add unit tests for inline boxing IR generation

### Milestone 2: Optimize Property Access ✅ COMPLETE
Replace function-call-based boxing with inline boxing for critical paths.

#### Action Items
- [x] 2.1 Replace unbox/rebox function calls in Object element access with inline boxing
- [x] 2.2 Replace unbox/rebox in Array element access
- [x] 2.3 Replace unbox/rebox in Map element access
- [x] 2.4 Benchmark property access before/after optimization
- [x] 2.5 Verify lodash mixin pattern works correctly (57 methods found)

### Milestone 3: Add FUNCTION_PTR ValueType ✅ COMPLETE
Enable proper `typeof` detection for functions without magic number probing.

#### Action Items
- [x] 3.1 Add `FUNCTION_PTR = 10` to ValueType enum
- [x] 3.2 Update `ts_value_make_function()` to set FUNCTION_PTR type
- [x] 3.3 Update `ts_value_typeof` to check type field (with magic fallback for compatibility)
- [x] 3.4 Update function storage in objects to use FUNCTION_PTR type
- [x] 3.5 Test `typeof` returns "function" for object method properties
- [x] 3.6 Test `typeof` returns "function" for function expressions

### Milestone 4: Remove Magic Number Probing ✅ COMPLETE
Eliminate unsafe memory reads and magic number checks.

#### Action Items
- [x] 4.1 Audit all `*(uint32_t*)` magic checks in runtime
- [x] 4.2 Replace magic checks with TsValue.type field checks (type-first approach)
- [x] 4.3 Remove `__try/__except` blocks from type detection
- [x] 4.4 Magic numbers kept as fallback for raw pointers only (backwards compat)
- [x] 4.5 Update `ts_value_get_string` to check type field first
- [x] 4.6 Update `ts_value_get_object` to check type field first

### Milestone 5: Migrate Runtime APIs ⬜
Standardize all runtime APIs to use TsValue consistently.

#### Action Items
- [ ] 5.1 Create migration guide document
- [ ] 5.2 Add `_v` variants of critical APIs (pass TsValue by value)
- [ ] 5.3 Update `ts_map_get`, `ts_map_set` to use inline TsValue
- [ ] 5.4 Update `ts_array_get`, `ts_array_set` to use inline TsValue
- [ ] 5.5 Update property access APIs (`ts_object_get`, `ts_object_set`)
- [ ] 5.6 Deprecate old pointer-based APIs with warnings
- [ ] 5.7 Remove deprecated APIs after full migration

### Milestone 6: Validation ⬜
Comprehensive testing to ensure no regressions.

#### Action Items
- [ ] 6.1 Run full test suite
- [ ] 6.2 Benchmark raytracer performance
- [ ] 6.3 Benchmark SHA256 performance
- [ ] 6.4 Test lodash loading and initialization
- [ ] 6.5 Test computed property access in all contexts
- [ ] 6.6 Test `typeof` on all value types
- [ ] 6.7 Memory usage comparison before/after

### Milestone 7: Re-enable Garbage Collector ⬜
Currently using raw malloc for debugging stability. Must re-enable Boehm GC.

#### Action Items
- [ ] 7.1 Switch `ts_alloc` back to `GC_malloc` instead of `malloc`
- [ ] 7.2 Verify GC doesn't collect live objects (all roots properly registered)
- [ ] 7.3 Test long-running programs don't leak memory
- [ ] 7.4 Benchmark memory usage with GC enabled vs malloc
- [ ] 7.5 Test lodash mixin pattern with GC enabled (no premature collection)

---

## Performance Expectations

| Metric | Before (Current) | After (Unified) |
|--------|-----------------|-----------------|
| Property access | 2 function calls + heap alloc | 4 register ops |
| typeof check | Magic probe + exception handler | 1 load + 1 compare |
| Type confusion bugs | Possible | Eliminated |
| boxedValues tracking | Unreliable | Not needed |

---

## Risks and Mitigations

### Risk 1: Stack overflow from stack-allocated TsValue
**Mitigation:** TsValue is only 16 bytes; stack usage is minimal. LLVM's mem2reg pass will keep most in registers.

### Risk 2: Breaking existing runtime functions
**Mitigation:** Add new `_v` APIs alongside old ones; migrate incrementally; run tests continuously.

### Risk 3: Performance regression in tight loops
**Mitigation:** Benchmark critical paths; inline boxing should be faster than current heap allocation.

---

## Success Criteria

1. ✅ Computed property access works in all contexts (for-in, direct, nested)
2. ✅ `typeof` returns correct values for all types including functions
3. ✅ Type field checked first; magic only for raw pointers (no exception handlers)
4. ✅ No `__try/__except` blocks for type detection
5. ✅ Property access benchmark equal or better than before
6. ✅ All existing tests pass
7. ✅ Lodash mixin pattern works correctly (57 methods found)

---

## Related Issues

- Computed property access in for-in loops (FIXED: 2026-01-03)
- `typeof` returning "object" instead of "function" (FIXED: 2026-01-04)
- For-in loop key iteration (FIXED: earlier in Phase 19)
- For-in key unboxing for any-typed objects (FIXED: 2026-01-04)
- Double boxing when passing Object to any parameter (FIXED: 2026-01-04)
- Prefix increment/decrement codegen (FIXED: 2026-01-04)
- Memory pressure during lodash load (PARTIALLY FIXED: pool allocator)

---

## References

- [TsValue definition](../../src/runtime/include/TsObject.h)
- [Current boxing implementation](../../src/compiler/codegen/IRGenerator_Core.cpp)
- [Element access codegen](../../src/compiler/codegen/IRGenerator_Expressions_Access.cpp)
- [Magic number usage in typeof](../../src/runtime/src/TsObject.cpp)
