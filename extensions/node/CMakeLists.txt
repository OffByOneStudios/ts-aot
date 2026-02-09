# Node.js module extensions for ts-aot
#
# Each subdirectory contains a Node.js module implementation that can be
# conditionally linked when the corresponding module is imported.
#
# Available modules:
#   - path: Path manipulation (path.join, path.resolve, etc.)
#   - os: Operating system information (os.platform, os.cpus, etc.)
#   - util: Utility functions (util.format, util.promisify, etc.)
#   - assert: Testing utilities (assert.ok, assert.equal, etc.)
#   - url: URL parsing (URL, URLSearchParams, querystring)
#   - dns: DNS resolution (dns.lookup, dns.resolve, etc.)
#   - dgram: UDP sockets (dgram.createSocket, socket.send, etc.)
#   - zlib: Compression (gzip, deflate, brotli)
#   - crypto: Cryptographic functions (hash, cipher, sign/verify)
#   - events: EventEmitter
#   - stream: Readable/Writable streams (pipe, pipeline, finished, etc.)
#   - fs: File system operations
#   - tty: TTY read/write streams
#   - net: TCP sockets and servers
#   - vm: VM stubs (AOT incompatible)
#   - v8: V8 stubs (AOT incompatible)
#   - inspector: Inspector stubs (no V8 inspector)
#   - module: Module utilities
#   - readline: Line input
#   - string_decoder: String decoding
#   - perf_hooks: Performance timing
#   - async_hooks: Async context tracking
#   - http: HTTP server/client
#   - http2: HTTP/2 server/client
#   - child_process: Process spawning
#   - cluster: Multi-process forking

# nodecore: Node.js infrastructure layer (Buffer, HTTP, Sockets, etc.)
# Must be before other extensions that depend on nodecore types
add_subdirectory(core)

add_subdirectory(path)
add_subdirectory(os)
add_subdirectory(util)
add_subdirectory(assert)
add_subdirectory(url)
add_subdirectory(dns)
add_subdirectory(dgram)
add_subdirectory(zlib)
add_subdirectory(crypto)
add_subdirectory(events)
add_subdirectory(stream)
add_subdirectory(fs)
add_subdirectory(tty)
add_subdirectory(net)
add_subdirectory(vm)
add_subdirectory(v8)
add_subdirectory(inspector)
add_subdirectory(module)
add_subdirectory(readline)
add_subdirectory(string_decoder)
add_subdirectory(perf_hooks)
add_subdirectory(async_hooks)
add_subdirectory(http)
add_subdirectory(http2)
add_subdirectory(child_process)
add_subdirectory(cluster)
add_subdirectory(fetch)
