# Plan: Fix Remaining 3 JavaScript XFAIL Tests

**Created:** January 5, 2026  
**Status:** Not Started  
**Target:** 30/30 JavaScript tests passing (100%)  
**Current:** 27/30 JavaScript tests passing (90%)

---

## Executive Summary

Three JavaScript tests remain in XFAIL status after Phase 1-3 fixes. All three involve **runtime function implementation issues**, not compiler codegen problems. The failures trace to:

1. **has_property.js** - `obj.hasOwnProperty('key')` returns `undefined`
2. **object_values_any.js** - `Object.values(obj)` returns `undefined`  
3. **umd_pattern.js** - Complex IIFE with `module.exports` returns `undefined`

### Root Causes Identified

| Test | Issue | Root Cause | Complexity |
|------|-------|------------|------------|
| has_property.js | Returns `undefined` | **ctx parameter not passed to native function** | MEDIUM |
| object_values_any.js | Returns `undefined` | **Wrong magic number check OR unboxing issue** | LOW-MEDIUM |
| umd_pattern.js | Returns `undefined` | **Module exports + IIFE interaction** | HIGH |

---

## Phase 1: Fix hasOwnProperty (ctx parameter passing)

**Priority:** HIGH  
**Estimated Time:** 2-3 hours  
**Risk:** MEDIUM - Affects method call codegen  
**Dependencies:** None

### Issue Analysis

**Current Behavior:**
```javascript
var obj = { foo: 42 };
console.log(obj.hasOwnProperty('foo'));  // Output: undefined (Expected: true)
console.log(obj.hasOwnProperty('bar'));  // Output: undefined (Expected: false)
```

**Runtime Implementation:** [TsObject.cpp:1800-1832]
```cpp
static TsValue* ts_object_hasOwnProperty_native(void* ctx, int argc, TsValue** argv) {
    if (!ctx) {
        return ts_value_make_bool(false);  // ← ctx is NULL!
    }
    // Rest of implementation is correct...
}
```

**Root Cause:** Method calls like `obj.hasOwnProperty('foo')` should pass `obj` as the `ctx` parameter to native functions, but currently `ctx` is NULL.

### Investigation Steps

1. **Find method call codegen:**
   ```powershell
   grep -r "ctx.*native.*method" src/compiler/codegen/
   grep -r "hasOwnProperty" src/compiler/codegen/
   ```

2. **Check how `obj.method()` passes `this`:**
   - Read `IRGenerator_Expressions_Calls_Member.cpp` (focus on line 636: `llvm::Value* thisArg = objPtr`)
   - Search for where native functions get registered with `ctx` parameter

3. **Compare with working method calls:**
   - Array methods (e.g., `arr.map()`) work correctly
   - Check how they pass `this` to native functions
   - Search for `ts_array_map` registration and usage

4. **Trace the call chain:**
   ```
   obj.hasOwnProperty('key')
     → IRGenerator::visitCallExpression
     → tryGenerateMemberCall
     → How does ctx get passed?
     → ts_object_hasOwnProperty_native(void* ctx, ...)
   ```

### Fix Strategy

**Option A: Codegen Fix (Preferred)**
- Modify member call codegen to pass `obj` as `ctx` for native methods
- Ensure native function signature includes `ctx` parameter
- **File:** `src/compiler/codegen/IRGenerator_Expressions_Calls_Member.cpp`

**Option B: Runtime Workaround**
- Change hasOwnProperty to extract object from `argv` instead of `ctx`
- **Downside:** Doesn't fix the underlying ctx passing issue
- **File:** `src/runtime/src/TsObject.cpp`

### Implementation Tasks

- [ ] **Task 1.1:** Search for how method calls generate LLVM IR
  - File: `IRGenerator_Expressions_Calls_Member.cpp`
  - Look for: `thisArg` variable, `obj.method()` codegen

- [ ] **Task 1.2:** Find where hasOwnProperty is registered
  - Search: `registerRuntimeApi.*hasOwnProperty`
  - Check: Does registration specify ctx parameter?

- [ ] **Task 1.3:** Compare with working methods (Array.map)
  - File: `IRGenerator_Expressions_Calls_Builtin.cpp` (line ~1852: `thisArg`)
  - Understand: How does `arr.map(fn)` pass `arr` as context?

- [ ] **Task 1.4:** Implement fix in codegen
  - Ensure `ctx` parameter is populated with object pointer
  - Handle boxing/unboxing correctly
  - Test with simple case: `{ a: 1 }.hasOwnProperty('a')`

