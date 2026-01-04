# Issue: JSON Imports Return Null Instead of Object

**Status:** Open  
**Priority:** MEDIUM  
**Created:** 2026-01-04  
**Category:** Compiler Bug - Module Loading  
**Affects:** JSON module imports

---

## Problem Statement

TypeScript JSON imports using `import config from './config.json'` compile successfully but return `null` at runtime instead of the parsed JSON object. Accessing properties on the null value causes runtime panic.

---

## Minimal Reproduction

**File:** `examples/config.json`
```json
{
    "name": "ts-aot",
    "version": "1.0.0",
    "debug": false
}
```

**Test:** `examples/test_json_import_minimal.ts`
```typescript
import config from './config.json';
console.log("Config loaded:", typeof config);
console.log("Config is null?", config === null);
console.log("Config is undefined?", config === undefined);
if (config) {
    console.log("Config.name:", config.name);
}
```

**Runtime Output:**
```
Config loaded: undefined
Config is null? true
Config is undefined? false
```

**Expected Output:**
```
Config loaded: object
Config is null? false
Config is undefined? false
Config.name: ts-aot
```

---

## Scope of Impact

**Affected:**
- json_import_test.ts ❌ - Crashes on property access
- test_json_import_minimal.ts ❌ - Returns null
- Any TypeScript code using JSON imports

**Not Affected:**
- JSON.parse() runtime function (works correctly)
- Regular module imports (work correctly)
- Inline IR operations (unrelated to module loading)

---

## Technical Analysis

### Expected Behavior
TypeScript JSON imports should:
1. Read JSON file at compile time
2. Parse JSON into object structure
3. Embed object data in compiled binary
4. Return object reference at runtime

### Current Behavior
1. Compilation succeeds (no errors)
2. JSON file is found (exists in examples/)
3. Runtime returns `null` instead of object
4. Property access on `null` causes panic

### Root Cause Hypothesis
The JSON import codegen is likely:
- Not embedding the JSON data correctly
- Returning `nullptr` from the import
- Not calling JSON parsing/construction code
- Missing module initialization

---

## Investigation Steps

### 1. Check JSON Import Codegen
```powershell
# Find JSON import handling
grep -r "json" src/compiler/codegen/*.cpp
grep -r "import.*json" src/compiler/*.cpp
```

### 2. Verify JSON File Loading
Check if compiler reads config.json:
```powershell
# Add debug logging to see if file is loaded
# Check AST dump for import statement
build\src\compiler\Release\ts-aot.exe examples\test_json_import_minimal.ts --dump-ast 2>&1 | Select-String "import|config"
```

### 3. Check Generated IR
```powershell
# See what IR is generated for JSON import
build\src\compiler\Release\ts-aot.exe examples\test_json_import_minimal.ts --dump-ir 2>&1 | Select-String "config"
```

Look for:
- Global variable initialization for `config`
- JSON parsing call
- Object construction code

### 4. Compare with Working Imports
Check how regular module imports work vs JSON imports.

### 5. Git History
```powershell
# When was JSON import added?
git log --all --oneline -- src/compiler/codegen/*json*
git log --all -p -S "json import"
```

---

## Possible Fixes

### Option 1: Fix JSON Embedding
Ensure JSON data is properly embedded in binary:
```cpp
// Generate global constant for JSON data
llvm::Constant* jsonData = createJsonConstant(parsedJson);
llvm::GlobalVariable* configVar = new llvm::GlobalVariable(
    *module, jsonObjectType, false,
    llvm::GlobalValue::InternalLinkage,
    jsonData, "config"
);
```

### Option 2: Fix JSON Parsing at Compile Time
Parse JSON and create object structure:
```cpp
// At compile time
auto jsonObj = parseJsonFile("config.json");
auto llvmObj = generateObjectConstant(jsonObj);
```

### Option 3: Fix Module Resolution
Ensure JSON import resolver returns correct module:
```cpp
// In module resolver
if (importPath.ends_with(".json")) {
    return loadJsonModule(importPath);  // Must return valid object
}
```

---

## Test Cases to Add

After fix, verify:
```typescript
// Test 1: Basic import
import config from './config.json';
console.assert(config !== null);
console.assert(typeof config === "object");

// Test 2: Property access
console.assert(config.name === "ts-aot");
console.assert(config.version === "1.0.0");

// Test 3: Nested objects
console.assert(config.settings.verbose === true);

// Test 4: Arrays
console.assert(config.features.length === 3);
console.assert(config.features[0] === "codegen");
```

---

## Priority Justification

**MEDIUM PRIORITY** because:
1. JSON imports are useful but not critical feature
2. Workaround exists: use `fs.readFileSync()` + `JSON.parse()`
3. Affects specific use case (compile-time JSON embedding)
4. Not blocking other development
5. Pre-existing issue, not regression

---

## Workaround

Until fixed, use runtime JSON loading:
```typescript
// Instead of:
import config from './config.json';

// Use:
import * as fs from 'fs';
const configData = fs.readFileSync('./config.json', 'utf8');
const config = JSON.parse(configData);
```

---

## Success Criteria

- ✅ `test_json_import_minimal.ts` returns object (not null)
- ✅ `json_import_test.ts` runs without panic
- ✅ Property access works: `config.name` returns string
- ✅ Nested objects work: `config.settings.verbose` returns boolean
- ✅ Arrays work: `config.features[0]` returns string
- ✅ Type checking works: `typeof config === "object"`

---

## Related Issues

None - this is isolated to JSON import feature.

---

## References

- Git commit 95d9988: "Implement JSON imports (compile-time embedding)"
- Test file: `examples/test_json_import_minimal.ts`
- JSON file: `examples/config.json`
- Investigation results: `docs/phase19/CRASH_INVESTIGATION_RESULTS.md`
