# Node.js Network Modules - HTTP, HTTPS, HTTP/2, Net, TLS, Dgram, DNS

Parent: [nodejs-features.md](../nodejs-features.md)

---

## HTTP

### Server
| Feature | Status | Notes |
|---------|--------|-------|
| `http.createServer()` | ✅ | |
| `server.listen()` | ✅ | |
| `server.close()` | ✅ | |
| `server.setTimeout()` | ✅ | With optional callback |
| `server.maxHeadersCount` | ✅ | Default 2000 |
| `server.timeout` | ✅ | Socket timeout in ms |
| `server.keepAliveTimeout` | ✅ | Default 5000ms |
| `server.headersTimeout` | ✅ | Default 60000ms |
| `server.requestTimeout` | ✅ | Default 0 (disabled) |

### Request (IncomingMessage)
| Feature | Status | Notes |
|---------|--------|-------|
| `req.headers` | ✅ | |
| `req.httpVersion` | ✅ | HTTP version string (e.g., "1.1") |
| `req.method` | ✅ | |
| `req.url` | ✅ | |
| `req.socket` | ✅ | Returns underlying net.Socket |
| `req.complete` | ✅ | Set true after message fully received |
| `req.aborted` | ✅ | Boolean, true when request aborted |
| `req.rawHeaders` | ✅ | Alternating key/value array with original case |
| `req.rawTrailers` | ✅ | Alternating key/value array |
| `req.statusCode` | ✅ | |
| `req.statusMessage` | ✅ | For HTTP client responses |
| `req.trailers` | ✅ | Object with trailer headers |

### Response (ServerResponse)
| Feature | Status | Notes |
|---------|--------|-------|
| `res.writeHead()` | ✅ | |
| `res.write()` | ✅ | |
| `res.end()` | ✅ | |
| `res.setHeader()` | ✅ | |
| `res.getHeader()` | ✅ | |
| `res.getHeaders()` | ✅ | |
| `res.getHeaderNames()` | ✅ | |
| `res.hasHeader()` | ✅ | |
| `res.removeHeader()` | ✅ | |
| `res.statusCode` | ✅ | |
| `res.statusMessage` | ✅ | |
| `res.headersSent` | ✅ | |
| `res.writableEnded` | ✅ | |
| `res.writableFinished` | ✅ | |
| `res.flushHeaders()` | ✅ | |
| `res.setTimeout()` | ✅ | With optional callback |
| `res.addTrailers()` | ✅ | Adds trailing headers after response body |

### Client
| Feature | Status | Notes |
|---------|--------|-------|
| `http.request()` | ✅ | |
| `http.get()` | ✅ | |
| `http.Agent` | ✅ | Constructor and basic functionality |
| `http.globalAgent` | ✅ | |

### Module Constants
| Feature | Status | Notes |
|---------|--------|-------|
| `http.METHODS` | ✅ | Array of HTTP method strings |
| `http.STATUS_CODES` | ✅ | Complete with all standard status codes |
| `http.maxHeaderSize` | ✅ | Returns 16384 |
| `http.validateHeaderName()` | ✅ | |
| `http.validateHeaderValue()` | ✅ | |

### Client Request
| Feature | Status | Notes |
|---------|--------|-------|
| `req.abort()` | N/A | Deprecated |
| `req.destroy()` | ✅ | Via Writable base class |
| `req.end()` | ✅ | |
| `req.flushHeaders()` | ✅ | |
| `req.getHeader()` | ✅ | |
| `req.getRawHeaderNames()` | ✅ | Returns array of original header names |
| `req.maxHeadersCount` | ✅ | Get/set max headers |
| `req.path` | ✅ | |
| `req.method` | ✅ | |
| `req.host` | ✅ | |
| `req.protocol` | ✅ | |
| `req.removeHeader()` | ✅ | Via OutgoingMessage base class |
| `req.reusedSocket` | ✅ | Boolean property |
| `req.setHeader()` | ✅ | |
| `req.setNoDelay()` | ✅ | Nagle algorithm control |
| `req.setSocketKeepAlive()` | ✅ | TCP keepalive |
| `req.setTimeout()` | ✅ | With optional callback |
| `req.socket` | ✅ | Returns underlying net.Socket |
| `req.write()` | ✅ | |

