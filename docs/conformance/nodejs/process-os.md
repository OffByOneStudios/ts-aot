# Node.js Process, OS, Child Process, Cluster

Parent: [nodejs-features.md](../nodejs-features.md)

---

## Process

### Properties
| Feature | Status | Notes |
|---------|--------|-------|
| `process.arch` | ✅ | |
| `process.argv` | ✅ | |
| `process.argv0` | ✅ | |
| `process.channel` | ✅ | IPC channel for cluster workers |
| `process.config` | ✅ | |
| `process.connected` | ✅ | IPC connection status |
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
| `process.disconnect()` | ✅ | IPC disconnect |
| `process.dlopen()` | ✅ | Stub (prints error - AOT incompatible) |
| `process.emitWarning()` | ✅ | |
| `process.exit()` | ✅ | |
| `process.getActiveResourcesInfo()` | ✅ | |
| `process.getegid()` | ✅ | Unix only, returns -1 on Windows |
| `process.geteuid()` | ✅ | Unix only, returns -1 on Windows |
| `process.getgid()` | ✅ | Unix only, returns -1 on Windows |
| `process.getgroups()` | ✅ | Unix only, returns [] on Windows |
| `process.getuid()` | ✅ | Unix only, returns -1 on Windows |
| `process.hasUncaughtExceptionCaptureCallback()` | ✅ | |
| `process.hrtime()` | ✅ | |
| `process.hrtime.bigint()` | ✅ | |
| `process.initgroups()` | ✅ | Unix only, no-op on Windows |
| `process.kill()` | ✅ | |
| `process.memoryUsage()` | ✅ | |
| `process.memoryUsage.rss()` | ✅ | |
| `process.nextTick()` | ✅ | |
| `process.resourceUsage()` | ✅ | |
| `process.send()` | ✅ | IPC messaging |
| `process.setegid()` | ✅ | Unix only, no-op on Windows |
| `process.seteuid()` | ✅ | Unix only, no-op on Windows |
| `process.setgid()` | ✅ | Unix only, no-op on Windows |
| `process.setgroups()` | ✅ | Unix only, no-op on Windows |
| `process.setuid()` | ✅ | Unix only, no-op on Windows |
| `process.setSourceMapsEnabled()` | ✅ | Stub (no-op - AOT incompatible) |
| `process.setUncaughtExceptionCaptureCallback()` | ✅ | |
| `process.umask()` | ✅ | |
| `process.uptime()` | ✅ | |

**Process Coverage: 55/55 (100%)**

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
