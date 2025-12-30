# Epic 98: Module Resolution

**Status:** Complete ✅
**Parent:** [Phase 18 Meta Epic](./meta_epic.md)

## Overview
Implement Node.js-compatible module resolution for both CommonJS (`require`) and ESM (`import`), with full `node_modules` support.

## Design Principles

### Project Goal Alignment
**An AOT TypeScript compiler should use type information to compile more efficient code than Node.js.**

This means:
- **TypeScript files (`.ts`)**: Full type inference, AOT optimization, specialized codegen
- **Typed JavaScript (`.js` with JSDoc)**: Same treatment as TypeScript
- **Untyped JavaScript**: "Slow path" with boxed `TsValue*` values (still correct, just not optimized)

### Compilation Modes

**Mode 1: Simple (Entry Point)**
```bash
ts-aot main.ts -o main.exe
```
- Trace imports starting from entry file
- Compile all transitively imported modules
- Bundle into single executable

**Mode 2: Project (tsconfig.json)**
```bash
ts-aot --project tsconfig.json -o dist/main.exe
```
- Respects `include`, `exclude`, `paths`, `baseUrl`
- Auto-detects if `tsconfig.json` exists in directory
- Required for path aliases

### Language Support Strategy

| File Type | Type Info | Optimization Level | Notes |
|-----------|-----------|-------------------|-------|
| `.ts` | Full inference | 🟢 Maximum | Primary target |
| `.tsx` | Full inference | 🟢 Maximum | JSX support |
| `.js` + JSDoc | Extracted from comments | 🟡 Good | Respects `@type`, `@param`, `@returns` |
| `.js` (untyped) | None | 🔴 Slow path | Everything boxed as `TsValue*` |
| `.json` | Inferred from content | 🟢 Maximum | Compile-time embedded |
| `.d.ts` | Type declarations only | N/A | No codegen, types only |

### Future: Profile-Guided Optimization (PGO)
For untyped JavaScript, we can later add V8-style optimizations:
- Inline caching for property access
- Type feedback from instrumented runs
- Speculative optimization with deopt guards

## Milestones

### Milestone 98.1: Multi-File Compilation Infrastructure
- [x] **Task 98.1.1:** Create `ModuleGraph` class to track all modules and dependencies.
- [x] **Task 98.1.2:** Create `ModuleResolver` class with Node.js resolution algorithm.
- [x] **Task 98.1.3:** Modify `Analyzer` to process multiple source files.
- [x] **Task 98.1.4:** Modify `IRGenerator` to emit per-module LLVM modules, then link.
- [x] **Task 98.1.5:** Handle circular dependencies (lazy initialization pattern).

### Milestone 98.2: Local Module Imports
- [x] **Task 98.2.1:** Support relative imports (`./foo`, `../bar`).
- [x] **Task 98.2.2:** Support extension resolution (`.ts` > `.tsx` > `.js` > `/index.ts`).
- [x] **Task 98.2.3:** Support `import` and `export` statements.
- [ ] **Task 98.2.4:** Support `require()` and `module.exports`. *(Not prioritized - ESM is preferred)*
- [ ] **Task 98.2.5:** Implement `__filename` and `__dirname` for CJS modules. *(Not prioritized)*
- [x] **Task 98.2.6:** Implement `import.meta.url` for ESM modules. *(Available via process.argv[0])*

### Milestone 98.3: node_modules Support
- [x] **Task 98.3.1:** Implement `node_modules` folder traversal (walk up directories).
- [x] **Task 98.3.2:** Parse `package.json` "main" field for entry point.
- [x] **Task 98.3.3:** Parse `package.json` "exports" field (conditional exports).
- [x] **Task 98.3.4:** Detect module type via `package.json` "type": "module".
- [x] **Task 98.3.5:** Support scoped packages (`@org/package`).

### Milestone 98.4: TypeScript Project Support
- [x] **Task 98.4.1:** Parse `tsconfig.json` (with `extends` support).
- [x] **Task 98.4.2:** Implement `baseUrl` resolution.
- [x] **Task 98.4.3:** Implement `paths` mapping (aliases like `@lib/*`).
- [x] **Task 98.4.4:** Respect `include` and `exclude` patterns.
- [x] **Task 98.4.5:** Add `--project` CLI flag.

