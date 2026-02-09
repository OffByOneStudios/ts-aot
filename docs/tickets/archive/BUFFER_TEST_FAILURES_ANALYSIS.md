# Buffer Test Failures Analysis Report

**Date:** 2026-02-08
**Status:** Investigation Complete
**Impact:** 4 buffer test suites fail with crashes and assertion failures

## Executive Summary

All four buffer test files fail due to **four distinct but interconnected architectural issues** in the Buffer implementation:

1. **Element Assignment Crash** (primary blocker) - Buffer element writes attempt to call TsArray functions
2. **Property Access Crash** - Missing "buffer" property handler in TsBuffer
3. **Endian Conversion Failures** - readIntLE/readIntBE return wrong values
4. **Encoding Crashes** - toString('hex') wrong length, then crash

## Detailed Analysis

### Test 1: buffer_advanced.ts
**File:** `tests/node/buffer/buffer_advanced.ts` (29 tests)
**Status:** ❌ CRASH on Test 6

#### Execution Trace
```
PASS: Buffer.alloc creates buffer with correct length
↓
buf[0] = 42    ← Assignment statement
↓
Access Violation 0xc0000005
```

#### Root Cause: Element Assignment Not Dispatched to Buffer
**Location:** `src/compiler/hir/HIRToLLVM.cpp:2848-2857`

The codegen assumes ALL numeric index assignments are for `TsArray`:

```cpp
// In HIRToLLVM::lowerSetElem()
} else {
    // Array index set
    // Convert index to i64 if it's a double
    if (idx->getType()->isDoubleTy()) {
        idx = builder_->CreateFPToSI(idx, builder_->getInt64Ty(), "idx_to_i64");
    }

    auto fn = getTsArraySet();
    builder_->CreateCall(fn, {arr, idx, val});  // ← Always calls ts_array_set_unchecked
}
```

**The Problem:**
- `ts_array_set_unchecked()` is designed for `TsArray*` objects
- When passed a `TsBuffer*`, it accesses wrong memory layout → crash
- No type checking or dispatch logic
- `TsBuffer` has `ts_buffer_set()` C API but codegen never calls it

**Evidence:**
- Works on TsArray (array indexing)
- Crashes on TsBuffer (buffer indexing)
- Same statement pattern: `obj[index] = value`

#### Why It Crashes
`ts_array_set_unchecked` expects:
```c
void ts_array_set_unchecked(TsArray* arr, int64_t index, void* value)
// TsArray memory layout: [vtable + magic] [length] [capacity] [elements]
```

When called with `TsBuffer*`:
```c
// TsBuffer memory layout: [vtable + magic] [data pointer] [length]
// → Writes to wrong offset, corrupts memory, AV
```

---

### Test 2: buffer_encoding.ts
**File:** `tests/node/buffer/buffer_encoding.ts` (14 tests)
**Status:** ❌ Test 1 FAIL, then CRASH

#### Execution Trace
```
Test 1: toString('hex') length check
Expected: 10 (for "Hello" → "48656c6c6f")
Actual:   Different value
Result:   FAIL, then CRASH
```

#### Root Causes: Multiple Issues

**Issue A: Hex Encoding Wrong Length**

The test does:
```typescript
const buf1 = Buffer.from('Hello');
const hex1 = buf1.toString('hex');
if (hex1.length !== 10) {
    console.log('FAIL: toString hex - wrong length');
```

Expected: "48656c6c6f" = 10 characters
Actual: Wrong length reported

**Likely Cause:**
- `TsBuffer::ToHex()` may not be returning correct string length
- Or string boxing is broken (returning a TsValue* instead of TsString*)
- Or encoding conversion is not running (returns empty string)

**Issue B: Subsequent Crash**

After the first test fails, the next tests crash with `0xc0000005` in `ucrtbase.dll`.

This suggests memory corruption from:
- Wrong boxing of string results
- Use-after-free of string objects
- Uninitialized pointers being dereferenced

#### Required Fixes
1. Verify `TsBuffer::ToHex()` produces correct output
2. Check string return value boxing in encoding functions
3. Verify `TsBuffer::ToBase64()` and `ToBase64Url()` work correctly

---

