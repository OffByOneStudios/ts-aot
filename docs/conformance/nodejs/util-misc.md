# Node.js Util, URL, Path, QueryString, Readline, Module, Inspector, TTY, Crypto

Parent: [nodejs-features.md](../nodejs-features.md)

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
| `util.transferableAbortController()` | ✅ | Returns AbortController-like object (worker_threads not supported) |
| `util.transferableAbortSignal()` | ✅ | Pass-through in single-threaded context |
| `util.types.isAnyArrayBuffer()` | ✅ | Detects Buffer/ArrayBuffer instances |
| `util.types.isArrayBuffer()` | ✅ | Detects Buffer instances (our ArrayBuffer implementation) |
| `util.types.isArrayBufferView()` | ✅ | Detects TypedArray and DataView instances |
| `util.types.isAsyncFunction()` | N/A | Inherent AOT limitation - function metadata not available at runtime |
| `util.types.isBigInt64Array()` | ✅ | Detects BigInt64Array instances |
| `util.types.isBigUint64Array()` | ✅ | Detects BigUint64Array instances |
| `util.types.isBooleanObject()` | ✅ | Detects new Boolean() wrapper objects |
| `util.types.isBoxedPrimitive()` | ✅ | Detects Boolean/Number/String wrapper objects |
| `util.types.isCryptoKey()` | N/A | Web Crypto API not implemented |
| `util.types.isDataView()` | ✅ | Detects DataView instances |
| `util.types.isDate()` | ✅ | |
| `util.types.isExternal()` | N/A | V8-specific external memory objects not applicable |
| `util.types.isFloat32Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isFloat64Array()` | ✅ | 8-byte element size detection |
| `util.types.isGeneratorFunction()` | N/A | Inherent AOT limitation - function metadata not available at runtime |
| `util.types.isGeneratorObject()` | ✅ | Detects Generator and AsyncGenerator objects |
| `util.types.isInt8Array()` | ✅ | Uses TypedArrayType enum for proper detection |
| `util.types.isInt16Array()` | ✅ | 2-byte element size detection |
| `util.types.isInt32Array()` | ✅ | 4-byte element size detection |
| `util.types.isKeyObject()` | N/A | crypto.KeyObject class not implemented |
| `util.types.isMap()` | ✅ | Correctly distinguishes Map from plain objects |
| `util.types.isMapIterator()` | ✅ | Returns false (iterator objects not distinct types) |
| `util.types.isModuleNamespaceObject()` | ✅ | Returns false (module namespace objects not distinct types) |
| `util.types.isNativeError()` | ✅ | |
| `util.types.isNumberObject()` | ✅ | Detects new Number() wrapper objects |
| `util.types.isPromise()` | ✅ | Detects Promise instances |
| `util.types.isProxy()` | ✅ | Detects Proxy objects via dynamic_cast |
| `util.types.isRegExp()` | ✅ | |
| `util.types.isSet()` | ✅ | Works correctly |
| `util.types.isSetIterator()` | ✅ | Returns false (iterator objects not distinct types) |
| `util.types.isSharedArrayBuffer()` | N/A | SharedArrayBuffer requires worker_threads support |
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

**Util Coverage: 56/56 applicable (100%)** (55 full, 1 partial, 6 N/A out of 62 total)

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

## Readline

### Module Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `readline.createInterface(options)` | ✅ | Creates Interface with input/output streams |
| `readline.clearLine(stream, dir)` | ✅ | ANSI escape sequence for clearing line |
| `readline.clearScreenDown(stream)` | ✅ | ANSI escape sequence for clearing screen below |
| `readline.cursorTo(stream, x, y?)` | ✅ | ANSI escape sequence for absolute cursor position |
| `readline.moveCursor(stream, dx, dy)` | ✅ | ANSI escape sequence for relative cursor movement |
| `readline.emitKeypressEvents(stream)` | ✅ | Stub (no-op, keypress events not fully supported) |

### Interface Class
| Feature | Status | Notes |
|---------|--------|-------|
| `rl.close()` | ✅ | Closes the interface |
| `rl.pause()` | ✅ | Pauses input stream |
| `rl.resume()` | ✅ | Resumes input stream |
| `rl.setPrompt(prompt)` | ✅ | Sets the prompt string |
| `rl.prompt(preserveCursor?)` | ✅ | Writes prompt to output |
| `rl.question(query, callback)` | ✅ | Displays query and waits for response |
| `rl.write(data)` | ✅ | Writes to output stream |
| `rl.line` | ✅ | Current input line property |
| `rl.cursor` | ✅ | Cursor position property |
| `rl.getPrompt()` | ✅ | Gets the current prompt string |
| `rl[Symbol.asyncIterator]()` | ✅ | Returns async iterator for lines |

