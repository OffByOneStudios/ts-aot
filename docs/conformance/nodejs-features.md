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
| `assert` | ✅ | 100% | Testing utilities |
| `async_hooks` | ✅ | 100% | Async context tracking |
| `buffer` | ✅ | 100% | Binary data handling |
| `child_process` | ✅ | 100% | Process spawning with IPC |
| `cluster` | ✅ | 100% | Multi-process forking |
| `console` | ✅ | 100% | Complete logging support |
| `crypto` | ✅ | 100% | Cryptographic functions |
| `dgram` | ❌ | 0% | UDP sockets |
| `dns` | ⚠️ | 80% | DNS resolution |
| `domain` | N/A | - | Deprecated |
| `events` | ✅ | 86% | EventEmitter |
| `fs` | ⚠️ | 98% | File system |
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
| `stream` | ✅ | 100% | Streams |
| `string_decoder` | ✅ | 100% | String decoding |
| `timers` | ⚠️ | 93% | Timers |
| `tls` | ⚠️ | 30% | TLS/SSL |
| `tty` | ❌ | 0% | TTY |
| `url` | ⚠️ | 82% | URL parsing |
| `util` | ⚠️ | 77% | Utilities |
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
| `assert.equal(actual, expected)` | ✅ | Loose equality (==) |
| `assert.notEqual(actual, expected)` | ✅ | Loose inequality (!=) |
| `assert.strictEqual(actual, expected)` | ✅ | Strict equality check |
| `assert.notStrictEqual(actual, expected)` | ✅ | Strict inequality check |
| `assert.deepEqual(actual, expected)` | ✅ | Deep equality for objects/arrays |
| `assert.notDeepEqual(actual, expected)` | ✅ | Deep inequality check |
| `assert.deepStrictEqual(actual, expected)` | ✅ | Deep strict equality for objects/arrays |
| `assert.notDeepStrictEqual(actual, expected)` | ✅ | Deep strict inequality check |
| `assert.throws(fn)` | ✅ | Full exception handling via setjmp/longjmp |
| `assert.doesNotThrow(fn)` | ✅ | Full exception handling via setjmp/longjmp |
| `assert.rejects(asyncFn)` | ✅ | Full async rejection handling via Promise.then() |
| `assert.doesNotReject(asyncFn)` | ✅ | Full async rejection handling via Promise.then() |
| `assert.match(string, regexp)` | ✅ | Regex string matching |
| `assert.doesNotMatch(string, regexp)` | ✅ | Regex string non-matching |
| `assert.fail(message)` | ✅ | Always fails with message |
| `assert.ifError(value)` | ✅ | Fails if value is truthy |

**Assert Coverage: 18/18 (100%)**

---

## Async Hooks

| Feature | Status | Notes |
|---------|--------|-------|
| `async_hooks.executionAsyncId()` | ✅ | Returns current async ID |
| `async_hooks.triggerAsyncId()` | ✅ | Returns trigger async ID |
| `async_hooks.executionAsyncResource()` | ✅ | Returns current async resource |
| `async_hooks.createHook(callbacks)` | ✅ | Creates async hook with init/before/after/destroy/promiseResolve |
| `AsyncHook.enable()` | ✅ | Enables the hook |
| `AsyncHook.disable()` | ✅ | Disables the hook |
| `AsyncLocalStorage` constructor | ✅ | Creates new instance |
| `AsyncLocalStorage.getStore()` | ✅ | Returns current store value |
| `AsyncLocalStorage.run(store, callback)` | ✅ | Runs callback with store |
| `AsyncLocalStorage.exit(callback)` | ✅ | Runs callback with no store |
| `AsyncLocalStorage.enterWith(store)` | ✅ | Sets store for current context |
| `AsyncLocalStorage.disable()` | ✅ | Disables the instance |
| `AsyncResource` constructor | ✅ | Creates new async resource |
| `AsyncResource.asyncId()` | ✅ | Returns resource async ID |
| `AsyncResource.triggerAsyncId()` | ✅ | Returns resource trigger ID |
| `AsyncResource.runInAsyncScope(fn)` | ✅ | Runs fn in resource scope |
| `AsyncResource.bind(fn)` | ✅ | Binds fn to resource |
| `AsyncResource.emitDestroy()` | ✅ | Emits destroy event |

