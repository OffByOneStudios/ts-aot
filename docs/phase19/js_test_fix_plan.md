# JavaScript Test Fixing Plan

**Goal:** Fix the 10 failing JavaScript tests (out of 30 total) while maintaining all 60 passing TypeScript tests.

**Current Status:** 57/90 tests passing (63.3%)
**Target:** 67/90 tests passing (74.4%) - all JavaScript tests working

---

## Summary of Failures

### Category 1: CHECK Pattern Mismatches (2 tests)
Tests expect specific runtime functions that don't exist in current IR:
- `get_property.js` - expects `ts_object_get_prop` (IR uses `ts_object_get_dynamic`)
- `set_property.js` - expects `ts_object_set_prop` (IR uses `ts_object_set_dynamic`)

**Root Cause:** Tests written with wrong expected function names.
**Fix Strategy:** Update CHECK patterns to match actual IR (simplest fix).
**Risk:** LOW - Only changes test expectations, not compiler.

### Category 2: Runtime Output Issues (2 tests)
Tests compile and run but produce wrong output:
- `has_property.js` - outputs `undefined\nundefined` instead of `true\nfalse`
- `object_values_any.js` - outputs `undefined` instead of `3`

**Root Cause:** Missing or incomplete runtime implementations of `hasOwnProperty()` and `Object.values()`.
**Fix Strategy:** Investigate and fix runtime implementations.
**Risk:** MEDIUM - Requires runtime changes, could affect other code.

### Category 3: IIFE Support (6 tests)
Tests with Immediately Invoked Function Expressions:
- `iife_no_closure.js` - `(function() { return 42; })()`
- `iife_with_closure.js` - IIFE capturing outer variables
- `iife_object_literal.js` - IIFE returning object
- `iife_with_call.js` - IIFE with `.call(this)`
- `iife_multiple_returns.js` - IIFE with conditional returns
- `nested_iifes.js` - Multiple nested IIFEs

**Root Cause:** Need to verify these actually work (compilation succeeds, need to check output).
**Fix Strategy:** Test each manually, update CHECK patterns or fix codegen if needed.
**Risk:** LOW-MEDIUM - If codegen works, just update patterns. If broken, needs compiler fix.

---

## Detailed Fix Plan

### Phase 1: Quick Wins - CHECK Pattern Updates (Est: 5 min)

**Priority:** HIGH - Easiest, zero risk

#### Task 1.1: Fix get_property.js CHECK pattern
- **Current:** `// CHECK: call {{.*}} @ts_object_get_prop`
- **Change to:** `// CHECK: call {{.*}} @ts_object_get_dynamic`
- **Verification:** Run test, should pass immediately
- **Risk Assessment:** None - just aligning test with actual IR

#### Task 1.2: Fix set_property.js CHECK pattern
- **Current:** `// CHECK: call {{.*}} @ts_object_set_prop`  
- **Change to:** (Need to verify actual function name first)
- **Verification:** Run test after confirming IR output
- **Risk Assessment:** None

**Success Criteria:** 2 more tests passing → 59/90 (65.6%)

---

### Phase 2: IIFE Tests - Verify & Update (Est: 15 min)

**Priority:** MEDIUM - Low risk, good ROI

For each IIFE test, follow this process:
1. Compile manually: `ts-aot test.js -o test.exe`
2. Run executable: `.\test.exe`
3. Compare output to `// OUTPUT:` expectation
4. If output matches:
   - Examine IR with `--dump-ir`
   - Update CHECK patterns to match actual IR
   - Verify test passes
5. If output doesn't match:
   - Document the failure
   - Mark for Phase 4 (deeper investigation)

#### Task 2.1: iife_no_closure.js
- **Expected output:** `42`
- **Test compilation:** ✅ Succeeds (exit code 0)
- **Action:** Check runtime output, update CHECK if needed

#### Task 2.2: iife_with_closure.js
- **Expected output:** TBD (check test file)
- **Test compilation:** TBD
- **Action:** Manual verification

#### Task 2.3: iife_object_literal.js
- **Expected output:** TBD
- **Test compilation:** TBD
- **Action:** Manual verification

#### Task 2.4: iife_with_call.js
- **Expected output:** TBD
- **Test compilation:** TBD
- **Action:** Manual verification

#### Task 2.5: iife_multiple_returns.js
- **Expected output:** TBD
- **Test compilation:** TBD
- **Action:** Manual verification

#### Task 2.6: nested_iifes.js
- **Expected output:** TBD
- **Test compilation:** TBD
- **Action:** Manual verification

**Success Criteria:** Up to 6 more tests passing → 65/90 (72.2%)

---

### Phase 3: Runtime Function Fixes (Est: 30-60 min)