- [ ] **Task 1.5:** Verify fix doesn't break other method calls
  - Run full test suite
  - Check: Array methods, Object methods, custom class methods

### Success Criteria

```javascript
// Test: has_property.js
var obj = { foo: 42 };
console.log(obj.hasOwnProperty('foo'));  // Output: true ✓
console.log(obj.hasOwnProperty('bar'));  // Output: false ✓
```

**Expected IR:**
```llvm
; Should see ctx parameter being passed
call ptr @ts_object_hasOwnProperty_native(ptr %obj, i32 1, ptr %argv)
; NOT: call ptr @ts_object_hasOwnProperty_native(ptr null, i32 1, ptr %argv)
```

---

## Phase 2: Fix Object.values (magic number check)

**Priority:** MEDIUM  
**Estimated Time:** 1-2 hours  
**Risk:** LOW - Isolated runtime fix  
**Dependencies:** None

### Issue Analysis

**Current Behavior:**
```javascript
var obj = { a: 1, b: 2, c: 3 };
console.log(Object.values(obj).length);  // Output: undefined (Expected: 3)
```

**Runtime Implementation:** [TsObject.cpp:1148-1161]
```cpp
TsValue* ts_object_values(TsValue* obj) {
    if (!obj) return ts_value_make_array(TsArray::Create(0));
    
    void* rawPtr = ts_value_get_object(obj);
    if (!rawPtr) rawPtr = obj;
    
    uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);  // ← Check offset 16
    if (magic == 0x4D415053) { // TsMap::MAGIC
        return ts_value_make_array(ts_map_values(rawPtr));
    }
    return ts_value_make_array(TsArray::Create(0));  // ← Returns empty array
}
```

**Hypothesis:** The magic number check is reading from the wrong offset or the object isn't a TsMap.

### Investigation Steps

1. **Verify TsMap memory layout:**
   ```powershell
   # Check TsMap structure definition
   grep -A 20 "class TsMap" src/runtime/include/TsObject.h
   # Look for MAGIC constant definition
   grep -r "0x4D415053\|MAGIC.*TsMap" src/runtime/
   ```

2. **Check where TsMap::magic is stored:**
   - Compare with Object.keys() implementation (which works!)
   - File: `TsObject.cpp` line ~1130 (Object.keys)
   - What offset does Object.keys use?

3. **Manual test with debug output:**
   ```cpp
   // Add to ts_object_values:
   std::printf("[DEBUG] rawPtr=%p\n", rawPtr);
   std::printf("[DEBUG] magic at +0 = %x\n", *(uint32_t*)rawPtr);
   std::printf("[DEBUG] magic at +8 = %x\n", *(uint32_t*)((char*)rawPtr + 8));
   std::printf("[DEBUG] magic at +16 = %x\n", *(uint32_t*)((char*)rawPtr + 16));
   std::printf("[DEBUG] magic at +24 = %x\n", *(uint32_t*)((char*)rawPtr + 24));
   ```

4. **Check if unboxing is the issue:**
   - Maybe `ts_value_get_object()` isn't unboxing correctly
   - Test: Is `rawPtr` actually pointing to a TsMap?

### Fix Strategy

**Most Likely Fix:** Correct the magic number offset
- Object.entries uses offset 24: `*(uint32_t*)((char*)rawPtr + 24)`
- Object.values uses offset 16: `*(uint32_t*)((char*)rawPtr + 16)`
- **Try changing offset 16 → 24 to match Object.entries**

### Implementation Tasks

- [ ] **Task 2.1:** Check Object.keys() implementation
  - File: `TsObject.cpp` line ~1130
  - Does it work? What offset does it use?

- [ ] **Task 2.2:** Compare all three: keys, values, entries
  - keys: line ~1130
  - values: line ~1148
  - entries: line ~1163
  - **Find pattern:** Which offset works?

- [ ] **Task 2.3:** Test with TypeScript (known to work)
  - File: `tests/golden_ir/typescript/objects/object_values.ts`
  - Does TypeScript Object.values work?
  - If yes, why does JS version fail?

- [ ] **Task 2.4:** Apply fix
  - Change magic offset from 16 to 24 (or correct value)
  - Build runtime: `cmake --build build --config Release --target tsruntime`

- [ ] **Task 2.5:** Verify with manual test
  ```javascript
  var obj = { a: 1, b: 2, c: 3 };
  console.log(Object.values(obj).length);
  ```

### Success Criteria

```javascript
// Test: object_values_any.js
var obj = { a: 1, b: 2, c: 3 };
console.log(Object.values(obj).length);  // Output: 3 ✓
```

