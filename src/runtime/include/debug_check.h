#pragma once
#include <cstdio>
#include <cstdint>

// Debug macro to check for magic numbers being used as pointers
#define DEBUG_CHECK_PTR(ptr, name) \
    do { \
        if (ptr && ((uintptr_t)(ptr) == 0x46554E43 || (uintptr_t)(ptr) == 0x4D415053 || (uintptr_t)(ptr) == 0x53545247)) { \
            printf("[DEBUG] %s: RECEIVED MAGIC NUMBER AS POINTER: %p\n", name, ptr); \
            printf("[DEBUG] Stack trace would be helpful here!\n"); \
        } \
    } while(0)
