# EXT-001: Node.js Module Extension Library Migrations

**Status:** In Progress
**Phase:** 4/5 - Extension Contract System

## Overview

Migrate Node.js modules from the monolithic tsruntime into separate extension libraries. Each module becomes its own CMake static library that is conditionally linked only when used.

## Completed

| Module | Library | Status |
|--------|---------|--------|
| path | ts_path | Done |

## Migration Phases

### Phase 1: Standalone Modules (No Dependencies)
Simple modules with no runtime dependencies on other Node.js modules.

| Module | Source Files | Priority | Est. Effort |
|--------|--------------|----------|-------------|
| os | TsOS.cpp | High | Small |
| url | TsURL.cpp, TsQueryString.cpp | High | Medium |
| assert | TsAssert.cpp | Medium | Small |
| util | TsUtil.cpp | High | Medium |
| perf_hooks | TsPerfHooks.cpp | Low | Small |
| string_decoder | TsStringDecoder.cpp | Low | Small |
| zlib | TsZlib.cpp | Medium | Small |
| module | TsModule.cpp | Low | Small |
| inspector | TsInspector.cpp | Low | Small |

### Phase 2: Buffer/Encoding (Shared Foundation)
Buffer is widely used by other modules, so it may need special handling.

| Module | Source Files | Priority | Notes |
|--------|--------------|----------|-------|
| buffer | TsBuffer.cpp, TsTextEncoding.cpp | High | Used by fs, crypto, http, net |

**Decision needed:** Keep buffer in tsruntime (since it's a core type) or extract with special linking rules.

### Phase 3: Event-Based Modules (Depend on EventEmitter)
These modules depend on the EventEmitter base class.

| Module | Source Files | Priority | Notes |
|--------|--------------|----------|-------|
| events | EventEmitter.cpp | High | Base class for all event modules |
| stream | Writable.cpp | High | Base class for streams |
| readline | TsReadline.cpp | Medium | Depends on events |
| async_hooks | TsAsyncHooks.cpp | Medium | Event-like patterns |

**Dependency chain:** events -> stream -> (fs, net, http, etc.)

### Phase 4: Network/IO Modules (Complex Dependencies)
These have significant interdependencies.

| Module | Source Files | Priority | Notes |
|--------|--------------|----------|-------|
| dns | TsDNS.cpp | Medium | Standalone network |
| dgram | TsUDPSocket.cpp | Medium | UDP sockets |
| net | Socket.cpp, Server.cpp, TTY.cpp | High | TCP sockets, depends on stream |
| tls | SecureSocket.cpp | High | Depends on net |
| fs | fs.cpp, ReadStream.cpp, WriteStream.cpp | High | File system, depends on stream |
| http | TsHttp.cpp, TsFetch.cpp | High | Depends on net, stream |
| http2 | TsHttp2.cpp | Medium | Depends on http, net |
| crypto | Crypto.cpp | High | Used by tls, https |
| child_process | TsChildProcess.cpp | Medium | Process management |
| cluster | TsCluster.cpp | Low | Depends on child_process |

## Module Dependency Graph

```
Core Runtime (tsruntime)
├── TsObject, TsArray, TsMap, TsString (fundamental types)
├── TsPromise, TsError (async/error handling)
├── TsDate, TsRegExp, TsJSON, Math (built-in types)
└── EventLoop, Memory, Core (infrastructure)

Extension Libraries
├── ts_path (done)
├── ts_os
├── ts_url (includes querystring)
├── ts_util
├── ts_assert
├── ts_buffer (decision: core or extension?)
├── ts_events (EventEmitter base)
│   └── ts_stream (Writable, Readable, etc.)
│       ├── ts_fs (ReadStream, WriteStream)
│       ├── ts_net (Socket, Server)
│       │   ├── ts_tls (SecureSocket)
│       │   └── ts_http
│       │       └── ts_http2
│       └── ts_readline
├── ts_dns
├── ts_dgram
├── ts_crypto
├── ts_child_process
│   └── ts_cluster
├── ts_async_hooks
├── ts_zlib
├── ts_perf_hooks
├── ts_string_decoder
├── ts_module
└── ts_inspector
```

## Recommended Migration Order

Based on dependencies and usage frequency:

1. **ts_os** - No dependencies, commonly used
2. **ts_util** - No dependencies, commonly used
3. **ts_assert** - No dependencies, test support
4. **ts_url** - No dependencies (merge querystring), commonly used
5. **ts_dns** - Standalone network primitives
6. **ts_dgram** - Standalone UDP
7. **ts_zlib** - Standalone compression
8. **ts_perf_hooks** - Standalone performance
9. **ts_string_decoder** - Standalone encoding
10. **ts_crypto** - Used by tls but can stand alone
11. **ts_events** - Base for event-driven modules
12. **ts_stream** - Depends on events
13. **ts_fs** - Depends on stream
14. **ts_net** - Depends on stream
15. **ts_tls** - Depends on net, crypto
16. **ts_http** - Depends on net, stream
17. **ts_http2** - Depends on http
18. **ts_readline** - Depends on events
19. **ts_child_process** - Process management
20. **ts_cluster** - Depends on child_process
21. **ts_async_hooks** - Async context tracking
22. **ts_module** - Module stubs
23. **ts_inspector** - Inspector stubs

## Directory Structure Template

```
extensions/node/<module>/
├── CMakeLists.txt          # Build configuration
├── <module>.ext.json       # Extension contract
├── include/
│   └── Ts<Module>.h        # Public C API header
└── src/
    └── <module>.cpp        # Implementation
```

## CMakeLists.txt Template

```cmake
add_library(ts_<module> STATIC
    src/<module>.cpp
)

target_include_directories(ts_<module> PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(ts_<module> PRIVATE
    ${CMAKE_SOURCE_DIR}/src/runtime/include
)

# If depends on other extensions:
# target_link_libraries(ts_<module> PRIVATE ts_<dependency>)

target_link_libraries(ts_<module> PRIVATE tsruntime)

# Copy extension contract to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/<module>.ext.json
    ${CMAKE_BINARY_DIR}/extensions/node/<module>/<module>.ext.json
    COPYONLY
)
```

## Driver.cpp Updates Needed

For each migrated module, add to Driver.cpp:
1. Library path for the extension
2. Library name to link list

Consider automating this via the extension registry - query which extensions are used and add their libraries dynamically.

## Future: Conditional Linking

Ideal state: Compiler analyzes imports and only links needed extensions:
```typescript
import * as path from 'path';  // Links ts_path.lib
import * as fs from 'fs';      // Links ts_fs.lib + ts_stream.lib + ts_events.lib
```

This requires:
1. Extension registry tracks which ts_* functions map to which library
2. Codegen marks which extensions are used during compilation
3. Driver queries used extensions and adds only those libraries

## Notes

- Keep buffer in tsruntime for now (too fundamental)
- EventEmitter may need to stay in tsruntime since many runtime types inherit from it
- Consider bundling small related modules (url+querystring, inspector+module)
