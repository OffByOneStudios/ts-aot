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
| `assert` | ❌ | 0% | Testing utilities |
| `async_hooks` | ❌ | 0% | Async context tracking |
| `buffer` | ⚠️ | 40% | Binary data handling |
| `child_process` | ❌ | 0% | Process spawning |
| `cluster` | ❌ | 0% | Multi-process |
| `console` | ⚠️ | 30% | Basic logging |
| `crypto` | ⚠️ | 10% | Cryptographic functions |
| `dgram` | ❌ | 0% | UDP sockets |
| `dns` | ❌ | 0% | DNS resolution |
| `domain` | N/A | - | Deprecated |
| `events` | ✅ | 90% | EventEmitter |
| `fs` | ⚠️ | 60% | File system |
| `http` | ⚠️ | 70% | HTTP server/client |
| `http2` | ❌ | 0% | HTTP/2 |
| `https` | ⚠️ | 70% | HTTPS server/client |
| `inspector` | ❌ | 0% | V8 inspector |
| `module` | ❌ | 0% | Module system |
| `net` | ⚠️ | 60% | TCP sockets |
| `os` | ❌ | 0% | OS utilities |
| `path` | ✅ | 95% | Path utilities |
| `perf_hooks` | ❌ | 0% | Performance |
| `process` | ⚠️ | 30% | Process info |
| `punycode` | N/A | - | Deprecated |
| `querystring` | ❌ | 0% | Query parsing |
| `readline` | ❌ | 0% | Line input |
| `repl` | ❌ | 0% | REPL |
| `stream` | ⚠️ | 50% | Streams |
| `string_decoder` | ❌ | 0% | String decoding |
| `timers` | ⚠️ | 60% | Timers |
| `tls` | ⚠️ | 30% | TLS/SSL |
| `tty` | ❌ | 0% | TTY |
| `url` | ⚠️ | 50% | URL parsing |
| `util` | ⚠️ | 20% | Utilities |
| `v8` | ❌ | 0% | V8 specific |
| `vm` | ❌ | 0% | VM contexts |
| `wasi` | ❌ | 0% | WebAssembly |
| `worker_threads` | ❌ | 0% | Threading |
| `zlib` | ❌ | 0% | Compression |

---

## Buffer

### Static Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `Buffer.alloc(size)` | ✅ | |
| `Buffer.allocUnsafe(size)` | ❌ | |
| `Buffer.allocUnsafeSlow(size)` | ❌ | |
| `Buffer.byteLength(string)` | ❌ | |
| `Buffer.compare(buf1, buf2)` | ❌ | |
| `Buffer.concat(list)` | ❌ | |
| `Buffer.from(array)` | ✅ | |
| `Buffer.from(arrayBuffer)` | ❌ | |
| `Buffer.from(buffer)` | ❌ | |
| `Buffer.from(string, encoding)` | ✅ | |
| `Buffer.isBuffer(obj)` | ❌ | |
| `Buffer.isEncoding(encoding)` | ❌ | |

### Instance Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `buf.buffer` | ❌ | |
| `buf.byteOffset` | ❌ | |
| `buf.length` | ✅ | |

