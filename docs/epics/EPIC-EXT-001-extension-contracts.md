# EPIC-EXT-001: Static Extension Contract System

**Status:** In Progress (Phase 4 - Node.js Module Migration)
**Priority:** High
**Estimated Effort:** Large (4-6 weeks)

## Overview

Replace the current hardcoded Node.js API implementation with a declarative contract-based extension system. Extensions define their types and functions in JSON, and the compiler/runtime implement the contract.

### Goals

1. **Separation of concerns**: API definitions separate from compiler internals
2. **Maintainability**: Add new APIs by writing JSON + C, not modifying compiler
3. **Extensibility**: Third parties can create extensions
4. **Type safety**: Contracts are the single source of truth

### Non-Goals

- Dynamic loading of extensions at runtime
- Compatibility with existing Node.js native modules (N-API)
- Hot-reloading of extensions

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Compile Time                           │
├─────────────────────────────────────────────────────────────┤
│  *.ext.json ──► ExtensionLoader ──► Analyzer (types)        │
│                       │                                     │
│                       └──────────► IRGenerator (lowering)   │
│                                    (call signatures)        │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                       Link Time                             │
├─────────────────────────────────────────────────────────────┤
│  User Code (.obj) + tsruntime.lib + extension.lib ──► .exe  │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Contract Schema & Loader ✅ COMPLETE

**Goal:** Define JSON schema and build loader infrastructure

### Completed Tasks

- ✅ Defined extension contract JSON schema (`extension-contract.schema.json`)
- ✅ Created C++ types matching schema (`ExtensionSchema.h`)
- ✅ Implemented JSON parser with validation (`ExtensionLoader.cpp`)
- ✅ Built ExtensionRegistry singleton for contract lookup
- ✅ Integrated with Driver to load contracts at startup

### Deliverables

- ✅ `src/compiler/extensions/ExtensionSchema.h` - C++ types for contract
- ✅ `src/compiler/extensions/ExtensionLoader.cpp` - JSON parser
- ✅ `src/compiler/extensions/ExtensionLoader.h` - Registry singleton
- ✅ `extensions/extension-contract.schema.json` - JSON schema
- ✅ `extensions/test/test.ext.json` - Test extension for verification

---

## Phase 2: Analyzer Integration ✅ COMPLETE

**Goal:** Register extension-defined types in the Analyzer's symbol table

### Completed Tasks

- ✅ Added `registerTypesFromExtensions()` to Analyzer
- ✅ Implemented `convertExtTypeRef()` for type conversion (string→TypeKind)
- ✅ Register extension classes with methods and properties
- ✅ Register extension globals referencing existing types
- ✅ Skip already-registered types to prevent conflicts with builtins
- ✅ Verified type inference works correctly

### Type Inference Verification

```
testInstance.getValue()   → () => double  → double
testInstance.add(10, 20)  → (double, double) => double → double
testInstance.getName()    → () => string  → string
testInstance.count        → double
testInstance.label        → string
```

### Deliverables

- ✅ `Analyzer::registerTypesFromExtensions()` implementation
- ✅ `convertExtTypeRef()` type conversion helper
- ✅ Test extension with verified type inference
- ✅ Console still works (builtin handling takes precedence)

### Known Issues

- Console extension disabled (conflicts with hardcoded builtin handling)
- Will be re-enabled after Phase 3 provides proper codegen integration

---

## Phase 3: Codegen Integration ✅ COMPLETE

**Goal:** Generate LLVM IR calls using extension lowering specifications

### Completed Tasks

- ✅ Extension-aware method call generation (`tryGenerateExtensionMethodCall`)
- ✅ Property access lowering with getter functions (`tryGenerateExtensionPropertyAccess`)
- ✅ Skip VTable generation for extension-defined types
- ✅ Extension global initialization via factory functions
- ✅ Added `factory` field to GlobalDefinition schema
- ✅ Test extension compiles and runs correctly

### Implementation Details

#### Method Call Generation
- Implemented in `IRGenerator_Extensions.cpp:tryGenerateExtensionMethodCall()`
- Checks if receiver type is an extension-defined ClassType
- Looks up method in ExtensionRegistry and generates direct runtime call
- Uses lowering spec for LLVM type conversions

#### Property Access Lowering
- Implemented in `IRGenerator_Extensions.cpp:tryGenerateExtensionPropertyAccess()`
- Properties can define `getter` field pointing to runtime function
- Uses lowering spec for return type conversion

#### Global Initialization
- Factory functions specified in extension JSON via `factory` field
- Called at module startup in `generateEntryPoint()` after CommonJS init
- Results stored in LLVM global variables

#### VTable Skip
- Extension types excluded from class collection in `IRGenerator_Classes.cpp`
- Prevents undefined symbol generation for runtime-implemented types

