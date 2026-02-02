# EPIC-EXT-001: Static Extension Contract System

**Status:** Planning
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
│                       └──────────► LoweringRegistry         │
│                                    (call signatures)        │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                       Link Time                             │
├─────────────────────────────────────────────────────────────┤
│  User Code (.obj) + tsruntime.lib + extension.lib ──► .exe  │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Contract Schema & Loader

**Goal:** Define JSON schema and build loader infrastructure

### Tasks

#### 1.1 Define Extension Contract Schema
```json
{
  "$schema": "https://ts-aot.dev/extension.schema.json",
  "name": "fs",
  "version": "1.0.0",
  "modules": ["fs", "node:fs", "fs/promises"],

  "types": {
    "Stats": {
      "kind": "class",
      "properties": {
        "size": { "type": "number", "get": "ts_fs_stats_size" },
        "mtime": { "type": "Date", "get": "ts_fs_stats_mtime" }
      },
      "methods": {
        "isFile": {
          "call": "ts_fs_stats_is_file",
          "returns": "boolean"
        }
      }
    }
  },

  "functions": {
    "readFileSync": {
      "call": "ts_fs_read_file_sync",
      "params": [
        { "name": "path", "type": "string" },
        { "name": "options", "type": "string | object", "optional": true }
      ],
      "returns": "string | Buffer",
      "lowering": {
        "args": ["ptr", "ptr"],
        "returns": "ptr"
      }
    }
  },

  "objects": {
    "fs": {
      "readFileSync": "functions.readFileSync",
      "writeFileSync": "functions.writeFileSync",
      "statSync": "functions.statSync"
    }
  }
}
```

#### 1.2 Create ExtensionLoader Class
- Parse JSON contracts
- Validate against schema
- Build internal representation

#### 1.3 Integrate with Analyzer
- ExtensionLoader populates Analyzer's type registry
- Types from contracts available for type checking

#### 1.4 Integrate with LoweringRegistry
- ExtensionLoader populates LoweringRegistry
- Call signatures from `lowering` field

### Deliverables
- [ ] `src/compiler/extensions/ExtensionSchema.h` - C++ types for contract
- [ ] `src/compiler/extensions/ExtensionLoader.cpp` - JSON parser
- [ ] `src/compiler/extensions/ExtensionRegistry.cpp` - Global registry
- [ ] Unit tests for loader

---

## Phase 2: Core Runtime Contract

**Goal:** Convert core runtime types (Array, String, Object) to contracts

### Tasks

#### 2.1 Array Contract
```json
{
  "name": "core",
  "types": {
    "Array<T>": {
      "kind": "generic-class",
      "typeParams": ["T"],
      "properties": {
        "length": { "type": "number", "get": "ts_array_length" }
      },
      "methods": {
        "push": {
          "call": "ts_array_push",
          "params": [{ "name": "items", "type": "T", "rest": true }],
          "returns": "number"
        },
        "map": {
          "call": "ts_array_map",
          "params": [{ "name": "fn", "type": "(T, number) => U" }],
          "returns": "Array<U>"
        }
      }
    }
  }
}
```

#### 2.2 String Contract
- String methods (substring, indexOf, etc.)
- String static methods (fromCharCode, etc.)

#### 2.3 Object Contract
- Object.keys, Object.values, Object.entries
- Object.assign, Object.freeze

#### 2.4 Remove Hardcoded Implementations
- Delete corresponding Analyzer_StdLib_*.cpp code
- Delete corresponding IRGenerator_*_Builtin_*.cpp code
- Verify tests still pass

### Deliverables
- [ ] `extensions/core/core.ext.json`
- [ ] Migrate Array (30+ methods)
- [ ] Migrate String (20+ methods)
- [ ] Migrate Object (15+ methods)
- [ ] All existing tests pass

---

## Phase 3: Node.js Module Contracts

**Goal:** Convert Node.js modules to contracts

### Priority Order (by usage)

| Priority | Module | Functions | Types |
|----------|--------|-----------|-------|
| P0 | fs | 40+ | Stats, Dirent, Dir |
| P0 | path | 15 | ParsedPath |
| P0 | buffer | 30+ | Buffer |
| P1 | http | 20+ | Server, Request, Response |
| P1 | url | 10+ | URL, URLSearchParams |
| P1 | process | 30+ | - |
| P2 | events | 10+ | EventEmitter |
| P2 | stream | 20+ | Readable, Writable, Duplex |
| P2 | crypto | 30+ | Hash, Hmac, Cipher |
| P3 | net | 15+ | Socket, Server |
| P3 | dns | 15+ | - |
| P3 | os | 20+ | - |
| P3 | util | 15+ | - |
| P4 | child_process | 10+ | ChildProcess |
| P4 | cluster | 10+ | Worker |
| P4 | zlib | 15+ | - |