### Instance Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `buf.compare(target)` | ❌ | |
| `buf.copy(target)` | ❌ | |
| `buf.entries()` | ❌ | |
| `buf.equals(otherBuffer)` | ❌ | |
| `buf.fill(value)` | ❌ | |
| `buf.includes(value)` | ❌ | |
| `buf.indexOf(value)` | ❌ | |
| `buf.keys()` | ❌ | |
| `buf.lastIndexOf(value)` | ❌ | |
| `buf.readBigInt64BE(offset)` | ❌ | |
| `buf.readBigInt64LE(offset)` | ❌ | |
| `buf.readBigUInt64BE(offset)` | ❌ | |
| `buf.readBigUInt64LE(offset)` | ❌ | |
| `buf.readDoubleBE(offset)` | ❌ | |
| `buf.readDoubleLE(offset)` | ❌ | |
| `buf.readFloatBE(offset)` | ❌ | |
| `buf.readFloatLE(offset)` | ❌ | |
| `buf.readInt8(offset)` | ❌ | |
| `buf.readInt16BE(offset)` | ❌ | |
| `buf.readInt16LE(offset)` | ❌ | |
| `buf.readInt32BE(offset)` | ❌ | |
| `buf.readInt32LE(offset)` | ❌ | |
| `buf.readIntBE(offset, byteLength)` | ❌ | |
| `buf.readIntLE(offset, byteLength)` | ❌ | |
| `buf.readUInt8(offset)` | ❌ | |
| `buf.readUInt16BE(offset)` | ❌ | |
| `buf.readUInt16LE(offset)` | ❌ | |
| `buf.readUInt32BE(offset)` | ❌ | |
| `buf.readUInt32LE(offset)` | ❌ | |
| `buf.readUIntBE(offset, byteLength)` | ❌ | |
| `buf.readUIntLE(offset, byteLength)` | ❌ | |
| `buf.slice(start, end)` | ⚠️ | Deprecated, use subarray |
| `buf.subarray(start, end)` | ❌ | |
| `buf.swap16()` | ❌ | |
| `buf.swap32()` | ❌ | |
| `buf.swap64()` | ❌ | |
| `buf.toJSON()` | ❌ | |
| `buf.toString(encoding)` | ✅ | |
| `buf.values()` | ❌ | |
| `buf.write(string)` | ✅ | |
| `buf.writeBigInt64BE(value)` | ❌ | |
| `buf.writeBigInt64LE(value)` | ❌ | |
| `buf.writeBigUInt64BE(value)` | ❌ | |
| `buf.writeBigUInt64LE(value)` | ❌ | |
| `buf.writeDoubleBE(value)` | ❌ | |
| `buf.writeDoubleLE(value)` | ❌ | |
| `buf.writeFloatBE(value)` | ❌ | |
| `buf.writeFloatLE(value)` | ❌ | |
| `buf.writeInt8(value)` | ❌ | |
| `buf.writeInt16BE(value)` | ❌ | |
| `buf.writeInt16LE(value)` | ❌ | |
| `buf.writeInt32BE(value)` | ❌ | |
| `buf.writeInt32LE(value)` | ❌ | |
| `buf.writeIntBE(value)` | ❌ | |
| `buf.writeIntLE(value)` | ❌ | |
| `buf.writeUInt8(value)` | ❌ | |
| `buf.writeUInt16BE(value)` | ❌ | |
| `buf.writeUInt16LE(value)` | ❌ | |
| `buf.writeUInt32BE(value)` | ❌ | |
| `buf.writeUInt32LE(value)` | ❌ | |
| `buf.writeUIntBE(value)` | ❌ | |
| `buf.writeUIntLE(value)` | ❌ | |

**Buffer Coverage: 7/67 (10%)**

---

## Console

| Feature | Status | Notes |
|---------|--------|-------|
| `console.log()` | ✅ | |
| `console.error()` | ❌ | |
| `console.warn()` | ❌ | |
| `console.info()` | ❌ | |
| `console.debug()` | ❌ | |
| `console.trace()` | ❌ | |
| `console.dir()` | ❌ | |
| `console.dirxml()` | ❌ | |
| `console.table()` | ❌ | |
| `console.count()` | ❌ | |
| `console.countReset()` | ❌ | |
| `console.group()` | ❌ | |
| `console.groupCollapsed()` | ❌ | |
| `console.groupEnd()` | ❌ | |
| `console.time()` | ❌ | |
| `console.timeEnd()` | ❌ | |
| `console.timeLog()` | ❌ | |
| `console.assert()` | ❌ | |
| `console.clear()` | ❌ | |

**Console Coverage: 1/19 (5%)**

---

## Crypto

### Hash Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createHash(algorithm)` | ❌ | |
| `crypto.getHashes()` | ❌ | |
| `crypto.md5(data)` | ✅ | Non-standard helper |

### HMAC
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createHmac(algorithm, key)` | ❌ | |

### Cipher/Decipher
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createCipheriv()` | ❌ | |
| `crypto.createDecipheriv()` | ❌ | |
| `crypto.getCiphers()` | ❌ | |

### Random
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.randomBytes(size)` | ❌ | |
| `crypto.randomFillSync(buffer)` | ❌ | |
| `crypto.randomFill(buffer, callback)` | ❌ | |
| `crypto.randomInt(min, max)` | ❌ | |
| `crypto.randomUUID()` | ❌ | |

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

### Other
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.pbkdf2()` | ❌ | |
| `crypto.pbkdf2Sync()` | ❌ | |
| `crypto.scrypt()` | ❌ | |
| `crypto.scryptSync()` | ❌ | |
| `crypto.timingSafeEqual()` | ❌ | |

