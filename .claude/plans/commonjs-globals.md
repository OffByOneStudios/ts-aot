# Plan: Complete CommonJS Global Objects

## Overview

Complete the CommonJS global objects (`exports`, `module`, and full `require()`) to bring Global Objects coverage from 71% to 100%.

## Current State

- **Global Coverage**: 5/7 (71%)
- **Working**: `global`, `globalThis`, `__dirname`, `__filename`
- **Missing**: `exports` (❌), `module` (❌), `require()` (⚠️ partial)

The analyzer already defines these symbols (in Analyzer_Core.cpp), but codegen doesn't generate the proper runtime initialization.

## Problem Analysis

When a file is compiled, it needs:
1. A module-scoped `exports` object (initially empty `{}`)
2. A module-scoped `module` object with an `exports` property pointing to the same object
3. `exports` and `module.exports` must be the same reference initially

Currently:
- `__dirname` and `__filename` work because they're generated inline as string constants
- `exports` and `module` need to be actual objects created at module initialization

## Implementation Approach

### Option A: Per-Module Global Initialization (Recommended)

Create module-local variables for `exports` and `module` that are initialized at program start.

**Changes needed:**

1. **IRGenerator_Expressions_Access.cpp** - Handle `exports` and `module` identifier access
2. **IRGenerator.cpp** - Generate module initialization that creates these objects
3. **Runtime** - Already has object creation support via `ts_object_create()`

### Implementation Details

#### 1. Module Initialization (IRGenerator)

In the entry function (`ts_main` or `user_main`), generate:
```llvm
; Create exports object
%exports = call ptr @ts_object_create()

; Create module object
%module = call ptr @ts_object_create()

; Set module.exports = exports
call void @ts_object_set_property(ptr %module, ptr @"exports_key", ptr %exports)

; Store both in module-level globals
store ptr %exports, ptr @__ts_exports_ptr
store ptr %module, ptr @__ts_module_ptr
```

#### 2. Identifier Access (IRGenerator_Expressions_Access.cpp)

When accessing `exports`:
```cpp
if (node->name == "exports") {
    // Load from module-level global
    llvm::Value* exportsPtr = module->getOrInsertGlobal("__ts_exports_ptr", builder->getPtrTy());
    lastValue = builder->CreateLoad(builder->getPtrTy(), exportsPtr);
    return;
}

if (node->name == "module") {
    // Load from module-level global
    llvm::Value* modulePtr = module->getOrInsertGlobal("__ts_module_ptr", builder->getPtrTy());
    lastValue = builder->CreateLoad(builder->getPtrTy(), modulePtr);
    return;
}
```

#### 3. Entry Point Generation

Modify `generateEntryPoint()` or add a `generateModuleInit()` function to create the exports/module objects.

## Files to Modify

| File | Changes |
|------|---------|
| `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` | Handle `exports` and `module` identifier access |
| `src/compiler/codegen/IRGenerator.cpp` | Generate module initialization code |
| `docs/conformance/nodejs-features.md` | Update Global Objects to 100% |

## Test Plan

```typescript
// tests/node/global/commonjs_globals.ts
function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: exports exists and is an object
    if (typeof exports === 'object') {
        console.log("PASS: exports is an object");
        passed++;
    } else {
        console.log("FAIL: exports should be an object");
        failed++;
    }

    // Test 2: module exists and has exports property
    if (module && typeof module.exports !== 'undefined') {
        console.log("PASS: module.exports exists");
        passed++;
    } else {
        console.log("FAIL: module.exports should exist");
        failed++;
    }

    // Test 3: exports and module.exports are the same object
    if (exports === module.exports) {
        console.log("PASS: exports === module.exports");
        passed++;
    } else {
        console.log("FAIL: exports and module.exports should be the same");
        failed++;
    }

    // Test 4: Can set property on exports
    exports.myValue = 42;
    if (exports.myValue === 42) {
        console.log("PASS: Can set property on exports");
        passed++;
    } else {
        console.log("FAIL: Should be able to set property on exports");
        failed++;
    }

    // Test 5: Setting on exports reflects in module.exports
    if (module.exports.myValue === 42) {
        console.log("PASS: exports change reflects in module.exports");
        passed++;
    } else {
        console.log("FAIL: exports change should reflect in module.exports");
        failed++;
    }

    // Test 6: Can reassign module.exports
    module.exports = { newProp: "hello" };
    if (module.exports.newProp === "hello") {
        console.log("PASS: Can reassign module.exports");
        passed++;
    } else {
        console.log("FAIL: Should be able to reassign module.exports");
        failed++;
    }

    console.log("\n=== Results: " + passed + " passed, " + failed + " failed ===");
    return failed > 0 ? 1 : 0;
}
```

## Conformance Matrix Update

After implementation:
```markdown
## Global Objects

| Feature | Status | Notes |
|---------|--------|-------|
| `global` | ✅ | |
| `globalThis` | ✅ | ES2020 alias for global |
| `__dirname` | ✅ | Absolute path to directory |
| `__filename` | ✅ | Absolute path to file |
| `exports` | ✅ | CommonJS exports object |
| `module` | ✅ | CommonJS module object with exports property |
| `require()` | ✅ | Module loading (compile-time resolution) |

**Global Coverage: 7/7 (100%)**
```

## Implementation Steps

1. [ ] Add module-level globals (`__ts_exports_ptr`, `__ts_module_ptr`) in IRGenerator
2. [ ] Generate initialization code in entry function to create exports/module objects
3. [ ] Handle `exports` identifier access in IRGenerator_Expressions_Access.cpp
4. [ ] Handle `module` identifier access in IRGenerator_Expressions_Access.cpp
5. [ ] Create test file
6. [ ] Update conformance matrix
7. [ ] Verify require() works fully (update ⚠️ to ✅ if tests pass)

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Multi-module complexity | Medium | Start with single-file; multi-module already has import/export |
| Reference equality | Low | Create exports once, share between exports and module.exports |
| Breaking existing code | Low | New functionality, existing code unaffected |

## Estimated Changes

- ~50 LOC in IRGenerator_Expressions_Access.cpp
- ~30 LOC in IRGenerator.cpp for initialization
- ~80 LOC test file
- Total: ~160 LOC
