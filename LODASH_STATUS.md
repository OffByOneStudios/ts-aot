# Lodash Import Status

## Fixed Issues ✅

### 1. Property Assignment on Function-Typed Expressions
**Symptom:** Combined assignment `(func.prop = val).other = x` didn't generate stores for the outer assignment.

**Root Cause:** In `IRGenerator_Expressions_Binary.cpp`, the `exprIsAny` check only handled `TypeKind::Any` and null types. Function-typed base expressions fell through all branches and hit the error case.

**Fix:** Extended `exprIsDynamic` to include `TypeKind::Function`, routing these through `emitInlineObjectSetProp()` just like `Any` and `Object` types.

**File:** `src/compiler/codegen/IRGenerator_Expressions_Binary.cpp` (lines ~935-967)

**Validation:** test_umdsim.ts now correctly generates stores for both assignments in `(freeModule.exports = _)._ = _;`

### 2. Property Access on Function Objects  
**Symptom:** Reading properties from function objects (like `result._`) always returned `undefined`.

**Root Cause:** `ts_object_get_dynamic()` only checked for `TsMap::MAGIC`, not `TsFunction::MAGIC`. When given a function object, it failed the magic check and returned undefined.

**Fix:** Added TsFunction handling to `ts_object_get_dynamic()`:
```cpp
if (magic16 == TsFunction::MAGIC) {
    TsFunction* func = (TsFunction*)rawObj;
    if (!func->properties) {
        return ts_value_make_undefined();
    }
    TsValue result = func->properties->Get(*key);
    // ... return boxed result
}
```

**File:** `src/runtime/src/TsObject.cpp` (lines ~1473-1490)

**Validation:** 
- test_umdsim.ts: `typeof exports._: function` ✅
- test_lodash_pattern.ts: `typeof result._: function` ✅  
- test_func_return.ts: Function return values work ✅

## Remaining Issues ❌

### Actual Lodash Still Exports UNDEFINED

**Symptom:**
```
[__ts_map_set_at] module.exports write: val_type=0 val_val=0000000000000000
```

The `_` variable (result of `runInContext()`) is UNDEFINED when the export happens at line 17201.

**Evidence:**
- test_umdsim.ts: ✅ Simple UMD pattern works (`val_type=10`)
- lodash_sim.js: ✅ Minimal lodash structure works  
- test_func_return.js: ✅ Function returns work
- actual lodash: ❌ Still writes UNDEFINED (`val_type=0`)

**Hypothesis:**
Lodash's `runInContext()` (massive function spanning thousands of lines) doesn't execute correctly in JavaScript "slow path" compilation. Possible causes:
- IIFE with `.call(this)` not handled correctly
- Closure capture issues in runInContext
- Variable hoisting not working for `var _ = runInContext()`
- JavaScript module initialization order issue

**Next Steps:**
1. Add tracing to lodash.js `runInContext()` to see if it executes
2. Check Monomorphizer.cpp JavaScript wrapper generation
3. Compare IR for working lodash_sim vs broken lodash
4. Debug why `var _ = runInContext()` results in undefined value

## Test Files

- ✅ **examples/test_umdsim.ts** - Minimal UMD pattern (WORKING)
- ✅ **examples/lodash_sim.js** - Simple lodash structure (WORKING)
- ✅ **examples/test_func_return.ts** - Function returns (WORKING)
- ❌ **examples/test_lodash_import.ts** - Real lodash (BROKEN)

## Key Runtime Functions

- `__ts_object_get_map(obj)` - Get properties map, handles TsMap and TsFunction
- `ts_object_get_dynamic(obj, key)` - Read property, now handles functions
- `__ts_map_set_at(map, key, val)` - Write property, traces module.exports writes

## Debugging Tools

```powershell
# Runtime trace for module.exports writes
$env:TS_DEBUG_LODASH = "1"

# Compile with IR dump
.\build\src\compiler\Release\ts-aot.exe file.ts --dump-ir -o file.exe 2>&1 | Out-File ir.txt

# Compile with type dump  
.\build\src\compiler\Release\ts-aot.exe file.ts --dump-types -o file.exe

# Check exports type
grep "module.exports write" output.txt
```

## Commit History

1. **Extended property assignment dispatch** - Added Function type handling to `visitAssignmentExpression`
2. **Enabled function property access** - Added TsFunction case to `ts_object_get_dynamic`

---

*Last updated: Current session*
*Status: Core fixes complete, lodash specific issue remains*
