#pragma once

#include <cstdint>

extern "C" {
    void* ts_fs_readFileSync(void* path);
    void* ts_fs_readFile_async(void* path);
    void* ts_fs_writeFile_async(void* path, void* content);
    void ts_fs_writeFileSync(void* path, void* content);
    bool ts_fs_existsSync(void* path);
    void ts_fs_mkdirSync(void* path);
    void ts_fs_rmdirSync(void* path);
    void ts_fs_unlinkSync(void* path);
    void* ts_fs_statSync(void* path);
    void* ts_fs_mkdir_async(void* path);
    void* ts_fs_stat_async(void* path);
    int64_t ts_parseInt(void* str);
}