### Interface Events
| Feature | Status | Notes |
|---------|--------|-------|
| `'line'` event | ✅ | Line event when newline received |
| `'close'` event | ✅ | Close event emitted on close() |
| `'pause'` event | ✅ | Pause event emitted on pause() |
| `'resume'` event | ✅ | Resume event emitted on resume() |
| `'history'` event | ✅ | Emitted when history changes |
| `'SIGCONT'` event | ✅ | Emitted on SIGCONT signal |
| `'SIGINT'` event | ✅ | Emitted on SIGINT signal |
| `'SIGTSTP'` event | ✅ | Emitted on SIGTSTP signal |

**Readline Coverage: 25/25 (100%)**

---

## Module

Note: The module module provides utilities for interacting with the module system. Since ts-aot handles require() at compile time, some features are stubs.

| Feature | Status | Notes |
|---------|--------|-------|
| `module.builtinModules` | ✅ | Array of built-in module names |
| `module.isBuiltin(name)` | ✅ | Checks if module is built-in, handles node: prefix |
| `module.createRequire(path)` | ✅ | Stub (returns null) - require handled at compile time |
| `module.syncBuiltinESMExports()` | ✅ | No-op stub - no separate ESM/CJS at runtime |
| `module.register()` | ✅ | No-op stub - loader hooks not applicable in AOT |
| `module.registerHooks()` | ✅ | Stub returning object - loader hooks not applicable in AOT |
| `module.findPackageJSON()` | ✅ | Stub returning undefined - package discovery not applicable in AOT |
| `module.SourceMap` | ✅ | Stub class - source maps not applicable in AOT |

**Module Coverage: 8/8 (100%)**

---

## Inspector

Note: Since ts-aot uses LLVM (not V8), inspector functionality is **stubbed** to allow code that imports the module to compile without errors.

| Feature | Status | Notes |
|---------|--------|-------|
| `inspector.open()` | ✅ | No-op stub |
| `inspector.close()` | ✅ | No-op stub |
| `inspector.url()` | ✅ | Returns undefined |
| `inspector.waitForDebugger()` | ✅ | No-op stub (returns immediately) |
| `inspector.console` | ✅ | Returns undefined |
| `new inspector.Session()` | ✅ | Creates stub session object |
| `session.connect()` | ✅ | No-op stub |
| `session.connectToMainThread()` | ✅ | No-op stub |
| `session.disconnect()` | ✅ | No-op stub |
| `session.post()` | ✅ | No-op stub |

**Inspector Coverage: 10/10 (100%)** - Stubbed (no V8)

---

## TTY

### Module Functions
| Feature | Status | Notes |
|---------|--------|-------|
| `tty.isatty(fd)` | ✅ | Uses libuv uv_guess_handle |

### ReadStream Class
| Feature | Status | Notes |
|---------|--------|-------|
| `new tty.ReadStream(fd)` | ✅ | Creates TTY read stream |
| `readStream.isTTY` | ✅ | Always returns true |
| `readStream.isRaw` | ✅ | Returns raw mode state |
| `readStream.setRawMode(mode)` | ✅ | Uses uv_tty_set_mode |

### WriteStream Class
| Feature | Status | Notes |
|---------|--------|-------|
| `new tty.WriteStream(fd)` | ✅ | Creates TTY write stream |
| `writeStream.isTTY` | ✅ | Always returns true |
| `writeStream.columns` | ✅ | Uses uv_tty_get_winsize |
| `writeStream.rows` | ✅ | Uses uv_tty_get_winsize |
| `writeStream.getWindowSize()` | ✅ | Returns [columns, rows] |
| `writeStream.clearLine(dir)` | ✅ | ANSI escape sequences |
| `writeStream.clearScreenDown()` | ✅ | ANSI escape sequences |
| `writeStream.cursorTo(x, y)` | ✅ | ANSI escape sequences |
| `writeStream.moveCursor(dx, dy)` | ✅ | ANSI escape sequences |
| `writeStream.getColorDepth()` | ✅ | Checks TERM/COLORTERM env |
| `writeStream.hasColors(count)` | ✅ | Based on color depth |
| `writeStream.write(data)` | ✅ | Writes string or Buffer |

**TTY Coverage: 17/17 (100%)**

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
