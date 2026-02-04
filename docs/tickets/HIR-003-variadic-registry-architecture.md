# HIR-003: Registry-Driven Variadic Function Handling

**Status:** Planning Complete
**Created:** 2026-02-04
**Priority:** High

---

## Problem Statement

Current variadic function handling has architectural issues:

1. **Hardcoded string checks** in ASTToHIR.cpp for console functions
2. **Hardcoded string checks** in HIRToLLVM.cpp for type-dispatch
3. **No single source of truth** for how variadic arguments should be handled
4. **Information doesn't flow** cleanly from definition to codegen

### Current Code Smells

**ASTToHIR.cpp (line ~2606):**
```cpp
// BAD: Hardcoded function names
bool isConsoleFunctionWithSpecialHandling =
    classNameIdent->name == "console" &&
    (propAccess->name == "log" || propAccess->name == "error" ||
     propAccess->name == "warn" || propAccess->name == "info" ||
     propAccess->name == "debug");
```

**HIRToLLVM.cpp (line ~2727):**
```cpp
// BAD: Hardcoded function names
if (funcName == "ts_console_log" || funcName == "ts_console_error" ||
    funcName == "ts_console_warn" || funcName == "ts_console_info" ||
    funcName == "ts_console_debug") {
    // type dispatch logic
}
```

---

## Architecture Principle

**LoweringRegistry is the single source of truth for LLVM-level call behavior.**

### Boundary Rules

| Source | Contains | Location |
|--------|----------|----------|
| ECMA core (console, Array, Math, JSON, Object) | `LoweringRegistry::registerBuiltins()` | `src/compiler/hir/` |
| Node.js APIs (util, fs, path, http) | Extension JSON files | `extensions/` |

Both sources feed into LoweringRegistry. Codegen queries only LoweringRegistry.

---

## Design

### New Enum: VariadicHandling

```cpp
// In LoweringRegistry.h
enum class VariadicHandling {
    None,           // Fixed args, no special handling
    PackArray,      // Pack rest args into TsArray before call
    TypeDispatch,   // Emit type-specific calls (ts_console_log_int, etc.)
    Inline          // Handled inline in HIRToLLVM (Math.max, etc.)
};
```

### Extended LoweringSpec

```cpp
struct LoweringSpec {
    std::string runtimeFuncName;
    LLVMTypeFactory returnType;
    std::vector<LLVMTypeFactory> argTypes;
    std::vector<ArgConversion> argConversions;
    ReturnHandling returnHandling = ReturnHandling::Raw;
    bool isVariadic = false;

    // NEW: Variadic handling
    VariadicHandling variadicHandling = VariadicHandling::None;
    size_t restParamIndex = SIZE_MAX;  // Where rest params start
};
```

### Builder API Extension

```cpp
LoweringSpecBuilder& variadicHandling(VariadicHandling handling, size_t restIndex = 0) {
    spec_.variadicHandling = handling;
    spec_.restParamIndex = restIndex;
    return *this;
}
```

### Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                     DEFINITION LAYER                            │
├─────────────────────┬───────────────────────────────────────────┤
│  registerBuiltins() │  Extension JSON files                     │
│  (ECMA core)        │  (Node.js APIs)                           │
│                     │                                           │
│  console.log →      │  util.format →                            │
│    TypeDispatch     │    PackArray                              │
└─────────┬───────────┴───────────────┬───────────────────────────┘
          │                           │
          │  registerFromExtensions() │
          │◄──────────────────────────┘
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                   LOWERING REGISTRY                             │
│                   (single source of truth)                      │
│                                                                 │
│  lookup("ts_console_log") → LoweringSpec { TypeDispatch }       │
│  lookup("ts_util_format") → LoweringSpec { PackArray }          │
└─────────────────────────────┬───────────────────────────────────┘
                              │
          ┌───────────────────┴───────────────────┐
          ▼                                       ▼
┌─────────────────────┐               ┌─────────────────────┐
│     ASTToHIR        │               │     HIRToLLVM       │
│                     │               │                     │
│ if PackArray:       │               │ if TypeDispatch:    │
│   create array      │               │   emit type call    │
│   push args         │               │                     │
└─────────────────────┘               └─────────────────────┘
```

---

## Implementation Phases

Each phase is independently committable and must pass all tests.

### Phase 1: Extend LoweringSpec Schema

**Status:** Not Started
**Risk:** None (additive only)

**Files to modify:**
- `src/compiler/hir/LoweringRegistry.h`

**Changes:**
1. Add `VariadicHandling` enum
2. Add `variadicHandling` and `restParamIndex` fields to `LoweringSpec`
3. Add `.variadicHandling(handling, index)` builder method

**Verification:**
```bash
cmake --build build --config Release --target ts-aot
python tests/golden_ir/runner.py tests/golden_ir  # Expect 120/146
python tests/node/run_tests.py                     # Expect 190/280
```

---

### Phase 2: Register ECMA Core Variadic Info

**Status:** Not Started
**Risk:** None (sets values not yet read)

**Files to modify:**
- `src/compiler/hir/LoweringRegistry.cpp`

**Changes:**
Register variadic handling for ECMA core functions in `registerBuiltins()`:

| Function | Handling | restParamIndex |
|----------|----------|----------------|
| ts_console_log | TypeDispatch | 0 |
| ts_console_error | TypeDispatch | 0 |
| ts_console_warn | TypeDispatch | 0 |
| ts_console_info | TypeDispatch | 0 |
| ts_console_debug | TypeDispatch | 0 |
| ts_array_of | PackArray | 0 |

**Example:**
```cpp
registerLowering("ts_console_log",
    lowering("ts_console_log")
        .returnsVoid()
        .boxedArg()
        .variadicHandling(VariadicHandling::TypeDispatch, 0)
        .build());