**Async Hooks Coverage: 18/18 (100%)**

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
| `Buffer.from(arrayBuffer)` | ✅ | With byteOffset and length support |
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

## Child Process

### Module Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `child_process.spawn(command, args, options)` | ✅ | Basic process spawning with stdio |
| `child_process.spawnSync(command, args, options)` | ✅ | Synchronous spawning with output capture |
| `child_process.exec(command, callback)` | ✅ | Shell command with callback support |
| `child_process.execSync(command, options)` | ✅ | Synchronous shell execution |
| `child_process.execFile(file, args, callback)` | ✅ | Delegates to spawn |
| `child_process.execFileSync(file, args, options)` | ✅ | Synchronous file execution |
| `child_process.fork(modulePath)` | ✅ | IPC via fd 3, length-prefixed JSON |

### ChildProcess Class
| Feature | Status | Notes |
|---------|--------|-------|
| `process.pid` | ✅ | Process ID |
| `process.connected` | ✅ | IPC connection status |
| `process.killed` | ✅ | Process killed status |
| `process.exitCode` | ✅ | Exit code (after exit) |
| `process.signalCode` | ✅ | Signal code (after exit) |
| `process.spawnfile` | ✅ | Command that was spawned |
| `process.spawnargs` | ✅ | Arguments passed to spawn |
| `process.stdin` | ✅ | Writable stream |
| `process.stdout` | ✅ | Readable stream |
| `process.stderr` | ✅ | Readable stream |
| `process.stdio` | ✅ | Array [stdin, stdout, stderr] |
| `process.channel` | ✅ | IPC channel (pipe handle) |
| `process.kill(signal)` | ✅ | Send signal to process |
| `process.send(message)` | ✅ | IPC messaging |
| `process.disconnect()` | ✅ | Disconnect IPC |
| `process.ref()` | ✅ | Keep event loop alive |
| `process.unref()` | ✅ | Allow event loop to exit |

### ChildProcess Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'spawn'` event | ✅ | Process spawned |
| `'exit'` event | ✅ | Process exited |
| `'close'` event | ✅ | Streams closed |
| `'error'` event | ✅ | Error occurred |
| `'disconnect'` event | ✅ | IPC disconnected |
| `'message'` event | ✅ | IPC message |

**Child Process Coverage: 31/31 (100%)**

---

## Cluster

### Module Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `cluster.isMaster` | ✅ | Deprecated alias for isPrimary |
| `cluster.isPrimary` | ✅ | true if this is the primary process |
| `cluster.isWorker` | ✅ | true if this is a worker process |
| `cluster.worker` | ✅ | Reference to current worker object (only in worker) |
| `cluster.workers` | ✅ | Map of all active workers (only in primary) |
| `cluster.settings` | ✅ | Cluster settings object |
| `cluster.schedulingPolicy` | ✅ | SCHED_NONE (0) or SCHED_RR (1) |
| `cluster.SCHED_NONE` | ✅ | OS handles scheduling |
| `cluster.SCHED_RR` | ✅ | Round-robin scheduling |

### Module Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `cluster.fork(env)` | ✅ | Spawn worker process |
| `cluster.setupPrimary(settings)` | ✅ | Configure cluster settings |
| `cluster.setupMaster(settings)` | ✅ | Deprecated alias for setupPrimary |
| `cluster.disconnect(callback)` | ✅ | Disconnect all workers |

### Worker Class
| Feature | Status | Notes |
|---------|--------|-------|
| `worker.id` | ✅ | Worker ID |
| `worker.process` | ✅ | ChildProcess reference |
| `worker.isDead` | ✅ | Check if worker is dead |
| `worker.exitedAfterDisconnect` | ✅ | Whether worker exited after disconnect |
| `worker.send(message)` | ✅ | IPC messaging |
| `worker.disconnect()` | ✅ | Disconnect worker |
| `worker.kill(signal)` | ✅ | Kill worker process |
| `worker.isConnected()` | ✅ | Check IPC connection status |