**Crypto Coverage: 1/28 (4%)**

---

## Events (EventEmitter)

### Static Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `EventEmitter.listenerCount(emitter, event)` | ❌ | Deprecated |
| `EventEmitter.on(emitter, event)` | ❌ | AsyncIterator |
| `EventEmitter.once(emitter, event)` | ❌ | Returns Promise |

### Instance Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `emitter.addListener(event, listener)` | ✅ | Alias for on() |
| `emitter.emit(event, ...args)` | ✅ | |
| `emitter.eventNames()` | ❌ | |
| `emitter.getMaxListeners()` | ❌ | |
| `emitter.listenerCount(event)` | ❌ | |
| `emitter.listeners(event)` | ❌ | |
| `emitter.off(event, listener)` | ✅ | Alias for removeListener |
| `emitter.on(event, listener)` | ✅ | |
| `emitter.once(event, listener)` | ✅ | |
| `emitter.prependListener(event, listener)` | ❌ | |
| `emitter.prependOnceListener(event, listener)` | ❌ | |
| `emitter.rawListeners(event)` | ❌ | |
| `emitter.removeAllListeners(event)` | ✅ | |
| `emitter.removeListener(event, listener)` | ✅ | |
| `emitter.setMaxListeners(n)` | ❌ | |

### Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'error'` event | ✅ | |
| `'newListener'` event | ❌ | |
| `'removeListener'` event | ❌ | |

**Events Coverage: 8/21 (38%)**

---

## File System (fs)

### Promises API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.promises.access()` | ❌ | |
| `fs.promises.appendFile()` | ❌ | |
| `fs.promises.chmod()` | ❌ | |
| `fs.promises.chown()` | ❌ | |
| `fs.promises.copyFile()` | ❌ | |
| `fs.promises.cp()` | ❌ | |
| `fs.promises.lchmod()` | ❌ | |
| `fs.promises.lchown()` | ❌ | |
| `fs.promises.link()` | ❌ | |
| `fs.promises.lstat()` | ❌ | |
| `fs.promises.lutimes()` | ❌ | |
| `fs.promises.mkdir()` | ✅ | |
| `fs.promises.mkdtemp()` | ❌ | |
| `fs.promises.open()` | ❌ | |
| `fs.promises.opendir()` | ❌ | |
| `fs.promises.readdir()` | ✅ | |
| `fs.promises.readFile()` | ✅ | |
| `fs.promises.readlink()` | ❌ | |
| `fs.promises.realpath()` | ❌ | |
| `fs.promises.rename()` | ❌ | |
| `fs.promises.rm()` | ❌ | |
| `fs.promises.rmdir()` | ❌ | |
| `fs.promises.stat()` | ✅ | |
| `fs.promises.symlink()` | ❌ | |
| `fs.promises.truncate()` | ❌ | |
| `fs.promises.unlink()` | ✅ | |
| `fs.promises.utimes()` | ❌ | |
| `fs.promises.watch()` | ❌ | |
| `fs.promises.writeFile()` | ✅ | |

### Synchronous API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.accessSync()` | ❌ | |
| `fs.appendFileSync()` | ❌ | |
| `fs.chmodSync()` | ❌ | |
| `fs.chownSync()` | ❌ | |
| `fs.closeSync()` | ✅ | |
| `fs.copyFileSync()` | ❌ | |
| `fs.cpSync()` | ❌ | |
| `fs.existsSync()` | ✅ | |
| `fs.fchmodSync()` | ❌ | |
| `fs.fchownSync()` | ❌ | |
| `fs.fdatasyncSync()` | ❌ | |
| `fs.fstatSync()` | ❌ | |
| `fs.fsyncSync()` | ❌ | |
| `fs.ftruncateSync()` | ❌ | |
| `fs.futimesSync()` | ❌ | |
| `fs.lchmodSync()` | ❌ | |
| `fs.lchownSync()` | ❌ | |
| `fs.linkSync()` | ❌ | |
| `fs.lstatSync()` | ❌ | |
| `fs.lutimesSync()` | ❌ | |
| `fs.mkdirSync()` | ✅ | |
| `fs.mkdtempSync()` | ❌ | |
| `fs.openSync()` | ✅ | |
| `fs.opendirSync()` | ❌ | |
| `fs.readSync()` | ✅ | |
| `fs.readdirSync()` | ✅ | |
| `fs.readFileSync()` | ✅ | |
| `fs.readlinkSync()` | ❌ | |
| `fs.realpathSync()` | ❌ | |
| `fs.renameSync()` | ❌ | |
| `fs.rmdirSync()` | ❌ | |
| `fs.rmSync()` | ❌ | |
| `fs.statSync()` | ✅ | |
| `fs.symlinkSync()` | ❌ | |
| `fs.truncateSync()` | ❌ | |
| `fs.unlinkSync()` | ✅ | |
| `fs.utimesSync()` | ❌ | |
| `fs.writeSync()` | ✅ | |
| `fs.writeFileSync()` | ✅ | |

