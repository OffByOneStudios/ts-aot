#pragma once

#include <cstdint>

extern "C" {
    void* ts_fs_readFileSync(void* path);
    void* ts_fs_readFile_async(void* path);
    void* ts_fs_writeFile_async(void* path, void* content);
    void* ts_fetch(void* url);
    void ts_fs_writeFileSync(void* path, void* content);
    bool ts_fs_existsSync(void* path);
    int64_t ts_parseInt(void* str);
}
