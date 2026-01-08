# Plan: Inline LLVM IR Operations (Option C)

**Epic:** Unified TsValue Boxing  
**Milestone:** 5.8 Alternative Implementation  
**Goal:** Implement property/element access entirely in LLVM IR to avoid Windows x64 ABI issues  
**Status:** Planning Phase  
**Estimated Total Effort:** 12-16 hours

---

## Problem Summary

Windows x64 ABI requires 16-byte structs (TsValue) to be passed/returned via hidden pointer (`sret`), causing ABI mismatch between LLVM-generated IR and MSVC-compiled runtime functions. Instead of fixing the ABI layer, we inline the entire operation in LLVM IR.

---

## Architecture Overview

### Current Flow (Problematic)
```
LLVM IR → call ts_map_get_v(map, key) → MSVC-compiled C++ → std::unordered_map lookup
         ↑ ABI mismatch here!
```

### New Flow (Inline IR)
```
LLVM IR → GEP to TsMap::impl → call __ts_map_lookup_inline(impl, key_hash, key_equal)
                                ↑ Simple C function taking scalars, no struct passing
```

---

## Phase 1: Helper Runtime Functions (3-4 hours)

Add new C-style helper functions that take **scalar values** instead of TsValue structs.

### 1.1 Map Lookup Helpers
**File:** `src/runtime/src/TsMap.cpp`

```cpp
extern "C" {
    // Returns index in bucket chain, or -1 if not found
    // Takes: unordered_map impl, key hash (i64), key type (i8), key value (i64)
    int64_t __ts_map_find_bucket(void* impl, uint64_t key_hash, 
                                  uint8_t key_type, int64_t key_val);
    
    // Returns TsValue fields via out-params (avoids struct return)
    // Takes: unordered_map impl, bucket index
    // Sets: out_type, out_value
    void __ts_map_get_value_at(void* impl, int64_t bucket_idx,
                                uint8_t* out_type, int64_t* out_value);
    
    // Insert/update value at bucket
    void __ts_map_set_at(void* impl, uint64_t key_hash,
                         uint8_t key_type, int64_t key_val,
                         uint8_t val_type, int64_t val_val);
}
```

**Implementation Strategy:**
- Expose minimal std::unordered_map operations as C functions
- Pass all TsValue fields separately (type as i8, value as i64/ptr)
- Avoid returning structs - use out-parameters instead

**Action Items:**
- [ ] 1.1.1 Implement `__ts_map_find_bucket` using existing TsValueHash/TsValueEqual
- [ ] 1.1.2 Implement `__ts_map_get_value_at` with bounds checking
- [ ] 1.1.3 Implement `__ts_map_set_at` with insert_or_assign logic
- [ ] 1.1.4 Add unit tests for helpers with various key/value types

### 1.2 Array Access Helpers  
**File:** `src/runtime/src/TsArray.cpp`

```cpp
extern "C" {
    // Get array element, returns type and value separately
    void __ts_array_get_inline(void* arr, int64_t index,
                                uint8_t* out_type, int64_t* out_value);
    
    // Set array element from separate type/value
    void __ts_array_set_inline(void* arr, int64_t index,
                                uint8_t val_type, int64_t val_value);
    
    // Get array length (already exists but ensure it's extern "C")
    int64_t __ts_array_length(void* arr);
}
```

**Action Items:**
- [ ] 1.2.1 Implement `__ts_array_get_inline` with bounds checking
- [ ] 1.2.2 Implement `__ts_array_set_inline` with capacity growth
- [ ] 1.2.3 Ensure `__ts_array_length` is exported
- [ ] 1.2.4 Add unit tests for inline array helpers

### 1.3 Object Property Helpers
**File:** `src/runtime/src/TsObject.cpp`

```cpp
extern "C" {
    // Get object's internal map pointer
    void* __ts_object_get_map(void* obj);
    
    // For primitives: convert to string and delegate to map
    void* __ts_value_to_property_key(uint8_t type, int64_t value);
}
```

**Action Items:**
- [ ] 1.3.1 Implement `__ts_object_get_map` - returns TsMap::impl pointer
- [ ] 1.3.2 Implement `__ts_value_to_property_key` for number→string conversion
- [ ] 1.3.3 Add tests for property key coercion

---

## Phase 2: LLVM IR Inline Implementations (6-8 hours)

Implement codegen helpers that emit pure LLVM IR for each operation.

### 2.1 Inline Map Get
**File:** `src/compiler/codegen/IRGenerator_InlineOps.cpp` (new file)

