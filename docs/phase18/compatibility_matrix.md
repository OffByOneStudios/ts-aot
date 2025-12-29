# Node.js v22.18.0 API Baseline

This document tracks the feature parity between Node.js LTS and ts-aot.

| Module | Feature | Type | Status | Notes |
|--------|---------|------|--------|-------|
| fs | Dir | function | 🟢 | |
| fs | Dirent | function | 🟢 | |
| fs | F_OK | number | 🟢 | |
| fs | FileReadStream | function | 🔴 | |
| fs | FileWriteStream | function | 🔴 | |
| fs | R_OK | number | 🟢 | |
| fs | ReadStream | function | 🟢 | |
| fs | Stats | function | 🟢 | |
| fs | W_OK | number | 🟢 | |
| fs | WriteStream | function | 🟢 | |
| fs | X_OK | number | 🟢 | |
| fs | _toUnixTimestamp | function | 🔴 | |
| fs | access | function | 🟢 | |
| fs | accessSync | function | 🟢 | |
| fs | appendFile | function | 🟢 | |
| fs | appendFileSync | function | 🟢 | |
| fs | chmod | function | 🟢 | |
| fs | chmodSync | function | 🟢 | |
| fs | chown | function | 🟢 | |
| fs | chownSync | function | 🟢 | |
| fs | close | function | 🟢 | |
| fs | closeSync | function | 🟢 | |
| fs | constants | object | 🟢 | |
| fs | copyFile | function | 🟢 | |
| fs | copyFileSync | function | 🟢 | |
| fs | cp | function | 🟢 | |
| fs | cpSync | function | 🟢 | |
| fs | createReadStream | function | 🟢 | |
| fs | createWriteStream | function | 🟢 | |
| fs | exists | function | 🟢 | |
| fs | existsSync | function | 🟢 | |
| fs | fchmod | function | 🟢 | |
| fs | fchmodSync | function | 🟢 | |
| fs | fchown | function | 🟢 | |
| fs | fchownSync | function | 🟢 | |
| fs | fdatasync | function | 🟢 | |
| fs | fdatasyncSync | function | 🟢 | |
| fs | fstat | function | 🟢 | |
| fs | fstatSync | function | 🟢 | |
| fs | fsync | function | 🟢 | |
| fs | fsyncSync | function | 🟢 | |
| fs | ftruncate | function | 🟢 | |
| fs | ftruncateSync | function | 🟢 | |
| fs | futimes | function | 🟢 | |
| fs | futimesSync | function | 🟢 | |
| fs | glob | function | 🔴 | |
| fs | globSync | function | 🔴 | |
| fs | lchmod | undefined | 🔴 | |
| fs | lchmodSync | undefined | 🔴 | |
| fs | lchown | function | 🟢 | |
| fs | lchownSync | function | 🟢 | |
| fs | link | function | 🟢 | |
| fs | linkSync | function | 🟢 | |
| fs | lstat | function | 🟢 | |
| fs | lstatSync | function | 🟢 | |
| fs | lutimes | function | 🟢 | |
| fs | lutimesSync | function | 🟢 | |
| fs | mkdir | function | 🟢 | |
| fs | mkdirSync | function | 🟢 | |
| fs | mkdtemp | function | 🟢 | |
| fs | mkdtempSync | function | 🟢 | |
| fs | open | function | 🟢 | |
| fs | openAsBlob | function | 🔴 | |
| fs | openSync | function | 🟢 | |
| fs | opendir | function | 🟢 | |
| fs | opendirSync | function | 🟢 | |
| fs | promises | object | 🟡 | `readFile`, `writeFile`, `readdir`, `stat`, `mkdir`, `open`, `close`, `read`, `write`, `access`, `chmod`, `chown`, `copyFile`, `cp`, `link`, `lstat`, `lutimes`, `mkdtemp`, `realpath`, `rename`, `rm`, `rmdir`, `symlink`, `truncate`, `unlink`, `utimes`, `appendFile`, `opendir`, `FileHandle.sync`, `FileHandle.datasync`, `FileHandle.readv`, `FileHandle.writev` implemented |
| fs | read | function | 🟢 | |
| fs | readFile | function | 🟢 | |
| fs | readFileSync | function | 🟢 | |
| fs | readSync | function | 🟢 | |
| fs | readdir | function | 🟢 | |
| fs | readdirSync | function | 🟢 | |
| fs | readlink | function | 🟢 | |
| fs | readlinkSync | function | 🟢 | |
| fs | readv | function | 🟢 | |
| fs | readvSync | function | 🟢 | |
| fs | realpath | function | 🟢 | |
| fs | realpathSync | function | 🟢 | |
| fs | rename | function | 🟢 | |
| fs | renameSync | function | 🟢 | |
| fs | rm | function | 🟢 | |
| fs | rmSync | function | 🟢 | |
| fs | rmdir | function | 🟢 | |
| fs | rmdirSync | function | 🟢 | |
| fs | stat | function | 🟢 | |
| fs | statSync | function | 🟢 | |
| fs | statfs | function | 🟢 | |
| fs | statfsSync | function | 🟢 | |
| fs | symlink | function | 🟢 | |
| fs | symlinkSync | function | 🟢 | |
| fs | truncate | function | 🟢 | |
| fs | truncateSync | function | 🟢 | |
| fs | unlink | function | 🟢 | |
| fs | unlinkSync | function | 🟢 | |
| fs | unwatchFile | function | 🟢 | |
| fs | utimes | function | 🟢 | |
| fs | utimesSync | function | 🟢 | |
| fs | watch | function | 🟢 | |
| fs | watchFile | function | 🟢 | |
| fs | write | function | 🟢 | |
| fs | writeFile | function | 🟢 | |
| fs | writeFileSync | function | 🟢 | |
| fs | writeSync | function | 🟢 | |
| fs | writev | function | 🟢 | |
| fs | writevSync | function | 🟢 | |
| path | _makeLong | function | 🔴 | |
| path | basename | function | 🟢 | |
| path | delimiter | string | 🟢 | |
| path | dirname | function | 🟢 | |
| path | extname | function | 🟢 | |
| path | format | function | 🟢 | |
| path | isAbsolute | function | 🟢 | |
| path | join | function | 🟢 | |
| path | matchesGlob | function | 🔴 | |
| path | normalize | function | 🟢 | |
| path | parse | function | 🟢 | |
| path | posix | object | 🟢 | |
| path | relative | function | 🟢 | |
| path | resolve | function | 🟢 | |
| path | sep | string | 🟢 | |
| path | toNamespacedPath | function | 🟢 | |
| path | win32 | object | 🟢 | |
| events | EventEmitter | function | 🟢 | Robust implementation in runtime |
| events | EventEmitterAsyncResource | function | 🔴 | |
| events | addAbortListener | function | 🔴 | |
| events | captureRejectionSymbol | symbol | 🔴 | |
| events | captureRejections | boolean | 🔴 | |
| events | defaultMaxListeners | number | 🔴 | |
| events | errorMonitor | symbol | 🔴 | |
| events | getEventListeners | function | 🔴 | |
| events | getMaxListeners | function | 🟢 | |
| events | init | function | 🔴 | |
| events | listenerCount | function | 🟢 | |
| events | on | function | 🟢 | Alias for addListener |
| events | once | function | 🟢 | |
| events | setMaxListeners | function | 🟢 | |
| events | usingDomains | boolean | 🔴 | |
| stream | Duplex | function | 🟢 | |
| stream | PassThrough | function | 🔴 | |
| stream | Readable | function | 🟢 | Flowing/Paused modes, backpressure |
| stream | Stream | function | 🟢 | |
| stream | Transform | function | 🟢 | |
| stream | Writable | function | 🟢 | Buffering, highWaterMark, drain event |
| stream | _isArrayBufferView | function | 🔴 | |
| stream | _isUint8Array | function | 🔴 | |
| stream | _uint8ArrayToBuffer | function | 🔴 | |
| stream | addAbortSignal | function | 🔴 | |
| stream | compose | function | 🔴 | |
| stream | destroy | function | 🔴 | |
| stream | duplexPair | function | 🔴 | |
| stream | finished | function | 🔴 | |
| stream | getDefaultHighWaterMark | function | 🔴 | |
| stream | isDestroyed | function | 🔴 | |
| stream | isDisturbed | function | 🔴 | |
| stream | isErrored | function | 🔴 | |
| stream | isReadable | function | 🔴 | |
| stream | isWritable | function | 🔴 | |
| stream | pipeline | function | 🔴 | |
| stream | promises | object | 🔴 | |
| stream | setDefaultHighWaterMark | function | 🔴 | |
| util | MIMEParams | function | 🔴 | |
| util | MIMEType | function | 🔴 | |
| util | TextDecoder | function | 🔴 | |
| util | TextEncoder | function | 🔴 | |
| util | _errnoException | function | 🔴 | |
| util | _exceptionWithHostPort | function | 🔴 | |
| util | _extend | function | 🔴 | |
| util | aborted | function | 🔴 | |
| util | callbackify | function | 🔴 | |
| util | debug | function | 🔴 | |
| util | debuglog | function | 🔴 | |
| util | deprecate | function | 🔴 | |
| util | diff | function | 🔴 | |
| util | format | function | 🔴 | |
| util | formatWithOptions | function | 🔴 | |
| util | getCallSite | function | 🔴 | |
| util | getCallSites | function | 🔴 | |
| util | getSystemErrorMap | function | 🔴 | |
| util | getSystemErrorMessage | function | 🔴 | |
| util | getSystemErrorName | function | 🔴 | |
| util | inherits | function | 🔴 | |
| util | inspect | function | 🔴 | |
| util | isArray | function | 🔴 | |
| util | isBoolean | function | 🔴 | |
| util | isBuffer | function | 🔴 | |
| util | isDate | function | 🔴 | |
| util | isDeepStrictEqual | function | 🔴 | |
| util | isError | function | 🔴 | |
| util | isFunction | function | 🔴 | |
| util | isNull | function | 🔴 | |
| util | isNullOrUndefined | function | 🔴 | |
| util | isNumber | function | 🔴 | |
| util | isObject | function | 🔴 | |
| util | isPrimitive | function | 🔴 | |
| util | isRegExp | function | 🔴 | |
| util | isString | function | 🔴 | |
| util | isSymbol | function | 🔴 | |
| util | isUndefined | function | 🔴 | |
| util | log | function | 🔴 | |
| util | parseArgs | function | 🔴 | |
| util | parseEnv | function | 🔴 | |
| util | promisify | function | 🔴 | |
| util | stripVTControlCharacters | function | 🔴 | |
| util | styleText | function | 🔴 | |
| util | toUSVString | function | 🔴 | |
| util | transferableAbortController | function | 🔴 | |
| util | transferableAbortSignal | function | 🔴 | |
| util | types | object | 🔴 | |
| url | URL | function | 🔴 | |
| url | URLSearchParams | function | 🔴 | |
| url | Url | function | 🔴 | |
| url | domainToASCII | function | 🔴 | |
| url | domainToUnicode | function | 🔴 | |
| url | fileURLToPath | function | 🔴 | |
| url | fileURLToPathBuffer | function | 🔴 | |
| url | format | function | 🔴 | |
| url | parse | function | 🔴 | |
| url | pathToFileURL | function | 🔴 | |
| url | resolve | function | 🔴 | |
| url | resolveObject | function | 🔴 | |
| url | urlToHttpOptions | function | 🔴 | |
| net | BlockList | function | 🔴 | |
| net | Server | function | 🟢 | TCP server via uv_tcp_t |
| net | Socket | function | 🟢 | TCP socket via uv_tcp_t |
| net | SocketAddress | function | 🔴 | |
| net | Stream | function | 🔴 | |
| net | _createServerHandle | function | 🔴 | |
| net | _normalizeArgs | function | 🔴 | |
| net | _setSimultaneousAccepts | function | 🔴 | |
| net | connect | function | 🟢 | |
| net | createConnection | function | 🟢 | Alias for net.connect |
| net | createServer | function | 🟢 | |
| net | getDefaultAutoSelectFamily | function | 🔴 | |

