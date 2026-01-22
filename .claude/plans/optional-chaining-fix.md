# Plan: Fix Optional Chaining Value Extraction

## Problem Statement

Optional chaining `obj.nested?.value` returns `0` instead of the correct value `42`.

## Root Cause Analysis

### Symptom
- `obj.nested.value` (non-optional) returns `0` (wrong)
- `obj.nested?.value` (optional) returns `42` (correct!)

This means the typed property access path is broken, while the dynamic optional path works.

### Data Flow

1. Object `{ nested: { value: 42 } }` is stored as a TsMap
2. The inner object `{ value: 42 }` is also a TsMap, stored as `TsValue{type=OBJECT_PTR, ptr_val=inner_map_ptr}` in the outer map
3. When accessing `obj.nested`, we call `__ts_map_get_value_at` which returns:
   - `out_type` = 5 (OBJECT_PTR)
   - `out_value` = the inner map pointer (as int64)
4. We store these into a stack-allocated `%TsValue` struct:
   ```llvm
   %mapGetResult = alloca %TsValue, align 8
   ...
   store i8 %type, ptr %35, align 1      ; offset 0
   store i64 %value, ptr %36, align 8    ; offset 8
   ```
5. The padding bytes (offsets 1-7) are **NOT** initialized - they contain stack garbage
6. When we call `ts_value_get_object(ptr %mapGetResult)`, it checks:
   ```cpp
   if (typeField <= 10 && byte1 == 0 && byte2 == 0 && byte3 == 0)
   ```
7. With garbage padding, this check FAILS, so the function falls through to magic number checks
8. Since `mapGetResult` is on the stack (not a real object), the magic number checks fail
9. The function returns `nullptr` or the wrong value
10. Property access on `nullptr` returns 0

### Why Optional Chaining Works

The optional path boxes the result with `ts_value_make_int()`, which creates a proper heap-allocated TsValue with zeroed padding. The non-optional path uses the stack-allocated TsValue directly.

## Proposed Fix

### Option A: Zero the padding in IR generation (Recommended)

Before storing type/value into `mapGetResult`, explicitly zero the padding field:

```llvm
%mapGetResult = alloca %TsValue, align 8
; Zero the padding bytes
%paddingPtr = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 1
store [7 x i8] zeroinitializer, ptr %paddingPtr, align 1
; Then store type and value...
```

**Pros**: Clean fix at the source
**Cons**: Adds IR instructions

### Option B: Memset the alloca

Use `llvm.memset` to zero the entire TsValue before use:

```llvm
%mapGetResult = alloca %TsValue, align 8
call void @llvm.memset.p0.i64(ptr %mapGetResult, i8 0, i64 16, i1 false)
```

**Pros**: Single instruction
**Cons**: Zeros bytes we immediately overwrite

### Option C: Fix ts_value_get_object to be more lenient

Instead of checking padding bytes, use a different heuristic:
- Only check `typeField <= 10`
- Trust that values with small type field are TsValues

**Pros**: No codegen changes needed
**Cons**: Less safe - could misinterpret random memory as TsValue

## Recommended Approach

**Option A** - Zero the padding field in IR generation when creating stack-allocated TsValue structs.

## Files to Modify

| File | Change |
|------|--------|
| `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` | Zero padding before storing map results |

## Test Plan

1. Build compiler: `cmake --build build --config Release`
2. Run test: `./examples/test_optional_debug.exe`
3. Run golden IR test: `python tests/golden_ir/runner.py tests/golden_ir/typescript/objects/optional_chaining.ts`
4. Run full test suite to verify no regressions
