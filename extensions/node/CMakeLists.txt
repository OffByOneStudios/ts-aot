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
#
# Future modules (to be migrated):
#   - http: HTTP server/client
#   - etc.

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