### Tasks per Module

For each module:
1. Create `extensions/<module>/<module>.ext.json`
2. Verify runtime functions match contract
3. Remove Analyzer_StdLib_<Module>.cpp
4. Remove IRGenerator_*_Builtin_<Module>.cpp
5. Update LoweringRegistry (or auto-generate)
6. Run module's tests

### Deliverables
- [ ] 16 extension contract files
- [ ] ~500 functions migrated
- [ ] ~50 types migrated
- [ ] All Node.js tests pass

---

## Phase 4: Tooling & Documentation

**Goal:** Make extensions easy to create and debug

### Tasks

#### 4.1 Extension Validator CLI
```bash
ts-aot validate-extension my-extension.ext.json
# Checks:
# - JSON schema validity
# - All referenced types exist
# - All runtime functions declared in header
```

#### 4.2 Extension Generator
```bash
ts-aot generate-extension my-module.d.ts
# Generates skeleton .ext.json from TypeScript declarations
```

#### 4.3 Documentation
- Extension authoring guide
- Contract schema reference
- Examples for common patterns

#### 4.4 Header Generator
```bash
ts-aot generate-header my-extension.ext.json > my_extension.h
# Generates C header with all function declarations
```

### Deliverables
- [ ] `ts-aot validate-extension` command
- [ ] `ts-aot generate-extension` command
- [ ] `ts-aot generate-header` command
- [ ] Extension authoring documentation

---

## Phase 5: Advanced Features

**Goal:** Support complex patterns

### Tasks

#### 5.1 Overloaded Functions
```json
{
  "readFileSync": {
    "overloads": [
      {
        "params": [{ "name": "path", "type": "string" }],
        "returns": "Buffer",
        "call": "ts_fs_read_file_sync_buffer"
      },
      {
        "params": [
          { "name": "path", "type": "string" },
          { "name": "encoding", "type": "string" }
        ],
        "returns": "string",
        "call": "ts_fs_read_file_sync_string"
      }
    ]
  }
}
```

#### 5.2 Generic Methods
```json
{
  "Array<T>": {
    "methods": {
      "map<U>": {
        "typeParams": ["U"],
        "params": [{ "name": "fn", "type": "(T) => U" }],
        "returns": "Array<U>"
      }
    }
  }
}
```

#### 5.3 Async Functions
```json
{
  "readFile": {
    "async": true,
    "call": "ts_fs_read_file_async",
    "returns": "Promise<Buffer>"
  }
}
```

#### 5.4 Static vs Instance
```json
{
  "Buffer": {
    "static": {
      "alloc": { "call": "ts_buffer_alloc", "returns": "Buffer" },
      "from": { "call": "ts_buffer_from", "returns": "Buffer" }
    },
    "instance": {
      "toString": { "call": "ts_buffer_to_string", "returns": "string" }
    }
  }
}
```

### Deliverables
- [ ] Overload resolution
- [ ] Generic method instantiation
- [ ] Async function handling
- [ ] Static/instance distinction

---

## Migration Strategy

### Step-by-Step per Module

1. **Write contract** - Create .ext.json
2. **Validate** - Run validator, fix issues
3. **Test in parallel** - Load contract alongside existing code
4. **Remove old code** - Delete Analyzer/IRGenerator implementations
5. **Verify** - Run all tests for that module
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

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Performance regression from accessor calls | Inline small accessors in LLVM pass |
| Contract schema too limiting | Start minimal, extend as needed |
| Migration breaks existing code | Parallel implementation, extensive testing |
| Generic types too complex | Monomorphize at contract load time |

---

## Timeline

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1: Schema & Loader | 1 week | None |
| Phase 2: Core Runtime | 1 week | Phase 1 |
| Phase 3: Node.js Modules | 2 weeks | Phase 2 |
| Phase 4: Tooling | 1 week | Phase 3 |
| Phase 5: Advanced | 1 week | Phase 3 |

**Total: 6 weeks**

---

## Related Documents

- [DEVELOPMENT.md](../../.github/DEVELOPMENT.md) - Current architecture
- [adding-nodejs-api.instructions.md](../../.github/instructions/adding-nodejs-api.instructions.md) - Current process (to be deprecated)
- [LoweringRegistry.cpp](../../src/compiler/hir/LoweringRegistry.cpp) - Current lowering specs
