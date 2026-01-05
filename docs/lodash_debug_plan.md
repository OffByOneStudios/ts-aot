# Lodash Debug Plan - Binary Search Approach

## Goal
Find exactly what pattern in lodash causes `runInContext()` to return undefined.

## Strategy
Progressive complexity testing - add lodash features incrementally until something breaks.

## Test Levels

### ✅ Level 0: Baseline (WORKING)
**File:** `examples/lodash_sim.js` (already created)
- Simple IIFE with `.call(this)`
- Minimal `runInContext()` returning inner function
- UMD export pattern
- **Status:** WORKS - exports function correctly

### Level 1: Add Variable Hoisting
**File:** `examples/lodash_test_1_hoisting.js`
**Features to test:**
- Multiple `var` declarations scattered throughout
- Forward references (use before declaration)
- Function hoisting
- Nested scopes

**Why:** JavaScript's `var` hoisting might be broken in slow-path compilation.

### Level 2: Add Closure Complexity
**File:** `examples/lodash_test_2_closures.js`
**Features to test:**
- `runInContext()` that captures external variables
- Multiple nested function scopes
- Returning functions that close over captured vars
- `this` binding in closures

**Why:** Lodash's `runInContext` returns a function that captures many variables from outer scope.

### Level 3: Add Object Factory Pattern
**File:** `examples/lodash_test_3_factory.js`
**Features to test:**
- Create object with many properties
- Assign functions as properties
- Return the object from `runInContext`
- Property access on returned object

**Why:** Lodash creates a massive lodash object with 300+ methods.

### Level 4: Add Lazy Initialization
**File:** `examples/lodash_test_4_lazy.js`
**Features to test:**
- Functions that aren't called immediately
- Delayed property assignment
- Conditional initialization
- Multiple execution paths

**Why:** Some lodash features are lazily initialized.

### Level 5: Add Prototype Chain
**File:** `examples/lodash_test_5_prototype.js`
**Features to test:**
- Constructor functions
- Prototype property assignment
- `new` operator
- `instanceof` checks

**Why:** Lodash uses prototypes for LodashWrapper.

### Level 6: Add Real Lodash Subset
**File:** `examples/lodash_test_6_subset.js`
**Features to test:**
- Extract first 100 lines of real lodash
- Extract `runInContext` preamble
- Extract first few utility functions (identity, noop)
- Keep full UMD wrapper

**Why:** Test with actual lodash code structure.

### Level 7: Gradually Add More Lodash Functions
**File:** `examples/lodash_test_7_more.js`
**Approach:**
- Start with working subset from Level 6
- Add functions in groups of 10-20
- Test after each addition
- When it breaks, narrow down to specific function

**Why:** Find the exact lodash function that breaks compilation.

### Level 8: Add Full lodash Module Wrapper
**File:** `examples/lodash_test_8_wrapper.js`
**Features to test:**
- Full module detection logic (freeGlobal, freeSelf, etc.)
- All the `typeof` checks
- AMD, CommonJS, and global export paths
- But with minimal `runInContext` body

**Why:** The module wrapper itself might have issues.

## Testing Protocol

For each level:

```powershell
# 1. Compile
.\build\src\compiler\Release\ts-aot.exe examples\lodash_test_N.js -o examples\lodash_test_N.exe 2>&1 | Tee-Object -FilePath lodash_test_N_compile.txt

# 2. Create test harness
# examples/test_lodash_N.ts:
const result = require('./lodash_test_N.js');
console.log('typeof result:', typeof result);
console.log('result:', result);

# 3. Compile harness
.\build\src\compiler\Release\ts-aot.exe examples\test_lodash_N.ts -o examples\test_lodash_N.exe

# 4. Run with tracing
$env:TS_DEBUG_LODASH = "1"
.\examples\test_lodash_N.exe 2>&1 | Tee-Object -FilePath lodash_test_N_run.txt

# 5. Check result
grep "val_type=" lodash_test_N_run.txt
# Expected: val_type=10 (FUNCTION_PTR)
# Broken: val_type=0 (UNDEFINED)

# 6. If broken, binary search within this level
#    - Comment out half the features
#    - Retest
#    - Repeat until minimal breaking case found
```

## Success Criteria

For each level to be considered "working":
- ✅ `val_type=10` in module.exports write trace
- ✅ `typeof result: function` in output
- ✅ Calling `result()` doesn't crash (if applicable)
- ✅ Properties accessible (if applicable)

## Break Analysis

When a level breaks:
1. **Save the IR:** `--dump-ir` for both working (N-1) and broken (N)
2. **Diff the IR:** Compare to find new patterns
3. **Isolate feature:** Comment out features one by one
4. **Minimal repro:** Create smallest possible breaking case
5. **Root cause:** Analyze codegen for that specific pattern

## Current Status

- ✅ **Level 0:** WORKING (lodash_sim.js) - BUT MAY NOT HAVE TESTED THOROUGHLY
- ❌ **Level 1:** BROKEN - Function returns undefined inside IIFE with `.call(this)`
- 🎯 **ROOT CAUSE FOUND:** `.call(this)` on IIFE breaks function return values!

## ROOT CAUSE DISCOVERED

**Bug:** When JavaScript file has pattern:
```javascript
;(function() {
  function returnsFunc() {
    function inner() { return 42; }
    return inner;  // ← This return value is LOST
  }
  var x = returnsFunc();  // ← x becomes undefined!
}.call(this));
```

**Evidence:**
- `test_func_return.js` (no IIFE): ✅ Returns work
- `lodash_test_minimal.js` (with `.call(this)`): ❌ Returns undefined
- Logs show function EXISTS but return value is undefined

**Impact:**
- Lodash uses `.call(this)` wrapper
- lodash's `runInContext()` returns function
- That return value is lost → `var _ = runInContext()` → `_` is undefined
- Export writes undefined: `val_type=0`

**Next Action:**
Find and fix how `.call(this)` is compiled in JavaScript slow-path.

## Next Steps

1. Create Level 1 test file (hoisting)
2. Run through testing protocol
3. If it works, proceed to Level 2
4. If it breaks, binary search within Level 1 to find minimal breaking pattern
5. Fix the identified issue
6. Continue to next level

---

*Goal: Find the FIRST pattern that breaks, then fix it. Repeat until full lodash works.*