```cpp
llvm::Value* IRGenerator::emitInlineMapGet(llvm::Value* rawMap, llvm::Value* keyBoxed) {
    initTsValueType();
    
    // 1. Load key fields from boxed TsValue
    llvm::Value* keyType = emitLoadTsValueType(keyBoxed);
    llvm::Value* keyVal = emitLoadTsValueUnion(keyBoxed);
    
    // 2. Compute hash inline (for simple types) or call helper
    llvm::Value* keyHash = emitComputeHash(keyType, keyVal);
    
    // 3. Call __ts_map_find_bucket(impl, hash, type, val)
    llvm::Value* mapImpl = emitGetMapImpl(rawMap); // GEP to TsMap::impl field
    llvm::FunctionType* findFt = llvm::FunctionType::get(
        builder->getInt64Ty(), 
        { builder->getPtrTy(), builder->getInt64Ty(), 
          builder->getInt8Ty(), builder->getInt64Ty() }, 
        false);
    llvm::FunctionCallee findFn = getRuntimeFunction("__ts_map_find_bucket", findFt);
    llvm::Value* bucketIdx = createCall(findFt, findFn.getCallee(), 
                                       { mapImpl, keyHash, keyType, keyVal });
    
    // 4. Check if found (idx >= 0)
    llvm::Value* notFound = builder->CreateICmpSLT(bucketIdx, builder->getInt64(0));
    
    // 5. Allocate result TsValue on stack
    llvm::AllocaInst* result = createEntryBlockAlloca(
        builder->GetInsertBlock()->getParent(), "mapGetResult", tsValueType);
    
    // 6. Branch: found vs not-found
    llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(*context, "map.found", 
                                                          builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* notFoundBB = llvm::BasicBlock::Create(*context, "map.notfound", 
                                                             builder->GetInsertBlock()->getParent());
    llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(*context, "map.done", 
                                                         builder->GetInsertBlock()->getParent());
    
    builder->CreateCondBr(notFound, notFoundBB, foundBB);
    
    // Found: call __ts_map_get_value_at
    builder->SetInsertPoint(foundBB);
    llvm::Value* outType = builder->CreateAlloca(builder->getInt8Ty());
    llvm::Value* outVal = builder->CreateAlloca(builder->getInt64Ty());
    llvm::FunctionType* getValFt = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        { builder->getPtrTy(), builder->getInt64Ty(), 
          builder->getPtrTy(), builder->getPtrTy() },
        false);
    llvm::FunctionCallee getValFn = getRuntimeFunction("__ts_map_get_value_at", getValFt);
    createCall(getValFt, getValFn.getCallee(), { mapImpl, bucketIdx, outType, outVal });
    
    // Store to result TsValue
    emitStoreTsValueFields(result, 
                          builder->CreateLoad(builder->getInt8Ty(), outType),
                          builder->CreateLoad(builder->getInt64Ty(), outVal));
    builder->CreateBr(doneBB);
    
    // Not found: store undefined
    builder->SetInsertPoint(notFoundBB);
    emitStoreTsValueFields(result, builder->getInt8(0), builder->getInt64(0)); // UNDEFINED
    builder->CreateBr(doneBB);
    
    // Done
    builder->SetInsertPoint(doneBB);
    boxedValues.insert(result);
    return result;
}
```

**Action Items:**
- [ ] 2.1.1 Create `IRGenerator_InlineOps.cpp` with helper functions
- [ ] 2.1.2 Implement `emitLoadTsValueType` - GEP field 0, load i8
- [ ] 2.1.3 Implement `emitLoadTsValueUnion` - GEP field 1, load i64
- [ ] 2.1.4 Implement `emitStoreTsValueFields` - GEP + store type and value
- [ ] 2.1.5 Implement `emitComputeHash` - inline for int/bool, call helper for string
- [ ] 2.1.6 Implement `emitGetMapImpl` - GEP to TsMap::impl (offset 8, after vtable+magic)
- [ ] 2.1.7 Implement full `emitInlineMapGet` as above
- [ ] 2.1.8 Test with simple Map.get() calls

### 2.2 Inline Map Set
**File:** `src/compiler/codegen/IRGenerator_InlineOps.cpp`

