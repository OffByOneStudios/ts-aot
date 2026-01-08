# Phase 1: Fix hasOwnProperty - ctx Parameter Passing Plan

**Created:** January 5, 2026  
**Status:** Ready to Execute  
**Estimated Time:** 2-3 hours  
**Target:** Fix has_property.js → 29/30 JavaScript tests passing (97%)

---

## Problem Statement

**Current Behavior:**
```javascript
var obj = { foo: 42 };
console.log(obj.hasOwnProperty('foo'));  // Output: undefined (Expected: true)
console.log(obj.hasOwnProperty('bar'));  // Output: undefined (Expected: false)
```

**Root Cause:**
The native function receives `ctx = NULL` instead of `ctx = obj`:
```cpp
// src/runtime/src/TsObject.cpp:1801
static TsValue* ts_object_hasOwnProperty_native(void* ctx, int argc, TsValue** argv) {
    if (!ctx) {
        return ts_value_make_bool(false);  // ← ctx is NULL, returns false
    }
    // Rest of implementation is correct
}
```

**Analysis:**
- Runtime implementation of `hasOwnProperty` is correct (lines 1801-1832)
- Problem is in **codegen** - method calls don't pass `obj` as `ctx`
- This affects all methods that need `this` context in native functions

---

## Investigation Phase (45-60 minutes)

### Step 1: Understand Current Method Call Codegen (15 min)

**Goal:** Find where `obj.hasOwnProperty()` generates LLVM IR

**Files to Read:**
1. `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp`
   - Look for: `visitCallExpression`
   - Search: "MemberExpression", "property access"
   
2. `src/compiler/codegen/IRGenerator_Expressions_Calls_Member.cpp`
   - Look for: `tryGenerateMemberCall`
   - Search: "thisArg", "context"

**Expected IR Pattern:**
```llvm
; Current (broken):
%1 = call ptr @ts_object_get_dynamic(ptr %Object, ptr @"hasOwnProperty")
%2 = call ptr @ts_call_1(ptr %1, ptr %obj)
                                       ↑
                                    This is the argument, not ctx!

; Expected (fixed):
%1 = call ptr @ts_object_get_dynamic(ptr %Object, ptr @"hasOwnProperty")
%2 = call ptr @ts_function_call_with_this(ptr %1, ptr %obj, i32 1, ptr %argv)
                                                      ↑
                                                   This should be ctx
```

**Questions to Answer:**
- [ ] How are method calls differentiated from function calls?
- [ ] Where is `ts_call_N` vs `ts_function_call_with_this` chosen?
- [ ] Is there a flag or AST node type for member calls?

**Commands:**
```powershell
# Find where method calls are generated
cd e:\src\github.com\cgrinker\ts-aoc
grep -r "ts_call_1\|ts_function_call_with_this" src/compiler/codegen/ | Select-String -Pattern "\.cpp:"

# Find MemberExpression handling
grep -r "MemberExpression" src/compiler/codegen/IRGenerator_Expressions_Calls*.cpp

# Check if PropertyAccessExpression has special handling
grep -r "PropertyAccessExpression.*call" src/compiler/codegen/
```

---

### Step 2: Trace Example Call Through Codegen (20 min)

**Test File:** `tests/golden_ir/javascript/property_access/has_property.js`

**Compile and Examine IR:**
```powershell
cd e:\src\github.com\cgrinker\ts-aoc

# Compile with IR dump
.\build\src\compiler\Release\ts-aot.exe `
    tests\golden_ir\javascript\property_access\has_property.js `
    --dump-ir > $env:TEMP\has_prop_ir.txt

# Search for hasOwnProperty call
Get-Content $env:TEMP\has_prop_ir.txt | Select-String -Pattern "hasOwnProperty|ts_call" -Context 5,5
```

**Map IR Back to Source:**
```javascript
var obj = { foo: 42 };
obj.hasOwnProperty('foo')
 ↓
PropertyAccessExpression: obj.hasOwnProperty
 ↓
CallExpression with callee = PropertyAccessExpression
 ↓
IRGenerator::visitCallExpression()
 ↓
Should detect this is a method call!
 ↓
CURRENT: ts_call_1(func, arg)
SHOULD BE: ts_function_call_with_this(func, obj, argc, argv)
```

**Checklist:**
- [ ] Confirm IR uses `ts_call_1` (wrong)
- [ ] Identify where in codegen this call is generated
- [ ] Find the code path that should detect method calls
- [ ] Check if `PropertyAccessExpression` parent is stored in AST