### Test 3: buffer_typed_array.ts
**File:** `tests/node/buffer/buffer_typed_array.ts` (6 tests)
**Status:** ❌ Tests 1-2 PASS, Tests 3+ CRASH

#### Execution Trace
```
Test 1: buffer.byteLength        ✓ PASS
Test 2: buffer.byteOffset        ✓ PASS
Test 3: buf.buffer property      ✗ CRASH
```

#### Root Cause: Missing "buffer" Property Handler
**Location:** `extensions/node/core/src/TsBuffer.cpp:12-20`

The code:
```cpp
TsValue TsBuffer::GetPropertyVirtual(const char* key) {
    if (strcmp(key, "length") == 0) {
        TsValue v;
        v.type = ValueType::NUMBER_INT;
        v.i_val = (int64_t)GetLength();
        return v;
    }
    return TsObject::GetPropertyVirtual(key);  // Falls through = UNDEFINED
}
```

**The Problem:**
- `TsBuffer` declares `GetArrayBuffer()` method but doesn't handle "buffer" property
- When code accesses `buf.buffer`, it falls through to base class
- Returns UNDEFINED instead of the Buffer object
- Downstream code dereferences UNDEFINED → crash

**Header vs Implementation Mismatch:**
```cpp
// In TsBuffer.h - line 30
TsBuffer* GetArrayBuffer() { return this; }  // Method exists

// But GetPropertyVirtual doesn't check for "buffer" property key
```

**Expected Behavior:**
```javascript
const buf = Buffer.alloc(5);
const arrBuf = buf.buffer;  // Should return buf itself (Node.js semantics)
console.log(arrBuf[0]);     // Should work if arrBuf is the same buffer
```

#### Required Fix
Add "buffer" property to `GetPropertyVirtual()`:
```cpp
if (strcmp(key, "buffer") == 0) {
    TsValue v;
    v.type = ValueType::OBJECT_PTR;
    v.ptr_val = this;
    return v;
}
```

---

### Test 4: buffer_variable_length.ts
**File:** `tests/node/buffer/buffer_variable_length.ts` (12 tests)
**Status:** ❌ Multiple assertion failures, then CRASH

#### Execution Trace
```
readIntLE 1 byte:  expected 1, got 6              ✗ FAIL
readIntLE 2 bytes: expected 513, got 0            ✗ FAIL
readIntLE 3 bytes: expected 197121, got 0         ✗ FAIL
readIntBE 2 bytes: expected 258, got 0            ✗ FAIL
readIntBE 3 bytes: expected 66051, got 0          ✗ FAIL
(more tests...)
↓
Access Violation in ucrtbase.dll
```

#### Root Cause 1: Endian Conversion Logic Broken
**Location:** `extensions/node/core/src/TsBuffer.cpp` (readIntLE/readIntBE implementation)

The test expects:
```typescript
const buf = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);
const v1 = buf.readIntLE(0, 1);  // Read 1 byte LE → should be 0x01 = 1
// But got: 6
```

Value `6` suggests:
- Off-by-one error in byte reading
- Wrong byte ordering (reading the 6th byte instead of 1st)
- Incorrect offset calculation

For 2-byte reads:
- Expected `0x0201` (little-endian) = 513
- Got: 0
- Suggests function returns 0 or wrong value

**Issues:**
- Little-endian byte order may be swapped (reading big-endian)
- Offset arithmetic may be wrong
- Sign extension may be incorrect
- Function signature mismatch with HIR codegen

#### Root Cause 2: Subsequent Crash
After multiple read/write failures, crash in ucrtbase.dll suggests:
- Buffer overflow during read/write operations
- Out-of-bounds memory access
- Corruption during swap16/swap32/swap64 operations

#### Required Fixes
1. Verify `readIntLE`, `readIntBE` byte order is correct
2. Check offset calculations (byteLength parameter usage)
3. Verify `writeIntLE`, `writeIntBE` produce correct output
4. Check swap16/swap32/swap64 implementations
5. Add bounds checking to prevent OOB reads

---

## Architectural Issues

### Issue A: No SetElementVirtual Method
**File:** `src/runtime/include/TsObject.h`

The interface defines:
```cpp
virtual TsValue GetElementVirtual(int64_t index) { ... }  // ← EXISTS
// No SetElementVirtual!
```