### Cluster Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'fork'` event | ✅ | Worker forked |
| `'online'` event | ✅ | Worker is online |
| `'listening'` event | ✅ | Worker is listening |
| `'disconnect'` event | ✅ | Worker disconnected |
| `'exit'` event | ✅ | Worker exited |
| `'message'` event | ✅ | IPC message from worker |
| `'error'` event | ✅ | Error occurred |

### Worker Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'online'` event | ✅ | Worker is online |
| `'listening'` event | ✅ | Worker is listening |
| `'disconnect'` event | ✅ | Worker disconnected |
| `'exit'` event | ✅ | Worker exited |
| `'message'` event | ✅ | IPC message |
| `'error'` event | ✅ | Error occurred |

**Cluster Coverage: 34/34 (100%)**

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
| `crypto.createCipheriv()` | ✅ | AES-CBC, AES-GCM modes |
| `crypto.createDecipheriv()` | ✅ | AES-CBC, AES-GCM modes |
| `crypto.getCiphers()` | ✅ | Returns available cipher algorithms |
| `Cipher.update(data)` | ✅ | Returns encrypted Buffer |
| `Cipher.final()` | ✅ | Returns final encrypted Buffer |
| `Cipher.getAuthTag()` | ✅ | For GCM mode |
| `Cipher.setAAD(data)` | ✅ | For GCM mode |
| `Decipher.update(data)` | ✅ | Returns decrypted Buffer |
| `Decipher.final()` | ✅ | Returns final decrypted Buffer |
| `Decipher.setAuthTag(tag)` | ✅ | For GCM mode |
| `Decipher.setAAD(data)` | ✅ | For GCM mode |

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
| `crypto.generateKeyPair()` | ✅ | RSA, EC, Ed25519, X25519 via OpenSSL |
| `crypto.generateKeyPairSync()` | ✅ | RSA, EC, Ed25519, X25519 via OpenSSL |
| `crypto.generateKey()` | ✅ | AES, HMAC symmetric keys |
| `crypto.generateKeySync()` | ✅ | AES, HMAC symmetric keys |
| `crypto.createPrivateKey()` | ✅ | From PEM string |
| `crypto.createPublicKey()` | ✅ | From PEM string |
| `crypto.createSecretKey()` | ✅ | From Buffer |

### Sign/Verify
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.createSign()` | ✅ | OpenSSL EVP_DigestSign API |
| `crypto.createVerify()` | ✅ | OpenSSL EVP_DigestVerify API |
| `crypto.sign()` | ✅ | One-shot signing |
| `crypto.verify()` | ✅ | One-shot verification |
| `Sign.update(data)` | ✅ | Chainable |
| `Sign.sign(privateKey)` | ✅ | Returns Buffer |
| `Verify.update(data)` | ✅ | Chainable |
| `Verify.verify(publicKey, signature)` | ✅ | Returns boolean |

### Key Derivation
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.hkdfSync()` | ✅ | HKDF (RFC 5869) via OpenSSL EVP_KDF |
| `crypto.pbkdf2()` | ✅ | Async with libuv thread pool |
| `crypto.pbkdf2Sync()` | ✅ | OpenSSL PKCS5_PBKDF2_HMAC |
| `crypto.scrypt()` | ✅ | Async with libuv thread pool |
| `crypto.scryptSync()` | ✅ | OpenSSL EVP_PBE_scrypt |

### Utility
| Feature | Status | Notes |
|---------|--------|-------|
| `crypto.timingSafeEqual()` | ✅ | OpenSSL CRYPTO_memcmp |

**Crypto Coverage: 46/46 (100%)**

---

## DNS