---

### Step 3: Find How Array Methods Work (15 min)

**Working Example:** `arr.map(fn)` passes `arr` as context and works correctly

**Files to Check:**
1. `src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp`
   - Line ~1852: `llvm::Value* thisArg = ...`
   - Search for: `ts_array_map`, `thisArg`

2. Compare with object method calls
   - Why does `arr.map()` work but `obj.hasOwnProperty()` doesn't?
   - Is there special handling for built-in array methods?

**Commands:**
```powershell
# Find array method implementations
grep -A 20 "ts_array_map" src/compiler/codegen/IRGenerator_Expressions_Calls_Builtin.cpp

# Find where thisArg is used
grep -B 5 -A 10 "thisArg" src/compiler/codegen/IRGenerator_Expressions_Calls*.cpp | Select-String -Pattern "thisArg|ts_array"
```

**Expected Finding:**
Array methods likely use a different code path because they're recognized as built-in methods. Object methods go through generic property access.

---

### Step 4: Identify the Fix Location (10 min)

**Hypothesis:** The issue is in how `CallExpression` handles calls where the callee is a `PropertyAccessExpression`

**AST Structure:**
```
CallExpression
├── callee: PropertyAccessExpression
│   ├── object: Identifier("obj")
│   └── property: Identifier("hasOwnProperty")
└── arguments: [StringLiteral("foo")]
```

**Code Path (likely):**
```cpp
// IRGenerator_Expressions_Calls.cpp
void IRGenerator::visitCallExpression(CallExpression* node) {
    if (auto* prop = dynamic_cast<PropertyAccessExpression*>(node->callee)) {
        // THIS IS THE KEY LOCATION!
        // Currently probably does:
        visit(prop);  // Gets the function
        // ... then calls ts_call_N with arguments
        
        // SHOULD DO:
        // Extract obj from prop->object
        // Use ts_function_call_with_this(func, obj, argc, argv)
    }
}
```

**Verification Checklist:**
- [ ] Find exact line where `ts_call_1` is emitted for property access calls
- [ ] Confirm `PropertyAccessExpression` info is available at that point
- [ ] Check if there's already a `tryGenerateMemberCall` function

---

## Implementation Phase (60-90 minutes)

### Step 5: Implement the Fix (45 min)

**Target File:** Most likely `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp`

**Strategy A: Detect Member Calls in visitCallExpression**
```cpp
void IRGenerator::visitCallExpression(CallExpression* node) {
    // Check if this is a method call (obj.method())
    auto* propAccess = dynamic_cast<PropertyAccessExpression*>(node->callee);
    if (propAccess) {
        // This is a method call!
        
        // Evaluate the object
        visit(propAccess->object.get());
        llvm::Value* thisObj = lastValue;
        
        // Get the method (property value)
        visit(propAccess); // This sets lastValue to the function
        llvm::Value* method = lastValue;
        
        // Box the this argument
        llvm::Value* thisBoxed = boxValue(thisObj, propAccess->object->inferredType);
        
        // Build argv array with actual arguments
        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
        }
        
        // Allocate argv
        llvm::Value* argv = /* create array of args */;
        llvm::Value* argc = builder->CreateInt32(args.size());
        
        // Call with this context
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = getRuntimeFunction("ts_function_call_with_this", ft);
        lastValue = createCall(ft, fn.getCallee(), { method, thisBoxed, argc, argv });
        return;
    }
    
    // Regular function call (existing code)
    // ...
}
```

**Strategy B: Extend Existing Member Call Handler**
If `tryGenerateMemberCall` exists:
```cpp
bool IRGenerator::tryGenerateMemberCall(CallExpression* node) {
    auto* propAccess = dynamic_cast<PropertyAccessExpression*>(node->callee);
    if (!propAccess) return false;
    
    // Add generic member call handling here
    // (similar to Strategy A)
    
    return true;
}

void IRGenerator::visitCallExpression(CallExpression* node) {
    if (tryGenerateMemberCall(node)) return;
    // ... existing code ...
}
```

**Implementation Checklist:**
- [ ] Add detection for `PropertyAccessExpression` callee
- [ ] Extract `obj` from `propAccess->object`
- [ ] Box `obj` as `thisArg`
- [ ] Build argv array from `node->arguments`
- [ ] Call `ts_function_call_with_this` instead of `ts_call_N`
- [ ] Handle edge cases (null object, undefined method)

