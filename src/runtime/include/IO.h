#pragma once

#include <cstdint>

extern "C" {
    void* ts_fs_readFileSync(void* path);
    int64_t ts_parseInt(void* str);
}