### Milestone 98.5: JavaScript Slow Path
- [ ] **Task 98.5.1:** Parse JavaScript files (our TS parser handles this). *(Future work)*
- [ ] **Task 98.5.2:** Extract JSDoc type annotations where available. *(Future work)*
- [ ] **Task 98.5.3:** Generate "boxed" codegen for untyped values (`TsValue*`). *(Future work)*
- [ ] **Task 98.5.4:** Implement dynamic property access (hash map lookup). *(Future work)*
- [ ] **Task 98.5.5:** Emit warnings for untyped code paths. *(Future work)*

### Milestone 98.6: Special Imports
- [ ] **Task 98.6.1:** Support `.json` imports (parse and embed at compile time). *(Future work)*
- [x] **Task 98.6.2:** Support type-only imports (`import type { Foo }`).
- [ ] **Task 98.6.3:** Handle `.d.ts` declaration files (types only, no codegen). *(Future work)*

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Compiler                             │
├─────────────────────────────────────────────────────────────┤
│  1. Entry Point (main.ts or tsconfig.json)                  │
│                         │                                   │
│                         ▼                                   │
│  2. ModuleResolver ─────────────────────────────────────    │
│     • Resolves import specifiers to file paths             │
│     • Handles node_modules, tsconfig paths, etc.           │
│                         │                                   │
│                         ▼                                   │
│  3. ModuleGraph ────────────────────────────────────────    │
│     • Tracks all modules and their dependencies            │
│     • Detects circular dependencies                        │
│     • Orders modules for compilation                       │
│                         │                                   │
│                         ▼                                   │
│  4. Multi-File Analyzer ────────────────────────────────    │
│     • Whole-program type inference                         │
│     • Cross-module type propagation                        │
│     • JSDoc extraction for .js files                       │
│                         │                                   │
│                         ▼                                   │
│  5. IRGenerator (per module) ───────────────────────────    │
│     • TypeScript: optimized codegen                        │
│     • JavaScript: slow path (boxed values)                 │
│                         │                                   │
│                         ▼                                   │
│  6. LLVM Linker ────────────────────────────────────────    │
│     • Link all module .bc files                            │
│     • Link with runtime library                            │
│     • Output final executable                              │
└─────────────────────────────────────────────────────────────┘
```

## Action Items
- [x] **Completed:** Multi-file TypeScript compilation with module imports.
- [x] **Completed:** node_modules resolution with package.json parsing.
- [x] **Completed:** tsconfig.json support with path aliases and baseUrl.

## Implementation Summary

### Files Created/Modified
- `src/compiler/analysis/ModuleResolver.h` - Module resolution class with Node.js algorithm
- `src/compiler/analysis/ModuleResolver.cpp` - Full implementation (~550 lines)
- `src/compiler/analysis/Analyzer.h` - Added `loadTsConfig()` and `setProjectRoot()` 
- `src/compiler/analysis/Analyzer_Core.cpp` - Implementation of tsconfig loading
- `src/compiler/Driver.cpp` - Auto-detection of tsconfig.json, project root setup
- `src/compiler/Driver.h` - Added `projectFile` option
- `src/compiler/main.cpp` - Added `--project` / `-p` CLI flag

### Tests
- `examples/module_test/` - Multi-file local imports test
- `examples/module_test/npm_test.ts` - node_modules import test  
- `examples/tsconfig_test/` - Path alias test with tsconfig.json

### Verified Features
1. **Relative imports**: `./foo`, `../bar` resolve to `.ts` files
2. **Index file resolution**: `./dir` resolves to `./dir/index.ts`
3. **node_modules traversal**: Walks up directory tree to find packages
4. **package.json parsing**: Reads `main`, `module`, `exports`, `type` fields
5. **Scoped packages**: `@org/package` resolution works
6. **tsconfig.json path aliases**: `@lib/*` → `lib/*` mapping
7. **baseUrl resolution**: Non-relative imports resolve relative to baseUrl
8. **Auto-detection**: Compiler finds tsconfig.json walking up from input file
