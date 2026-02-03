# PERF-002: Integer Optimization V8/JSC Parity

**Status:** In Progress (Phases 1-2 Complete)
**Category:** Performance
**Priority:** High
**Related:** IntegerOptimizationPass (src/compiler/hir/passes/IntegerOptimizationPass.cpp)

## Overview

Enhance IntegerOptimizationPass to achieve parity with V8 and JavaScriptCore integer optimizations while leveraging our AOT compilation advantage.

## Research: How V8/JSC Handle Integers

### V8 Smi (Small Integer) Optimization

Source: [V8 Internals: How Small is a "Small Integer?"](https://medium.com/fhinkel/v8-internals-how-small-is-a-small-integer-e0badc18b6da)

- **Range:** 31-bit signed integers (-2^31 to 2^31-1) on 64-bit with pointer compression
- **Tagged pointers:** LSB=0 means Smi, LSB=1 means heap pointer
- **Memory layout:** `|____int32_value____|0000000000000000000|`
- **Key benefit:** No heap allocation for small integers
- **Overflow handling:** Explicit overflow check, falls back to HeapNumber
- **Array optimization:** PACKED_SMI_ELEMENTS for pure-Smi arrays ([Elements kinds in V8](https://v8.dev/blog/elements-kinds))

### JavaScriptCore NaN-Boxing

Source: [JavaScriptCore Engine Internals](https://browser.training.ret2.systems/content/module_1/8_jsc_internals/jsc_objects)

- **Encoding:** Uses NaN space in IEEE754 representation
- **Top 16-bits denote type:**
  - Pointer: `0000:PPPP:PPPP:PPPP`
  - Double: `0001:****` through `FFFE:****`
  - Integer: `FFFF:0000:IIII:IIII`
- **Integer range:** 32-bit signed integers
- **Advantage over V8:** Doubles don't need heap allocation either

### Current ts-aot Implementation

Our IntegerOptimizationPass:
- Converts Float64 constants/operations to Int64 based on dataflow analysis
- Uses full 64-bit integers (not tagged - AOT advantage!)
- Type known at compile time, no runtime tagging overhead
- Division always stays Float64 (correct JS semantics)

## Gap Analysis

| Feature | V8 | JSC | ts-aot Current | Action Needed |
|---------|----|----|----------------|---------------|
| Integer range | 31-bit | 32-bit | 64-bit | None (better!) |
| Runtime tagging | Yes | Yes | No | None (AOT advantage) |
| Overflow detection | Yes | Yes | ✅ Yes | ~~Implement~~ Done |
| Safe integer range | 2^31 | 2^32 | ✅ Checked | ~~Add checks~~ Done |
| Bitwise ops (32-bit result) | Yes | Yes | ✅ Yes | ~~Implement~~ Done |
| Loop counter opt | Yes | Yes | **Basic** | **Enhance** |
| Array element kinds | Yes | Yes | **No** | **Future work** |
| Negative zero handling | Yes | Yes | **No** | **Implement** |

## Implementation Plan

### Phase 1: Overflow Safety (High Priority) ✅ COMPLETE

**Goal:** Match V8's overflow detection to prevent incorrect results.

**Implementation (commit 946aa9f):**
- Added `wouldOverflowI64Add`, `wouldOverflowI64Sub`, `wouldOverflowI64Mul` functions
- `combineAdd`, `combineSub`, `combineMul` now detect potential overflow and fall back to Float64
- Tests: `tests/golden_hir/passes/integer_overflow.ts`

1. **Track value ranges with overflow potential**
   ```cpp
   struct NumericRange {
       // Add overflow tracking
       bool mayOverflow = false;

       // Check before converting operation
       bool canSafelyConvertToI64() const {
           return isSafeInteger() && !mayOverflow;
       }
   };
   ```

2. **Add overflow checks for arithmetic**
   - Addition: Check if result exceeds MAX_SAFE_INTEGER
   - Multiplication: Check if product exceeds safe range
   - Subtraction: Similar to addition

3. **Implement `combineWithOverflowCheck` helpers**
   ```cpp
   NumericRange combineAdd(NumericRange lhs, NumericRange rhs) {
       // Check for potential overflow
       if ((rhs.maxVal > 0 && lhs.maxVal > MAX_SAFE - rhs.maxVal) ||
           (rhs.minVal < 0 && lhs.minVal < MIN_SAFE - rhs.minVal)) {
           return NumericRange::makeFloat();  // Fall back to Float64
       }
       return NumericRange::makeRange(lhs.minVal + rhs.minVal,
                                       lhs.maxVal + rhs.maxVal);
   }
   ```

### Phase 2: Bitwise Operations (High Priority) ✅ COMPLETE

**Goal:** Implement correct 32-bit semantics for bitwise operations.

**Implementation:**
- Updated `HIRToLLVM.cpp` to use proper 32-bit JavaScript semantics:
  - `lowerAndI64`, `lowerOrI64`, `lowerXorI64`: Truncate to 32-bit, sign-extend result
  - `lowerShlI64`, `lowerShrI64`: Truncate operands, mask shift amount to 5 bits
  - `lowerUShrI64`: Truncate to 32-bit, logical shift, zero-extend (UIToFP for unsigned result)
  - `lowerNotI64`: Truncate to 32-bit, complement, sign-extend
- Tests: `tests/golden_hir/passes/integer_bitwise.ts`, `integer_bitwise_edge_cases.ts`

JavaScript bitwise operations convert to 32-bit signed integers:
- `|`, `&`, `^` - produce 32-bit signed result
- `<<`, `>>` - shift amount masked to 5 bits
- `>>>` - produces 32-bit unsigned result

1. **Add Int32 type tracking**
   ```cpp
   enum class Kind {
       Unknown,
       Integer,      // 64-bit safe integer
       Integer32,    // 32-bit signed (from bitwise ops)
       Float,
       IntegerRange,
       MayOverflow
   };
   ```

2. **Handle bitwise operation results**
   - Result of `|`, `&`, `^`, `<<`, `>>` is 32-bit signed
   - Result of `>>>` is 32-bit unsigned
   - Both convert back to Number (Float64) in JS semantics

### Phase 3: Enhanced Loop Counter Optimization

**Goal:** Optimize common loop patterns more aggressively.

1. **Detect loop induction variables**
   ```typescript
   for (let i = 0; i < 1000; i++) {
       // i is provably 0..999, always Int64
   }
   ```

2. **Track increment/decrement patterns**
   - `i++` with known bound → range is [init, bound-1]
   - `i--` with known bound → range is [bound+1, init]

3. **Propagate loop bounds to array indexing**
   - If `i < arr.length` and arr.length known, array access is bounds-safe

### Phase 4: Negative Zero Handling

**Goal:** Correctly handle -0 vs +0 semantics.

JavaScript distinguishes -0 and +0:
```javascript
1 / 0   // Infinity
1 / -0  // -Infinity
0 === -0  // true (!)
```

1. **Track negative zero potential**
   ```cpp
   struct NumericRange {
       bool mayBeNegativeZero = false;
   };
   ```

2. **Operations that produce -0:**
   - `0 * negative` → -0
   - `-0 + 0` → -0
   - `negative / Infinity` → -0

3. **Keep as Float64 if -0 matters**
   - Division results (already Float64)
   - Values used with division

### Phase 5: Array Element Kinds (Future)

**Goal:** V8-style monomorphic array optimizations.

This is a larger architectural change for the future:
- Track array element types through the pipeline
- Generate specialized code for SMI-only arrays
- Avoid boxing/unboxing for homogeneous arrays

## Testing Strategy

### Unit Tests for NumericRange
```cpp
TEST(NumericRange, OverflowDetection) {
    auto large = NumericRange::makeInteger(MAX_SAFE_INTEGER - 1);
    auto one = NumericRange::makeInteger(1);
    auto result = combineAdd(large, one);
    EXPECT_TRUE(result.isSafeInteger());  // Still safe

    auto two = NumericRange::makeInteger(2);
    result = combineAdd(large, two);
    EXPECT_TRUE(result.mustBeFloat());  // Overflow → Float64
}
```

### Golden HIR Tests
```typescript
// tests/golden_hir/passes/integer_overflow.ts
// Test that overflow falls back to Float64
function user_main(): number {
    const big = 9007199254740990;  // MAX_SAFE_INTEGER - 1
    const result = big + 2;        // Overflows → Float64
    console.log(result);
    return 0;
}
// HIR-CHECK: const.f64 9007199254740992
// NOT: const.i64
```

### Conformance Tests
- Verify all operations produce same results as Node.js
- Edge cases: MAX_SAFE_INTEGER boundaries, -0, bitwise edge cases

## Success Metrics

1. **Correctness:** All existing tests pass, new edge cases covered
2. **Coverage:** Match V8/JSC optimization cases (except array element kinds)
3. **Performance:** Maintain or improve benchmark results

## Files to Modify

| File | Changes |
|------|---------|
| `IntegerOptimizationPass.h` | Add overflow tracking to NumericRange |
| `IntegerOptimizationPass.cpp` | Implement overflow detection, bitwise handling |
| `HIRBuilder.cpp` | Add Int32 operation types if needed |
| `tests/golden_hir/passes/` | Add overflow, bitwise, -0 tests |

## Timeline

- **Phase 1 (Overflow Safety):** 1-2 days
- **Phase 2 (Bitwise Operations):** 1-2 days
- **Phase 3 (Loop Counters):** 1 day
- **Phase 4 (Negative Zero):** 0.5 days
- **Phase 5 (Array Kinds):** Future epic

## References

- [V8 Smi Implementation](https://medium.com/fhinkel/v8-internals-how-small-is-a-small-integer-e0badc18b6da)
- [Elements kinds in V8](https://v8.dev/blog/elements-kinds)
- [Speculation in JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/)
- [NaN-Boxing vs Tagged Pointers](https://witch.works/en/posts/javascript-trip-of-js-value-tagged-pointer-nan-boxing)
- [Speeding up JavaScript addition in V8](https://www.kvakil.me/posts/quicker_smi_add_v8.html)
