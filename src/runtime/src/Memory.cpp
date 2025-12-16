#include "GC.h"

// Define GC_THREADS before including gc.h to enable thread support
#define GC_THREADS
#include <gc/gc.h>

extern "C" {

void* ts_alloc(size_t size) {
    return GC_malloc(size);
}

void ts_gc_init() {
    GC_INIT();
}

}