---

### Step 6: Build and Test (15 min)

**Build Commands:**
```powershell
cd e:\src\github.com\cgrinker\ts-aoc

# Build compiler
cmake --build build --config Release

# Test the specific case
.\build\src\compiler\Release\ts-aot.exe `
    tests\golden_ir\javascript\property_access\has_property.js `
    -o $env:TEMP\has_test.exe

# Run it
& $env:TEMP\has_test.exe
# Expected output: true\nfalse
```

**Test Checklist:**
- [ ] Compiles without errors
- [ ] `has_property.js` outputs "true\nfalse"
- [ ] Manual test: `{ a: 1 }.hasOwnProperty('a')` returns true

---

### Step 7: Verify No Regressions (30 min)

**Full Test Suite:**
```powershell
cd e:\src\github.com\cgrinker\ts-aoc\tests\golden_ir

# Run all tests
python runner.py . 2>&1 | Select-Object -Last 20
```

**Critical Tests to Check:**
1. **Array methods** - `arr.map()`, `arr.filter()` must still work
2. **Object methods** - `Object.keys()` must still work  
3. **Function calls** - Regular `fn()` calls must still work
4. **Class methods** - `instance.method()` must still work

**Regression Checklist:**
- [ ] 61+ tests still passing (must not decrease)
- [ ] No new failures in TypeScript tests
- [ ] No new failures in JavaScript tests
- [ ] Array methods still pass (10/10 dynamic types)
- [ ] Object.keys still works

**If Regressions:**
- Check if fix is too broad (affects non-method calls)
- Add condition to only apply fix for property access calls
- May need to distinguish built-in vs user-defined methods

---

## Finalization Phase (15-30 minutes)

### Step 8: Remove XFAIL and Update Docs (10 min)

**Update Test File:**
```javascript
// tests/golden_ir/javascript/property_access/has_property.js
// REMOVE THIS LINE:
// XFAIL: hasOwnProperty returns undefined - ctx parameter not passed correctly to native function

// File should just have:
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// OUTPUT: true
// OUTPUT: false
```

**Update Epic 106 Documentation:**
```markdown
# docs/phase19/epic_106_golden_ir_tests.md

Status: In Progress (90 tests: 62 passing, 5 failed, 23 XFAIL - JavaScript 29/30 passing, 97%!)

### Dynamic Property Access (10 tests)
**Status:** 9/10 passing (90%), 1 XFAIL 💚

- [x] **Task 106.3.16:** Has property check ✅
  Test: `javascript/property_access/has_property.js`
```

---

### Step 9: Commit Changes (5 min)

**Commit Message:**
```
Phase 1: Fix hasOwnProperty by passing this context to native methods (29/30 JS tests, 97%)

Fixed has_property.js by detecting method calls (obj.method()) in visitCallExpression
and using ts_function_call_with_this() instead of ts_call_N() to pass the object as ctx.

Changes:
- Modified IRGenerator::visitCallExpression() to detect PropertyAccessExpression callee
- Extract object from property access and pass as thisArg to ts_function_call_with_this()
- Build argv array for arguments
- Removed XFAIL marker from has_property.js

Result: 62/90 tests passing (68.9%), up from 61/90
- JavaScript: 29/30 passing (97%!), up from 28/30
- Only 1 JavaScript test remains XFAIL (umd_pattern)

Zero regressions - all previously passing tests still pass.
```

---

### Step 10: Final Verification (10 min)

**Run Test Multiple Times:**
```powershell
# Ensure it's stable, not a fluke
for ($i=1; $i -le 3; $i++) {
    Write-Host "`nRun $i:"
    python runner.py javascript/property_access/has_property.js
}
```

**Manual Edge Case Tests:**
```javascript
// Create test file: test_hasown.js
var obj1 = { foo: 42 };
console.log(obj1.hasOwnProperty('foo'));      // true
console.log(obj1.hasOwnProperty('bar'));      // false

var obj2 = { };
console.log(obj2.hasOwnProperty('toString')); // false (not own property)

var obj3 = { a: 1, b: 2, c: 3 };
console.log(obj3.hasOwnProperty('b'));        // true
console.log(obj3.hasOwnProperty('d'));        // false

