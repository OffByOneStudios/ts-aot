#include <catch2/catch_test_macros.hpp>
#include "GC.h"
#include "TsRuntime.h"
#include "TsObject.h"
#include "TsString.h"
#include <uv.h>
#include <cstring>

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

TEST_CASE("Type System", "[runtime][types]") {
    SECTION("TaggedValue Size") {
        // TaggedValue should be reasonably small (type + union)
        // 1 byte type + 8 byte union + padding = 16 bytes usually
        REQUIRE(sizeof(TaggedValue) <= 16);
    }

    SECTION("TsString Creation") {
        TsString* str = TsString::Create("Hello World");
        REQUIRE(str != nullptr);
        
        const char* utf8 = str->ToUtf8();
        REQUIRE(std::strcmp(utf8, "Hello World") == 0);
    }
    
    SECTION("TsString UTF-8 Conversion") {
        // Test with some non-ASCII chars if possible, but keep it simple for now
        TsString* str = TsString::Create("Test");
        REQUIRE(std::strcmp(str->ToUtf8(), "Test") == 0);
    }
}

TEST_CASE("Event Loop", "[runtime][loop]") {
    ts_loop_init();

    SECTION("Run Loop") {
        // We need to add a handle to the loop so it doesn't exit immediately with nothing to do,
        // or verify that it runs and returns if empty.
        // uv_run returns 0 if there are no more active handles.
        
        // Let's add a simple timer that fires once to ensure the loop processes events.
        uv_loop_t* loop = uv_default_loop();
        uv_timer_t timer;
        uv_timer_init(loop, &timer);
        
        bool timer_fired = false;
        timer.data = &timer_fired;
        
        uv_timer_start(&timer, [](uv_timer_t* handle) {
            bool* fired = static_cast<bool*>(handle->data);
            *fired = true;
            uv_close((uv_handle_t*)handle, nullptr);
        }, 10, 0);

        ts_loop_run();
        
        REQUIRE(timer_fired == true);
    }
}