```cpp
void IRGenerator::emitInlineMapSet(llvm::Value* rawMap, 
                                   llvm::Value* keyBoxed, 
                                   llvm::Value* valBoxed) {
    // Similar to MapGet but simpler - always call __ts_map_set_at
    llvm::Value* keyType = emitLoadTsValueType(keyBoxed);
    llvm::Value* keyVal = emitLoadTsValueUnion(keyBoxed);
    llvm::Value* valType = emitLoadTsValueType(valBoxed);
    llvm::Value* valVal = emitLoadTsValueUnion(valBoxed);
    
    llvm::Value* keyHash = emitComputeHash(keyType, keyVal);
    llvm::Value* mapImpl = emitGetMapImpl(rawMap);
    
    llvm::FunctionType* setFt = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        { builder->getPtrTy(), builder->getInt64Ty(),
          builder->getInt8Ty(), builder->getInt64Ty(),
          builder->getInt8Ty(), builder->getInt64Ty() },
        false);
    llvm::FunctionCallee setFn = getRuntimeFunction("__ts_map_set_at", setFt);
    createCall(setFt, setFn.getCallee(), 
              { mapImpl, keyHash, keyType, keyVal, valType, valVal });
}
```

**Action Items:**
- [ ] 2.2.1 Implement `emitInlineMapSet` as above
- [ ] 2.2.2 Test with Map.set() calls and object literals

### 2.3 Inline Array Get
**File:** `src/compiler/codegen/IRGenerator_InlineOps.cpp`

```cpp
llvm::Value* IRGenerator::emitInlineArrayGet(llvm::Value* rawArr, llvm::Value* index) {
    llvm::AllocaInst* result = createEntryBlockAlloca(
        builder->GetInsertBlock()->getParent(), "arrayGetResult", tsValueType);
    
    llvm::Value* outType = builder->CreateAlloca(builder->getInt8Ty());
    llvm::Value* outVal = builder->CreateAlloca(builder->getInt64Ty());
    
    llvm::FunctionType* ft = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context),
        { builder->getPtrTy(), builder->getInt64Ty(),
          builder->getPtrTy(), builder->getPtrTy() },
        false);
    llvm::FunctionCallee fn = getRuntimeFunction("__ts_array_get_inline", ft);
    createCall(ft, fn.getCallee(), { rawArr, index, outType, outVal });
    
    emitStoreTsValueFields(result,
                          builder->CreateLoad(builder->getInt8Ty(), outType),
                          builder->CreateLoad(builder->getInt64Ty(), outVal));
    boxedValues.insert(result);
    return result;
}
```

**Action Items:**
- [ ] 2.3.1 Implement `emitInlineArrayGet`
- [ ] 2.3.2 Test with array element access

### 2.4 Inline Array Set
**Action Items:**
- [ ] 2.4.1 Implement `emitInlineArraySet` (similar to ArrayGet but void return)

### 2.5 Inline Object Property Access
**Action Items:**
- [ ] 2.5.1 Implement `emitInlineObjectGetProp` - call `__ts_object_get_map` then `emitInlineMapGet`
- [ ] 2.5.2 Implement `emitInlineObjectSetProp` - similar with `emitInlineMapSet`
- [ ] 2.5.3 Handle computed properties (number keys → string conversion)

---

## Phase 3: Migration to Inline Ops (2-3 hours)

Replace all existing `emitMapGetV`, `emitObjectGetPropV`, etc. calls.

### 3.1 Update Call Sites
**Files to modify:**
- `IRGenerator_Expressions_Access.cpp` (element/property access)
- `IRGenerator_Expressions_Binary.cpp` (compound assignments)
- `IRGenerator_Classes.cpp` (object literals)
- `IRGenerator_Expressions_Calls_Member.cpp` (Map.get/set methods)
- `IRGenerator_Expressions_Calls_Builtin.cpp` (JSON generation)
- `IRGenerator_Functions.cpp` (closures)

**Search/Replace Pattern:**
```cpp
// OLD:
emitMapGetV(rawMap, keyBoxed)

// NEW:
emitInlineMapGet(rawMap, keyBoxed)
```

**Action Items:**
- [ ] 3.1.1 Replace all `emitMapGetV` → `emitInlineMapGet` (6 call sites)
- [ ] 3.1.2 Replace all `emitMapSetV` → `emitInlineMapSet` (8 call sites)
- [ ] 3.1.3 Replace all `emitObjectGetPropV` → `emitInlineObjectGetProp` (3 call sites)
- [ ] 3.1.4 Replace all `emitObjectSetPropV` → `emitInlineObjectSetProp` (2 call sites)
- [ ] 3.1.5 Replace all `emitArrayGetV` → `emitInlineArrayGet` (1 call site)
- [ ] 3.1.6 Replace all `emitArraySetV` → `emitInlineArraySet` (1 call site)

