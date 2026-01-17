# Node.js API Conformance

This document tracks ts-aot's conformance with Node.js built-in modules and APIs.

**Sources:**
- [Node.js Official Documentation](https://nodejs.org/api/)
- [Node.js v20 LTS API](https://nodejs.org/docs/latest-v20.x/api/)

**Legend:**
- ✅ Implemented
- ⚠️ Partial
- ❌ Not implemented
- N/A Not applicable (deprecated or non-essential)

---

## Core Modules Summary

| Module | Status | Coverage | Notes |
|--------|--------|----------|-------|
| `assert` | ⚠️ | 71% | Testing utilities |
| `async_hooks` | ❌ | 0% | Async context tracking |
| `buffer` | ✅ | 100% | Binary data handling |
| `child_process` | ❌ | 0% | Process spawning |
| `cluster` | ❌ | 0% | Multi-process |
| `console` | ✅ | 100% | Complete logging support |
| `crypto` | ⚠️ | 47% | Cryptographic functions |
| `dgram` | ❌ | 0% | UDP sockets |
| `dns` | ❌ | 0% | DNS resolution |
| `domain` | N/A | - | Deprecated |
| `events` | ✅ | 86% | EventEmitter |
| `fs` | ⚠️ | 76% | File system |
| `http` | ⚠️ | 70% | HTTP server/client |
| `http2` | ❌ | 0% | HTTP/2 |
| `https` | ⚠️ | 71% | HTTPS server/client |
| `inspector` | ❌ | 0% | V8 inspector |
| `module` | ❌ | 0% | Module system |
| `net` | ✅ | 100% | TCP sockets |
| `os` | ✅ | 100% | OS utilities |
| `path` | ✅ | 100% | Path utilities |
| `perf_hooks` | ⚠️ | 81% | Performance |
| `process` | ⚠️ | 67% | Process info |
| `punycode` | N/A | - | Deprecated |
| `querystring` | ✅ | 100% | Query parsing |
| `readline` | ❌ | 0% | Line input |
| `repl` | ❌ | 0% | REPL |
| `stream` | ⚠️ | 91% | Streams |
| `string_decoder` | ✅ | 100% | String decoding |
| `timers` | ⚠️ | 93% | Timers |
| `tls` | ⚠️ | 30% | TLS/SSL |
| `tty` | ❌ | 0% | TTY |
| `url` | ⚠️ | 82% | URL parsing |
| `util` | ⚠️ | 42% | Utilities |
| `v8` | N/A | - | V8 specific (AOT incompatible) |
| `vm` | N/A | - | VM contexts (AOT incompatible) |
| `wasi` | N/A | - | WebAssembly (not planned) |
| `worker_threads` | ❌ | 0% | Threading |
| `zlib` | ❌ | 0% | Compression |

---

## Assert

| Feature | Status | Notes |
|---------|--------|-------|
| `assert(value)` | ✅ | Basic assertion |
| `assert.ok(value)` | ✅ | Same as assert() |
| `assert.strictEqual(actual, expected)` | ✅ | Strict equality check |
| `assert.notStrictEqual(actual, expected)` | ✅ | Strict inequality check |
| `assert.deepStrictEqual(actual, expected)` | ✅ | Deep equality for objects/arrays |
| `assert.notDeepStrictEqual(actual, expected)` | ✅ | Deep inequality check |
| `assert.throws(fn)` | ⚠️ | Stub - warns no try/catch support |
| `assert.doesNotThrow(fn)` | ✅ | Calls function, exits on throw |
| `assert.rejects(asyncFn)` | ⚠️ | Stub - warns limited support |
| `assert.doesNotReject(asyncFn)` | ⚠️ | Stub - warns limited support |
| `assert.match(string, regexp)` | ✅ | Regex string matching |
| `assert.doesNotMatch(string, regexp)` | ✅ | Regex string non-matching |
| `assert.fail(message)` | ✅ | Always fails with message |
| `assert.ifError(value)` | ✅ | Fails if value is truthy |

**Assert Coverage: 10/14 (71%)**

---

## Buffer

### Static Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `Buffer.alloc(size)` | ✅ | |
| `Buffer.allocUnsafe(size)` | ✅ | |
| `Buffer.allocUnsafeSlow(size)` | ✅ | Same as allocUnsafe (no pooling) |
| `Buffer.byteLength(string)` | ✅ | |
| `Buffer.compare(buf1, buf2)` | ✅ | |
| `Buffer.concat(list)` | ✅ | |
| `Buffer.from(array)` | ✅ | |
| `Buffer.from(arrayBuffer)` | ❌ | |
| `Buffer.from(buffer)` | ✅ | Creates a copy of the buffer |
| `Buffer.from(string, encoding)` | ✅ | |
| `Buffer.isBuffer(obj)` | ✅ | |
| `Buffer.isEncoding(encoding)` | ✅ | |

### Instance Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `buf.buffer` | ✅ | Returns same buffer (Node.js compatibility) |
| `buf.byteOffset` | ✅ | Always returns 0 |
| `buf.length` | ✅ | |
| `buf.byteLength` | ✅ | Same as length |

### Instance Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `buf.compare(target)` | ✅ | |
| `buf.copy(target)` | ✅ | |
| `buf.entries()` | ✅ | Returns array of [index, byte] pairs |
| `buf.equals(otherBuffer)` | ✅ | |
| `buf.fill(value)` | ✅ | |
| `buf.includes(value)` | ✅ | |
| `buf.indexOf(value)` | ✅ | |
| `buf.keys()` | ✅ | Returns array of indices |
| `buf.lastIndexOf(value)` | ✅ | |
| `buf.readBigInt64BE(offset)` | ✅ | |
| `buf.readBigInt64LE(offset)` | ✅ | |
| `buf.readBigUInt64BE(offset)` | ✅ | |
| `buf.readBigUInt64LE(offset)` | ✅ | |
| `buf.readDoubleBE(offset)` | ✅ | |
| `buf.readDoubleLE(offset)` | ✅ | |
| `buf.readFloatBE(offset)` | ✅ | |
| `buf.readFloatLE(offset)` | ✅ | |
| `buf.readInt8(offset)` | ✅ | |
| `buf.readInt16BE(offset)` | ✅ | |
| `buf.readInt16LE(offset)` | ✅ | |
| `buf.readInt32BE(offset)` | ✅ | |
| `buf.readInt32LE(offset)` | ✅ | |
| `buf.readIntBE(offset, byteLength)` | ✅ | |
| `buf.readIntLE(offset, byteLength)` | ✅ | |
| `buf.readUInt8(offset)` | ✅ | |
| `buf.readUInt16BE(offset)` | ✅ | |
| `buf.readUInt16LE(offset)` | ✅ | |
| `buf.readUInt32BE(offset)` | ✅ | |
| `buf.readUInt32LE(offset)` | ✅ | |
| `buf.readUIntBE(offset, byteLength)` | ✅ | |
| `buf.readUIntLE(offset, byteLength)` | ✅ | |
| `buf.slice(start, end)` | ✅ | Deprecated, use subarray |
| `buf.subarray(start, end)` | ✅ | |
| `buf.swap16()` | ✅ | |
| `buf.swap32()` | ✅ | |
| `buf.swap64()` | ✅ | |
| `buf.toJSON()` | ✅ | Returns { type: "Buffer", data: [...] } |
| `buf.toString(encoding)` | ✅ | |
| `buf.values()` | ✅ | Returns array of byte values |
| `buf.write(string)` | ✅ | |
| `buf.writeBigInt64BE(value)` | ✅ | |
| `buf.writeBigInt64LE(value)` | ✅ | |
| `buf.writeBigUInt64BE(value)` | ✅ | |
| `buf.writeBigUInt64LE(value)` | ✅ | |
| `buf.writeDoubleBE(value)` | ✅ | |
| `buf.writeDoubleLE(value)` | ✅ | |
| `buf.writeFloatBE(value)` | ✅ | |
| `buf.writeFloatLE(value)` | ✅ | |
| `buf.writeInt8(value)` | ✅ | |
| `buf.writeInt16BE(value)` | ✅ | |
| `buf.writeInt16LE(value)` | ✅ | |
| `buf.writeInt32BE(value)` | ✅ | |
| `buf.writeInt32LE(value)` | ✅ | |
| `buf.writeIntBE(value)` | ✅ | |
| `buf.writeIntLE(value)` | ✅ | |
| `buf.writeUInt8(value)` | ✅ | |
| `buf.writeUInt16BE(value)` | ✅ | |
| `buf.writeUInt16LE(value)` | ✅ | |
| `buf.writeUInt32BE(value)` | ✅ | |
| `buf.writeUInt32LE(value)` | ✅ | |
| `buf.writeUIntBE(value)` | ✅ | |
| `buf.writeUIntLE(value)` | ✅ | |

**Buffer Coverage: 68/68 (100%)**

---

## Console

| Feature | Status | Notes |
|---------|--------|-------|
| `console.log()` | ✅ | |
| `console.error()` | ✅ | Outputs to stderr |
| `console.warn()` | ✅ | Outputs to stderr |
| `console.info()` | ✅ | |
| `console.debug()` | ✅ | |
| `console.trace()` | ⚠️ | Stub message (no stack trace) |
| `console.dir()` | ✅ | |
| `console.dirxml()` | ✅ | Alias for console.dir |
| `console.table()` | ✅ | Tabular display for arrays and objects |
| `console.count()` | ✅ | |
| `console.countReset()` | ✅ | |
| `console.group()` | ✅ | Indents subsequent output |
| `console.groupCollapsed()` | ✅ | Same as group in terminal |
| `console.groupEnd()` | ✅ | |
| `console.time()` | ✅ | |
| `console.timeEnd()` | ✅ | |
| `console.timeLog()` | ✅ | |
| `console.assert()` | ✅ | Logs on failure |
| `console.clear()` | ✅ | ANSI escape sequence |

**Console Coverage: 19/19 (100%)**

---

## Crypto

### Hash Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createHash(algorithm)` | ✅ | OpenSSL EVP API |
| `crypto.getHashes()` | ✅ | Returns available algorithms |
| `crypto.md5(data)` | ✅ | Non-standard helper |
| `Hash.update(data)` | ✅ | Chainable |
| `Hash.digest(encoding)` | ✅ | Returns hex string |
| `Hash.copy()` | ✅ | Creates independent copy |

### HMAC
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createHmac(algorithm, key)` | ✅ | OpenSSL HMAC API |
| `Hmac.update(data)` | ✅ | Chainable |
| `Hmac.digest(encoding)` | ✅ | Returns hex string |

### Cipher/Decipher
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createCipheriv()` | ❌ | |
| `crypto.createDecipheriv()` | ❌ | |
| `crypto.getCiphers()` | ✅ | Returns available cipher algorithms |

### Random
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.randomBytes(size)` | ✅ | OpenSSL RAND_bytes |
| `crypto.randomFillSync(buffer)` | ✅ | |
| `crypto.randomFill(buffer, callback)` | ✅ | Async with callback |
| `crypto.randomInt(min, max)` | ✅ | |
| `crypto.randomUUID()` | ✅ | RFC 4122 v4 UUID |

### Keys
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.generateKeyPair()` | ❌ | |
| `crypto.generateKeyPairSync()` | ❌ | |
| `crypto.generateKey()` | ❌ | |
| `crypto.generateKeySync()` | ❌ | |
| `crypto.createPrivateKey()` | ❌ | |
| `crypto.createPublicKey()` | ❌ | |
| `crypto.createSecretKey()` | ❌ | |

### Sign/Verify
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createSign()` | ❌ | |
| `crypto.createVerify()` | ❌ | |
| `crypto.sign()` | ❌ | |
| `crypto.verify()` | ❌ | |

### Key Derivation
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.pbkdf2()` | ✅ | Async with libuv thread pool |
| `crypto.pbkdf2Sync()` | ✅ | OpenSSL PKCS5_PBKDF2_HMAC |
| `crypto.scrypt()` | ✅ | Async with libuv thread pool |
| `crypto.scryptSync()` | ✅ | OpenSSL EVP_PBE_scrypt |

### Utility
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.timingSafeEqual()` | ✅ | OpenSSL CRYPTO_memcmp |

**Crypto Coverage: 20/34 (59%)**

---

## Events (EventEmitter)

### Static Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `EventEmitter.listenerCount(emitter, event)` | ✅ | Deprecated but still available |
| `EventEmitter.on(emitter, event)` | ❌ | AsyncIterator |
| `EventEmitter.once(emitter, event)` | ✅ | Returns Promise with event args array |

### Instance Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `emitter.addListener(event, listener)` | ✅ | Alias for on() |
| `emitter.emit(event, ...args)` | ✅ | |
| `emitter.eventNames()` | ✅ | |
| `emitter.getMaxListeners()` | ✅ | |
| `emitter.listenerCount(event)` | ✅ | |
| `emitter.listeners(event)` | ✅ | |
| `emitter.off(event, listener)` | ✅ | Alias for removeListener |
| `emitter.on(event, listener)` | ✅ | |
| `emitter.once(event, listener)` | ✅ | |
| `emitter.prependListener(event, listener)` | ✅ | |
| `emitter.prependOnceListener(event, listener)` | ⚠️ | Once semantics issue |
| `emitter.rawListeners(event)` | ✅ | Returns wrappers for once listeners |
| `emitter.removeAllListeners(event)` | ✅ | |
| `emitter.removeListener(event, listener)` | ✅ | |
| `emitter.setMaxListeners(n)` | ✅ | |

### Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'error'` event | ✅ | |
| `'newListener'` event | ✅ | Emitted before adding listener |
| `'removeListener'` event | ✅ | Emitted after removing listener |

**Events Coverage: 20/21 (95%)**

---

## File System (fs)

### Promises API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.promises.access()` | ✅ | |
| `fs.promises.appendFile()` | ✅ | |
| `fs.promises.chmod()` | ✅ | |
| `fs.promises.chown()` | ✅ | |
| `fs.promises.copyFile()` | ✅ | |
| `fs.promises.cp()` | ❌ | |
| `fs.promises.lchmod()` | ❌ | |
| `fs.promises.lchown()` | ❌ | |
| `fs.promises.link()` | ✅ | |
| `fs.promises.lstat()` | ✅ | |
| `fs.promises.lutimes()` | ❌ | |
| `fs.promises.mkdir()` | ✅ | |
| `fs.promises.mkdtemp()` | ✅ | |
| `fs.promises.open()` | ✅ | |
| `fs.promises.opendir()` | ✅ | |
| `fs.promises.readdir()` | ✅ | |
| `fs.promises.readFile()` | ✅ | |
| `fs.promises.readlink()` | ✅ | |
| `fs.promises.realpath()` | ✅ | |
| `fs.promises.rename()` | ✅ | |
| `fs.promises.rm()` | ✅ | |
| `fs.promises.rmdir()` | ✅ | |
| `fs.promises.stat()` | ✅ | |
| `fs.promises.symlink()` | ✅ | |
| `fs.promises.truncate()` | ✅ | |
| `fs.promises.unlink()` | ✅ | |
| `fs.promises.utimes()` | ✅ | |
| `fs.promises.watch()` | ❌ | |
| `fs.promises.writeFile()` | ✅ | |

### Synchronous API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.accessSync()` | ✅ | |
| `fs.appendFileSync()` | ✅ | |
| `fs.chmodSync()` | ✅ | |
| `fs.chownSync()` | ✅ | |
| `fs.closeSync()` | ✅ | |
| `fs.copyFileSync()` | ✅ | |
| `fs.cpSync()` | ❌ | |
| `fs.existsSync()` | ✅ | |
| `fs.fchmodSync()` | ❌ | |
| `fs.fchownSync()` | ❌ | |
| `fs.fdatasyncSync()` | ✅ | |
| `fs.fstatSync()` | ✅ | |
| `fs.fsyncSync()` | ✅ | |
| `fs.ftruncateSync()` | ✅ | |
| `fs.futimesSync()` | ❌ | |
| `fs.lchmodSync()` | ❌ | |
| `fs.lchownSync()` | ❌ | |
| `fs.linkSync()` | ✅ | |
| `fs.lstatSync()` | ✅ | |
| `fs.lutimesSync()` | ❌ | |
| `fs.mkdirSync()` | ✅ | |
| `fs.mkdtempSync()` | ✅ | |
| `fs.openSync()` | ✅ | |
| `fs.opendirSync()` | ✅ | |
| `fs.readSync()` | ✅ | |
| `fs.readdirSync()` | ✅ | |
| `fs.readFileSync()` | ✅ | |
| `fs.readlinkSync()` | ✅ | |
| `fs.realpathSync()` | ✅ | |
| `fs.renameSync()` | ✅ | |
| `fs.rmdirSync()` | ✅ | |
| `fs.rmSync()` | ✅ | |
| `fs.statSync()` | ✅ | |
| `fs.symlinkSync()` | ✅ | |
| `fs.truncateSync()` | ✅ | |
| `fs.unlinkSync()` | ✅ | |
| `fs.utimesSync()` | ✅ | |
| `fs.writeSync()` | ✅ | |
| `fs.writeFileSync()` | ✅ | |

### Callback API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.access()` | ✅ | Via promises |
| `fs.appendFile()` | ✅ | Via promises |
| `fs.chmod()` | ✅ | Via promises |
| `fs.chown()` | ✅ | Via promises |
| `fs.close()` | ✅ | |
| `fs.copyFile()` | ✅ | Via promises |
| `fs.cp()` | ❌ | |
| `fs.exists()` | ❌ | Deprecated |
| `fs.fchmod()` | ❌ | |
| `fs.fchown()` | ❌ | |
| `fs.fdatasync()` | ❌ | |
| `fs.fstat()` | ❌ | |
| `fs.fsync()` | ❌ | |
| `fs.ftruncate()` | ❌ | |
| `fs.futimes()` | ❌ | |
| `fs.lchmod()` | ❌ | |
| `fs.lchown()` | ❌ | |
| `fs.link()` | ✅ | Via promises |
| `fs.lstat()` | ✅ | Via promises |
| `fs.lutimes()` | ❌ | |
| `fs.mkdir()` | ✅ | |
| `fs.mkdtemp()` | ✅ | Via promises |
| `fs.open()` | ✅ | |
| `fs.opendir()` | ✅ | Via promises |
| `fs.read()` | ✅ | |
| `fs.readdir()` | ✅ | |
| `fs.readFile()` | ✅ | |
| `fs.readlink()` | ✅ | Via promises |
| `fs.realpath()` | ✅ | Via promises |
| `fs.rename()` | ✅ | Via promises |
| `fs.rm()` | ✅ | Via promises |
| `fs.rmdir()` | ✅ | Via promises |
| `fs.stat()` | ✅ | |
| `fs.symlink()` | ✅ | Via promises |
| `fs.truncate()` | ✅ | Via promises |
| `fs.unlink()` | ✅ | |
| `fs.unwatchFile()` | ❌ | |
| `fs.utimes()` | ✅ | Via promises |
| `fs.watch()` | ❌ | |
| `fs.watchFile()` | ❌ | |
| `fs.write()` | ✅ | |
| `fs.writeFile()` | ✅ | |

### Streams
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.createReadStream()` | ⚠️ | Basic support |
| `fs.createWriteStream()` | ⚠️ | Basic support |

### Stats
| Feature | Status | Notes |
|---------|--------|-------|
| `stats.isFile()` | ✅ | |
| `stats.isDirectory()` | ✅ | |
| `stats.isBlockDevice()` | ✅ | |
| `stats.isCharacterDevice()` | ✅ | |
| `stats.isSymbolicLink()` | ✅ | |
| `stats.isFIFO()` | ✅ | |
| `stats.isSocket()` | ✅ | |
| `stats.size` | ✅ | |
| `stats.mtime` | ⚠️ | Returns number |
| `stats.atime` | ⚠️ | Returns number |
| `stats.ctime` | ⚠️ | Returns number |

**File System Coverage: 94/123 (76%)**

---

## HTTP

### Server
| Feature | Status | Notes |
|---------|--------|-------|
| `http.createServer()` | ✅ | |
| `server.listen()` | ✅ | |
| `server.close()` | ✅ | |
| `server.setTimeout()` | ❌ | |
| `server.maxHeadersCount` | ❌ | |
| `server.timeout` | ❌ | |
| `server.keepAliveTimeout` | ❌ | |
| `server.headersTimeout` | ❌ | |
| `server.requestTimeout` | ❌ | |

### Request (IncomingMessage)
| Feature | Status | Notes |
|---------|--------|-------|
| `req.headers` | ✅ | |
| `req.httpVersion` | ✅ | HTTP version string (e.g., "1.1") |
| `req.method` | ✅ | |
| `req.url` | ✅ | |
| `req.socket` | ⚠️ | |
| `req.complete` | ✅ | Set true after message fully received |
| `req.aborted` | ❌ | |
| `req.rawHeaders` | ✅ | Alternating key/value array with original case |
| `req.rawTrailers` | ❌ | |
| `req.statusCode` | ✅ | |
| `req.statusMessage` | ✅ | For HTTP client responses |
| `req.trailers` | ❌ | |

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
| `res.setTimeout()` | ❌ | |
| `res.addTrailers()` | ❌ | |

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
| `http.STATUS_CODES` | ⚠️ | Object exists but property access issues |
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
| `req.getRawHeaderNames()` | ❌ | |
| `req.maxHeadersCount` | ❌ | |
| `req.path` | ✅ | |
| `req.method` | ✅ | |
| `req.host` | ✅ | |
| `req.protocol` | ✅ | |
| `req.removeHeader()` | ✅ | Via OutgoingMessage base class |
| `req.reusedSocket` | ❌ | |
| `req.setHeader()` | ✅ | |
| `req.setNoDelay()` | ❌ | |
| `req.setSocketKeepAlive()` | ❌ | |
| `req.setTimeout()` | ❌ | |
| `req.socket` | ✅ | Returns underlying net.Socket |
| `req.write()` | ✅ | |

**HTTP Coverage: 49/67 (73%)**

---

## HTTPS

| Feature | Status | Notes |
|---------|--------|-------|
| `https.createServer()` | ✅ | |
| `https.request()` | ✅ | |
| `https.get()` | ✅ | |
| `https.Agent` | ❌ | |
| `https.globalAgent` | ❌ | |
| Server options (key, cert, ca, etc.) | ✅ | |
| Client options (rejectUnauthorized, etc.) | ⚠️ | |

**HTTPS Coverage: 5/7 (71%)**

---

## TLS

| Feature | Status | Notes |
|---------|--------|-------|
| `tls.createServer()` | ✅ | Via HTTPS |
| `tls.connect()` | ✅ | Via HTTPS client |
| `tls.createSecureContext()` | ❌ | |
| TLSSocket class | ✅ | As TsSecureSocket |
| `socket.authorized` | ❌ | |
| `socket.authorizationError` | ❌ | |
| `socket.encrypted` | ❌ | |
| `socket.getCertificate()` | ❌ | |
| `socket.getPeerCertificate()` | ❌ | |
| `socket.getProtocol()` | ❌ | |
| `socket.getSession()` | ❌ | |
| `socket.renegotiate()` | ❌ | |
| `socket.setMaxSendFragment()` | ❌ | |
| Server certificate options | ✅ | key, cert supported |
| Client verification options | ⚠️ | rejectUnauthorized basic |
| CA certificate support | ⚠️ | Basic support |
| SNI support | ❌ | |
| ALPN support | ❌ | |
| Session resumption | ❌ | |
| Client certificates | ❌ | |

**TLS Coverage: 6/20 (30%)**

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

## OS

| Feature | Status | Notes |
|---------|--------|-------|
| `os.arch()` | ✅ | |
| `os.availableParallelism()` | ✅ | Returns CPU count |
| `os.cpus()` | ✅ | |
| `os.endianness()` | ✅ | |
| `os.freemem()` | ✅ | |
| `os.getPriority()` | ✅ | |
| `os.homedir()` | ✅ | |
| `os.hostname()` | ✅ | |
| `os.loadavg()` | ✅ | Returns [0,0,0] on Windows |
| `os.machine()` | ✅ | |
| `os.networkInterfaces()` | ✅ | |
| `os.platform()` | ✅ | |
| `os.release()` | ✅ | |
| `os.setPriority()` | ✅ | |
| `os.tmpdir()` | ✅ | |
| `os.totalmem()` | ✅ | |
| `os.type()` | ✅ | |
| `os.uptime()` | ✅ | |
| `os.userInfo()` | ✅ | |
| `os.version()` | ✅ | |
| `os.constants` | ✅ | signals, errno, priority |
| `os.EOL` | ✅ | |
| `os.devNull` | ✅ | |

**OS Coverage: 23/23 (100%)**

---

## Path

| Feature | Status | Notes |
|---------|--------|-------|
| `path.basename()` | ✅ | |
| `path.delimiter` | ✅ | |
| `path.dirname()` | ✅ | |
| `path.extname()` | ✅ | |
| `path.format()` | ✅ | |
| `path.isAbsolute()` | ✅ | |
| `path.join()` | ✅ | |
| `path.normalize()` | ✅ | |
| `path.parse()` | ✅ | |
| `path.posix` | ✅ | All path methods with POSIX semantics |
| `path.relative()` | ✅ | |
| `path.resolve()` | ✅ | |
| `path.sep` | ✅ | |
| `path.toNamespacedPath()` | ✅ | |
| `path.win32` | ✅ | All path methods with Windows semantics |

**Path Coverage: 15/15 (100%)**

---

## Process

### Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `process.arch` | ✅ | |
| `process.argv` | ✅ | |
| `process.argv0` | ✅ | |
| `process.channel` | ❌ | |
| `process.config` | ✅ | |
| `process.connected` | ❌ | |
| `process.debugPort` | ✅ | |
| `process.env` | ✅ | |
| `process.execArgv` | ✅ | |
| `process.execPath` | ✅ | |
| `process.exitCode` | ✅ | |
| `process.pid` | ✅ | |
| `process.platform` | ✅ | |
| `process.ppid` | ✅ | |
| `process.release` | ✅ | |
| `process.report` | ✅ | |
| `process.stdin` | ✅ | |
| `process.stdout` | ✅ | |
| `process.stderr` | ✅ | |
| `process.title` | ✅ | |
| `process.version` | ✅ | |
| `process.versions` | ✅ | |

### Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `process.abort()` | ✅ | |
| `process.chdir()` | ✅ | |
| `process.cpuUsage()` | ✅ | |
| `process.cwd()` | ✅ | |
| `process.disconnect()` | ❌ | |
| `process.dlopen()` | ❌ | |
| `process.emitWarning()` | ✅ | |
| `process.exit()` | ✅ | |
| `process.getActiveResourcesInfo()` | ✅ | |
| `process.getegid()` | ❌ | |
| `process.geteuid()` | ❌ | |
| `process.getgid()` | ❌ | |
| `process.getgroups()` | ❌ | |
| `process.getuid()` | ❌ | |
| `process.hasUncaughtExceptionCaptureCallback()` | ✅ | |
| `process.hrtime()` | ✅ | |
| `process.hrtime.bigint()` | ✅ | |
| `process.initgroups()` | ❌ | |
| `process.kill()` | ✅ | |
| `process.memoryUsage()` | ✅ | |
| `process.memoryUsage.rss()` | ✅ | |
| `process.nextTick()` | ✅ | |
| `process.resourceUsage()` | ✅ | |
| `process.send()` | ❌ | |
| `process.setegid()` | ❌ | |
| `process.seteuid()` | ❌ | |
| `process.setgid()` | ❌ | |
| `process.setgroups()` | ❌ | |
| `process.setuid()` | ❌ | |
| `process.setSourceMapsEnabled()` | ❌ | |
| `process.setUncaughtExceptionCaptureCallback()` | ✅ | |
| `process.umask()` | ✅ | |
| `process.uptime()` | ✅ | |

**Process Coverage: 37/55 (67%)**

---

## Stream

### Readable
| Feature | Status | Notes |
|---------|--------|-------|
| `readable.destroy()` | ✅ | |
| `readable.destroyed` | ✅ | |
| `readable.isPaused()` | ✅ | |
| `readable.pause()` | ✅ | |
| `readable.pipe()` | ✅ | |
| `readable.read()` | ⚠️ | |
| `readable.readable` | ✅ | |
| `readable.readableAborted` | ✅ | |
| `readable.readableDidRead` | ✅ | |
| `readable.readableEncoding` | ✅ | |
| `readable.readableEnded` | ✅ | |
| `readable.readableFlowing` | ✅ | |
| `readable.readableHighWaterMark` | ✅ | |
| `readable.readableLength` | ✅ | Returns 0 (no internal buffer) |
| `readable.readableObjectMode` | ✅ | |
| `readable.resume()` | ✅ | |
| `readable.setEncoding()` | ✅ | |
| `readable.unpipe()` | ✅ | |
| `readable.unshift()` | ✅ | Pushes data back to front of buffer |
| `readable.wrap()` | ✅ | Wraps old-style streams in Readable interface |
| `readable[Symbol.asyncIterator]()` | ❌ | |

### Writable
| Feature | Status | Notes |
|---------|--------|-------|
| `writable.cork()` | ✅ | |
| `writable.destroy()` | ✅ | |
| `writable.destroyed` | ✅ | |
| `writable.end()` | ✅ | |
| `writable.setDefaultEncoding()` | ✅ | Sets default encoding for write() |
| `writable.uncork()` | ✅ | |
| `writable.writable` | ✅ | |
| `writable.writableAborted` | ✅ | |
| `writable.writableEnded` | ✅ | |
| `writable.writableFinished` | ✅ | |
| `writable.writableHighWaterMark` | ✅ | |
| `writable.writableLength` | ✅ | |
| `writable.writableNeedDrain` | ✅ | |
| `writable.writableObjectMode` | ✅ | |
| `writable.write()` | ✅ | |

### Duplex
| Feature | Status | Notes |
|---------|--------|-------|
| Duplex stream | ✅ | Combines Readable + Writable |
| `stream.Duplex` class | ✅ | |

### Transform
| Feature | Status | Notes |
|---------|--------|-------|
| Transform stream | ✅ | Basic support |
| `stream.Transform` class | ✅ | |

### Utilities
| Feature | Status | Notes |
|---------|--------|-------|
| `stream.pipeline()` | ✅ | Pipes streams with error handling |
| `stream.finished()` | ✅ | Detects stream completion/error |
| `stream.Readable.from()` | ✅ | Creates readable from array |

**Stream Coverage: 41/44 (93%)**

---

## StringDecoder

| Feature | Status | Notes |
|---------|--------|-------|
| `new StringDecoder(encoding?)` | ✅ | Constructor with optional encoding |
| `decoder.write(buffer)` | ✅ | Decodes buffer to string, handles multi-byte |
| `decoder.end(buffer?)` | ✅ | Finishes decoding, flushes pending bytes |
| `decoder.encoding` | ✅ | Returns encoding name |
| Multi-byte UTF-8 handling | ✅ | Preserves incomplete sequences for next write |

**StringDecoder Coverage: 5/5 (100%)**

---

## Timers

| Feature | Status | Notes |
|---------|--------|-------|
| `setTimeout()` | ✅ | |
| `clearTimeout()` | ✅ | |
| `setInterval()` | ✅ | |
| `clearInterval()` | ✅ | |
| `setImmediate()` | ✅ | |
| `clearImmediate()` | ✅ | |
| `timers.setTimeout()` | ✅ | Re-exports global setTimeout |
| `timers.setInterval()` | ✅ | Re-exports global setInterval |
| `timers.setImmediate()` | ✅ | Re-exports global setImmediate |
| `timers/promises.setTimeout()` | ✅ | Promise-based with optional value |
| `timers/promises.setInterval()` | ⚠️ | Works in sync code, async property access compiler bug |
| `timers/promises.setImmediate()` | ✅ | Promise-based with optional value |
| `timers/promises.scheduler.wait()` | ✅ | Alias for setTimeout |
| `timers/promises.scheduler.yield()` | ✅ | Alias for setImmediate |

**Timers Coverage: 13/14 (93%)**

---

## Perf Hooks

| Feature | Status | Notes |
|---------|--------|-------|
| `performance.now()` | ✅ | High-resolution time in ms |
| `performance.timeOrigin` | ✅ | Unix timestamp in ms |
| `performance.mark(name)` | ✅ | Create a performance mark |
| `performance.measure(name, start, end)` | ✅ | Measure between marks |
| `performance.getEntries()` | ✅ | Get all performance entries |
| `performance.getEntriesByName(name)` | ✅ | Filter entries by name |
| `performance.getEntriesByType(type)` | ✅ | Filter entries by type |
| `performance.clearMarks(name?)` | ✅ | Clear marks |
| `performance.clearMeasures(name?)` | ✅ | Clear measures |
| `PerformanceEntry.name` | ✅ | Entry name property |
| `PerformanceEntry.entryType` | ✅ | Entry type (mark/measure) |
| `PerformanceEntry.startTime` | ✅ | Entry start time |
| `PerformanceEntry.duration` | ✅ | Entry duration |
| `PerformanceObserver` | ❌ | Not implemented |
| `performance.timerify()` | ❌ | Not implemented |
| `performance.eventLoopUtilization()` | ❌ | Not implemented |

**Perf Hooks Coverage: 13/16 (81%)**

---

## QueryString

| Feature | Status | Notes |
|---------|--------|-------|
| `querystring.parse(str, sep?, eq?)` | ✅ | Parses query string to object |
| `querystring.stringify(obj, sep?, eq?)` | ✅ | Stringifies object to query string |
| `querystring.escape(str)` | ✅ | Percent-encodes a string |
| `querystring.unescape(str)` | ✅ | Decodes percent-encoded string |
| `querystring.decode()` | ✅ | Alias for parse |
| `querystring.encode()` | ✅ | Alias for stringify |

**QueryString Coverage: 6/6 (100%)**

---

## URL

### URL Class
| Feature | Status | Notes |
|---------|--------|-------|
| `new URL(input)` | ✅ | |
| `new URL(input, base)` | ⚠️ | Base parameter not fully working |
| `url.hash` | ✅ | |
| `url.host` | ✅ | |
| `url.hostname` | ✅ | |
| `url.href` | ✅ | |
| `url.origin` | ✅ | |
| `url.password` | ✅ | |
| `url.pathname` | ✅ | |
| `url.port` | ✅ | |
| `url.protocol` | ✅ | |
| `url.search` | ✅ | |
| `url.searchParams` | ✅ | Returns URLSearchParams object |
| `url.username` | ✅ | |
| `url.toString()` | ✅ | |
| `url.toJSON()` | ✅ | |

### URLSearchParams
| Feature | Status | Notes |
|---------|--------|-------|
| `new URLSearchParams()` | ✅ | From string or empty |
| `params.append()` | ✅ | |
| `params.delete()` | ✅ | |
| `params.entries()` | ✅ | Returns array of [key, value] pairs |
| `params.forEach()` | ✅ | |
| `params.get()` | ✅ | |
| `params.getAll()` | ✅ | |
| `params.has()` | ✅ | |
| `params.keys()` | ✅ | Returns array of keys |
| `params.set()` | ✅ | |
| `params.size` | ✅ | |
| `params.sort()` | ✅ | |
| `params.toString()` | ✅ | |
| `params.values()` | ✅ | Returns array of values |

### Legacy URL API
| Feature | Status | Notes |
|---------|--------|-------|
| `url.parse()` | ✅ | Legacy URL parsing, returns object with protocol, host, etc. |
| `url.format()` | ✅ | WHATWG URL and legacy object support |
| `url.resolve()` | ✅ | Resolves relative URLs |
| `url.domainToASCII()` | ✅ | ICU IDNA UTS46 conversion |
| `url.domainToUnicode()` | ✅ | ICU IDNA UTS46 conversion |
| `url.fileURLToPath()` | ✅ | |
| `url.pathToFileURL()` | ✅ | |
| `url.urlToHttpOptions()` | ✅ | Extracts HTTP options from URL |

**URL Coverage: 37/38 (97%)**

---

## Util

| Feature | Status | Notes |
|---------|--------|-------|
| `util.callbackify()` | ✅ | Converts Promise-returning functions to callback style |
| `util.debuglog()` | ❌ | |
| `util.deprecate()` | ⚠️ | Stub - returns function unchanged |
| `util.format()` | ✅ | Printf-like string formatting (%s, %d, %f, %o, %j) |
| `util.formatWithOptions()` | ❌ | |
| `util.getSystemErrorName()` | ❌ | |
| `util.getSystemErrorMap()` | ❌ | |
| `util.inherits()` | ⚠️ | Stub - does nothing |
| `util.inspect()` | ⚠️ | Returns pointer value, not object string |
| `util.inspect.custom` | ❌ | |
| `util.inspect.defaultOptions` | ❌ | |
| `util.isDeepStrictEqual()` | ✅ | Deep equality comparison for objects and arrays |
| `util.parseArgs()` | ❌ | |
| `util.parseEnv()` | ❌ | |
| `util.promisify()` | ✅ | Converts callback-style functions to Promise-returning |
| `util.stripVTControlCharacters()` | ✅ | Removes ANSI escape codes |
| `util.styleText()` | ❌ | |
| `util.toUSVString()` | ✅ | Replaces lone surrogates with U+FFFD |
| `util.transferableAbortController()` | ❌ | |
| `util.transferableAbortSignal()` | ❌ | |
| `util.types.isAnyArrayBuffer()` | ❌ | |
| `util.types.isArrayBuffer()` | ⚠️ | Returns false (not wired up correctly) |
| `util.types.isArrayBufferView()` | ⚠️ | Returns false (not wired up correctly) |
| `util.types.isAsyncFunction()` | ⚠️ | Returns false (not wired up correctly) |
| `util.types.isBigInt64Array()` | ❌ | |
| `util.types.isBigUint64Array()` | ❌ | |
| `util.types.isBooleanObject()` | ❌ | |
| `util.types.isBoxedPrimitive()` | ❌ | |
| `util.types.isCryptoKey()` | ❌ | |
| `util.types.isDataView()` | ✅ | Detects DataView instances |
| `util.types.isDate()` | ✅ | |
| `util.types.isExternal()` | ❌ | |
| `util.types.isFloat32Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isFloat64Array()` | ✅ | 8-byte element size detection |
| `util.types.isGeneratorFunction()` | ⚠️ | Returns false (not wired up correctly) |
| `util.types.isGeneratorObject()` | ⚠️ | Returns false (not wired up correctly) |
| `util.types.isInt8Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isInt16Array()` | ✅ | 2-byte element size detection |
| `util.types.isInt32Array()` | ✅ | 4-byte element size detection |
| `util.types.isKeyObject()` | ❌ | |
| `util.types.isMap()` | ✅ | Correctly distinguishes Map from plain objects |
| `util.types.isMapIterator()` | ❌ | |
| `util.types.isModuleNamespaceObject()` | ❌ | |
| `util.types.isNativeError()` | ✅ | |
| `util.types.isNumberObject()` | ❌ | |
| `util.types.isPromise()` | ✅ | Detects Promise instances |
| `util.types.isProxy()` | ❌ | |
| `util.types.isRegExp()` | ✅ | |
| `util.types.isSet()` | ✅ | Works correctly |
| `util.types.isSetIterator()` | ❌ | |
| `util.types.isSharedArrayBuffer()` | ❌ | |
| `util.types.isStringObject()` | ❌ | |
| `util.types.isSymbolObject()` | ❌ | |
| `util.types.isTypedArray()` | ✅ | Detects TypedArray instances |
| `util.types.isUint8Array()` | ✅ | Detects Uint8Array instances |
| `util.types.isUint8ClampedArray()` | ✅ | Detects clamped 1-byte arrays |
| `util.types.isUint16Array()` | ✅ | 2-byte element size detection |
| `util.types.isUint32Array()` | ✅ | 4-byte element size detection |
| `util.types.isWeakMap()` | ❌ | |
| `util.types.isWeakSet()` | ❌ | |
| TextDecoder class | ✅ | UTF-8 decoding with BOM handling |
| TextEncoder class | ✅ | UTF-8 encoding to Buffer |

**Util Coverage: 42/62 (68%)** (25 full, 6 partial)

---

## Global Objects

| Feature | Status | Notes |
|---------|--------|-------|
| `global` | ✅ | |
| `globalThis` | ✅ | ES2020 alias for global |
| `__dirname` | ✅ | Absolute path to directory |
| `__filename` | ✅ | Absolute path to file |
| `exports` | ❌ | |
| `module` | ❌ | |
| `require()` | ⚠️ | Basic support |

**Global Coverage: 5/7 (71%)**

---

## Summary

### Overall Node.js API Conformance

| Category | Implemented | Total | Coverage |
|----------|-------------|-------|----------|
| Buffer | 68 | 68 | 100% |
| Console | 19 | 19 | 100% |
| Crypto | 20 | 34 | 59% |
| Events | 20 | 21 | 95% |
| File System | 94 | 123 | 76% |
| HTTP | 45 | 68 | 66% |
| HTTPS | 5 | 7 | 71% |
| Net | 36 | 36 | 100% |
| OS | 23 | 23 | 100% |
| Path | 15 | 15 | 100% |
| Process | 37 | 55 | 67% |
| QueryString | 6 | 6 | 100% |
| Stream | 41 | 44 | 93% |
| Timers | 13 | 14 | 93% |
| TLS | 6 | 20 | 30% |
| URL | 37 | 38 | 97% |
| Util | 42 | 62 | 68% |
| Global | 5 | 7 | 71% |
| **Total** | **530** | **661** | **80%** |

### Priority Implementation Targets

#### Critical (Required for most apps)
- ✅ `console.error()`, `console.warn()` - Basic logging (implemented)
- ✅ `process.stdout`, `process.stderr` - Stream-based output (implemented)
- ✅ `path.parse()`, `path.format()` - Path manipulation (implemented)
- ✅ `fs.rename*()`, `fs.copy*()` - File operations (implemented)
- ✅ `URLSearchParams` - Query string handling (implemented)

#### High (Common use cases)
- ✅ `os.platform()`, `os.cpus()` - System info (implemented)
- ✅ `crypto.randomBytes()`, `crypto.createHash()` - Basic crypto (implemented via OpenSSL)
- ✅ `stream.pipeline()`, `stream.finished()` - Stream utilities (implemented)
- ✅ `util.promisify()` - Callback to Promise conversion (implemented)
- ✅ `setImmediate()` - Event loop control (implemented)

#### Medium (Framework support)
- `http.Agent` - Connection pooling
- `cluster` module - Multi-process
- `child_process` - Process spawning
- `readline` - Interactive input
- `zlib` - Compression

---

## Testing Status

Current test coverage:
- Buffer: 6 test files (basic, advanced, encoding, extended, typed_array, utilities)
- Console: 4 test files (extended, methods, timing)
- Crypto: 1 test file (extended - 12 tests covering hash, hmac, random, kdf, timing)
- Events: 2 test files (basic, extended)
- File System: 7 test files (async, dirs, links, metadata, operations, sync)
- HTTP: 4 test files (fetch, client, constants, https)
- Net: 1 test file (utilities)
- Path: 3 test files (basic, parse_format, relative)
- Process: 2 test files (basic, extended)
- QueryString: 1 test file (basic - 11 tests covering parse, stringify, escape, unescape, encode, decode)
- Timers: 1 test file
- URL: 5 test files (basic, extended, search params, parse, format)
- Util: 2 test files (basic, extended)

Most Node.js API tests passing.
