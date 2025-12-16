#include <catch2/catch_test_macros.hpp>
#include "GC.h"

TEST_CASE("Memory Management", "[runtime][gc]") {
    ts_gc_init();

    SECTION("Allocation") {
        void* ptr = ts_alloc(1024);
        REQUIRE(ptr != nullptr);
        
        // Write to memory to ensure it's valid
        int* int_ptr = static_cast<int*>(ptr);
        *int_ptr = 42;
        REQUIRE(*int_ptr == 42);
    }
}