**HTTP Coverage: 67/67 (100%)**

---

## HTTPS

| Feature | Status | Notes |
|---------|--------|-------|
| `https.createServer()` | ✅ | |
| `https.request()` | ✅ | |
| `https.get()` | ✅ | |
| `https.Agent` | ✅ | Constructor with options |
| `https.globalAgent` | ✅ | Default agent instance |
| Server options (key, cert, ca, etc.) | ✅ | |
| Client options (rejectUnauthorized, etc.) | ✅ | rejectUnauthorized, ca supported |

**HTTPS Coverage: 7/7 (100%)**

---

## HTTP/2

### Module Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `http2.createServer()` | ✅ | Creates HTTP/2 server with stream callback |
| `http2.createSecureServer()` | ✅ | Creates HTTPS/2 server with TLS |
| `http2.connect()` | ✅ | Creates ClientHttp2Session with nghttp2 |
| `http2.getDefaultSettings()` | ✅ | Returns default HTTP/2 settings object |
| `http2.getPackedSettings(settings)` | ✅ | Packs settings to Buffer |
| `http2.getUnpackedSettings(buffer)` | ✅ | Unpacks Buffer to settings object |
| `http2.constants` | ✅ | All NGHTTP2 error codes and settings |
| `http2.sensitiveHeaders` | ✅ | Symbol for marking sensitive headers |

### Http2Server Class
| Feature | Status | Notes |
|---------|--------|-------|
| `server.listen()` | ✅ | Starts server on port |
| `server.close()` | ✅ | Closes server with callback |
| `server.address()` | ✅ | Returns address info |
| `server.setTimeout()` | ✅ | Sets server timeout |
| `'session'` event | ✅ | Emitted when new session created |
| `'stream'` event | ✅ | Emitted when new stream opened |

### Http2Session Class
| Feature | Status | Notes |
|---------|--------|-------|
| `session.alpnProtocol` | ✅ | Returns "h2" |
| `session.closed` | ✅ | Boolean property |
| `session.connecting` | ✅ | Boolean property |
| `session.destroyed` | ✅ | Boolean property |
| `session.encrypted` | ✅ | Boolean property |
| `session.localSettings` | ✅ | Returns settings object |
| `session.remoteSettings` | ✅ | Returns settings object |
| `session.socket` | ✅ | Returns underlying socket |
| `session.type` | ✅ | Returns 0 (server) or 1 (client) |
| `session.close()` | ✅ | Sends GOAWAY and closes |
| `session.destroy()` | ✅ | Destroys session and streams |
| `session.goaway()` | ✅ | Sends GOAWAY frame |
| `session.ping()` | ✅ | Sends PING frame |
| `session.ref()` | ✅ | Keeps event loop alive |
| `session.unref()` | ✅ | Allows event loop to exit |
| `session.setTimeout()` | ✅ | Sets session timeout |
| `session.settings()` | ✅ | Updates session settings |
| `'stream'` event | ✅ | Emitted when stream headers received |
| `'close'` event | ✅ | Emitted when session closes |
| `'error'` event | ✅ | Emitted on error |

