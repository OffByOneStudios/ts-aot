#ifndef TS_FS_EXT_H
#define TS_FS_EXT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Synchronous File Operations
// =============================================================================

void* ts_fs_readFileSync(void* path);
void ts_fs_writeFileSync(void* path, void* content);
void ts_fs_appendFileSync(void* path, void* content);
void ts_fs_unlinkSync(void* path);
void ts_fs_mkdirSync(void* path, void* options);
void ts_fs_rmdirSync(void* path, void* options);
void ts_fs_rmSync(void* path, void* options);
void ts_fs_renameSync(void* oldPath, void* newPath);
void ts_fs_copyFileSync(void* src, void* dest, int32_t flags);
void ts_fs_cpSync(void* src, void* dest, void* options);
void ts_fs_truncateSync(void* path, int64_t len);
void* ts_fs_mkdtempSync(void* prefix);
bool ts_fs_existsSync(void* path);
void* ts_fs_statSync(void* path);
void* ts_fs_lstatSync(void* path);
void ts_fs_linkSync(void* existingPath, void* newPath);
void ts_fs_symlinkSync(void* target, void* path, void* type);
void* ts_fs_readlinkSync(void* path);
void* ts_fs_realpathSync(void* path);
void* ts_fs_readdirSync(void* path, void* options);
void* ts_fs_opendirSync(void* path, void* options);
void ts_fs_accessSync(void* path, int32_t mode);
void ts_fs_chmodSync(void* path, int32_t mode);
void ts_fs_chownSync(void* path, int32_t uid, int32_t gid);
void ts_fs_utimesSync(void* path, double atime, double mtime);
void ts_fs_lchmodSync(void* path, int32_t mode);
void ts_fs_lchownSync(void* path, int32_t uid, int32_t gid);
void ts_fs_lutimesSync(void* path, double atime, double mtime);
void* ts_fs_statfsSync(void* path);

// =============================================================================
// Synchronous File Descriptor Operations
// =============================================================================

double ts_fs_openSync(void* path_val, void* flags_val, double mode);
void ts_fs_closeSync(double fd);
double ts_fs_readSync(double fd, void* buffer_val, double offset, double length, double position);
double ts_fs_writeSync(double fd, void* buffer_val, double offset, double length, double position);
void ts_fs_fchmodSync(double fd, double mode);
void ts_fs_fchownSync(double fd, double uid, double gid);
void ts_fs_futimesSync(double fd, double atime, double mtime);
void* ts_fs_fstatSync(double fd);
void ts_fs_fsyncSync(double fd);
void ts_fs_fdatasyncSync(double fd);
void ts_fs_ftruncateSync(double fd, double len);
double ts_fs_readvSync(double fd, void* buffers_val, double position);
double ts_fs_writevSync(double fd, void* buffers_val, double position);

// =============================================================================
// Callback-based Async Operations
// =============================================================================

void ts_fs_exists(void* path, void* callback);
void ts_fs_open(void* path_val, void* flags_val, double mode, void* callback);
void ts_fs_close(double fd, void* callback);
void ts_fs_read(double fd, void* buffer_val, double offset, double length, double position, void* callback);
void ts_fs_write(double fd, void* buffer_val, double offset, double length, double position, void* callback);
void ts_fs_fchmod(double fd, double mode, void* callback);
void ts_fs_fchown(double fd, double uid, double gid, void* callback);
void ts_fs_futimes(double fd, double atime, double mtime, void* callback);
void ts_fs_fstat(double fd, void* callback);
void ts_fs_fsync(double fd, void* callback);
void ts_fs_fdatasync(double fd, void* callback);
void ts_fs_ftruncate(double fd, double len, void* callback);
void ts_fs_readv(double fd, void* buffers_val, double position, void* callback);
void ts_fs_writev(double fd, void* buffers_val, double position, void* callback);

// =============================================================================
// Promise-based Async Operations (fs.promises)
// =============================================================================