**Expected IR:** (No change needed - issue is runtime-only)

---

## Phase 3: Fix UMD Pattern (module.exports + IIFE)

**Priority:** LOW (Edge case)  
**Estimated Time:** 3-5 hours  
**Risk:** HIGH - Complex interaction between modules and IIFEs  
**Dependencies:** Phase 1 may help (ctx passing)

### Issue Analysis

**Current Behavior:**
```javascript
var myLib = (function(global, factory) {
    if (typeof module === 'object') {
        module.exports = factory();  // ← This branch executes
    } else {
        global.myLib = factory();
    }
})(this, function() {
    return { version: 42 };  // ← This returns correctly
});
console.log(myLib.version);  // Output: undefined (Expected: 42)
```

**Root Cause:** The IIFE returns `undefined` instead of the factory result. This is a complex interaction between:
1. `.call(this)` invocation (IIFE with explicit `this`)
2. `module.exports` assignment inside the IIFE
3. IIFE return value capture

### Investigation Steps

1. **Check previous `.call(this)` bug fixes:**
   - Read: `docs/call_this_investigation.md`
   - Check: Was this specific pattern tested?
   - Files: `src/compiler/analysis/Monomorphizer.cpp`

2. **Trace IIFE compilation:**
   ```powershell
   # Compile with IR dump
   .\build\src\compiler\Release\ts-aot.exe tests\golden_ir\javascript\closures\umd_pattern.js --dump-ir > umd_ir.txt
   
   # Search for return value handling
   grep -A 5 "ret.*version\|module.exports" umd_ir.txt
   ```

3. **Check module.exports handling:**
   - File: `src/runtime/src/TsObject.cpp` (line ~2167: module.exports)
   - Does assignment to `module.exports` affect IIFE return?

4. **Simplify the test:**
   ```javascript
   // Minimal repro:
   var result = (function() {
       return { version: 42 };
   })();
   console.log(result.version);  // Does this work?
   
   // Add module.exports:
   var result2 = (function() {
       module.exports = { version: 42 };
       return { version: 42 };
   })();
   console.log(result2.version);  // Does module.exports break it?
   ```

### Fix Strategy

**Option A: Module wrapper fix**
- IIFEs in JavaScript modules may be wrapped differently
- Check: Does the wrapper capture IIFE return values?
- File: `src/compiler/analysis/Monomorphizer.cpp` (line ~174)

**Option B: Return value propagation**
- Ensure IIFE return values are stored and used
- Check: Is there a missing `store` instruction in IR?
- File: `src/compiler/codegen/IRGenerator_Functions.cpp`

**Option C: Mark as "Won't Fix" (Edge Case)**
- UMD pattern is rare in modern JavaScript (mostly for lodash)
- Document limitation and move on
- Focus effort on Phases 1 & 2

### Implementation Tasks

- [ ] **Task 3.1:** Create minimal repro
  - Test 1: Simple IIFE return
  - Test 2: IIFE with `module.exports`
  - Test 3: IIFE with `.call(this)`
  - Test 4: Full UMD pattern
  - **Goal:** Find which part breaks

- [ ] **Task 3.2:** Check previous `.call(this)` fix
  - Read: commit history for "call this" fixes
  - Check: `docs/call_this_investigation.md`
  - Verify: Does lodash work now?

- [ ] **Task 3.3:** Examine IR for return value
  - Look for: `store` instruction for IIFE result
  - Check: Is return value captured?
  - Compare: Working IIFE vs UMD pattern IR

- [ ] **Task 3.4:** Decide: Fix vs Document
  - If fix is < 2 hours → proceed
  - If fix is > 3 hours → document as "Known Limitation"
  - UMD is legacy pattern, low priority

- [ ] **Task 3.5:** Implement fix (if proceeding)
  - Based on root cause findings
  - Likely file: `Monomorphizer.cpp` or `IRGenerator_Functions.cpp`

### Success Criteria

```javascript
// Test: umd_pattern.js
var myLib = (function(global, factory) {
    if (typeof module === 'object') {
        module.exports = factory();
    } else {
        global.myLib = factory();
    }
})(this, function() {
    return { version: 42 };
});
console.log(myLib.version);  // Output: 42 ✓
```

**Alternative (Document as limitation):**
```javascript
// XFAIL: UMD pattern with module.exports inside IIFE
// KNOWN LIMITATION: Complex interaction between module system and IIFE return values
// Workaround: Use modern ES6 modules instead of UMD pattern
```

---

## Execution Order

### Recommended Sequence

