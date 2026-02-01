# HIR-002: Remaining XFAIL Test Fixes

**Status:** Complete
**Priority:** High
**Completed:** 2026-02-01

## Overview

Two XFAIL tests were preventing 100% HIR test coverage:
1. `spread_operator.ts` - Simple fix (1 line)
2. `abstract_class.ts` - Simpler fix than anticipated

---

## Issue 1: Spread Operator (FIXED)

**Test:** `tests/golden_hir/ast_to_hir/other/spread_operator.ts`
**Expected:** `[1, 2, 3, 4, 5]`
**Actual (before fix):** `[4, 5]`

### Root Cause

In `src/compiler/hir/ASTToHIR.cpp:2288`, the spread element handling discarded the return value:

```cpp
// BROKEN:
builder_.createCall("ts_array_concat", {arr, spreadArr}, HIRType::makeVoid());
// Result [1,2,3] discarded, arr still empty, then 4,5 pushed to empty array
```

`ts_array_concat()` returns a NEW array - it doesn't modify in-place like `push()`.

### Fix Applied

```cpp
// FIX - capture the returned array:
arr = builder_.createCall("ts_array_concat", {arr, spreadArr},
                          HIRType::makeArray(elemType, false));
```

---

## Issue 2: Abstract Class super() Calls (FIXED)

**Test:** `tests/golden_hir/ast_to_hir/classes/abstract_class.ts`
**Error (before fix):** Access violation (calling null function pointer)
**Expected Output:** `78.5`, `100`

### Root Cause

The super() call handling in `ASTToHIR.cpp` line 1807-1824 required `currentClass_->baseClass->constructor` to be non-null. When the base class (Shape, which is abstract) has no explicit constructor, the super() call would fall through to a generic handler that tried to call a null function pointer.

### Fix Applied

Modified the super() handling to treat super() as a no-op when the base class has no explicit constructor:

```cpp
if (superExpr && currentClass_ && currentClass_->baseClass) {
    if (currentClass_->baseClass->constructor) {
        // Base class has explicit constructor - call it with [this, ...args]
        std::vector<std::shared_ptr<HIRValue>> ctorArgs;
        auto thisVal = lookupVariable("this");
        if (thisVal) {
            ctorArgs.push_back(thisVal);
        } else {
            ctorArgs.push_back(builder_.createConstNull());
        }
        for (auto& arg : args) {
            ctorArgs.push_back(arg);
        }
        builder_.createCall(currentClass_->baseClass->constructor->name, ctorArgs, HIRType::makeVoid());
    }
    // If base class has no explicit constructor (e.g., abstract class),
    // super() is a no-op - just continue with the derived class constructor
    lastValue_ = builder_.createConstUndefined();
    return;
}
```

---

## Final Results

- [x] `tests/golden_hir/ast_to_hir/other/spread_operator.ts` passes (outputs 1,2,3,4,5)
- [x] `tests/golden_hir/ast_to_hir/classes/abstract_class.ts` passes (outputs 78.5, 100)
- [x] No regressions in existing tests (117 tests all passing)
- [x] HIR test suite at 100% pass rate (117/117)

---

## Related Documentation

- [EPIC-HIR-001-hir-infrastructure.md](../epics/EPIC-HIR-001-hir-infrastructure.md)
- `src/compiler/hir/ASTToHIR.cpp` - Main HIR lowering
- `src/runtime/src/TsArray.cpp` - Array runtime (ts_array_concat)
