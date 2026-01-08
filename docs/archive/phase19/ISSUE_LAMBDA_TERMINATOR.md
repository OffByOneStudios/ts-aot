# Issue: Lambda Functions Missing Terminators in LLVM IR

**Status:** ✅ FIXED  
**Priority:** HIGH  
**Created:** 2026-01-04  
**Fixed:** 2026-01-04
**Category:** Compiler Bug - IRGenerator  
**Blocks:** Lodash integration, arrow function tests

---

## Resolution

**Fix Applied:** Defer `currentReturnBB` insertion into function until actually needed.

**Root Cause:** 
- `currentReturnBB` was created and immediately added to the function at line 1007
- For arrow functions with expression bodies, the code directly returned without branching to `currentReturnBB`
- The unused block had no terminator and no predecessors, causing LLVM verification failure

**Solution:**
1. Create `currentReturnBB` without parent: `llvm::BasicBlock::Create(*context, "return")` (no function parameter)
2. Only insert block if code branches to it: `currentReturnBB->insertInto(function)`
3. Delete unused block if never inserted: `delete currentReturnBB`

**Files Changed:**
- `IRGenerator_Functions.cpp` line 1007: Defer block creation
- `IRGenerator_Functions.cpp` line 1095: Conditional insertion
- `IRGenerator_Expressions_Access.cpp`: Replace removed `ts_object_get_prop` with `ts_object_get_dynamic`
- `BoxingPolicy.cpp`: Update API registry

**Commit:** 5b7acb8

---

## Problem Statement (ORIGINAL)

Arrow functions and lambda expressions fail to compile with LLVM module verification error: "Basic Block in function 'lambda_X' does not have terminator!"

---

## Minimal Reproduction

```typescript
// test_lambda_minimal.ts
const arr = [1, 2, 3];
const result = arr.map(x => x * 2);
console.log("Result:", result);
```

**Compilation Output:**
```
[error] E:\src\github.com\cgrinker\ts-aoc\src\compiler\codegen\CodeGenerator.cpp:116 
LLVM Module verification failed: 
Basic Block in function 'lambda_0' does not have terminator!
```

---

## Scope of Impact

**All arrow functions are broken:**
- Simple arrow expressions: `x => x * 2` ❌
- Arrow function callbacks: `arr.map(x => ...)` ❌
- Arrow functions in tests: arrow_func.ts ❌
- Lodash patterns: test_lodash.ts ❌ (5 lambda functions fail)
- Lodash loading: lodash_load_only.ts ❌ (blocked - requires arrow functions internally)

**Evidence this is pre-existing:**
- Git commit e526aa5: "WIP: Debug lodash crash - ...lodash IIFE crashes on entry"
- Arrow functions have never worked in ts-aot
- This is a fundamental compiler bug, not a recent regression

---

## Technical Analysis

### Root Cause
IRGenerator fails to emit proper basic block terminators (CreateBr or CreateRet) when generating LLVM IR for lambda/arrow functions.

### LLVM Requirements
Every basic block in LLVM IR **must** have exactly one terminator instruction:
- `br` (unconditional branch)
- `br` with condition (conditional branch)
- `ret` (return)
- `unreachable`
- Other control flow terminators (switch, invoke, etc.)

### Current Behavior
The lambda codegen creates basic blocks but doesn't add terminators, violating LLVM's SSA invariants.

---

## Investigation Steps

### 1. Locate Lambda Codegen
```powershell
# Find lambda/arrow function generation code
grep -r "lambda" src/compiler/codegen/*.cpp
grep -r "arrow" src/compiler/codegen/*.cpp
```

**Expected files:**
- `IRGenerator_Functions.cpp` - Function/lambda codegen
- `IRGenerator_Expressions.cpp` - Arrow expression codegen
- `IRGenerator.h` - Lambda helper methods

### 2. Review Basic Block Creation
Look for code that:
- Creates new basic blocks for lambda bodies
- Should call `builder->CreateRet()` or `builder->CreateBr()`
- May be missing terminator emission after lambda body codegen

### 3. Check Similar Working Code
Compare with regular function codegen (which works):
```cpp
// Regular function (WORKING)
builder->CreateRet(returnValue);  // Has terminator ✅

// Lambda function (BROKEN)
// ??? Missing CreateRet or CreateBr ???  // No terminator ❌
```

### 4. Test Fix
After fixing, verify:
```powershell
build\src\compiler\Release\ts-aot.exe examples\test_lambda_minimal.ts -o examples\test_lambda_minimal.exe
```

Expected: Compilation succeeds, no terminator errors.

---

## Affected Tests

| Test | Status | Error |
|------|--------|-------|
| test_lambda_minimal.ts | ❌ Fails | lambda_0 missing terminator |
| arrow_func.ts | ❌ Fails | lambda_0 missing terminator |
| test_lodash.ts | ❌ Fails | lambda_1-5 missing terminators |
| lodash_load_only.ts | ❌ Crashes | Blocked by lambda issue |
| map_set_test.ts | ✅ Passes | No arrow functions used |
| test_inline_ops.ts | ✅ Passes | No arrow functions used |

---

## Recommended Fix Approach

1. **Locate the Lambda IR Generation Code**
   - Find where arrow functions are converted to LLVM functions
   - Identify the basic block creation for lambda bodies

2. **Add Terminator Emission**
   ```cpp
   // After generating lambda body, add:
   if (!currentBlock->getTerminator()) {
       if (hasReturnValue) {
           builder->CreateRet(returnValue);
       } else {
           builder->CreateRetVoid();
       }
   }
   ```

3. **Handle Edge Cases**
   - Lambda with explicit return: `x => { return x * 2; }`
   - Lambda with implicit return: `x => x * 2`
   - Lambda with no return: `x => { console.log(x); }`

4. **Test Incrementally**
   - Start with simplest case: `x => x * 2`
   - Then callbacks: `arr.map(x => x * 2)`
   - Finally complex: lodash patterns

---

## Priority Justification

**HIGH PRIORITY** because:
1. Blocks all arrow function usage (common TypeScript pattern)
2. Prevents lodash integration (critical library)
3. Is a fundamental compiler bug, not edge case
4. Has clear fix path (add missing terminators)
5. Affects many tests and use cases

---

## Success Criteria

- ✅ `test_lambda_minimal.ts` compiles and runs
- ✅ `arrow_func.ts` compiles and runs
- ✅ `test_lodash.ts` compiles (may still have other issues)
- ✅ Array methods work: `arr.map()`, `arr.filter()`, `arr.reduce()`
- ✅ No LLVM verification errors for lambda functions

---

## Related Issues

- Lodash loading crash (blocked by this issue)
- Arrow function tests (blocked by this issue)

---

## References

- LLVM IR Language Reference: https://llvm.org/docs/LangRef.html#terminators
- Test file: `examples/test_lambda_minimal.ts`
- Investigation results: `docs/phase19/CRASH_INVESTIGATION_RESULTS.md`
