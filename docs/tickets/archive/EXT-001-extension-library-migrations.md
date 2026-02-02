# EXT-001: Node.js Module Extension Library Migrations

**Status:** Complete
**Completed:** 2026-02-02

## Overview

Migrated all 26 Node.js modules from the monolithic tsruntime into separate extension libraries. Each module is its own CMake static library that is linked at compile time.

## Completed Modules

| Module | Library | Status |
|--------|---------|--------|
| path | ts_path | Done |
| os | ts_os | Done |
| util | ts_util | Done |
| assert | ts_assert | Done |
| url | ts_url | Done |
| dns | ts_dns | Done |
| dgram | ts_dgram | Done |
| zlib | ts_zlib | Done |
| crypto | ts_crypto | Done |
| events | ts_events | Done |
| stream | ts_stream | Done |
| fs | ts_fs | Done |
| net | ts_net | Done |
| tty | ts_tty | Done |
| http | ts_http | Done |
| http2 | ts_http2 | Done |
| child_process | ts_child_process | Done |
| cluster | ts_cluster | Done |
| readline | ts_readline | Done |
| string_decoder | ts_string_decoder | Done |
| perf_hooks | ts_perf_hooks | Done |
| async_hooks | ts_async_hooks | Done |
| inspector | ts_inspector | Done (stub) |
| module | ts_module | Done (stub) |
| vm | ts_vm | Done (stub) |
| v8 | ts_v8 | Done (stub) |

## Migration Phases Completed

### Phase 1-2: Foundation Modules
- path, os, util, assert, url, dns, dgram, zlib, crypto

### Phase 3-4: Core I/O Modules
- events, stream, fs

### Phase 5-6: Network Modules
- net, tty, http, http2

### Phase 7: Process Modules
- child_process, cluster

### Phase 8-9: Utility and Stub Modules
- readline, string_decoder, perf_hooks, async_hooks
- inspector, module, vm, v8 (stubs for AOT-incompatible features)

## Architecture

```
extensions/node/<module>/
├── CMakeLists.txt          # Build configuration
├── include/
│   └── Ts<Module>Ext.h     # Public C API header (optional)
└── src/
    └── <module>.cpp        # Extern "C" wrappers
```

**Key Principles:**
- Runtime class implementations remain in tsruntime (shared code)
- Extern "C" wrappers are isolated in extension libraries
- Each extension links against tsruntime for access to runtime types
- Driver.cpp links all extension libraries

## Benefits

1. **Modularity**: Clean separation between runtime classes and C API wrappers
2. **Maintainability**: Each module can be tested and updated independently
3. **Future**: Architecture supports selective linking (only link used modules)

## Future Work

- Implement conditional linking based on import analysis
- Add extension registry for automatic library discovery
- Consider bundling related small modules
