# PERF-002: Integer Optimization V8/JSC Parity

**Status:** In Progress (Phases 1, 2, 4, 5 Infrastructure Complete)
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
| Array element kinds | Yes | Yes | ⚠️ Infra | Runtime fast paths |
| Negative zero handling | Yes | Yes | ✅ Yes | ~~Implement~~ Done |

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

### Phase 4: Negative Zero Handling ✅ COMPLETE

**Goal:** Correctly handle -0 vs +0 semantics.

JavaScript distinguishes -0 and +0:
```javascript
1 / 0   // Infinity
1 / -0  // -Infinity
0 === -0  // true (!)
```

**Implementation:**
- Added `mayBeNegativeZero` flag to `NumericRange` struct
- Updated `isSafeInteger()` to return false if -0 is possible (can't represent as int64)
- Updated `combineMul()` to detect when multiplication could produce -0:
  - `0 * negative = -0`
  - `negative * 0 = -0`
  - Propagates -0 flag from operands
- Updated `combineAdd()`: result may be -0 only if both operands may be -0
- Updated `combineSub()`: result may be -0 if lhs may be -0 and rhs could be 0
- Tests: `tests/golden_hir/passes/negative_zero.ts`

The implementation ensures that values which could be negative zero stay as Float64, preserving correct JavaScript semantics for division operations.

### Phase 5: Array Element Kinds (Infrastructure Complete)

**Goal:** V8-style monomorphic array optimizations for 2-5x array operation speedup.

**Background:** V8 tracks "element kinds" for arrays to avoid boxing/unboxing overhead:
- `PACKED_SMI_ELEMENTS` - Small integers only (31-bit)
- `PACKED_DOUBLE_ELEMENTS` - IEEE 754 doubles only
- `PACKED_ELEMENTS` - Mixed/object elements (boxed)
- `HOLEY_*` variants - Arrays with holes (sparse)

**Status (commit a17341f):**
- ✅ Phase 5.1: ElementKind enum in Type.h and TsArray.h
- ✅ Phase 5.2: Element kind inference in Analyzer
- ✅ Phase 5.3: Codegen infrastructure (specialized codegen deferred)
- ✅ Phase 5.4: Runtime TransitionTo() method
- ✅ Phase 5.5: Benchmarks showing 5.6x opportunity on double arrays

**What's Done:**
- Element kinds are inferred at compile time (PackedSmi, PackedDouble, PackedString)
- ArrayType stores elementKind for optimization decisions
- Runtime has element kind field and transition support
- Tests pass (146/146 golden IR tests)

**Remaining Work (Future):**
- Update runtime push/pop/get/set to check element kind and use fast unboxed paths
- Enable specialized codegen for inferred element kinds once runtime supports it
- Currently deferred because runtime operations still expect boxed TsValue* values

**Benchmark Results:**
```
ts-aot vs Node.js:
- Integer array: 11ms vs 12ms (1.1x faster - AOT advantage!)
- Double array: 56ms vs 10ms (5.6x slower - boxing overhead)
- Mixed array: 6ms vs 2ms (3x slower)
```

The 5.6x gap on double arrays shows the potential improvement from enabling fast paths.

#### Phase 5.1: Element Kind Enum and Metadata ✅ COMPLETE

1. **Add ElementKind enum to Type.h:**
   ```cpp
   enum class ElementKind {
       Unknown,           // Type not yet determined
       PackedSmi,        // Small integers (-2^30 to 2^30-1)
       PackedDouble,     // IEEE 754 doubles
       PackedString,     // TsString* pointers only
       PackedObject,     // Homogeneous object type
       PackedAny,        // Mixed types (current generic)
       HoleySmi,         // SMI with holes
       HoleyDouble,      // Double with holes
       HoleyAny          // Mixed with holes
   };
   ```

2. **Extend ArrayType:**
   ```cpp
   struct ArrayType : public Type {
       std::shared_ptr<Type> elementType;
       ElementKind elementKind = ElementKind::Unknown;
       bool mayHaveHoles = false;
   };
   ```

3. **Extend TsArray runtime class:**
   ```cpp
   class TsArray {
       uint8_t elementKind;  // Use enum value
       // ... existing fields
   };
   ```

#### Phase 5.2: Element Kind Inference ✅ COMPLETE

1. **Array literal inference (IRGenerator_Expressions_Literals.cpp):**
   - Analyze all elements to determine most specific kind
   - `[1, 2, 3]` → PackedSmi (all values fit in 31 bits)
   - `[1.5, 2.0, 3.14]` → PackedDouble
   - `[1, "hello"]` → PackedAny (mixed)
   - `[1, , 3]` → HoleySmi (has holes)

2. **Array method return type inference:**
   - `arr.map(x => x * 2)` on PackedSmi → PackedSmi
   - `arr.filter(...)` preserves element kind
   - `arr.concat(other)` → union of element kinds

#### Phase 5.3: Specialized Code Generation ⚠️ INFRASTRUCTURE ONLY

1. **Array access codegen:**
   ```cpp
   // Current: binary specialized vs generic
   // Proposed: multi-path based on element kind
   switch (elementKind) {
       case PackedSmi:
           // Direct int64 load, no unboxing
           break;
       case PackedDouble:
           // Direct double load
           break;
       case PackedAny:
           // Current boxed path
           break;
   }
   ```

2. **Array push codegen:**
   - Detect kind transitions at compile time where possible
   - Generate kind transition code when needed

#### Phase 5.4: Kind Transitions ✅ COMPLETE

When an operation would change the element kind:
1. `PackedSmi.push(1.5)` → transition to PackedDouble
2. `PackedDouble.push("x")` → transition to PackedAny
3. Transitions are one-way (never go back to more specific)

```cpp
void TsArray::TransitionTo(ElementKind newKind) {
    if (newKind <= elementKind) return;  // No-op
    // Reallocate and convert elements if needed
    // e.g., SMI to Double: widen all int64 to double
}
```

#### Phase 5.5: Benchmarking ✅ COMPLETE

Create benchmarks to measure improvement:
```typescript
// Array sum benchmark
function sumArray(arr: number[]): number {
    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }
    return sum;
}

// Measure with SMI array vs generic array
const smiArr = [1, 2, 3, 4, 5];  // Should use PackedSmi
const mixedArr: any[] = [1, 2, 3, 4, 5];  // Uses PackedAny
```

**Expected Speedup:**
- Array element access: 2-3x (avoid boxing/unboxing)
- Array iteration: 2-5x (cache-friendly data layout)
- Array methods (map, filter): 2-3x

#### Files to Modify (Phase 5)

| File | Changes |
|------|---------|
| `src/compiler/analysis/Type.h` | Add ElementKind enum, extend ArrayType |
| `src/compiler/analysis/Analyzer_Types.cpp` | Infer element kinds from literals |
| `src/compiler/codegen/IRGenerator_Expressions_Literals.cpp` | Generate kind-specific array creation |
| `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` | Multi-path element access |
| `src/runtime/include/TsArray.h` | Add elementKind field |
| `src/runtime/src/TsArray.cpp` | Implement kind transitions |
| `examples/benchmarks/array_*.ts` | Element kind benchmarks |

#### Dependencies

Phase 5 depends on:
- Phase 1 (Overflow Safety) ✅ - needed for SMI range checking
- Phase 2 (Bitwise Operations) ✅ - needed for tagged integer operations

Phase 5 does NOT depend on:
- Phase 3 (Loop Counters) - independent optimization
- Phase 4 (Negative Zero) ✅ - already complete

**Estimated Effort:** 2-3 weeks for full implementation

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

- **Phase 1 (Overflow Safety):** ✅ Complete (commit 946aa9f)
- **Phase 2 (Bitwise Operations):** ✅ Complete (commit 9ef52d2)
- **Phase 3 (Loop Counters):** Pending (requires CFG analysis)
- **Phase 4 (Negative Zero):** ✅ Complete (commit 6b71c9d)
- **Phase 5 (Array Element Kinds):** ⚠️ Infrastructure complete (commit a17341f)
  - Enum, inference, transitions: Done
  - Fast runtime paths: Future work (update push/pop/get/set)

## References

- [V8 Smi Implementation](https://medium.com/fhinkel/v8-internals-how-small-is-a-small-integer-e0badc18b6da)
- [Elements kinds in V8](https://v8.dev/blog/elements-kinds)
- [Speculation in JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/)
- [NaN-Boxing vs Tagged Pointers](https://witch.works/en/posts/javascript-trip-of-js-value-tagged-pointer-nan-boxing)
- [Speeding up JavaScript addition in V8](https://www.kvakil.me/posts/quicker_smi_add_v8.html)
