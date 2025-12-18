#pragma once

#include <cstdint>

extern "C" {
    void* ts_fs_readFileSync(void* path);
    void* ts_fs_readFile_async(void* path);
    void* ts_fetch(void* url);
    void ts_fs_writeFileSync(void* path, void* content);
    int64_t ts_parseInt(void* str);
}
