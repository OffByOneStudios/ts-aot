# Extension System Migration Audit

**Date:** 2026-02-06
**Status:** In Progress (approximately 50% migrated)

This document audits all Node.js-specific code remaining in the core compiler and runtime that should be driven by the extension system instead.

---

## 1. Analyzer: Hardcoded Type Registration

### Current State

All 30 Node.js modules have dedicated `Analyzer_StdLib_*.cpp` files that manually register types, methods, and properties in C++. These are called from `registerBuiltins()` in `Analyzer_StdLib.cpp`.

The extension system's `registerTypesFromExtensions()` is called LAST and skips anything already registered, meaning the hardcoded C++ always wins and extension JSON type definitions are effectively dead code for any module that has both.

### Files and Duplication Status

| Module | Analyzer File | Extension JSON | Status |
|--------|--------------|----------------|--------|
| assert | Analyzer_StdLib_Assert.cpp | assert.ext.json | Duplicated |
| async_hooks | Analyzer_StdLib_AsyncHooks.cpp | async_hooks.ext.json | Duplicated |
| buffer | Analyzer_StdLib_Buffer.cpp | buffer.ext.json | Duplicated |
| child_process | Analyzer_StdLib_ChildProcess.cpp | child_process.ext.json | Duplicated |
| cluster | Analyzer_StdLib_Cluster.cpp | cluster.ext.json | Duplicated |
| console | Inline in Analyzer_StdLib.cpp | console.ext.json | Duplicated (mixed) |
| crypto | Analyzer_StdLib_Crypto.cpp | crypto.ext.json | Duplicated |
| dgram | Analyzer_StdLib_Dgram.cpp | dgram.ext.json | Duplicated |
| dns | Analyzer_StdLib_DNS.cpp | dns.ext.json | Duplicated |
| events | Analyzer_StdLib_Events.cpp | events.ext.json | Duplicated |
| fs | Analyzer_StdLib_FS.cpp | fs.ext.json | Duplicated |
| http | Analyzer_StdLib_HTTP.cpp | http.ext.json | Duplicated |
| http2 | Analyzer_StdLib_Http2.cpp | http2.ext.json | Duplicated |
| inspector | Analyzer_StdLib_Inspector.cpp | inspector.ext.json | Duplicated |
| module | Analyzer_StdLib_Module.cpp | module.ext.json | Duplicated |
| net | Analyzer_StdLib_Net.cpp | net.ext.json | Duplicated |
| os | Analyzer_StdLib_OS.cpp | os.ext.json | Duplicated |
| path | Analyzer_StdLib_Path.cpp | path.ext.json | Duplicated |
| perf_hooks | Analyzer_StdLib_PerfHooks.cpp | perf_hooks.ext.json | Duplicated |
| process | Analyzer_StdLib_Process.cpp | process.ext.json | Duplicated |
| querystring | Analyzer_StdLib_QueryString.cpp | querystring.ext.json | Duplicated |
| readline | Analyzer_StdLib_Readline.cpp | readline.ext.json | Duplicated |
| stream | Analyzer_StdLib_Stream.cpp | stream.ext.json | Duplicated |
| string_decoder | Analyzer_StdLib_StringDecoder.cpp | string_decoder.ext.json | Duplicated |
| tls | Analyzer_StdLib_Tls.cpp | tls.ext.json | Duplicated |
| tty | Analyzer_StdLib_TTY.cpp | tty.ext.json | Duplicated |
| url | Analyzer_StdLib_URLModule.cpp | url.ext.json | Duplicated |
| util | Analyzer_StdLib_Util.cpp | util.ext.json | Duplicated |
| v8 | Analyzer_StdLib_V8.cpp | v8.ext.json | Duplicated |
| vm | Analyzer_StdLib_VM.cpp | vm.ext.json | Duplicated |

### Other Hardcoded Analyzer Code

| Location | What It Does | Why Hardcoded |
|----------|-------------|---------------|
| `Analyzer_Expressions_Calls.cpp:42-78` | Contextual callback typing for http/net/fs/EventEmitter | Extension schema has no callback signature field |
| `Analyzer_Expressions_Calls.cpp:190-224` | Promise `.then()`/`.catch()` callback inference | Requires inspecting receiver's generic type args |
| `Analyzer_Expressions_Calls.cpp:749-885` | `new Promise(executor)` type inference from resolve() calls | Requires AST body scanning |
| `Analyzer_Expressions.cpp:188` | `fs.promises` nested property special-casing | Handled by nestedObjects in extensions |
| `ModuleResolver.cpp:23-30` | Hardcoded list of 35 builtin module names | Extension system loaded too late to affect resolution |

---

## 2. Lowering Registry: Dual Registration

### Current State

`LoweringRegistry.cpp` has two registration paths that both populate the same registry:

1. `registerBuiltinsImpl()` - ~200 hardcoded lowerings in C++ (runs first)
2. `registerFromExtensions()` - reads from `.ext.json` files (skips duplicates)

### Node.js-Specific Lowerings in registerBuiltinsImpl()

These are hardcoded in builtins but also defined in extension JSON:

| Prefix | Count | Extension JSON | Status |
|--------|-------|---------------|--------|
| `ts_console_*` | 18 | console.ext.json | Duplicated |
| `ts_crypto_*` | 6 | crypto.ext.json | Duplicated |
| `ts_performance_*` | 2 | perf_hooks.ext.json | Duplicated |

### Core Lowerings (Should Stay Hardcoded)

| Prefix | Count | Reason |
|--------|-------|--------|
| `ts_array_*` | 40 | Core data structure |
| `ts_math_*` | 35 | Core numeric ops |
| `ts_string_*` | 26 | Core data type |
| `ts_map_*` | 14 | Core data structure |
| `ts_object_*` | 13 | Core language feature |
| `ts_set_*` | 11 | Core data structure |
| `ts_bigint_*` | 6 | Core data type |
| `ts_json_*` | 2 | Core utility |
| `ts_symbol_*` | 3 | Core data type |
| `ts_regexp_*` | 2 | Core data type |
| `ts_error_*` | 1 | Core exception |
| `ts_typeof/instanceof` | 2 | Core language feature |

---

## 3. Runtime: Node.js Code in Core Library

### Current State

The core runtime (`src/runtime/`) includes ~26,700 LOC of Node.js-specific code compiled into `tsruntime.lib`. Many of these files have been copied to extension directories but are STILL compiled as part of the core library.

### Files in src/runtime/src/ That Should Be Extensions Only

| File | LOC | Module | Extension Dir Exists |
|------|-----|--------|---------------------|
| TsHttp.cpp | 2,538 | http | Yes |
| TsHttp2.cpp | 2,135 | http2 | Yes |
| TsUtil.cpp | 2,624 | util | Yes |
| TsDNS.cpp | 1,513 | dns | Yes |
| TsZlib.cpp | 1,415 | zlib | Yes |
| TsChildProcess.cpp | 1,373 | child_process | Yes |
| TsURL.cpp | 1,279 | url | Yes |
| TsUDPSocket.cpp | 927 | dgram | Yes |
| TsReadline.cpp | 734 | readline | Yes |
| TsPerfHooks.cpp | 644 | perf_hooks | Yes |
| TsAssert.cpp | 613 | assert | Yes |
| TsCluster.cpp | 607 | cluster | Yes |
| TsAsyncHooks.cpp | 522 | async_hooks | Yes |
| TsEventEmitter.cpp | ~400 | events | Yes |
| TsQueryString.cpp | ~400 | querystring | Yes |
| TsGlobals.cpp | ~300 | global | Hybrid |
| Crypto.cpp | ~300 | crypto | Yes |
| TsOS.cpp | ~200 | os | Yes |
| TsInspector.cpp | ~200 | inspector | Yes |
| TsModule.cpp | ~200 | module | Yes |
| TsV8.cpp | ~100 | v8 | Yes |
| TsVM.cpp | ~100 | vm | Yes |

### Files in src/runtime/src/node/ That Should Be Extensions Only

| File | LOC | Module |
|------|-----|--------|
| fs.cpp | 3,468 | fs |
| Socket.cpp | 1,052 | net |
| SecureSocket.cpp | 901 | tls |
| ReadStream.cpp | 882 | stream |
| TTY.cpp | 415 | tty |
| WriteStream.cpp | 226 | stream |
| Server.cpp | 200 | net |
| Writable.cpp | 170 | stream |

### Circular Dependency: TsObject.cpp

`TsObject.cpp` (core runtime) has hardcoded `dynamic_cast` dispatches for Node.js types:
- `TsIncomingMessage` property access (statusCode, method, url, headers)
- `TsServerResponse` property access
- `TsSocket` magic byte checks

This creates a dependency from the core runtime on Node.js headers (`#include "TsHttp.h"`).

---

## 4. Missing Extension Metadata

These extension directories exist with source files but have no `.ext.json` contract:

- `extensions/node/async_hooks/`
- `extensions/node/cluster/`
- `extensions/node/dns/`
- `extensions/node/events/`
- `extensions/node/fs/`
- `extensions/node/http/`
- `extensions/node/http2/`
- `extensions/node/inspector/`
- `extensions/node/module/`
- `extensions/node/readline/`
- `extensions/node/stream/`
- `extensions/node/tty/`
- `extensions/node/v8/`
- `extensions/node/vm/`

---

## 5. Migration Priorities

### Phase 1: Remove Analyzer Duplication
Remove the 30 `Analyzer_StdLib_*.cpp` files once the extension system can handle all their functionality. This requires the schema enhancements described in `analyzer-hardcoding-analysis.md`.

### Phase 2: Move Runtime Files to Extensions
Remove Node.js `.cpp` files from `src/runtime/CMakeLists.txt` and compile them only as extension libraries.

### Phase 3: Remove Core Runtime Circular Dependencies
Replace hardcoded property dispatch in `TsObject.cpp` with an extensible mechanism (vtable dispatch, registered property handlers, etc.).

### Phase 4: Derive Module List from Extensions
Replace the hardcoded `BUILTIN_MODULES` list in `ModuleResolver.cpp` with a query to the extension registry.