void* ts_fs_get_promises();
void* ts_fs_readFile_async(void* path);
void* ts_fs_writeFile_async(void* path, void* content);
void* ts_fs_appendFile_async(void* path, void* content);
void* ts_fs_unlink_async(void* path);
void* ts_fs_mkdir_async(void* path, void* options);
void* ts_fs_rmdir_async(void* path, void* options);
void* ts_fs_rm_async(void* path, void* options);
void* ts_fs_rename_async(void* oldPath, void* newPath);
void* ts_fs_copyFile_async(void* src, void* dest, double flags);
void* ts_fs_cp_async(void* src, void* dest, void* options);
void* ts_fs_truncate_async(void* path, double len);
void* ts_fs_mkdtemp_async(void* prefix);
void* ts_fs_stat_async(void* path);
void* ts_fs_lstat_async(void* path);
void* ts_fs_link_async(void* existingPath, void* newPath);
void* ts_fs_symlink_async(void* target, void* path, void* type);
void* ts_fs_readlink_async(void* path);
void* ts_fs_realpath_async(void* path);
void* ts_fs_readdir_async(void* path, void* options);
void* ts_fs_opendir_async(void* path, void* options);
void* ts_fs_access_async(void* path, double mode);
void* ts_fs_chmod_async(void* path, double mode);
void* ts_fs_chown_async(void* path, double uid, double gid);
void* ts_fs_utimes_async(void* path, double atime, double mtime);
void* ts_fs_lchmod_async(void* path, double mode);
void* ts_fs_lchown_async(void* path, double uid, double gid);
void* ts_fs_lutimes_async(void* path, double atime, double mtime);
void* ts_fs_statfs_async(void* path);

// =============================================================================
// Promise-based File Descriptor Operations
// =============================================================================

void* ts_fs_open_async(void* path_val, void* flags_val, double mode);
void* ts_fs_close_async(double fd);
void* ts_fs_read_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
void* ts_fs_write_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
void* ts_fs_fchmod_async(double fd, double mode);
void* ts_fs_fchown_async(double fd, double uid, double gid);
void* ts_fs_futimes_async(double fd, double atime, double mtime);
void* ts_fs_fstat_async(double fd);
void* ts_fs_fsync_async(double fd);
void* ts_fs_fdatasync_async(double fd);
void* ts_fs_ftruncate_async(double fd, double len);
void* ts_fs_readv_async(int64_t fd, void* buffers_val, double position);
void* ts_fs_writev_async(int64_t fd, void* buffers_val, double position);

// =============================================================================
// FileHandle Methods (fs.promises.open returns FileHandle)
// =============================================================================

double ts_fs_filehandle_get_fd(void* handle);
void* ts_fs_filehandle_close_async(void* handle);
void* ts_fs_filehandle_stat_async(void* handle);
void* ts_fs_filehandle_chmod_async(void* handle, double mode);
void* ts_fs_filehandle_chown_async(void* handle, double uid, double gid);
void* ts_fs_filehandle_sync_async(void* handle);
void* ts_fs_filehandle_datasync_async(void* handle);
void* ts_fs_filehandle_truncate_async(void* handle, double len);
void* ts_fs_filehandle_utimes_async(void* handle, double atime, double mtime);
void* ts_fs_filehandle_read_async(void* handle, void* buffer, double offset, double length, double position);
void* ts_fs_filehandle_write_async(void* handle, void* buffer, double offset, double length, double position);
void* ts_fs_filehandle_readv_async(void* handle, void* buffers, double position);
void* ts_fs_filehandle_writev_async(void* handle, void* buffers, double position);

// FileHandle native function wrappers (used by vtable)
void* ts_fs_filehandle_close(void* context, int argc, void** argv);
void* ts_fs_filehandle_stat(void* context, int argc, void** argv);
void* ts_fs_filehandle_chmod(void* context, int argc, void** argv);
void* ts_fs_filehandle_chown(void* context, int argc, void** argv);
void* ts_fs_filehandle_sync(void* context, int argc, void** argv);
void* ts_fs_filehandle_datasync(void* context, int argc, void** argv);
void* ts_fs_filehandle_truncate(void* context, int argc, void** argv);
void* ts_fs_filehandle_utimes(void* context, int argc, void** argv);
void* ts_fs_filehandle_read(void* context, int argc, void** argv);
void* ts_fs_filehandle_write(void* context, int argc, void** argv);
void* ts_fs_filehandle_readv(void* context, int argc, void** argv);
void* ts_fs_filehandle_writev(void* context, int argc, void** argv);

// =============================================================================
// Watch Operations
// =============================================================================

void* ts_fs_watch(void* path_val, void* options_val, void* listener_val);
void ts_fs_watchFile(void* path_val, void* options_val, void* listener_val);
void ts_fs_unwatchFile(void* path_val, void* listener_val);
void* ts_fs_promises_watch(void* filename, void* options);

// Watcher native function wrappers (used by vtable)
void* ts_fs_watcher_on(void* context, int argc, void** argv);
void* ts_fs_watcher_close(void* context, int argc, void** argv);

// =============================================================================
// Utility Functions
// =============================================================================

void* ts_fs_get_constants();

#ifdef __cplusplus
}
#endif

#endif // TS_FS_EXT_H
