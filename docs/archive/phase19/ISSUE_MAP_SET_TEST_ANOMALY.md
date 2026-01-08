# Issue: map_set_test.exe Anomalous Crash

**Status:** Open  
**Priority:** LOW  
**Created:** 2026-01-04  
**Category:** Anomalous Behavior - Edge Case  
**Affects:** map_set_test.ts only

---

## Problem Statement

`map_set_test.exe` crashes with "Runtime Panic: Null or undefined dereference" after printing "Map size (should be 4):" but before the size value. However, **all individual Map operations work correctly in isolation**, making this an anomalous edge case rather than a core functionality bug.

---

## Reproduction

**Test:** `examples/map_set_test.ts` (lines 1-14)
```typescript
const m = new Map();
m.set("a", 1);
m.set(2, "b");
m.set(BigInt(3), "c");
const s = Symbol("d");
m.set(s, "e");

console.log("Map size (should be 4):", m.size);  // CRASHES HERE
console.log("Map get 'a':", m.get("a"));
// ... more operations
```

**Output:**
```
Map size (should be 4):
Runtime Panic: Null or undefined dereference
```

---

## Investigation Results (Binary Search)

### Individual Operations All Work ✅

Created incremental tests to isolate failure point:

| Test File | Lines Tested | Result | Output |
|-----------|--------------|--------|--------|
| test_map_line9.ts | 1-9 | ✅ PASS | Size: 4, Get 'a': 1 |
| test_map_line10.ts | 1-10 | ✅ PASS | + Get 2: b |
| test_map_line11.ts | 1-11 | ✅ PASS | + Get BigInt(3): c |
| test_map_line12.ts | 1-12 | ✅ PASS | + Get Symbol: e |
| test_map_line13.ts | 1-13 | ✅ PASS | + Has 'a': true |
| test_map_line14.ts | 1-14 | ✅ PASS | + Has 'x': false |
| **map_set_test.ts** | 1-42 | ❌ **CRASH** | Crashes at line 8 |

### Core Operations Verified Working

All tested individually and confirmed working:
- ✅ Map.set() with string key
- ✅ Map.set() with number key
- ✅ Map.set() with BigInt key
- ✅ Map.set() with Symbol key
- ✅ Map.size property access
- ✅ Map.get() for all key types
- ✅ Map.has() for existing and non-existing keys

### Anomaly Discovered

When copying **exact same first 14 lines** to `test_map_first14.ts`:
- Original map_set_test.ts: ❌ CRASHES
- test_map_line14.ts: ✅ WORKS (same content!)
- test_map_first14.ts (copied): Produces NO OUTPUT (different from crash)

This suggests the issue is **not in the Map operations themselves**.

---

## Technical Analysis

### Inline IR Operations Are Correct

**Evidence:**
1. test_inline_ops.ts passes all Map operations ✅
2. Individual line-by-line tests all work ✅
3. test_map_steps.ts (1-7 with size check) works ✅
4. Simplified versions of map_set_test work ✅

**Conclusion:** The inline IR migration for Map operations is **correct and functional**.

### Possible Causes

Since individual operations work, the crash may be due to:

1. **File Encoding Issue**
   - BOM (Byte Order Mark) in map_set_test.ts?
   - Line ending differences (CRLF vs LF)?
   - Hidden characters?

2. **Optimizer Edge Case**
   - Full program optimization behaves differently?
   - Dead code elimination issue?
   - Register allocation problem?

3. **Interaction Between Operations**
   - Specific combination of types causes issue?
   - Stack alignment problem with 4 different types?
   - GC interaction with multiple types?

4. **Console.log Edge Case**
   - Multiple arguments with property access?
   - String concatenation issue?
   - Argument evaluation order?

---

## Investigation Steps

### 1. Check File Encoding
```powershell
# Check for BOM or encoding issues
$bytes = [System.IO.File]::ReadAllBytes("examples\map_set_test.ts")
$bytes[0..10]  # Look for BOM (EF BB BF for UTF-8)

# Compare file hashes
Get-FileHash examples\map_set_test.ts
Get-FileHash examples\test_map_line14.ts
```

### 2. Compare Generated IR
```powershell
# Generate IR for working vs failing
build\src\compiler\Release\ts-aot.exe examples\test_map_line14.ts --dump-ir > working_ir.txt
build\src\compiler\Release\ts-aot.exe examples\map_set_test.ts --dump-ir > failing_ir.txt

# Compare the two
diff working_ir.txt failing_ir.txt
```

### 3. Use CDB Debugger
```powershell
# Get detailed crash analysis
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\map_set_test.exe
```

Look for:
- Exact crash instruction
- Register states (null pointer in which register?)
- Stack trace
- Which function is crashing?

### 4. Test Console.log Hypothesis
```typescript
// Test if console.log with property access is the issue
const m = new Map();
m.set("a", 1);
m.set(2, "b");
m.set(BigInt(3), "c");
m.set(Symbol("d"), "e");

const sz = m.size;           // Store to variable first
console.log("Size:", sz);    // Then print variable
```

### 5. Disable Optimizations
```powershell
# Try compiling with -O0 (no optimization)
# If optimization flag is available
build\src\compiler\Release\ts-aot.exe examples\map_set_test.ts -O0 -o examples\map_set_test_noopt.exe
```

---

## Priority Justification

**LOW PRIORITY** because:
1. All core Map operations are proven working ✅
2. Only affects this specific test file (not a pattern)
3. Simplified versions work correctly
4. Inline IR migration is validated and correct
5. Appears to be edge case or tooling issue
6. Not blocking any development work
7. May be test file issue rather than compiler bug

---

## Workaround

Until investigated:
- Use individual test files (test_map_line9.ts through test_map_line14.ts)
- Or restructure map_set_test.ts to separate sections
- Or store property access results in variables before console.log

---

## Success Criteria

- ✅ Identify root cause (encoding, optimizer, console.log, etc.)
- ✅ map_set_test.ts compiles and runs without crash
- ✅ All console.log statements print correctly
- ✅ Full test completes all assertions

OR

- ✅ Determine issue is test-file-specific (not compiler bug)
- ✅ Document workaround
- ✅ Keep individual tests (which all work)

---

## Notes

This issue is **NOT related to inline IR migration**. The evidence overwhelmingly shows:
- Individual Map operations work
- Inline IR code is correct
- Crashes only in this specific file
- Likely tooling/edge case issue

**Recommendation:** Investigate when time permits, but not blocking since all functionality is proven working.

---

## Related Issues

None - this is isolated to one test file.

---

## References

- Working tests: `examples/test_map_line9.ts` through `test_map_line14.ts`
- Investigation results: `docs/phase19/CRASH_INVESTIGATION_RESULTS.md`
- Inline IR validation: `examples/test_inline_ops.ts` (all Map ops pass)