### Callback API
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.access()` | ❌ | |
| `fs.appendFile()` | ❌ | |
| `fs.chmod()` | ❌ | |
| `fs.chown()` | ❌ | |
| `fs.close()` | ✅ | |
| `fs.copyFile()` | ❌ | |
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
| `fs.link()` | ❌ | |
| `fs.lstat()` | ❌ | |
| `fs.lutimes()` | ❌ | |
| `fs.mkdir()` | ✅ | |
| `fs.mkdtemp()` | ❌ | |
| `fs.open()` | ✅ | |
| `fs.opendir()` | ❌ | |
| `fs.read()` | ✅ | |
| `fs.readdir()` | ✅ | |
| `fs.readFile()` | ✅ | |
| `fs.readlink()` | ❌ | |
| `fs.realpath()` | ❌ | |
| `fs.rename()` | ❌ | |
| `fs.rm()` | ❌ | |
| `fs.rmdir()` | ❌ | |
| `fs.stat()` | ✅ | |
| `fs.symlink()` | ❌ | |
| `fs.truncate()` | ❌ | |
| `fs.unlink()` | ✅ | |
| `fs.unwatchFile()` | ❌ | |
| `fs.utimes()` | ❌ | |
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
| `stats.isBlockDevice()` | ❌ | |
| `stats.isCharacterDevice()` | ❌ | |
| `stats.isSymbolicLink()` | ❌ | |
| `stats.isFIFO()` | ❌ | |
| `stats.isSocket()` | ❌ | |
| `stats.size` | ✅ | |
| `stats.mtime` | ⚠️ | Returns number |
| `stats.atime` | ⚠️ | Returns number |
| `stats.ctime` | ⚠️ | Returns number |

**File System Coverage: 32/112 (29%)**

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
| `req.httpVersion` | ❌ | |
| `req.method` | ✅ | |
| `req.url` | ✅ | |
| `req.socket` | ⚠️ | |
| `req.complete` | ❌ | |
| `req.aborted` | ❌ | |
| `req.rawHeaders` | ❌ | |
| `req.rawTrailers` | ❌ | |
| `req.statusCode` | ✅ | |
| `req.statusMessage` | ❌ | |
| `req.trailers` | ❌ | |

### Response (ServerResponse)
| Feature | Status | Notes |
|---------|--------|-------|
| `res.writeHead()` | ✅ | |
| `res.write()` | ✅ | |
| `res.end()` | ✅ | |
| `res.setHeader()` | ✅ | |
| `res.getHeader()` | ❌ | |
| `res.getHeaders()` | ❌ | |
| `res.getHeaderNames()` | ❌ | |
| `res.hasHeader()` | ❌ | |
| `res.removeHeader()` | ❌ | |
| `res.statusCode` | ✅ | |
| `res.statusMessage` | ❌ | |
| `res.headersSent` | ❌ | |
| `res.writableEnded` | ❌ | |
| `res.writableFinished` | ❌ | |
| `res.flushHeaders()` | ❌ | |
| `res.setTimeout()` | ❌ | |
| `res.addTrailers()` | ❌ | |

### Client
| Feature | Status | Notes |
|---------|--------|-------|
| `http.request()` | ✅ | |
| `http.get()` | ✅ | |
| `http.Agent` | ❌ | |
| `http.globalAgent` | ❌ | |

### Client Request
| Feature | Status | Notes |
|---------|--------|-------|
| `req.abort()` | ❌ | Deprecated |
| `req.destroy()` | ❌ | |
| `req.end()` | ✅ | |
| `req.flushHeaders()` | ❌ | |
| `req.getHeader()` | ❌ | |
| `req.getRawHeaderNames()` | ❌ | |
| `req.maxHeadersCount` | ❌ | |
| `req.path` | ❌ | |
| `req.method` | ❌ | |
| `req.host` | ❌ | |
| `req.protocol` | ❌ | |
| `req.removeHeader()` | ❌ | |
| `req.reusedSocket` | ❌ | |
| `req.setHeader()` | ❌ | |
| `req.setNoDelay()` | ❌ | |
| `req.setSocketKeepAlive()` | ❌ | |
| `req.setTimeout()` | ❌ | |
| `req.socket` | ❌ | |
| `req.write()` | ✅ | |

**HTTP Coverage: 18/63 (29%)**

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

## Net

### Server
| Feature | Status | Notes |
|---------|--------|-------|
| `net.createServer()` | ✅ | |
| `server.listen()` | ✅ | |
| `server.close()` | ✅ | |
| `server.address()` | ❌ | |
| `server.getConnections()` | ❌ | |
| `server.maxConnections` | ❌ | |
| `server.ref()` | ❌ | |
| `server.unref()` | ❌ | |

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
| `socket.setTimeout()` | ❌ | |
| `socket.setNoDelay()` | ❌ | |
| `socket.setKeepAlive()` | ❌ | |
| `socket.address()` | ❌ | |
| `socket.remoteAddress` | ❌ | |
| `socket.remoteFamily` | ❌ | |
| `socket.remotePort` | ❌ | |
| `socket.localAddress` | ❌ | |
| `socket.localPort` | ❌ | |
| `socket.localFamily` | ❌ | |
| `socket.bytesRead` | ❌ | |
| `socket.bytesWritten` | ❌ | |
| `socket.connecting` | ❌ | |
| `socket.destroyed` | ❌ | |
| `socket.pending` | ❌ | |
| `socket.readyState` | ❌ | |
| `socket.ref()` | ❌ | |
| `socket.unref()` | ❌ | |

### Utilities
| Feature | Status | Notes |
|---------|--------|-------|
| `net.isIP()` | ❌ | |
| `net.isIPv4()` | ❌ | |
| `net.isIPv6()` | ❌ | |

**Net Coverage: 10/36 (28%)**

---

## OS

| Feature | Status | Notes |
|---------|--------|-------|
| `os.arch()` | ❌ | |
| `os.cpus()` | ❌ | |
| `os.endianness()` | ❌ | |
| `os.freemem()` | ❌ | |
| `os.getPriority()` | ❌ | |
| `os.homedir()` | ❌ | |
| `os.hostname()` | ❌ | |
| `os.loadavg()` | ❌ | |
| `os.machine()` | ❌ | |
| `os.networkInterfaces()` | ❌ | |
| `os.platform()` | ❌ | |
| `os.release()` | ❌ | |
| `os.setPriority()` | ❌ | |
| `os.tmpdir()` | ❌ | |
| `os.totalmem()` | ❌ | |
| `os.type()` | ❌ | |
| `os.uptime()` | ❌ | |
| `os.userInfo()` | ❌ | |
| `os.version()` | ❌ | |
| `os.constants` | ❌ | |
| `os.EOL` | ❌ | |
| `os.devNull` | ❌ | |

**OS Coverage: 0/22 (0%)**

---

## Path

| Feature | Status | Notes |
|---------|--------|-------|
| `path.basename()` | ✅ | |
| `path.delimiter` | ✅ | |
| `path.dirname()` | ✅ | |
| `path.extname()` | ✅ | |
| `path.format()` | ❌ | |
| `path.isAbsolute()` | ✅ | |
| `path.join()` | ✅ | |
| `path.normalize()` | ✅ | |
| `path.parse()` | ❌ | |
| `path.posix` | ❌ | |
| `path.relative()` | ❌ | |
| `path.resolve()` | ✅ | |
| `path.sep` | ✅ | |
| `path.toNamespacedPath()` | ❌ | |
| `path.win32` | ❌ | |

**Path Coverage: 9/15 (60%)**

---

## Process

### Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `process.arch` | ❌ | |
| `process.argv` | ✅ | |
| `process.argv0` | ❌ | |
| `process.channel` | ❌ | |
| `process.config` | ❌ | |
| `process.connected` | ❌ | |
| `process.debugPort` | ❌ | |
| `process.env` | ✅ | |
| `process.execArgv` | ❌ | |
| `process.execPath` | ❌ | |
| `process.exitCode` | ❌ | |
| `process.pid` | ❌ | |
| `process.platform` | ❌ | |
| `process.ppid` | ❌ | |
| `process.release` | ❌ | |
| `process.report` | ❌ | |
| `process.stdin` | ❌ | |
| `process.stdout` | ❌ | |
| `process.stderr` | ❌ | |
| `process.title` | ❌ | |
| `process.version` | ❌ | |
| `process.versions` | ❌ | |

### Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `process.abort()` | ❌ | |
| `process.chdir()` | ❌ | |
| `process.cpuUsage()` | ❌ | |
| `process.cwd()` | ✅ | |
| `process.disconnect()` | ❌ | |
| `process.dlopen()` | ❌ | |
| `process.emitWarning()` | ❌ | |
| `process.exit()` | ✅ | |
| `process.getActiveResourcesInfo()` | ❌ | |
| `process.getegid()` | ❌ | |
| `process.geteuid()` | ❌ | |
| `process.getgid()` | ❌ | |
| `process.getgroups()` | ❌ | |
| `process.getuid()` | ❌ | |
| `process.hasUncaughtExceptionCaptureCallback()` | ❌ | |
| `process.hrtime()` | ❌ | |
| `process.hrtime.bigint()` | ❌ | |
| `process.initgroups()` | ❌ | |
| `process.kill()` | ❌ | |
| `process.memoryUsage()` | ❌ | |
| `process.memoryUsage.rss()` | ❌ | |
| `process.nextTick()` | ⚠️ | Basic support |
| `process.resourceUsage()` | ❌ | |
| `process.send()` | ❌ | |
| `process.setegid()` | ❌ | |
| `process.seteuid()` | ❌ | |
| `process.setgid()` | ❌ | |
| `process.setgroups()` | ❌ | |
| `process.setuid()` | ❌ | |
| `process.setSourceMapsEnabled()` | ❌ | |
| `process.setUncaughtExceptionCaptureCallback()` | ❌ | |
| `process.umask()` | ❌ | |
| `process.uptime()` | ❌ | |

**Process Coverage: 5/55 (9%)**

---

## Stream

### Readable
| Feature | Status | Notes |
|---------|--------|-------|
| `readable.destroy()` | ✅ | |
| `readable.destroyed` | ❌ | |
| `readable.isPaused()` | ❌ | |
| `readable.pause()` | ✅ | |
| `readable.pipe()` | ✅ | |
| `readable.read()` | ⚠️ | |
| `readable.readable` | ❌ | |
| `readable.readableAborted` | ❌ | |
| `readable.readableDidRead` | ❌ | |
| `readable.readableEncoding` | ❌ | |
| `readable.readableEnded` | ❌ | |
| `readable.readableFlowing` | ❌ | |
| `readable.readableHighWaterMark` | ❌ | |
| `readable.readableLength` | ❌ | |
| `readable.readableObjectMode` | ❌ | |
| `readable.resume()` | ✅ | |
| `readable.setEncoding()` | ❌ | |
| `readable.unpipe()` | ❌ | |
| `readable.unshift()` | ❌ | |
| `readable.wrap()` | ❌ | |
| `readable[Symbol.asyncIterator]()` | ❌ | |

### Writable
| Feature | Status | Notes |
|---------|--------|-------|
| `writable.cork()` | ❌ | |
| `writable.destroy()` | ✅ | |
| `writable.destroyed` | ❌ | |
| `writable.end()` | ✅ | |
| `writable.setDefaultEncoding()` | ❌ | |
| `writable.uncork()` | ❌ | |
| `writable.writable` | ❌ | |
| `writable.writableAborted` | ❌ | |
| `writable.writableEnded` | ❌ | |
| `writable.writableFinished` | ❌ | |
| `writable.writableHighWaterMark` | ❌ | |
| `writable.writableLength` | ❌ | |
| `writable.writableNeedDrain` | ❌ | |
| `writable.writableObjectMode` | ❌ | |
| `writable.write()` | ✅ | |

### Duplex
| Feature | Status | Notes |
|---------|--------|-------|
| Duplex stream | ✅ | Combines Readable + Writable |
| `stream.Duplex` class | ✅ | |

### Transform
| Feature | Status | Notes |
|---------|--------|-------|
| Transform stream | ❌ | |
| `stream.Transform` class | ❌ | |

### Utilities
| Feature | Status | Notes |
|---------|--------|-------|
| `stream.pipeline()` | ❌ | |
| `stream.finished()` | ❌ | |
| `stream.Readable.from()` | ❌ | |

**Stream Coverage: 10/44 (23%)**

---

## Timers

| Feature | Status | Notes |
|---------|--------|-------|
| `setTimeout()` | ✅ | |
| `clearTimeout()` | ✅ | |
| `setInterval()` | ✅ | |
| `clearInterval()` | ✅ | |
| `setImmediate()` | ❌ | |
| `clearImmediate()` | ❌ | |
| `timers.setTimeout()` | ❌ | |
| `timers.setInterval()` | ❌ | |
| `timers.setImmediate()` | ❌ | |
| `timers/promises.setTimeout()` | ❌ | |
| `timers/promises.setInterval()` | ❌ | |
| `timers/promises.setImmediate()` | ❌ | |
| `timers/promises.scheduler.wait()` | ❌ | |
| `timers/promises.scheduler.yield()` | ❌ | |

**Timers Coverage: 4/14 (29%)**

---

## URL

### URL Class
| Feature | Status | Notes |
|---------|--------|-------|
| `new URL(input)` | ✅ | |
| `new URL(input, base)` | ❌ | |
| `url.hash` | ❌ | |
| `url.host` | ✅ | |
| `url.hostname` | ✅ | |
| `url.href` | ❌ | |
| `url.origin` | ❌ | |
| `url.password` | ❌ | |
| `url.pathname` | ✅ | |
| `url.port` | ⚠️ | |
| `url.protocol` | ✅ | |
| `url.search` | ✅ | |
| `url.searchParams` | ❌ | |
| `url.username` | ❌ | |
| `url.toString()` | ❌ | |
| `url.toJSON()` | ❌ | |

### URLSearchParams
| Feature | Status | Notes |
|---------|--------|-------|
| `new URLSearchParams()` | ❌ | |
| `params.append()` | ❌ | |
| `params.delete()` | ❌ | |
| `params.entries()` | ❌ | |
| `params.forEach()` | ❌ | |
| `params.get()` | ❌ | |
| `params.getAll()` | ❌ | |
| `params.has()` | ❌ | |
| `params.keys()` | ❌ | |
| `params.set()` | ❌ | |
| `params.size` | ❌ | |
| `params.sort()` | ❌ | |
| `params.toString()` | ❌ | |
| `params.values()` | ❌ | |

### Legacy URL API
| Feature | Status | Notes |
|---------|--------|-------|
| `url.parse()` | ❌ | Deprecated |
| `url.format()` | ❌ | |
| `url.resolve()` | ❌ | |
| `url.domainToASCII()` | ❌ | |
| `url.domainToUnicode()` | ❌ | |
| `url.fileURLToPath()` | ❌ | |
| `url.pathToFileURL()` | ❌ | |
| `url.urlToHttpOptions()` | ❌ | |

**URL Coverage: 6/38 (16%)**

---

## Util

| Feature | Status | Notes |
|---------|--------|-------|
| `util.callbackify()` | ❌ | |
| `util.debuglog()` | ❌ | |
| `util.deprecate()` | ❌ | |
| `util.format()` | ✅ | |
| `util.formatWithOptions()` | ❌ | |
| `util.getSystemErrorName()` | ❌ | |
| `util.getSystemErrorMap()` | ❌ | |
| `util.inherits()` | ❌ | |
| `util.inspect()` | ✅ | Basic support |
| `util.inspect.custom` | ❌ | |
| `util.inspect.defaultOptions` | ❌ | |
| `util.isDeepStrictEqual()` | ❌ | |
| `util.parseArgs()` | ❌ | |
| `util.parseEnv()` | ❌ | |
| `util.promisify()` | ❌ | |
| `util.stripVTControlCharacters()` | ❌ | |
| `util.styleText()` | ❌ | |
| `util.toUSVString()` | ❌ | |
| `util.transferableAbortController()` | ❌ | |
| `util.transferableAbortSignal()` | ❌ | |
| `util.types.isAnyArrayBuffer()` | ❌ | |
| `util.types.isArrayBuffer()` | ❌ | |
| `util.types.isArrayBufferView()` | ❌ | |
| `util.types.isAsyncFunction()` | ❌ | |
| `util.types.isBigInt64Array()` | ❌ | |
| `util.types.isBigUint64Array()` | ❌ | |
| `util.types.isBooleanObject()` | ❌ | |
| `util.types.isBoxedPrimitive()` | ❌ | |
| `util.types.isCryptoKey()` | ❌ | |
| `util.types.isDataView()` | ❌ | |
| `util.types.isDate()` | ❌ | |
| `util.types.isExternal()` | ❌ | |
| `util.types.isFloat32Array()` | ❌ | |
| `util.types.isFloat64Array()` | ❌ | |
| `util.types.isGeneratorFunction()` | ❌ | |
| `util.types.isGeneratorObject()` | ❌ | |
| `util.types.isInt8Array()` | ❌ | |
| `util.types.isInt16Array()` | ❌ | |
| `util.types.isInt32Array()` | ❌ | |
| `util.types.isKeyObject()` | ❌ | |
| `util.types.isMap()` | ❌ | |
| `util.types.isMapIterator()` | ❌ | |
| `util.types.isModuleNamespaceObject()` | ❌ | |
| `util.types.isNativeError()` | ❌ | |
| `util.types.isNumberObject()` | ❌ | |
| `util.types.isPromise()` | ❌ | |
| `util.types.isProxy()` | ❌ | |
| `util.types.isRegExp()` | ❌ | |
| `util.types.isSet()` | ❌ | |
| `util.types.isSetIterator()` | ❌ | |
| `util.types.isSharedArrayBuffer()` | ❌ | |
| `util.types.isStringObject()` | ❌ | |
| `util.types.isSymbolObject()` | ❌ | |
| `util.types.isTypedArray()` | ❌ | |
| `util.types.isUint8Array()` | ❌ | |
| `util.types.isUint8ClampedArray()` | ❌ | |
| `util.types.isUint16Array()` | ❌ | |
| `util.types.isUint32Array()` | ❌ | |
| `util.types.isWeakMap()` | ❌ | |
| `util.types.isWeakSet()` | ❌ | |
| TextDecoder class | ❌ | |
| TextEncoder class | ❌ | |

**Util Coverage: 2/62 (3%)**

---

## Global Objects

| Feature | Status | Notes |
|---------|--------|-------|
| `global` | ❌ | |
| `globalThis` | ❌ | |
| `__dirname` | ❌ | |
| `__filename` | ❌ | |
| `exports` | ❌ | |
| `module` | ❌ | |
| `require()` | ⚠️ | Basic support |

**Global Coverage: 1/7 (14%)**

---

## Summary

### Overall Node.js API Conformance

| Category | Implemented | Total | Coverage |
|----------|-------------|-------|----------|
| Buffer | 7 | 67 | 10% |
| Console | 1 | 19 | 5% |
| Crypto | 1 | 28 | 4% |
| Events | 8 | 21 | 38% |
| File System | 32 | 112 | 29% |
| HTTP | 18 | 63 | 29% |
| HTTPS | 5 | 7 | 71% |
| Net | 10 | 36 | 28% |
| OS | 0 | 22 | 0% |
| Path | 9 | 15 | 60% |
| Process | 5 | 55 | 9% |
| Stream | 10 | 44 | 23% |
| Timers | 4 | 14 | 29% |
| URL | 6 | 38 | 16% |
| Util | 2 | 62 | 3% |
| Global | 1 | 7 | 14% |
| **Total** | **119** | **610** | **20%** |

### Priority Implementation Targets

#### Critical (Required for most apps)
- `console.error()`, `console.warn()` - Basic logging
- `process.stdout`, `process.stderr` - Stream-based output
- `path.parse()`, `path.format()` - Path manipulation
- `fs.rename*()`, `fs.copy*()` - File operations
- `URLSearchParams` - Query string handling

#### High (Common use cases)
- `os.platform()`, `os.cpus()` - System info
- `crypto.randomBytes()`, `crypto.createHash()` - Basic crypto
- `stream.pipeline()`, `stream.finished()` - Stream utilities
- `util.promisify()` - Callback to Promise conversion
- `setImmediate()` - Event loop control

#### Medium (Framework support)
- `http.Agent` - Connection pooling
- `cluster` module - Multi-process
- `child_process` - Process spawning
- `readline` - Interactive input
- `zlib` - Compression

---

## Testing Status

Current test coverage:
- Buffer: 1 test file
- Console: Used in all tests
- Events: 1 test file
- File System: 1 test file
- HTTP: 1 test file (fetch API)
- Path: 1 test file
- Process: 1 test file
- Timers: 1 test file
- URL: 1 test file
- Util: 1 test file

All 42 Node.js API tests passing (28 TypeScript + 14 JavaScript).