### 3.2 Remove Old `_v` Helpers
**Action Items:**
- [ ] 3.2.1 Delete `emitMapGetV`, `emitMapSetV`, etc. from IRGenerator_Core.cpp
- [ ] 3.2.2 Remove declarations from IRGenerator.h
- [ ] 3.2.3 Remove `_v` APIs from BoxingPolicy (since we don't call them anymore)

---

## Phase 4: Testing & Validation (1-2 hours)

### 4.1 Unit Tests
**Action Items:**
- [ ] 4.1.1 Test inline map get/set with int keys
- [ ] 4.1.2 Test inline map get/set with string keys
- [ ] 4.1.3 Test inline array access (get/set)
- [ ] 4.1.4 Test object property access (computed and static)
- [ ] 4.1.5 Test undefined/not-found cases

### 4.2 Integration Tests
**Action Items:**
- [ ] 4.2.1 Run full test suite (examples/*.ts)
- [ ] 4.2.2 Test lodash mixin pattern (57 methods)
- [ ] 4.2.3 Test for-in loops with computed access
- [ ] 4.2.4 Benchmark property access performance

### 4.3 IR Verification
**Action Items:**
- [ ] 4.3.1 Dump IR and verify no `ts_map_get_v` calls remain
- [ ] 4.3.2 Verify all property access uses inline helpers
- [ ] 4.3.3 Check for unexpected struct-passing in IR

---

## Phase 5: Cleanup & Documentation (1 hour)

### 5.1 Remove Deprecated APIs
**Action Items:**
- [ ] 5.1.1 Remove `[[deprecated]]` attributes (no longer needed)
- [ ] 5.1.2 Optionally remove `_v` API implementations from runtime (unused)
- [ ] 5.1.3 Keep old pointer-based APIs for backwards compatibility

### 5.2 Update Documentation
**Action Items:**
- [ ] 5.2.1 Update epic document: mark 5.8 complete via Option C
- [ ] 5.2.2 Document inline operation architecture
- [ ] 5.2.3 Add comments explaining hash/lookup inline strategy
- [ ] 5.2.4 Update DEVELOPMENT.md with inline IR patterns

---

## Benefits of This Approach

1. **No ABI Issues:** All runtime helpers take scalar values (i8, i64, ptr), avoiding struct passing
2. **Better Performance:** Fewer function calls, LLVM can optimize across operation boundaries
3. **Architectural Alignment:** Matches original inline boxing vision
4. **Platform Independent:** Works on Windows, Linux, macOS without special calling conventions
5. **Debuggable:** Inline operations are visible in LLVM IR dumps

---

## Risks & Mitigations

### Risk: Hash computation inline is complex
**Mitigation:** Only inline trivial cases (int, bool). Delegate string hashing to C helper.

### Risk: Breaking changes if TsMap/TsArray layout changes
**Mitigation:** Use symbolic offsets via `module->getDataLayout().getStructLayout()`.

### Risk: Harder to debug runtime issues
**Mitigation:** Keep old pointer-based APIs as fallback for debugging.

---

## Estimated Timeline

| Phase | Task | Hours |
|-------|------|-------|
| 1.1 | Map lookup helpers | 2 |
| 1.2 | Array access helpers | 1 |
| 1.3 | Object property helpers | 1 |
| 2.1 | Inline map get | 2 |
| 2.2 | Inline map set | 1 |
| 2.3-2.4 | Inline array get/set | 1 |
| 2.5 | Inline object props | 2 |
| 3.1 | Migrate call sites | 2 |
| 3.2 | Remove old helpers | 0.5 |
| 4.1-4.2 | Testing | 1.5 |
| 4.3 | IR verification | 0.5 |
| 5.1-5.2 | Cleanup & docs | 1 |
| **Total** | | **15.5 hours** |

---

## Next Steps

1. **Review this plan** with team/user
2. **Prototype Phase 1.1** (map helpers) to validate approach
3. **Test prototype** with single map.get() operation
4. **Iterate** based on findings
5. **Proceed sequentially** through phases

---

## Success Criteria

- ✅ All property/element access works without calling `_v` APIs
- ✅ No struct-passing in LLVM IR for map/array/object operations
- ✅ All existing tests pass
- ✅ Performance equal or better than current implementation
- ✅ Lodash mixin pattern works (57 methods)
- ✅ No Windows-specific crashes or ABI issues