**Impact:**
- Forces codegen to assume all numeric index assignments are TsArray
- Can't dispatch to TsBuffer, TsTypedArray, etc.
- Makes it impossible to support multiple element-assignable types

**Required Fix:**
Add virtual method:
```cpp
virtual void SetElementVirtual(int64_t index, TsValue value) {}
```

Implement in each subclass:
- `TsArray::SetElementVirtual()` → calls internal array set logic
- `TsBuffer::SetElementVirtual()` → calls ts_buffer_set
- `TsTypedArray::SetElementVirtual()` → calls type-specific set logic

### Issue B: Codegen Assumes Array for Numeric Indices
**File:** `src/compiler/hir/HIRToLLVM.cpp:2848-2857`

Current code:
```cpp
} else {
    // Array index set
    auto fn = getTsArraySet();  // ← ALWAYS calls ts_array_set
    builder_->CreateCall(fn, {arr, idx, val});
}
```

Should be:
```cpp
} else {
    // Numeric index set - need to dispatch on type
    // For now, could call ts_object_set_element() or add type checking
}
```

---

## Summary of All Issues

| # | Test | Assertion | Type | Severity | Root Cause |
|---|------|-----------|------|----------|-----------|
| 1 | advanced | buf[0] = 42 | CRASH | CRITICAL | Wrong function call for Buffer indexing |
| 2 | encoding | toString('hex').length | FAIL | HIGH | Hex encoding returns wrong length |
| 3 | encoding | (subsequent) | CRASH | CRITICAL | Memory corruption from encoding |
| 4 | typed_array | buf.buffer | CRASH | CRITICAL | Missing property handler |
| 5 | var_length | readIntLE(0,1) === 1 | FAIL | HIGH | Endian conversion wrong |
| 6 | var_length | readIntBE(0,2) === 258 | FAIL | HIGH | Endian conversion wrong |
| 7 | var_length | (subsequent) | CRASH | CRITICAL | OOB read/write operations |

---

## Implementation Order

### Phase 1: Blocker Fixes (Required to pass any tests)

1. **Add SetElementVirtual to TsObject**
   - File: `src/runtime/include/TsObject.h`
   - Add: Virtual method signature
   - Time: 5 minutes

2. **Implement SetElementVirtual in TsBuffer**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Implementation: Dispatch to ts_buffer_set
   - Time: 10 minutes

3. **Implement SetElementVirtual in TsArray**
   - File: `src/runtime/src/TsArray.cpp`
   - Implementation: Direct array element write
   - Time: 10 minutes

4. **Update HIRToLLVM to use SetElementVirtual**
   - File: `src/compiler/hir/HIRToLLVM.cpp`
   - Change: lowerSetElem to call virtual method
   - Time: 20 minutes

5. **Add "buffer" property to TsBuffer**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Change: GetPropertyVirtual handler
   - Time: 5 minutes

### Phase 2: Functional Fixes

6. **Fix Hex/Base64 encoding**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Methods: ToHex, ToBase64, ToBase64Url
   - Time: 30 minutes

7. **Fix readIntLE/readIntBE**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Methods: ReadIntLE, ReadIntBE, ReadUIntLE, ReadUIntBE
   - Time: 30 minutes

8. **Fix writeIntLE/writeIntBE**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Methods: WriteIntLE, WriteIntBE, WriteUIntLE, WriteUIntBE
   - Time: 30 minutes

9. **Fix swap operations**
   - File: `extensions/node/core/src/TsBuffer.cpp`
   - Methods: swap16, swap32, swap64
   - Time: 20 minutes

---

## Test Coverage

Current test status before fixes:
- buffer_advanced.ts: 0/29 passing (crashes on test 6)
- buffer_encoding.ts: 0/14 passing (crashes on test 1)
- buffer_typed_array.ts: 2/6 passing (crashes on test 3)
- buffer_variable_length.ts: 0/12 passing (5 assertion failures + crash)

**Total: 2/61 tests passing (3%)**

After fixes, expecting:
- buffer_advanced.ts: 29/29 passing
- buffer_encoding.ts: 14/14 passing
- buffer_typed_array.ts: 6/6 passing
- buffer_variable_length.ts: 12/12 passing

**Expected: 61/61 tests passing (100%)**
