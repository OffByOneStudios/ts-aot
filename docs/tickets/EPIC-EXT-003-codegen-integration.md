# EPIC-EXT-003: Codegen Integration

**Status:** Planning
**Category:** Extension Contract System
**Epic:** EPIC-EXT-001
**Priority:** High

## Overview

Integrate extension contracts with the LLVM IR generator so that calls to extension-defined methods and properties generate correct runtime function calls instead of VTable dispatch.

## Problem Statement

After Phase 2 (Analyzer Integration), extension types are correctly inferred:
```
testInstance.getValue() → () => double → double
```

However, codegen still:
1. Generates VTable dispatch → undefined symbols (`TestClass_getValue`)
2. Doesn't initialize extension globals → null dereference at runtime

## Goals

1. Generate direct calls to extension runtime functions (from `lowering.call`)
2. Initialize extension-defined globals at module startup
3. Skip VTable generation for extension-defined types
4. Support property access via getter functions

## Implementation Plan

### Task 1: Add Lookup Methods to ExtensionRegistry

**File:** `src/compiler/extensions/ExtensionLoader.h`

```cpp
class ExtensionRegistry {
public:
    // Existing...

    // New lookup methods for codegen
    const ext::MethodDefinition* lookupMethod(const std::string& typeName,
                                               const std::string& methodName) const;
    const ext::PropertyDefinition* lookupProperty(const std::string& typeName,
                                                   const std::string& propName) const;
    const ext::TypeDefinition* lookupType(const std::string& typeName) const;
    bool isExtensionType(const std::string& typeName) const;

    // Get all globals that need initialization
    std::vector<std::pair<std::string, ext::GlobalDefinition>> getGlobals() const;
};
```

### Task 2: Extension-Aware Method Call Generation

**File:** `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp`

Before generating VTable dispatch for a method call:
1. Check if the receiver type is an extension type
2. If yes, look up the method in ExtensionRegistry
3. Generate a direct call using `lowering.call` function name
4. Convert argument types using `lowering.args`
5. Box/unbox return value using `lowering.returns`

```cpp
bool IRGenerator::tryGenerateExtensionMethodCall(
    ast::CallExpression* call,
    ast::PropertyAccessExpression* prop,
    const std::string& typeName
) {
    auto& registry = ext::ExtensionRegistry::get();
    auto* method = registry.lookupMethod(typeName, prop->name);
    if (!method) return false;

    // Get the runtime function name from lowering spec
    std::string funcName = method->call;

    // Build LLVM types from lowering spec
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(builder->getPtrTy());  // self
    for (const auto& argSpec : method->lowering.args) {
        argTypes.push_back(convertLoweringType(argSpec));
    }

    llvm::Type* retType = convertLoweringType(method->lowering.returns);

    auto ft = llvm::FunctionType::get(retType, argTypes, false);
    auto fn = module->getOrInsertFunction(funcName, ft);

    // Generate call with converted arguments
    std::vector<llvm::Value*> args;
    args.push_back(selfValue);
    for (size_t i = 0; i < call->arguments.size(); i++) {
        llvm::Value* arg = visitExpression(call->arguments[i].get());
        args.push_back(convertToLoweringType(arg, method->lowering.args[i]));
    }

    lastValue = createCall(ft, fn.getCallee(), args);

    // Box return value if needed
    if (method->lowering.returns != "ptr") {
        lastValue = boxLoweringValue(lastValue, method->lowering.returns);
    }

    return true;
}
```

### Task 3: Property Access Lowering

**File:** `src/compiler/codegen/IRGenerator_Expressions_Access.cpp`

For property access on extension types:
1. Check if property has a `get` lowering spec
2. Generate call to getter function
3. Box/unbox as needed

```cpp
bool IRGenerator::tryGenerateExtensionPropertyAccess(
    ast::PropertyAccessExpression* prop,
    const std::string& typeName
) {
    auto& registry = ext::ExtensionRegistry::get();
    auto* propDef = registry.lookupProperty(typeName, prop->name);
    if (!propDef || propDef->lowering.get.empty()) return false;

    std::string funcName = propDef->lowering.get;

    auto ft = llvm::FunctionType::get(
        convertLoweringType(propDef->lowering.returns),
        { builder->getPtrTy() },  // self
        false
    );
    auto fn = module->getOrInsertFunction(funcName, ft);

    lastValue = createCall(ft, fn.getCallee(), { selfValue });

    // Box if needed
    if (propDef->lowering.returns != "ptr") {
        lastValue = boxLoweringValue(lastValue, propDef->lowering.returns);
    }

    return true;
}
```

### Task 4: Skip VTable Generation for Extension Types

**File:** `src/compiler/codegen/IRGenerator_Classes.cpp`

When generating VTables:
1. Check if the class is an extension type
2. If yes, skip VTable generation entirely

```cpp
void IRGenerator::generateClassVTable(const std::string& className) {
    // Skip VTable for extension types - they're implemented by runtime
    if (ext::ExtensionRegistry::get().isExtensionType(className)) {
        return;
    }

    // Existing VTable generation...
}
```

### Task 5: Extension Global Initialization

**File:** `src/compiler/codegen/IRGenerator.cpp`

Generate initialization function for extension globals:

```cpp
void IRGenerator::generateExtensionInitialization() {
    auto& registry = ext::ExtensionRegistry::get();
    auto globals = registry.getGlobals();

    if (globals.empty()) return;

    // Create __ts_init_extensions function
    auto ft = llvm::FunctionType::get(builder->getVoidTy(), {}, false);
    auto fn = llvm::Function::Create(
        ft, llvm::Function::ExternalLinkage,
        "__ts_init_extensions", module.get()
    );

    auto bb = llvm::BasicBlock::Create(context, "entry", fn);
    builder->SetInsertPoint(bb);

    for (const auto& [name, globalDef] : globals) {
        if (globalDef.kind == ext::GlobalDefinition::Kind::Property) {
            // Get the factory function name
            std::string factoryName = "ts_" + name + "_create";

            // Declare global variable
            auto globalVar = module->getOrInsertGlobal(
                name, builder->getPtrTy()
            );

            // Call factory and store result
            auto factoryFt = llvm::FunctionType::get(
                builder->getPtrTy(), {}, false
            );
            auto factoryFn = module->getOrInsertFunction(factoryName, factoryFt);
            auto instance = createCall(factoryFt, factoryFn.getCallee(), {});

            builder->CreateStore(instance, globalVar);
        }
    }

    builder->CreateRetVoid();
}
```

Call from module initialization:
```cpp
void IRGenerator::generateModuleInit() {
    // ... existing init code ...

    // Initialize extension globals
    auto extInitFn = module->getFunction("__ts_init_extensions");
    if (extInitFn) {
        builder->CreateCall(extInitFn);
    }
}
```

### Task 6: Lowering Type Conversion Helpers

**File:** `src/compiler/codegen/IRGenerator.cpp`

```cpp
llvm::Type* IRGenerator::convertLoweringType(const std::string& spec) {
    if (spec == "i64") return builder->getInt64Ty();
    if (spec == "i32") return builder->getInt32Ty();
    if (spec == "f64") return builder->getDoubleTy();
    if (spec == "ptr") return builder->getPtrTy();
    if (spec == "void") return builder->getVoidTy();
    if (spec == "bool") return builder->getInt1Ty();

    // Default to ptr for unknown types
    return builder->getPtrTy();
}

llvm::Value* IRGenerator::convertToLoweringType(
    llvm::Value* val,
    const std::string& targetSpec
) {
    llvm::Type* targetType = convertLoweringType(targetSpec);

    if (val->getType() == targetType) return val;

    if (targetSpec == "i64" && val->getType()->isDoubleTy()) {
        return builder->CreateFPToSI(val, targetType);
    }
    if (targetSpec == "f64" && val->getType()->isIntegerTy(64)) {
        return builder->CreateSIToFP(val, targetType);
    }

    // Pointer conversion
    if (targetType->isPointerTy() && val->getType()->isIntegerTy()) {
        return builder->CreateIntToPtr(val, targetType);
    }

    return val;
}

llvm::Value* IRGenerator::boxLoweringValue(
    llvm::Value* val,
    const std::string& spec
) {
    if (spec == "i64") {
        return boxValue(val, std::make_shared<Type>(TypeKind::Int));
    }
    if (spec == "f64") {
        return boxValue(val, std::make_shared<Type>(TypeKind::Double));
    }
    if (spec == "bool") {
        return boxValue(val, std::make_shared<Type>(TypeKind::Boolean));
    }

    // ptr values are already boxed or don't need boxing
    return val;
}
```

## Runtime Requirements

For the test extension to work, the runtime needs:

1. **Factory function:** `ts_testInstance_create()` → creates TestClass instance
2. **Method implementations:** Already added in TsTestExtension.cpp
3. **Property getters:** Already added in TsTestExtension.cpp

## Test Plan

### Test 1: Method Calls

```typescript
function user_main(): number {
    console.log(testInstance.getValue());  // Expected: 42
    console.log(testInstance.add(10, 20)); // Expected: 30
    console.log(testInstance.getName());   // Expected: TestInstance
    return 0;
}
```

### Test 2: Property Access

```typescript
function user_main(): number {
    console.log(testInstance.count);  // Expected: 100
    console.log(testInstance.label);  // Expected: test-label
    return 0;
}
```

### Test 3: No VTable Symbols

Verify that `TestClass_getValue`, `TestClass_add`, etc. are NOT generated in the object file.

## Acceptance Criteria

- [ ] Extension method calls compile without undefined symbol errors
- [ ] Extension method calls execute correctly at runtime
- [ ] Extension property access works correctly
- [ ] Extension globals are initialized before user code runs
- [ ] No VTable generated for extension-defined types
- [ ] All existing tests continue to pass
- [ ] Console extension can be re-enabled after this phase

## Files to Modify

| File | Changes |
|------|---------|
| `src/compiler/extensions/ExtensionLoader.h` | Add lookup methods |
| `src/compiler/extensions/ExtensionLoader.cpp` | Implement lookup methods |
| `src/compiler/codegen/IRGenerator.h` | Add extension codegen helpers |
| `src/compiler/codegen/IRGenerator.cpp` | Add type conversion, init generation |
| `src/compiler/codegen/IRGenerator_Expressions_Calls.cpp` | Extension method call dispatch |
| `src/compiler/codegen/IRGenerator_Expressions_Access.cpp` | Extension property access |
| `src/compiler/codegen/IRGenerator_Classes.cpp` | Skip VTable for extension types |
| `src/runtime/src/TsTestExtension.cpp` | Add factory function |

## Risks

1. **Integration complexity:** Extension codegen must integrate cleanly with existing paths
2. **Boxing mismatches:** Lowering types must match what runtime expects
3. **Initialization order:** Extension globals must be initialized before user code

## Estimated Effort

- Lookup methods: 2 hours
- Method call generation: 4 hours
- Property access: 2 hours
- VTable skip: 1 hour
- Global initialization: 2 hours
- Testing and debugging: 4 hours

**Total: ~15 hours (2 days)**