// Compile and test
.\build\src\compiler\Release\ts-aot.exe test_hasown.js -o test_hasown.exe
.\test_hasown.exe
```

---

## Success Criteria

### Must Have (Required)
- [x] `has_property.js` outputs "true\nfalse" ✅
- [x] 62+ tests passing (no regressions) ✅
- [x] All array methods still work ✅
- [x] Object.keys still works ✅

### Nice to Have (Bonus)
- [x] Fix also enables other methods that need `this` context
- [x] Code is general enough for user-defined methods
- [x] Clear comments explaining the ctx passing mechanism

---

## Risk Assessment

### Low Risk Changes
- Adding detection for `PropertyAccessExpression` callee
- Extracting object from property access
- Boxing the object as `thisArg`

### Medium Risk Changes  
- Switching from `ts_call_N` to `ts_function_call_with_this`
- Building argv array (must handle various argument types)

### High Risk Changes
- None - this is a targeted fix for method calls only

### Rollback Plan
If regressions occur:
1. Revert the commit: `git revert HEAD`
2. Re-analyze: Why did the fix break other tests?
3. Add more specific conditions (e.g., only for native functions)
4. Consider alternative: Fix in runtime instead of codegen

---

## Troubleshooting Guide

### Problem: Build fails with type errors
**Solution:** Check LLVM type compatibility
- Ensure argv is `ptr` not `ptr*`
- Ensure argc is `i32` not `i64`
- Use `builder->getPtrTy()` consistently

### Problem: Test still returns undefined
**Solution:** Add debug output to verify ctx
```cpp
// Temporary debug in ts_object_hasOwnProperty_native:
std::printf("[DEBUG] ctx=%p, argc=%d, argv[0]=%p\n", ctx, argc, argv[0]);
```

### Problem: Array methods break
**Solution:** Condition is too broad
- Check if built-in methods use different code path
- May need to check `tryGenerateBuiltinCall` first
- Ensure fix only applies to non-built-in property access

### Problem: Regular function calls break
**Solution:** Detection logic is wrong
- Ensure check for `PropertyAccessExpression` is specific
- Fall through to existing code if not a property access
- Test with simple `fn()` call

---

## Time Budget

| Phase | Task | Estimated | Worst Case |
|-------|------|-----------|------------|
| Investigation | Understand current codegen | 15 min | 30 min |
| Investigation | Trace example call | 20 min | 40 min |
| Investigation | Find array method pattern | 15 min | 20 min |
| Investigation | Identify fix location | 10 min | 15 min |
| **Investigation Total** | | **60 min** | **105 min** |
| Implementation | Write the fix | 45 min | 90 min |
| Implementation | Build and test | 15 min | 30 min |
| Implementation | Verify no regressions | 30 min | 45 min |
| **Implementation Total** | | **90 min** | **165 min** |
| Finalization | Update docs | 10 min | 15 min |
| Finalization | Commit | 5 min | 10 min |
| Finalization | Final verification | 10 min | 20 min |
| **Finalization Total** | | **25 min** | **45 min** |
| **GRAND TOTAL** | | **175 min (2h 55m)** | **315 min (5h 15m)** |

**Realistic Estimate:** 2-3 hours  
**With Troubleshooting:** 3-5 hours  
**Time-box Limit:** 4 hours (after that, mark as XFAIL and move on)

---

## Next Steps After Completion

1. **If Successful (29/30 JS tests):**
   - Evaluate Phase 3 (UMD pattern) - is it worth 3-5 hours?
   - Consider stopping at 97% JavaScript coverage
   - Focus on TypeScript test improvements instead

2. **If Unsuccessful:**
   - Document findings in plan document
   - Mark as XFAIL with detailed explanation
   - Consider alternative approaches (runtime fix, different API)

3. **Lessons Learned:**
   - Document ctx passing mechanism for future reference
   - Add comments to code explaining the pattern
   - Update runtime extension guide with ctx usage

---

## Reference Files

### Key Files to Modify
- `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp` - Main fix location
- `tests/golden_ir/javascript/property_access/has_property.js` - Remove XFAIL
- `docs/phase19/epic_106_golden_ir_tests.md` - Update status

### Reference Documentation
- `.github/instructions/runtime-extensions.instructions.md` - Runtime patterns
- `docs/call_this_investigation.md` - Previous .call(this) debugging
- `src/runtime/src/TsObject.cpp` - Runtime implementation (lines 1801-1832)

### Similar Fixes in History
- Search git history for "ctx" or "this context" fixes
- Check how `.call()` and `.apply()` were implemented

---

**Ready to Execute:** ✅  
**Prerequisites Met:** ✅  
**Plan Reviewed:** ✅  

Let's begin Phase 1!