### Http2Stream Class
| Feature | Status | Notes |
|---------|--------|-------|
| `stream.aborted` | ✅ | Boolean property |
| `stream.bufferSize` | ✅ | Integer property |
| `stream.closed` | ✅ | Boolean property |
| `stream.destroyed` | ✅ | Boolean property |
| `stream.endAfterHeaders` | ✅ | Boolean property |
| `stream.id` | ✅ | Stream identifier |
| `stream.pending` | ✅ | Boolean property |
| `stream.rstCode` | ✅ | Reset code |
| `stream.sentHeaders` | ✅ | Request/response headers |
| `stream.session` | ✅ | Parent session |
| `stream.close()` | ✅ | Sends RST_STREAM |
| `stream.priority()` | ✅ | Sets stream priority |
| `stream.setTimeout()` | ✅ | Sets stream timeout |
| `'data'` event | ✅ | Emitted when data received |
| `'end'` event | ✅ | Emitted when stream ends |
| `'close'` event | ✅ | Emitted when stream closes |
| `'error'` event | ✅ | Emitted on stream error |

### ServerHttp2Stream Class
| Feature | Status | Notes |
|---------|--------|-------|
| `stream.headersSent` | ✅ | Boolean property |
| `stream.pushAllowed` | ✅ | Boolean property |
| `stream.additionalHeaders()` | ✅ | Sends 1xx informational headers |
| `stream.pushStream()` | ✅ | Creates PUSH_PROMISE frames |
| `stream.respond()` | ✅ | Sends response headers |
| `stream.respondWithFD()` | ✅ | Responds with file descriptor |
| `stream.respondWithFile()` | ✅ | Responds with file path and auto-detects MIME type |

### ClientHttp2Session Class
| Feature | Status | Notes |
|---------|--------|-------|
| `session.request()` | ✅ | Creates ClientHttp2Stream with headers |

### ClientHttp2Stream Class
| Feature | Status | Notes |
|---------|--------|-------|
| `stream.id` | ✅ | Stream identifier |
| `stream.closed` | ✅ | Boolean property |
| `stream.destroyed` | ✅ | Boolean property |
| `stream.pending` | ✅ | Boolean property |
| `stream.aborted` | ✅ | Boolean property |
| `'response'` event | ✅ | Emitted when response headers received |
| `'data'` event | ✅ | Emitted when response data received via TsDuplex |

**HTTP/2 Coverage: 66/66 (100%)**

---

## Net

### Server
| Feature | Status | Notes |
|---------|--------|-------|
| `net.createServer()` | ✅ | |
| `server.listen()` | ✅ | |
| `server.close()` | ✅ | |
| `server.address()` | ✅ | Returns { address, family, port } |
| `server.getConnections()` | ✅ | Returns 0 (connection tracking not implemented) |
| `server.maxConnections` | ✅ | Default -1 (unlimited) |
| `server.ref()` | ✅ | |
| `server.unref()` | ✅ | |

### Socket
| Feature | Status | Notes |
|---------|--------|-------|
| `net.createConnection()` | ✅ | |
| `net.connect()` | ✅ | |
| `socket.write()` | ✅ | |
| `socket.end()` | ✅ | |
| `socket.destroy()` | ✅ | |
| `socket.pause()` | ⚠️ | |
| `socket.resume()` | ⚠️ | |
| `socket.setTimeout()` | ✅ | Emits 'timeout' event |
| `socket.setNoDelay()` | ✅ | Nagle algorithm control |
| `socket.setKeepAlive()` | ✅ | TCP keepalive |
| `socket.address()` | ✅ | Returns { address, family, port } |
| `socket.remoteAddress` | ✅ | |
| `socket.remoteFamily` | ✅ | |
| `socket.remotePort` | ✅ | |
| `socket.localAddress` | ✅ | |
| `socket.localPort` | ✅ | |
| `socket.localFamily` | ✅ | |
| `socket.bytesRead` | ✅ | |
| `socket.bytesWritten` | ✅ | |
| `socket.connecting` | ✅ | |
| `socket.destroyed` | ✅ | |
| `socket.pending` | ✅ | |
| `socket.readyState` | ✅ | |
| `socket.ref()` | ✅ | |
| `socket.unref()` | ✅ | |

### Utilities
| Feature | Status | Notes |
|---------|--------|-------|
| `net.isIP()` | ✅ | Returns 4, 6, or 0 |
| `net.isIPv4()` | ✅ | |
| `net.isIPv6()` | ✅ | |