### Deliverables

- ✅ `ExtensionRegistry::findMethod()` - Find method lowering info
- ✅ `ExtensionRegistry::findProperty()` - Find property lowering info
- ✅ `IRGenerator` integration for extension method calls
- ✅ Extension global initialization in module startup
- ✅ Skip VTable generation for extension-defined types
- ✅ Test extension compiles and runs correctly

### Test Results

```
getValue: 42
add(10, 20): 30
getName: TestInstance
count: 100
label: test-label
```

### Acceptance Criteria ✅ MET

```typescript
// This compiles AND runs correctly:
function user_main(): number {
    console.log(testInstance.getValue());  // Output: 42
    console.log(testInstance.add(10, 20)); // Output: 30
    console.log(testInstance.getName());   // Output: TestInstance
    return 0;
}
```

---

## Phase 4: Node.js Module Migration

**Goal:** Convert Node.js modules from hardcoded to contract-based

### Scope Clarification

**Stay in Core (NOT extension contracts):**
- TypeScript language features (classes, generics, decorators, etc.)
- ECMAScript features (Array methods, Object methods, Promise, etc.)
- `console` module (fundamental debugging, stays hardcoded)

**Move to Extension Contracts:**
- Node.js APIs (fs, http, net, crypto, etc.)
- Third-party integrations
- Platform-specific APIs

### Priority Order (by usage)

| Priority | Module | Functions | Status |
|----------|--------|-----------|--------|
| P0 | fs | 40+ | Not started |
| P0 | path | 15 | Not started |
| P0 | buffer | 30+ | Not started |
| P1 | http | 20+ | Not started |
| P1 | url | 10+ | Not started |
| P1 | process | 30+ | Not started |
| P1 | net | 15+ | Not started |
| P2 | crypto | 30+ | Not started |
| P2 | stream | 20+ | Not started |
| P2 | events | 10+ | Not started |

### Tasks per Module

For each module:
1. Create `extensions/<module>/<module>.ext.json`
2. Define types, methods, and lowering specs
3. Verify runtime functions match contract
4. Test with contract-based codegen
5. Remove Analyzer_StdLib_<Module>.cpp (gradual)
6. Remove IRGenerator_*_Builtin_<Module>.cpp (gradual)
7. Run module's tests

---

## Phase 5: Tooling & Documentation

**Goal:** Make extensions easy to create and debug

### Tasks

- [ ] `ts-aot validate-extension` command
- [ ] `ts-aot generate-extension` command (from .d.ts)
- [ ] `ts-aot generate-header` command (C header from contract)
- [ ] Extension authoring documentation

---

## Phase 6: Advanced Features

**Goal:** Support complex patterns

- [ ] Overloaded functions (multiple signatures)
- [ ] Generic methods with type parameters
- [ ] Async functions returning Promise
- [ ] Static vs instance method distinction

---

## Migration Strategy

### Step-by-Step per Module

1. **Write contract** - Create .ext.json
2. **Validate** - Run validator, fix issues
3. **Test in parallel** - Load contract alongside existing code
4. **Verify codegen** - Extension calls work correctly
5. **Remove old code** - Delete Analyzer/IRGenerator implementations
6. **Commit** - One module per commit

### Rollback Plan

Keep old implementation behind a flag until migration complete:
```bash
ts-aot --legacy-builtins  # Uses old hardcoded implementation
```

---

## Success Criteria

- [ ] All 1000+ Node.js tests pass with contract-based implementation
- [ ] No Analyzer_StdLib_*.cpp files remain (except core language)
- [ ] No IRGenerator_*_Builtin_*.cpp files remain
- [ ] Extension contracts are the single source of truth
- [ ] Third-party extension example works end-to-end

---

## Timeline

| Phase | Duration | Status |
|-------|----------|--------|
| Phase 1: Schema & Loader | 1 week | ✅ Complete |
| Phase 2: Analyzer Integration | 1 week | ✅ Complete |
| Phase 3: Codegen Integration | 1 week | ✅ Complete |
| Phase 4: Node.js Modules | 2 weeks | Not started |
| Phase 5: Tooling | 1 week | Not started |
| Phase 6: Advanced | 1 week | Not started |

**Total: 6 weeks**

---

## Related Documents

- [DEVELOPMENT.md](../../.github/DEVELOPMENT.md) - Current architecture
- [adding-nodejs-api.instructions.md](../../.github/instructions/adding-nodejs-api.instructions.md) - Current process (to be deprecated)
- [ExtensionLoader.h](../../src/compiler/extensions/ExtensionLoader.h) - Extension registry
- [test.ext.json](../../extensions/test/test.ext.json) - Test extension contract