### Callback API
| Feature | Status | Notes |
|---------|--------|-------|
| `dns.lookup(hostname, callback)` | ✅ | Uses libuv getaddrinfo |
| `dns.lookup(hostname, options, callback)` | ✅ | Family option supported |
| `dns.resolve(hostname, callback)` | ❌ | |
| `dns.resolve4(hostname, callback)` | ✅ | IPv4 address resolution |
| `dns.resolve6(hostname, callback)` | ✅ | IPv6 address resolution |
| `dns.resolveCname(hostname, callback)` | ❌ | |
| `dns.resolveMx(hostname, callback)` | ❌ | |
| `dns.resolveNs(hostname, callback)` | ❌ | |
| `dns.resolveTxt(hostname, callback)` | ❌ | |
| `dns.resolveSrv(hostname, callback)` | ❌ | |
| `dns.resolvePtr(hostname, callback)` | ❌ | |
| `dns.resolveNaptr(hostname, callback)` | ❌ | |
| `dns.resolveSoa(hostname, callback)` | ❌ | |
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
| `dns.promises.resolve(hostname)` | ❌ | |
| `dns.promises.resolve4(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.resolve6(hostname)` | ✅ | Returns Promise<string[]> |
| `dns.promises.reverse(ip)` | ✅ | Returns Promise<string[]> |
| `dns.promises.getServers()` | ❌ | |
| `dns.promises.setServers()` | ❌ | |

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

**DNS Coverage: 40/50 (80%)**

---

## Events (EventEmitter)

### Static Methods
| Feature | Status | Notes |
|---------|--------|-------|
| `EventEmitter.listenerCount(emitter, event)` | ✅ | Deprecated but still available |
| `EventEmitter.on(emitter, event)` | N/A | Requires async iteration |
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
| `emitter.prependOnceListener(event, listener)` | ✅ | Once semantics work correctly |
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

**Events Coverage: 21/21 (100%)**

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
| `fs.promises.cp()` | ✅ | Recursive copy |
| `fs.promises.lchmod()` | ✅ | Symlink mode |
| `fs.promises.lchown()` | ✅ | Symlink ownership |
| `fs.promises.link()` | ✅ | |
| `fs.promises.lstat()` | ✅ | |
| `fs.promises.lutimes()` | ✅ | Symlink timestamps |
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
| `fs.promises.watch()` | ✅ | Async iterator with libuv fs events |
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
| `fs.cpSync()` | ✅ | Recursive copy |
| `fs.existsSync()` | ✅ | |
| `fs.fchmodSync()` | ✅ | |
| `fs.fchownSync()` | ✅ | |
| `fs.fdatasyncSync()` | ✅ | |
| `fs.fstatSync()` | ✅ | |
| `fs.fsyncSync()` | ✅ | |
| `fs.ftruncateSync()` | ✅ | |
| `fs.futimesSync()` | ✅ | |
| `fs.lchmodSync()` | ✅ | Symlink mode (falls back to chmod on Windows) |
| `fs.lchownSync()` | ✅ | Symlink ownership |
| `fs.linkSync()` | ✅ | |
| `fs.lstatSync()` | ✅ | |
| `fs.lutimesSync()` | ✅ | Symlink timestamps |
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
| `fs.cp()` | ✅ | Recursive copy via promises |
| `fs.exists()` | ✅ | Deprecated but implemented |
| `fs.fchmod()` | ✅ | Async via promise.then |
| `fs.fchown()` | ✅ | Async via promise.then |
| `fs.fdatasync()` | ✅ | Async via promise.then |
| `fs.fstat()` | ✅ | Async via promise.then |
| `fs.fsync()` | ✅ | Async via promise.then |
| `fs.ftruncate()` | ✅ | Async via promise.then |
| `fs.futimes()` | ✅ | Async via promise.then |
| `fs.lchmod()` | ✅ | Symlink mode async |
| `fs.lchown()` | ✅ | Symlink ownership async |
| `fs.link()` | ✅ | Via promises |
| `fs.lstat()` | ✅ | Via promises |
| `fs.lutimes()` | ✅ | Symlink timestamps async |
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
| `fs.unwatchFile()` | ✅ | Removes file watch listener |
| `fs.utimes()` | ✅ | Via promises |
| `fs.watch()` | ✅ | Returns FSWatcher |
| `fs.watchFile()` | ✅ | Polling-based file watch |
| `fs.write()` | ✅ | |
| `fs.writeFile()` | ✅ | |