| net | getDefaultAutoSelectFamilyAttemptTimeout | function | 🔴 | |
| net | isIP | function | 🟢 | `ts_net_is_ip()` |
| net | isIPv4 | function | 🟢 | `ts_net_is_ipv4()` |
| net | isIPv6 | function | 🟢 | `ts_net_is_ipv6()` |
| net | setDefaultAutoSelectFamily | function | 🔴 | |
| net | setDefaultAutoSelectFamilyAttemptTimeout | function | 🔴 | |
| http | Agent | function | 🔴 | |
| http | ClientRequest | function | 🟢 | Full implementation with events, statusCode, data/end handlers |
| http | CloseEvent | function | 🔴 | |
| http | IncomingMessage | function | 🟢 | |
| http | METHODS | object | 🟢 | `ts_http_get_methods()` |
| http | MessageEvent | function | 🔴 | |
| http | OutgoingMessage | function | 🔴 | |
| http | STATUS_CODES | object | 🟢 | `ts_http_get_status_codes()` |
| http | Server | function | 🟢 | |
| http | ServerResponse | function | 🟢 | |
| http | WebSocket | function | 🔴 | |
| http | _connectionListener | function | 🔴 | |
| http | createServer | function | 🟢 | |
| http | get | function | 🟢 | Working implementation |
| http | globalAgent | object | 🔴 | |
| http | maxHeaderSize | number | 🟢 | `ts_http_get_max_header_size()` |
| http | request | function | 🟢 | Working implementation |
| http | setMaxIdleHTTPParsers | function | 🔴 | |
| http | validateHeaderName | function | 🟢 | `ts_http_validate_header_name()` |
| http | validateHeaderValue | function | 🟢 | `ts_http_validate_header_value()` |
| https | Agent | function | 🔴 | |
| https | Server | function | 🟢 | TLS server with certificate support |
| https | createServer | function | 🟢 | Full implementation with key/cert options |
| https | get | function | 🟢 | |
| https | request | function | 🟢 | |
| buffer | Blob | function | 🔴 | |
| buffer | Buffer | function | 🟡 | `TsBuffer` core implemented (length, indexing, data access) |
| buffer | File | function | 🔴 | |
| buffer | INSPECT_MAX_BYTES | number | 🔴 | |
| buffer | SlowBuffer | function | 🔴 | |
| buffer | atob | function | 🔴 | |
| buffer | btoa | function | 🔴 | |
| buffer | constants | object | 🔴 | |
| buffer | isAscii | function | 🔴 | |
| buffer | isUtf8 | function | 🔴 | |
| buffer | kMaxLength | number | 🔴 | |
| buffer | kStringMaxLength | number | 🔴 | |
| buffer | resolveObjectURL | function | 🔴 | |
| buffer | transcode | function | 🔴 | |
| process | _debugEnd | function | 🔴 | |
| process | _debugProcess | function | 🔴 | |
| process | _events | object | 🔴 | |
| process | _eventsCount | number | 🔴 | |
| process | _exiting | boolean | 🔴 | |
| process | _fatalException | function | 🔴 | |
| process | _getActiveHandles | function | 🔴 | |
| process | _getActiveRequests | function | 🔴 | |
| process | _kill | function | 🔴 | |
| process | _linkedBinding | function | 🔴 | |
| process | _maxListeners | undefined | 🔴 | |
| process | _preload_modules | object | 🔴 | |
| process | _rawDebug | function | 🔴 | |
| process | _startProfilerIdleNotifier | function | 🔴 | |
| process | _stopProfilerIdleNotifier | function | 🔴 | |
| process | _tickCallback | function | 🔴 | |
| process | abort | function | 🟢 | `ts_process_abort()` |
| process | allowedNodeEnvironmentFlags | object | 🔴 | |
| process | arch | string | 🟢 | `ts_process_get_arch()` |
| process | argv | object | 🟢 | `ts_get_process_argv()` |
| process | argv0 | string | 🟢 | `ts_process_get_argv0()` |
| process | assert | function | 🔴 | |
| process | availableMemory | function | 🔴 | |
| process | binding | function | 🔴 | |
| process | chdir | function | 🟢 | `ts_process_chdir()` |
| process | config | object | 🟢 | `ts_process_get_config()` |
| process | constrainedMemory | function | 🔴 | |
| process | cpuUsage | function | 🟢 | `ts_process_cpu_usage()` |
| process | cwd | function | 🟢 | `ts_process_cwd()` |
| process | debugPort | number | 🟢 | `ts_process_get_debug_port()` |
| process | dlopen | function | 🔴 | |
| process | domain | object | 🔴 | |
| process | emitWarning | function | 🟢 | `ts_process_emit_warning()` |
| process | env | object | 🟢 | `ts_get_process_env()` |
| process | execArgv | object | 🟢 | `ts_process_get_exec_argv()` |
| process | execPath | string | 🟢 | `ts_process_get_exec_path()` |
| process | execve | function | 🔴 | |
| process | exit | function | 🟢 | `ts_process_exit()` |
| process | exitCode | number | 🟢 | `ts_process_get/set_exit_code()` |
| process | features | object | 🟢 | `ts_process_get_features()` |
| process | finalization | object | 🔴 | |
| process | getActiveResourcesInfo | function | 🔴 | |
| process | getBuiltinModule | function | 🔴 | |
| process | hasUncaughtExceptionCaptureCallback | function | 🔴 | |
| process | hrtime | function | 🟢 | `ts_process_hrtime()` |
| process | kill | function | 🟢 | `ts_process_kill()` |
| process | loadEnvFile | function | 🔴 | |
| process | mainModule | object | 🔴 | |
| process | memoryUsage | function | 🟢 | `ts_process_memory_usage()` |
| process | moduleLoadList | object | 🔴 | |
| process | nextTick | function | 🟢 | `ts_process_next_tick()` |
| process | openStdin | function | 🔴 | |
| process | pid | number | 🟢 | `ts_process_get_pid()` |
| process | platform | string | 🟢 | `ts_process_get_platform()` |
| process | ppid | number | 🟢 | `ts_process_get_ppid()` |
| process | reallyExit | function | 🔴 | |
| process | ref | function | 🔴 | |
| process | release | object | 🟢 | `ts_process_get_release()` |
| process | report | object | 🔴 | |
| process | resourceUsage | function | 🟢 | `ts_process_resource_usage()` |
| process | setSourceMapsEnabled | function | 🔴 | |
| process | setUncaughtExceptionCaptureCallback | function | 🔴 | |
| process | sourceMapsEnabled | boolean | 🔴 | |
| process | stderr | object | 🟢 | `ts_process_get_stderr()` Writable stream |
| process | stdin | object | 🟢 | `ts_process_get_stdin()` Readable stream |
| process | stdout | object | 🟢 | `ts_process_get_stdout()` Writable stream |
| process | title | string | 🟢 | `ts_process_get_title()` |
| process | umask | function | 🟢 | `ts_process_umask()` |
| process | unref | function | 🔴 | |
| process | uptime | function | 🟢 | `ts_process_uptime()` |
| process | version | string | 🟢 | `ts_process_get_version()` |
| process | versions | object | 🟢 | `ts_process_get_versions()` |
| globals | console | object | 🟢 | `log`, `error`, `warn`, `info`, `time`, `timeEnd`, `trace` |
| globals | setTimeout | function | 🟢 | `ts_set_timeout()` |
| globals | setInterval | function | 🟢 | `ts_set_interval()` |
| globals | clearTimeout | function | 🟢 | `ts_clear_timer()` |
| globals | clearInterval | function | 🟢 | `ts_clear_timer()` |
| globals | setImmediate | function | 🟢 | `ts_set_immediate()` (zero-timeout timer) |
| globals | clearImmediate | function | 🟢 | `ts_clear_timer()` |
| globals | TextEncoder | function | 🔴 | |
| globals | TextDecoder | function | 🔴 | |