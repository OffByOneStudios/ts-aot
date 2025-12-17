#include <catch2/catch_test_macros.hpp>
#include "TsString.h"
#include <cstring>

TEST_CASE("TsString Operations", "[runtime][string]") {
    SECTION("Creation and UTF-8 Conversion") {
        TsString* str = TsString::Create("Hello World");
        REQUIRE(str != nullptr);
        
        const char* utf8 = str->ToUtf8();
        REQUIRE(std::strcmp(utf8, "Hello World") == 0);
    }

    SECTION("Concatenation") {
        TsString* s1 = TsString::Create("Hello");
        TsString* s2 = TsString::Create(" World");
        TsString* result = TsString::Concat(s1, s2);
        
        REQUIRE(std::strcmp(result->ToUtf8(), "Hello World") == 0);
    }

    SECTION("Empty String") {
        TsString* empty = TsString::Create("");
        REQUIRE(std::strcmp(empty->ToUtf8(), "") == 0);
        
        TsString* s = TsString::Create("Test");
        TsString* res1 = TsString::Concat(empty, s);
        REQUIRE(std::strcmp(res1->ToUtf8(), "Test") == 0);
        
        TsString* res2 = TsString::Concat(s, empty);
        REQUIRE(std::strcmp(res2->ToUtf8(), "Test") == 0);
    }
}