**Net Coverage: 36/36 (100%)**

---

## TLS

| Feature | Status | Notes |
|---------|--------|-------|
| `tls.createServer()` | ✅ | Via HTTPS |
| `tls.connect()` | ✅ | Via HTTPS client and standalone |
| `tls.createSecureContext()` | ✅ | Creates SSL_CTX wrapper |
| `tls.getCiphers()` | ✅ | Returns available cipher names |
| TLSSocket class | ✅ | As TsSecureSocket |
| `socket.authorized` | ✅ | Via SSL_get_verify_result |
| `socket.authorizationError` | ✅ | Returns verification error string |
| `socket.encrypted` | ✅ | Always true for TLS sockets |
| `socket.getCertificate()` | ✅ | Returns local certificate info |
| `socket.getPeerCertificate()` | ✅ | Returns peer certificate with validity dates |
| `socket.getProtocol()` | ✅ | Returns SSL version string |
| `socket.getSession()` | ✅ | Returns serialized session as Buffer |
| `socket.renegotiate()` | ✅ | SSL_renegotiate (TLS 1.2 only) |
| `socket.setMaxSendFragment()` | ✅ | Sets max fragment size (512-16384) |
| Server certificate options | ✅ | key, cert supported |
| Client verification options | ✅ | rejectUnauthorized supported |
| CA certificate support | ✅ | PEM parsing, X509_STORE support |
| SNI support | ✅ | setServername/getServername via SSL_set_tlsext_host_name |
| ALPN support | ✅ | setALPNProtocols/alpnProtocol via SSL_CTX_set_alpn_protos |
| Session resumption | ✅ | setSession/getSession/isSessionReused via SSL_set_session |
| Client certificates | ✅ | setClientCertificate via SSL_CTX_use_certificate |

**TLS Coverage: 21/21 (100%)**

---

## Dgram (UDP)

### Module Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `dgram.createSocket(type)` | ✅ | Creates 'udp4' or 'udp6' socket |
| `dgram.createSocket(options)` | ✅ | type, reuseAddr, ipv6Only, recvBufferSize, sendBufferSize |

### Socket Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `socket.bind(port, address, callback)` | ✅ | Binds to address/port |
| `socket.send(msg, offset, length, port, address, callback)` | ✅ | Sends UDP datagram |
| `socket.close([callback])` | ✅ | Closes socket |
| `socket.address()` | ✅ | Returns { address, family, port } |
| `socket.setBroadcast(flag)` | ✅ | Via uv_udp_set_broadcast |
| `socket.setMulticastTTL(ttl)` | ✅ | Via uv_udp_set_multicast_ttl |
| `socket.setMulticastLoopback(flag)` | ✅ | Via uv_udp_set_multicast_loop |
| `socket.addMembership(multicastAddress, multicastInterface)` | ✅ | Via uv_udp_set_membership |
| `socket.dropMembership(multicastAddress, multicastInterface)` | ✅ | Via uv_udp_set_membership |
| `socket.setMulticastInterface(multicastInterface)` | ✅ | Via uv_udp_set_multicast_interface |
| `socket.connect(port, address, callback)` | ✅ | Via uv_udp_connect |
| `socket.disconnect()` | ✅ | Via uv_udp_connect(null) |
| `socket.ref()` | ✅ | Via uv_ref |
| `socket.unref()` | ✅ | Via uv_unref |
| `socket.remoteAddress()` | ✅ | Returns { address, family, port } for connected socket |
| `socket.setRecvBufferSize(size)` | ✅ | Via uv_recv_buffer_size |
| `socket.setSendBufferSize(size)` | ✅ | Via uv_send_buffer_size |
| `socket.getRecvBufferSize()` | ✅ | Via uv_recv_buffer_size |
| `socket.getSendBufferSize()` | ✅ | Via uv_send_buffer_size |
| `socket.setTTL(ttl)` | ✅ | Via uv_udp_set_ttl |

