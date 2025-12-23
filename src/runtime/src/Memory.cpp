#include "GC.h"
#include <cstdio>
#include <cstdlib>

// Define GC_THREADS before including gc.h to enable thread support
#define GC_THREADS
#include <gc/gc.h>

extern "C" {

void* ts_alloc(size_t size) {
    void* ptr = GC_malloc(size);
    return ptr;
}

void ts_gc_init() {
    GC_INIT();
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
