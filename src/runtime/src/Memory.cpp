#include "GC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Define GC_THREADS before including gc.h to enable thread support
#define GC_THREADS
#include <gc/gc.h>

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>

// Custom assertion handler to print to stderr instead of GUI popup
int CustomReportHook(int reportType, char* message, int* returnValue) {
    fprintf(stderr, "[ASSERT] %s\n", message);
    fflush(stderr);
    *returnValue = 0; // Don't break
    return TRUE; // We handled it
}
#endif

// Custom abort handler for GC library - intercepts fatal GC errors
static void GC_CALLBACK CustomGCAbortHandler(const char* msg) {
    if (msg) {
        fprintf(stderr, "[GC ABORT] %s\n", msg);
    } else {
        fprintf(stderr, "[GC ABORT] Fatal GC error (no message)\n");
    }
    fflush(stderr);
    // Exit cleanly without showing a popup
    exit(1);
}

// ============================================================================
// Size-class Pool Allocator for Small Objects (closures, TsValues, etc.)
// ============================================================================
// This reduces allocation overhead by:
// 1. Allocating large blocks from system malloc instead of GC
// 2. Using simple bump allocation within blocks
// 3. Avoids "too many heap sections" by not creating GC heap sections

// Block size for pool allocations (256KB per block)
static const size_t POOL_BLOCK_SIZE = 256 * 1024;

// Maximum object size to pool (larger objects go directly to GC)
static const size_t MAX_POOL_SIZE = 512;

// Size classes: 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512
static const size_t SIZE_CLASSES[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512 };
static const size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

// Per-size-class allocation state
struct PoolBlock {
    char* data;         // Block data
    size_t offset;      // Current offset in block
    size_t capacity;    // Total capacity
};

// Thread-local pool blocks for each size class
static thread_local PoolBlock g_pool_blocks[NUM_SIZE_CLASSES] = {};

// Find the size class index for a given size
static inline int find_size_class(size_t size) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return static_cast<int>(i);
        }
    }
    return -1; // Too large for pooling
}

// Internal pool allocation (does not call GC_malloc)
static void* pool_alloc_internal(size_t size) {
    int classIdx = find_size_class(size);
    if (classIdx < 0) {
        return nullptr; // Too large for pooling
    }
    
    size_t allocSize = SIZE_CLASSES[classIdx];
    PoolBlock& block = g_pool_blocks[classIdx];
    
    // Check if we need a new block
    if (block.data == nullptr || block.offset + allocSize > block.capacity) {
        // Use system malloc to avoid GC heap section issues
        block.data = (char*)malloc(POOL_BLOCK_SIZE);
        block.offset = 0;
        block.capacity = POOL_BLOCK_SIZE;
        
        if (!block.data) {
            return nullptr;
        }
    }
    
    // Bump allocate from the block
    void* ptr = block.data + block.offset;
    block.offset += allocSize;
    
    // Zero the memory
    memset(ptr, 0, allocSize);
    
    return ptr;
}

extern "C" {

// Main allocation function - uses pool for small objects
void* ts_alloc(size_t size) {
    // Try pool allocation for small objects
    if (size <= MAX_POOL_SIZE) {
        void* ptr = pool_alloc_internal(size);
        if (ptr) return ptr;
    }
    
    // Fall back to GC for large objects or if pool fails
    return GC_malloc(size);
}

// Explicit pool allocation (same as ts_alloc for small sizes)
void* ts_pool_alloc(size_t size) {
    return ts_alloc(size);
}

void ts_gc_init() {
#ifdef _WIN32
    // Disable Windows error dialogs - send to stderr instead
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    
    // Redirect CRT assertions to stderr
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    
    // Set custom report hook
    _CrtSetReportHook(CustomReportHook);
    
    fprintf(stderr, "[GC] Windows error dialogs disabled, redirecting to stderr\n");
    fflush(stderr);
#endif

    GC_INIT();
    
    // Install custom GC abort handler - intercepts "too many heap sections" and other fatal GC errors
    GC_set_abort_func(CustomGCAbortHandler);
    
    // Disable GC - we use pool allocator for small objects, GC only for large ones
    GC_disable();
    
    GC_set_max_heap_size(2ULL * 1024 * 1024 * 1024);  // 2GB max
    
    fprintf(stderr, "[GC] Pool allocator active for objects <= 512 bytes\n");
    fflush(stderr);
}

void __stack_chk_fail() {
    fprintf(stderr, "Stack smashing detected!\n");
    abort();
}

void ts_cfi_fail() {
    fprintf(stderr, "Control Flow Integrity check failed!\n");
    abort();
}

void ts_panic(const char* msg) {
    fprintf(stderr, "Runtime Panic: %s\n", msg);
    abort();
}

}