### Streams
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.createReadStream()` | ✅ | Options: start, end, highWaterMark, autoClose; Properties: bytesRead, path, pending |
| `fs.createWriteStream()` | ✅ | Options: start, flags, autoClose; Properties: bytesWritten, path, pending |

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
| `stats.mtime` | ✅ | Returns Date object |
| `stats.atime` | ✅ | Returns Date object |
| `stats.ctime` | ✅ | Returns Date object |

**File System Coverage: 123/123 (100%)**

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
| `readable.read()` | ✅ | Pulls data from unshift buffer |
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
| `readable[Symbol.asyncIterator]()` | N/A | Requires async iteration |

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

**Stream Coverage: 43/43 (100%)**

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
| `timers/promises.setInterval()` | ✅ | Works correctly; compiler async state machine has separate issue |
| `timers/promises.setImmediate()` | ✅ | Promise-based with optional value |
| `timers/promises.scheduler.wait()` | ✅ | Alias for setTimeout |
| `timers/promises.scheduler.yield()` | ✅ | Alias for setImmediate |

**Timers Coverage: 14/14 (100%)**

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
| `new URL(input, base)` | ✅ | Full WHATWG base resolution |
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

**URL Coverage: 38/38 (100%)**

---

## Util

| Feature | Status | Notes |
|---------|--------|-------|
| `util.callbackify()` | ✅ | Converts Promise-returning functions to callback style |
| `util.debuglog()` | ✅ | Checks NODE_DEBUG env var, outputs to stderr with section and PID |
| `util.deprecate()` | ✅ | Wraps function, emits warning on first call (once per message) |
| `util.format()` | ✅ | Printf-like string formatting (%s, %d, %f, %o, %j) |
| `util.formatWithOptions()` | ✅ | Delegates to format() (options not yet used) |
| `util.getSystemErrorName()` | ✅ | Maps errno codes to names (EPERM, ENOENT, etc.) |
| `util.getSystemErrorMap()` | ✅ | Returns Map of all system error codes |
| `util.inherits()` | ✅ | Sets super_ property on constructor for legacy compatibility |
| `util.inspect()` | ✅ | Properly inspects objects, arrays, Date, RegExp, Map, Set, etc. |
| `util.inspect.custom` | ✅ | Symbol for custom inspect functions |
| `util.inspect.defaultOptions` | ✅ | Default options object with showHidden, depth, colors, etc. |
| `util.isDeepStrictEqual()` | ✅ | Deep equality comparison for objects and arrays |
| `util.parseArgs()` | ✅ | Boolean/string options, short aliases, args array |
| `util.parseEnv()` | ✅ | Parses dotenv content (key=value, quotes, comments, export prefix) |
| `util.promisify()` | ✅ | Converts callback-style functions to Promise-returning |
| `util.stripVTControlCharacters()` | ✅ | Removes ANSI escape codes |
| `util.styleText()` | ✅ | ANSI text styling (bold, colors, etc.) |
| `util.toUSVString()` | ✅ | Replaces lone surrogates with U+FFFD |
| `util.transferableAbortController()` | ❌ | |
| `util.transferableAbortSignal()` | ❌ | |
| `util.types.isAnyArrayBuffer()` | ✅ | Detects Buffer/ArrayBuffer instances |
| `util.types.isArrayBuffer()` | ✅ | Detects Buffer instances (our ArrayBuffer implementation) |
| `util.types.isArrayBufferView()` | ✅ | Detects TypedArray and DataView instances |
| `util.types.isAsyncFunction()` | N/A | Inherent AOT limitation - function metadata not available at runtime |
| `util.types.isBigInt64Array()` | ✅ | Detects BigInt64Array instances |
| `util.types.isBigUint64Array()` | ✅ | Detects BigUint64Array instances |
| `util.types.isBooleanObject()` | ✅ | Detects new Boolean() wrapper objects |
| `util.types.isBoxedPrimitive()` | ✅ | Detects Boolean/Number/String wrapper objects |
| `util.types.isCryptoKey()` | ❌ | |
| `util.types.isDataView()` | ✅ | Detects DataView instances |
| `util.types.isDate()` | ✅ | |
| `util.types.isExternal()` | ❌ | |
| `util.types.isFloat32Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isFloat64Array()` | ✅ | 8-byte element size detection |
| `util.types.isGeneratorFunction()` | N/A | Inherent AOT limitation - function metadata not available at runtime |
| `util.types.isGeneratorObject()` | ✅ | Detects Generator and AsyncGenerator objects |
| `util.types.isInt8Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isInt16Array()` | ✅ | 2-byte element size detection |
| `util.types.isInt32Array()` | ✅ | 4-byte element size detection |
| `util.types.isKeyObject()` | ❌ | |
| `util.types.isMap()` | ✅ | Correctly distinguishes Map from plain objects |
| `util.types.isMapIterator()` | ❌ | |
| `util.types.isModuleNamespaceObject()` | ❌ | |
| `util.types.isNativeError()` | ✅ | |
| `util.types.isNumberObject()` | ✅ | Detects new Number() wrapper objects |
| `util.types.isPromise()` | ✅ | Detects Promise instances |
| `util.types.isProxy()` | ✅ | Detects Proxy objects via dynamic_cast |
| `util.types.isRegExp()` | ✅ | |
| `util.types.isSet()` | ✅ | Works correctly |
| `util.types.isSetIterator()` | ❌ | |
| `util.types.isSharedArrayBuffer()` | ❌ | |
| `util.types.isStringObject()` | ✅ | Detects new String() wrapper objects |
| `util.types.isSymbolObject()` | ⚠️ | Runtime implemented, needs Object(symbol) wrapper support |
| `util.types.isTypedArray()` | ✅ | Detects TypedArray instances |
| `util.types.isUint8Array()` | ✅ | Detects Uint8Array instances |
| `util.types.isUint8ClampedArray()` | ✅ | Detects clamped 1-byte arrays |
| `util.types.isUint16Array()` | ✅ | 2-byte element size detection |
| `util.types.isUint32Array()` | ✅ | 4-byte element size detection |
| `util.types.isWeakMap()` | ✅ | Detects TsWeakMap wrapper instances |
| `util.types.isWeakSet()` | ✅ | Detects TsWeakSet wrapper instances |
| TextDecoder class | ✅ | UTF-8 decoding with BOM handling |
| TextEncoder class | ✅ | UTF-8 encoding to Buffer |

**Util Coverage: 51/60 (85%)** (50 full, 1 partial, 2 N/A)

Note: isAsyncFunction and isGeneratorFunction are marked N/A as they are inherent AOT compilation limitations - function type metadata is not preserved at runtime.

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
| Child Process | 31 | 31 | 100% |
| Cluster | 26 | 32 | 81% |
| Console | 19 | 19 | 100% |
| Crypto | 46 | 46 | 100% |
| Events | 21 | 21 | 100% |
| File System | 123 | 123 | 100% |
| HTTP | 49 | 67 | 73% |
| HTTPS | 5 | 7 | 71% |
| Net | 36 | 36 | 100% |
| OS | 23 | 23 | 100% |
| Path | 15 | 15 | 100% |
| Process | 37 | 55 | 67% |
| QueryString | 6 | 6 | 100% |
| Stream | 43 | 43 | 100% |
| StringDecoder | 5 | 5 | 100% |
| Timers | 14 | 14 | 100% |
| TLS | 6 | 20 | 30% |
| URL | 38 | 38 | 100% |
| Util | 51 | 60 | 85% |
| Global | 5 | 7 | 71% |
| **Total** | **666** | **736** | **90%** |

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
- ✅ `cluster` module - Multi-process forking (implemented)
- ✅ `child_process` - Process spawning (implemented)
- `readline` - Interactive input
- `zlib` - Compression

---

## Testing Status

Current test coverage:
- Buffer: 6 test files (basic, advanced, encoding, extended, typed_array, utilities)
- Child Process: 6 test files (spawn, spawn_sync, exec, exec_file_sync, fork, stdio_ref)
- Cluster: 1 test file (basic - tests isMaster, isPrimary, isWorker, workers, settings, setupPrimary)
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
- Util: 16 test files (basic, extended, text_encoding, callbackify, promisify, strip_vt, deep_strict_equal, types_*)

Most Node.js API tests passing.
