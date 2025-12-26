#pragma once

#include <cstdint>

extern "C" {
    void* ts_fs_readFileSync(void* path);
    void* ts_fs_readFile_async(void* path);
    void* ts_fs_writeFile_async(void* path, void* content);
    void ts_fs_writeFileSync(void* path, void* content);
    bool ts_fs_existsSync(void* path);
    void* ts_fs_statSync(void* path);
    void ts_fs_accessSync(void* path, int32_t mode);
    void ts_fs_chmodSync(void* path, int32_t mode);
    void ts_fs_chownSync(void* path, int32_t uid, int32_t gid);
    void ts_fs_utimesSync(void* path, double atime, double mtime);
    void* ts_fs_statfsSync(void* path);
    void* ts_fs_get_constants();
    void* ts_fs_get_promises();
    int64_t ts_fs_openSync(void* path, void* flags);
    void ts_fs_closeSync(int64_t fd);
    int64_t ts_fs_readSync(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
    int64_t ts_fs_writeSync(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
    void* ts_fs_open_async(void* path, void* flags);
    void* ts_fs_close_async(int64_t fd);
    void* ts_fs_read_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
    void* ts_fs_write_async(int64_t fd, void* buffer, int64_t offset, int64_t length, int64_t position);
    void* ts_fs_readdirSync(void* path, void* options);
    void* ts_fs_readdir_async(void* path, void* options);
    void* ts_fs_opendirSync(void* path, void* options);
    void* ts_fs_opendir_async(void* path, void* options);
    void* ts_path_join(void* path1, void* path2);
    void* ts_fs_createReadStream(void* path);
    void ts_event_emitter_on(void* emitter, void* event, void* callback);
    int64_t ts_parseInt(void* str);
}
