# EXT-001: Node.js Standard Library Migration to Extensions

**Status:** In Progress
**Created:** 2026-02-01
**Category:** Architecture / Build System

## Overview

Migrate Node.js standard library modules from `src/runtime/` to `extensions/node/` as separate CMake static libraries. This enables conditional linking based on imports and reduces binary size.

## Scope

**In Scope:** Node.js standard library modules (fs, http, net, etc.)

**Out of Scope:**
- ECMA language features (Array, Map, Set, Promise, Date, RegExp, etc.)
- TypeScript features
- Core runtime (Memory, EventLoop, String, Object, etc.)

## Completed Migrations (Phase 1)

| Module | Library | Source | Status |
|--------|---------|--------|--------|
| path | ts_path | path.cpp | ✅ Complete |
| os | ts_os | TsOS.cpp | ✅ Complete |
| util | ts_util | TsUtil.cpp | ✅ Complete |
| assert | ts_assert | TsAssert.cpp | ✅ Complete |
| url | ts_url | TsURL.cpp, TsQueryString.cpp | ✅ Complete |
| dns | ts_dns | TsDNS.cpp | ✅ Complete |
| dgram | ts_dgram | TsUDPSocket.cpp | ✅ Complete |
| zlib | ts_zlib | TsZlib.cpp | ✅ Complete |
| crypto | ts_crypto | Crypto.cpp | ✅ Complete |

## Remaining Migrations

### Phase 2: Core Event System (COMPLETE)
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| events | ts_events | events.cpp | Core only | ✅ Complete |

**Note:** EventEmitter uses a split pattern:
- `TsEventEmitter.cpp` (class impl) stays in tsruntime (other classes inherit from it)
- `events.cpp` (extern "C" wrappers) moved to extension

### Phase 3: Stream System (COMPLETE)
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| stream | ts_stream | stream.cpp | events | ✅ Complete |

**Note:** Stream module uses split pattern similar to events:
- Class implementations (TsReadStream, TsWriteStream, TsReadable, TsWritable, TsTransform) stay in tsruntime
- `stream.cpp` (extern "C" wrappers) moved to extension
- Runtime source files wrapped with `#if 0` to disable duplicate extern "C" sections

### Phase 4: File System (COMPLETE)
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| fs | ts_fs | fs.cpp | libuv | ✅ Complete |

**Note:** fs module is self-contained (TsDirent, TsDir, TsFSWatcher, TsFileHandle are only used within fs.cpp), so entire implementation moved to extension.

### Phase 5: Networking (COMPLETE)
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| net | ts_net | net.cpp | stream, libuv, OpenSSL | ✅ Complete |
| tty | ts_tty | tty.cpp | libuv | ✅ Complete |

**Note:** Both net and tty use the split pattern - class implementations stay in tsruntime (Socket inherits from TsDuplex, TTY streams inherit from Read/WriteStream), while extern "C" wrappers moved to extensions. TsSocketAddress and TsBlockList are self-contained structs moved entirely to the net extension.

### Phase 6: HTTP
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| http | ts_http | TsHttp.cpp, TsFetch.cpp | net, llhttp | ⬜ Pending |
| http2 | ts_http2 | TsHttp2.cpp | net, nghttp2 | ⬜ Pending |

### Phase 7: Process Management
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| child_process | ts_child_process | TsChildProcess.cpp | libuv | ⬜ Pending |
| cluster | ts_cluster | TsCluster.cpp | child_process | ⬜ Pending |

### Phase 8: Utility Modules
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| readline | ts_readline | TsReadline.cpp | events, stream | ⬜ Pending |
| string_decoder | ts_string_decoder | TsStringDecoder.cpp | Core only | ⬜ Pending |
| perf_hooks | ts_perf_hooks | TsPerfHooks.cpp | Core only | ⬜ Pending |
| async_hooks | ts_async_hooks | TsAsyncHooks.cpp | Core only | ⬜ Pending |

### Phase 9: Stub Modules
| Module | Library | Source Files | Dependencies | Status |
|--------|---------|--------------|--------------|--------|
| inspector | ts_inspector | TsInspector.cpp | Core only | ⬜ Pending |
| module | ts_module | TsModule.cpp | Core only | ⬜ Pending |
| vm | ts_vm | TsVM.cpp | Core only | ⬜ Pending |
| v8 | ts_v8 | TsV8.cpp | Core only | ⬜ Pending |

## Migration Pattern

For each module:

1. **Create extension directory structure:**
   ```
   extensions/node/<module>/
   ├── include/
   │   └── Ts<Module>Ext.h    # C API declarations only
   ├── src/
   │   └── <module>.cpp       # Implementation (copied from runtime)
   └── CMakeLists.txt         # Build configuration
   ```

2. **Create public header** (`Ts<Module>Ext.h`):
   - Extract `extern "C"` function declarations
   - Use `void*` for all object types
   - Include guards and C++ extern wrapper

3. **Create CMakeLists.txt:**
   - Define static library target
   - Set include directories (PUBLIC for header, PRIVATE for runtime)
   - Link dependencies (tsruntime + external libs)

4. **Update extensions/node/CMakeLists.txt:**
   - Add `add_subdirectory(<module>)`
   - Update module list in header comment

5. **Update Driver.cpp:**
   - Add module to `extensionModules` vector
   - Add `ts_<module>.lib` to linker command

6. **Update src/runtime/CMakeLists.txt:**
   - Comment out source file with migration note

7. **Test:**
   - Build: `cmake --build build --config Release`
   - Run existing tests for that module

## Progress Summary

| Phase | Modules | Completed | Remaining |
|-------|---------|-----------|-----------|
| 1 | 9 | 9 | 0 |
| 2 | 1 | 1 | 0 |
| 3 | 1 | 1 | 0 |
| 4 | 1 | 1 | 0 |
| 5 | 2 | 2 | 0 |
| 6 | 2 | 0 | 2 |
| 7 | 2 | 0 | 2 |
| 8 | 4 | 0 | 4 |
| 9 | 4 | 0 | 4 |
| **Total** | **26** | **14** | **12** |

## Notes

- **Header naming:** Use `Ts<Module>Ext.h` suffix to avoid Windows case-insensitive conflicts
- **libsodium vcpkg bug:** crypto extension includes workaround for broken target properties
- **md5.h location:** crypto extension needs `src/runtime/src` in include path for md5.h
- **Dependency order:** Respect dependencies when migrating (events before stream, stream before net, etc.)
- **Events special case:** TsObject.cpp returns function pointers to ts_event_emitter_* functions, so unit_tests and any executable needs ts_events.lib
- **Stream split pattern:** Stream classes (TsReadStream, TsWriteStream, TsReadable, TsWritable, TsTransform, TsDuplex) stay in runtime for inheritance; extern "C" wrappers in extension include runtime headers and use classes directly

## Files Modified

- `extensions/node/CMakeLists.txt` - Module registry
- `src/compiler/Driver.cpp` - Linker integration
- `src/runtime/CMakeLists.txt` - Source file removal
