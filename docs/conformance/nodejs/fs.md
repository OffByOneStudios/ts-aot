# Node.js File System (fs)

Parent: [nodejs-features.md](../nodejs-features.md)

---

## Promises API
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

## Synchronous API
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

## Callback API
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

## Streams
| Feature | Status | Notes |
|---------|--------|-------|
| `fs.createReadStream()` | ✅ | Options: start, end, highWaterMark, autoClose; Properties: bytesRead, path, pending |
| `fs.createWriteStream()` | ✅ | Options: start, flags, autoClose; Properties: bytesWritten, path, pending |

## Stats
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