1. **Start with Phase 2 (Object.values)** - Easiest, highest confidence
   - Quick win, likely just an offset change
   - ~1 hour to completion
   - **Result:** 28/30 passing (93%)

2. **Then Phase 1 (hasOwnProperty)** - Medium complexity, high value
   - Fixes a common method call pattern
   - May require codegen changes
   - ~2-3 hours to completion
   - **Result:** 29/30 passing (97%)

3. **Finally Phase 3 (UMD pattern)** - Evaluate first, then decide
   - **Decision point:** Is it worth 3-5 hours for 1 edge case test?
   - If NO → document as known limitation
   - If YES → deep dive into IIFE + module.exports
   - **Result:** 30/30 passing (100%) OR documented limitation

### Time Budget

| Phase | Estimated | Worst Case | Priority |
|-------|-----------|------------|----------|
| Phase 2 | 1-2 hours | 3 hours | HIGH |
| Phase 1 | 2-3 hours | 5 hours | HIGH |
| Phase 3 | 3-5 hours | 8 hours | LOW |
| **Total** | **6-10 hours** | **16 hours** | |

**Realistic Goal:** Phases 1 & 2 → 29/30 passing (97%)  
**Stretch Goal:** All phases → 30/30 passing (100%)

---

## Verification Process

After each phase:

1. **Build Runtime:**
   ```powershell
   cmake --build build --config Release --target tsruntime
   ```

2. **Run Specific Test:**
   ```powershell
   python runner.py javascript/property_access/has_property.js --details
   python runner.py javascript/property_access/object_values_any.js --details
   python runner.py javascript/closures/umd_pattern.js --details
   ```

3. **Run Full Test Suite:**
   ```powershell
   cd tests/golden_ir
   python runner.py .
   ```

4. **Verify No Regressions:**
   - All originally passing tests must still pass
   - No new XFAIL tests
   - No test timing regressions

5. **Commit After Each Phase:**
   ```powershell
   git add -A
   git commit -m "Phase X: Fix [test_name] - [brief description]"
   ```

---

## Success Metrics

### MVP Success (Phases 1 & 2)
- 29/30 JavaScript tests passing (97%)
- hasOwnProperty works correctly
- Object.values works correctly
- Zero regressions in other tests

### Full Success (All Phases)
- 30/30 JavaScript tests passing (100%)
- UMD pattern works correctly
- Comprehensive fix for IIFE + module.exports interaction

### Acceptable Outcome (Phase 3 skipped)
- 29/30 JavaScript tests passing (97%)
- UMD pattern documented as "Known Limitation"
- Focus shifted to TypeScript test improvements

---

## Risk Mitigation

### Phase 1 Risks (ctx parameter)
- **Risk:** Breaking other method calls
- **Mitigation:** Run full test suite after each change
- **Rollback:** Keep commits small and atomic

### Phase 2 Risks (magic number)
- **Risk:** Breaking Object.keys or Object.entries
- **Mitigation:** Test all three functions together
- **Rollback:** Trivial - just revert offset change

### Phase 3 Risks (UMD pattern)
- **Risk:** Deep changes to module system or IIFE codegen
- **Mitigation:** Time-box investigation to 3 hours
- **Rollback:** Mark as XFAIL and document limitation

---

## References

### Related Files
- `src/runtime/src/TsObject.cpp` - Runtime implementations
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Member.cpp` - Method calls
- `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp` - Built-in methods
- `src/compiler/analysis/Monomorphizer.cpp` - Module wrapping
- `docs/call_this_investigation.md` - Previous .call(this) debugging

### Related Tests
- `tests/golden_ir/javascript/property_access/has_property.js`
- `tests/golden_ir/javascript/property_access/object_values_any.js`
- `tests/golden_ir/javascript/closures/umd_pattern.js`
- `tests/golden_ir/typescript/objects/object_values.ts` (working version)

### Previous Work
- Phase 1-3 (completed): Fixed CHECK patterns, IIFE verification, documented limitations
- Commit b55baa4: Fixed property access CHECK patterns
- Commit a89b3a4: Verified IIFE tests
- Commit 22c9c59: Documented runtime limitations

---

## Next Steps

1. **User Approval:** Review this plan and approve execution order
2. **Time Allocation:** Decide on time budget (6-10 hours realistic)
3. **Start Phase 2:** Begin with Object.values (easiest, highest confidence)
4. **Progress Updates:** Report after each phase completion
5. **Decision Point:** After Phase 2, evaluate Phase 3 priority

**Ready to proceed?** 🚀