```

**Verification:** Same as Phase 1

---

### Phase 3: Extend Extension Schema

**Status:** Not Started
**Risk:** None (additive only)

**Files to modify:**
- `src/compiler/extensions/ExtensionSchema.h`
- `src/compiler/extensions/ExtensionLoader.cpp`

**Changes:**
1. Add `VariadicHandling` enum to ext namespace (or reuse from hir)
2. Add `std::optional<VariadicHandling> variadicHandling` to `ext::LoweringSpec`
3. Add `size_t restParamIndex = SIZE_MAX` to `ext::LoweringSpec`
4. Parse from JSON in `ExtensionLoader::parseLowering()`

**JSON format:**
```json
"lowering": {
  "args": ["ptr", "ptr"],
  "returns": "ptr",
  "variadicHandling": "pack-array",
  "restParamIndex": 1
}
```

**Verification:** Same as Phase 1

---

### Phase 4: Update Extension JSONs

**Status:** Not Started
**Risk:** None (values not yet read)

**Files to modify:**
- `extensions/node/util/util.ext.json`
- Any other extensions with variadic methods

**Changes:**
Add explicit `variadicHandling` to methods with rest parameters:

| Extension | Method | variadicHandling | restParamIndex |
|-----------|--------|------------------|----------------|
| util | format | pack-array | 1 |

**Verification:** Same as Phase 1

---

### Phase 5: ExtensionLoader Feeds LoweringRegistry

**Status:** Not Started
**Risk:** Low (new code path, existing path unchanged)

**Files to modify:**
- `src/compiler/hir/LoweringRegistry.cpp`

**Changes:**
In `registerFromExtensions()`, convert extension variadic info to hir::LoweringSpec:

```cpp
void LoweringRegistry::registerFromExtensions() {
    auto& extRegistry = ext::ExtensionRegistry::instance();

    for (auto& contract : extRegistry.getContracts()) {
        for (auto& [objName, objDef] : contract.objects) {
            for (auto& [methodName, methodDef] : objDef.methods) {
                if (methodDef.lowering && methodDef.lowering->variadicHandling) {
                    // Update existing registration with variadic info
                    auto* existing = lookup(methodDef.call);
                    if (existing) {
                        // Modify in place or re-register
                    }
                }
            }
        }
    }
}
```

**Verification:** Same as Phase 1

---

### Phase 6: ASTToHIR Uses LoweringRegistry

**Status:** Not Started
**Risk:** Medium (changes behavior)

**Files to modify:**
- `src/compiler/hir/ASTToHIR.cpp`

**Changes:**
1. Remove hardcoded `isConsoleFunctionWithSpecialHandling` check
2. Query LoweringRegistry for variadic handling:

```cpp
// BEFORE
bool isConsoleFunctionWithSpecialHandling =
    classNameIdent->name == "console" && ...;

if (restParamIndex != SIZE_MAX && !isConsoleFunctionWithSpecialHandling) {
    // pack array
}

// AFTER
auto* spec = hir::LoweringRegistry::instance().lookup(runtimeFunc);
bool shouldPackArray = spec && spec->variadicHandling == hir::VariadicHandling::PackArray;

if (restParamIndex != SIZE_MAX && shouldPackArray) {
    // pack array
}
```

**Verification:**
```bash
cmake --build build --config Release --target ts-aot
python tests/golden_ir/runner.py tests/golden_ir  # MUST be 120/146
python tests/node/run_tests.py                     # MUST be 190/280
```

---

### Phase 7: HIRToLLVM Uses LoweringRegistry

**Status:** Not Started
**Risk:** Medium (changes behavior)

**Files to modify:**
- `src/compiler/hir/HIRToLLVM.cpp`

**Changes:**
1. Remove hardcoded console function string checks
2. Query LoweringRegistry for type dispatch:

```cpp
// BEFORE
if (funcName == "ts_console_log" || funcName == "ts_console_error" || ...) {
    // type dispatch
}

// AFTER
auto* spec = hir::LoweringRegistry::instance().lookup(funcName);
if (spec && spec->variadicHandling == hir::VariadicHandling::TypeDispatch) {
    // type dispatch
}
```

**Verification:** Same as Phase 6

---

### Phase 8: Cleanup and Documentation

**Status:** Not Started
**Risk:** None

**Changes:**
1. Add validation: warn if method has `rest: true` but no `variadicHandling`
2. Update DEVELOPMENT.md with variadic handling documentation
3. Update this ticket with completion status

---

## Success Criteria

- [ ] Zero test regressions (120/146 golden_ir, 190/280 node)
- [ ] No string matching for function names in ASTToHIR
- [ ] No string matching for function names in HIRToLLVM
- [ ] ECMA core variadic behavior defined in `registerBuiltins()`
- [ ] Node.js variadic behavior defined in extension JSONs
- [ ] Single query point: `LoweringRegistry::lookup()`

---

## Rollback Plan

Each phase can be independently reverted:
- Phases 1-5: Safe to revert, no behavior changes
- Phase 6: Revert restores hardcoded ASTToHIR check
- Phase 7: Revert restores hardcoded HIRToLLVM check

---

## Related Files

**Core:**
- `src/compiler/hir/LoweringRegistry.h`
- `src/compiler/hir/LoweringRegistry.cpp`
- `src/compiler/hir/ASTToHIR.cpp`
- `src/compiler/hir/HIRToLLVM.cpp`

**Extensions:**
- `src/compiler/extensions/ExtensionSchema.h`
- `src/compiler/extensions/ExtensionLoader.cpp`
- `extensions/node/util/util.ext.json`

**Tests:**
- `tests/golden_ir/` (120/146 baseline)
- `tests/node/` (190/280 baseline)
