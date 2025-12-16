#pragma once

#include <cstddef>

extern "C" {
    // Allocates memory using the Garbage Collector.
    // The memory is automatically cleared (zeroed).
    void* ts_alloc(size_t size);

    // Initializes the Garbage Collector.
    // Must be called before any allocation.
    void ts_gc_init();
}
