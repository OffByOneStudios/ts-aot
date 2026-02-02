# EPIC-EXT-002: Extension Contract Analyzer Integration

**Status:** Planning
**Phase:** 2 of 3
**Depends On:** EPIC-EXT-001 (Extension Contract System - Complete)

## Overview

Integrate extension contracts with the Analyzer to enable type inference for APIs defined in extension contracts. This replaces hardcoded type definitions in `Analyzer_StdLib_*.cpp` files.

## Goals

1. Analyzer queries ExtensionRegistry for type definitions at startup
2. Extension-defined types are registered in the symbol table
3. Type inference works for APIs defined in extension contracts
4. Eventually migrate existing StdLib definitions to extension contracts

## Current State

**Phase 1 Complete:**
- ExtensionSchema.h - C++ types for contracts
- ExtensionLoader - JSON parsing
- ExtensionRegistry - Singleton with lookup methods
- LoweringRegistry integration - HIR → LLVM lowering

**What's Missing:**
- Analyzer doesn't query ExtensionRegistry
- Types defined in contracts aren't registered in symbol table
- Can't do type inference for extension-defined APIs

## Implementation Plan

### Task 1: Add `registerTypesFromExtensions()` to Analyzer

Create a method that queries ExtensionRegistry and registers types:

```cpp
// In Analyzer.h
void registerTypesFromExtensions();

// In Analyzer_StdLib.cpp
void Analyzer::registerTypesFromExtensions() {
    auto& extRegistry = ext::ExtensionRegistry::instance();

    // Register types (classes, interfaces)
    for (const auto& contract : extRegistry.getContracts()) {
        for (const auto& [typeName, typeDef] : contract.types) {
            auto classType = convertExtTypeToAnalyzerType(typeDef);
            symbols.defineType(typeName, classType);
        }
    }

    // Register globals (console, process, etc.)
    for (const auto& [globalName, globalDef] : contract.globals) {
        auto globalType = convertExtGlobalToAnalyzerType(globalDef);
        symbols.define(globalName, globalType);
    }

    // Register objects (Math, JSON, etc.)
    for (const auto& [objName, objDef] : contract.objects) {
        auto objType = convertExtObjectToAnalyzerType(objDef);
        symbols.define(objName, objType);
    }
}
```

### Task 2: Type Conversion Helpers

Convert extension contract types to Analyzer types:

```cpp
std::shared_ptr<Type> convertExtTypeRef(const ext::TypeReference& ref);
std::shared_ptr<ClassType> convertExtTypeToAnalyzerType(const ext::TypeDefinition& def);
std::shared_ptr<ObjectType> convertExtObjectToAnalyzerType(const ext::ObjectDefinition& def);
std::shared_ptr<FunctionType> convertExtMethodToAnalyzerType(const ext::MethodDefinition& def);
```

**Type mapping:**
| Extension Type | Analyzer Type |
|----------------|---------------|
| `"string"` | `TypeKind::String` |
| `"number"` | `TypeKind::Number` |
| `"boolean"` | `TypeKind::Boolean` |
| `"void"` | `TypeKind::Void` |
| `"any"` | `TypeKind::Any` |
| `"object"` | `TypeKind::Object` |
| `"null"` | `TypeKind::Null` |
| `"undefined"` | `TypeKind::Undefined` |
| `{ name: "Array", typeArgs: [...] }` | `ArrayType` with element type |
| `{ name: "Promise", typeArgs: [...] }` | `ClassType("Promise")` |
| `"ClassName"` | `ClassType(name)` |

### Task 3: Call from registerBuiltins()

```cpp
void Analyzer::registerBuiltins() {
    // ... existing builtin registrations ...

    // Register types from extension contracts
    registerTypesFromExtensions();
}
```

### Task 4: Create Test Extension

Create a simple test extension to verify type inference works:

```json
// extensions/test/test.ext.json
{
  "$schema": "../extension-contract.schema.json",
  "name": "test",
  "version": "1.0.0",
  "types": {
    "TestClass": {
      "kind": "class",
      "methods": {
        "getValue": {
          "call": "ts_test_get_value",
          "params": [],
          "returns": "number",
          "lowering": { "args": [], "returns": "i64" }
        }
      }
    }
  },
  "globals": {
    "testInstance": {
      "type": "TestClass"
    }
  }
}
```

Test file:
```typescript
// tmp/test_ext_types.ts
function user_main(): number {
    const val = testInstance.getValue();  // Should infer number
    console.log(val);
    return 0;
}
```

### Task 5: Migrate Console to Extension-Only

Once type registration works:
1. Remove hardcoded console registration from `Analyzer_StdLib.cpp`
2. Verify console.ext.json provides all types needed
3. Test that `console.log()` still works with proper type inference

## Files to Modify

| File | Changes |
|------|---------|
| `src/compiler/analysis/Analyzer.h` | Add `registerTypesFromExtensions()` declaration |
| `src/compiler/analysis/Analyzer_StdLib.cpp` | Implement `registerTypesFromExtensions()`, call from `registerBuiltins()` |
| `extensions/test/test.ext.json` | Create test extension |

## Verification

1. **Type inference test:**
   ```bash
   build/src/compiler/Release/ts-aot.exe tmp/test_ext_types.ts --dump-types
   ```
   Should show `testInstance.getValue()` returns `number`

2. **Console migration test:**
   ```bash
   build/src/compiler/Release/ts-aot.exe tmp/test_console.ts -o tmp/test_console.exe
   ./tmp/test_console.exe
   ```
   Should work without hardcoded console types

3. **No regressions:**
   ```bash
   python tests/node/run_tests.py
   python tests/golden_ir/runner.py
   ```

## Future Phases

**Phase 3: Codegen Integration**
- Use extension contracts to generate HIR method calls
- Replace hardcoded IRGenerator_Expressions_Calls_Builtin_*.cpp handlers
- Enable fully declarative API definitions

## Risks

1. **Type system complexity:** Extension contracts may not capture all TypeScript type features (conditional types, mapped types, etc.)
2. **Ordering:** Extensions must be loaded before Analyzer runs
3. **Circular dependencies:** Type A referencing Type B which references Type A

## Success Criteria

- [ ] `registerTypesFromExtensions()` implemented and called
- [ ] Type conversion helpers for all basic types
- [ ] Test extension works end-to-end
- [ ] Console works without hardcoded Analyzer types
- [ ] No test regressions