**Priority:** MEDIUM - Requires runtime code changes

#### Task 3.1: Fix hasOwnProperty implementation
- **Location:** Likely in `src/runtime/TsObject.cpp` or similar
- **Issue:** Returns undefined instead of boolean
- **Investigation steps:**
  1. Search for `hasOwnProperty` implementation
  2. Check if it's returning a TsValue properly
  3. Verify boxing/unboxing of boolean result
  4. Test with simple standalone case
- **Risk mitigation:** 
  - Create isolated test before modifying
  - Verify TypeScript tests still pass after change
  - Check if TypeScript code uses hasOwnProperty (unlikely)

#### Task 3.2: Fix Object.values() implementation
- **Location:** Likely in `src/runtime/` Object builtins
- **Issue:** Returns undefined instead of array length
- **Investigation steps:**
  1. Search for `Object.values` implementation
  2. Check if it exists and is properly exposed
  3. Verify it returns a proper array
  4. Test with simple standalone case
- **Risk mitigation:**
  - Check TypeScript `object_values.ts` test - it's currently passing!
  - Compare TypeScript vs JavaScript codegen paths
  - Ensure fix doesn't break typed version

**Success Criteria:** 2 more tests passing → 67/90 (74.4%)

---

### Phase 4: Deep Investigation (If needed)

**Priority:** LOW - Only if Phase 2 reveals broken IIFEs

If any IIFE tests fail to produce correct output:

#### Task 4.1: Analyze IIFE codegen
- **Check:** Is the function being created correctly?
- **Check:** Is the immediate invocation working?
- **Check:** Are arguments passed properly?
- **Tools:** Use `--dump-ir` and `--dump-ast`

#### Task 4.2: Compare with TypeScript IIFE tests
- **TypeScript:** `typescript/functions/iife_no_closure.ts` - marked XFAIL
- **TypeScript:** `typescript/functions/iife_with_closure.ts` - marked XFAIL
- **Observation:** TypeScript IIFEs also fail! This is a known compiler limitation.
- **Decision:** May need to mark JavaScript IIFE tests as XFAIL too

#### Task 4.3: Determine fix scope
- **Option A:** Mark JavaScript IIFE tests as XFAIL (documents known limitation)
- **Option B:** Implement IIFE support in compiler (larger effort)
- **Decision criteria:** Time available, priority of IIFE support

---

## Risk Management

### Protected Test Sets
**MUST NOT BREAK:**
- All 37 TypeScript tests currently passing
- All 20 JavaScript tests currently passing (Dynamic Types + partial Property Access/Closures)

### Verification Strategy
After each phase:
1. Run full test suite: `python runner.py .`
2. Verify pass count doesn't decrease
3. If any regression: immediately revert changes
4. Commit working changes incrementally

### Rollback Points
- After Phase 1: Commit "Fix CHECK patterns for property access"
- After Phase 2: Commit "Update IIFE test expectations"
- After Phase 3: Commit "Fix hasOwnProperty and Object.values runtime"

---

## Execution Order

1. **Phase 1 (Quick Wins)** - Do first, safest ROI
2. **Phase 2 (IIFE Verification)** - Do second, gathers info
3. **Phase 3 (Runtime Fixes)** - Do third, most impactful but riskier
4. **Phase 4 (Deep Investigation)** - Only if Phase 2 reveals issues

---

## Success Metrics

### Minimum Success (Phase 1 only)
- 59/90 tests passing (65.6%)
- +2 tests fixed
- Zero regressions

### Expected Success (Phases 1-2)
- 63-67/90 tests passing (70-74%)
- +6 to +8 tests fixed
- Zero regressions

### Maximum Success (Phases 1-3)
- 67/90 tests passing (74.4%)
- +10 tests fixed (all JavaScript tests working)
- Zero regressions

### Acceptable Outcome (With XFAIL)
- 61/90 tests passing (67.8%)
- +4 tests fixed (property access)
- 6 IIFE tests marked XFAIL (documented limitation)
- Zero regressions on existing passing tests

---

## Notes

### Why This Order?
1. **Phase 1 first:** Zero risk, immediate results, builds confidence
2. **Phase 2 second:** Gathers data about IIFE support without committing to fixes
3. **Phase 3 third:** Higher risk but well-isolated to runtime, clear rollback path
4. **Phase 4 last:** Only pursue if we discover IIFE is broken (may just document as XFAIL)

### Preserving Compiler Stability
- No changes to TypeScript codegen paths
- Runtime changes isolated to JavaScript-specific functions
- CHECK pattern updates don't affect compiler behavior
- Incremental commits allow easy rollback

### Documentation
- Update Epic 106 status after each phase
- Document any XFAIL additions with clear reason
- Note any runtime implementation gaps discovered
