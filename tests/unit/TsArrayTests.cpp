#include <catch2/catch_test_macros.hpp>
#include "TsArray.h"
#include "GC.h"

TEST_CASE("TsArray Creation", "[runtime][array]") {
    ts_gc_init();
    TsArray* arr = TsArray::Create();
    REQUIRE(arr != nullptr);
    REQUIRE(arr->Length() == 0);
}

TEST_CASE("TsArray Push and Get", "[runtime][array]") {
    ts_gc_init();
    TsArray* arr = TsArray::Create();
    
    arr->Push(10);
    arr->Push(20);
    arr->Push(30);

    REQUIRE(arr->Length() == 3);
    REQUIRE(arr->Get(0) == 10);
    REQUIRE(arr->Get(1) == 20);
    REQUIRE(arr->Get(2) == 30);
}

TEST_CASE("TsArray Set", "[runtime][array]") {
    ts_gc_init();
    TsArray* arr = TsArray::Create();
    
    arr->Push(10);
    arr->Set(0, 99);
    
    REQUIRE(arr->Get(0) == 99);
}

TEST_CASE("TsArray Auto-Growth", "[runtime][array]") {
    ts_gc_init();
    TsArray* arr = TsArray::Create(2); // Small capacity
    
    arr->Push(1);
    arr->Push(2);
    arr->Push(3); // Should trigger resize
    
    REQUIRE(arr->Length() == 3);
    REQUIRE(arr->Get(2) == 3);
}

TEST_CASE("TsArray Out of Bounds", "[runtime][array]") {
    ts_gc_init();
    TsArray* arr = TsArray::Create();
    
    // Should return 0 and print error (we can't easily check stderr here, but ensure it doesn't crash)
    REQUIRE(arr->Get(100) == 0);
}