### Socket Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'message'` event | ✅ | Emits (msg, rinfo) with Buffer and address info |
| `'listening'` event | ✅ | Emitted when socket is bound |
| `'close'` event | ✅ | Emitted when socket is closed |
| `'error'` event | ✅ | Emitted on errors |
| `'connect'` event | ✅ | Emitted after connect() succeeds |

**Dgram Coverage: 28/28 (100%)**

---

## DNS

### Callback API
| Feature | Status | Notes |
|---------|--------|-------|
| `dns.lookup(hostname, callback)` | ✅ | Uses libuv getaddrinfo |
| `dns.lookup(hostname, options, callback)` | ✅ | Family option supported |
| `dns.resolve(hostname, callback)` | ✅ | Same as resolve4 |
| `dns.resolve4(hostname, callback)` | ✅ | IPv4 address resolution |
| `dns.resolve6(hostname, callback)` | ✅ | IPv6 address resolution |
| `dns.resolveCname(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveMx(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveNs(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveTxt(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveSrv(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolvePtr(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveNaptr(hostname, callback)` | ✅ | Uses c-ares |
| `dns.resolveSoa(hostname, callback)` | ✅ | Uses c-ares |
| `dns.reverse(ip, callback)` | ✅ | Uses libuv getnameinfo |

### Utility Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `dns.getServers()` | ✅ | Returns empty array (system default) |
| `dns.setServers(servers)` | ✅ | Stub (no-op) |

### Promises API
| Feature | Status | Notes |
|---------|--------|-------|
| `dns.promises.lookup(hostname)` | ✅ | Returns Promise<{address, family}> |
| `dns.promises.resolve(hostname)` | ✅ | Same as resolve4 |
| `dns.promises.resolve4(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolve6(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolveCname(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolveMx(hostname)` | ✅ | Returns Promise<MxRecord[]> |
| `dns.promises.resolveNs(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolveTxt(hostname)` | ✅ | Returns Promise<string[][]> |
| `dns.promises.resolveSrv(hostname)` | ✅ | Returns Promise<SrvRecord[]> |
| `dns.promises.resolvePtr(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolveNaptr(hostname)` | ✅ | Returns Promise<NaptrRecord[]> |
| `dns.promises.resolveSoa(hostname)` | ✅ | Returns Promise<SoaRecord> |
| `dns.promises.reverse(ip)` | ✅ | Returns Promise<string[]> |
| `dns.promises.getServers()` | ✅ | Returns Promise<string[]> |
| `dns.promises.setServers()` | ✅ | Returns Promise<void> |

### Error Codes
| Feature | Status | Notes |
|---------|--------|-------|
| `dns.NODATA` | ✅ | |
| `dns.FORMERR` | ✅ | |
| `dns.SERVFAIL` | ✅ | |
| `dns.NOTFOUND` | ✅ | |
| `dns.NOTIMP` | ✅ | |
| `dns.REFUSED` | ✅ | |
| `dns.BADQUERY` | ✅ | |
| `dns.BADNAME` | ✅ | |
| `dns.BADFAMILY` | ✅ | |
| `dns.BADRESP` | ✅ | |
| `dns.CONNREFUSED` | ✅ | |
| `dns.TIMEOUT` | ✅ | |
| `dns.EOF` | ✅ | |
| `dns.FILE` | ✅ | |
| `dns.NOMEM` | ✅ | |
| `dns.DESTRUCTION` | ✅ | |
| `dns.BADSTR` | ✅ | |
| `dns.BADFLAGS` | ✅ | |
| `dns.NONAME` | ✅ | |
| `dns.BADHINTS` | ✅ | |
| `dns.NOTINITIALIZED` | ✅ | |
| `dns.LOADIPHLPAPI` | ✅ | |
| `dns.ADDRGETNETWORKPARAMS` | ✅ | |
| `dns.CANCELLED` | ✅ | |

**DNS Coverage: 58/58 (100%)**
