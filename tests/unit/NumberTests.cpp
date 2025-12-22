#include <catch2/catch_test_macros.hpp>
#include "TsString.h"
#include "TsRuntime.h"
#include <cstring>
#include <cmath>

extern "C" {
    void* ts_int_to_string(int64_t value, int64_t radix);
    void* ts_double_to_string(double value, int64_t radix);
    void* ts_double_to_fixed(double value, int64_t fractionDigits);
}

TEST_CASE("Number to String Conversions", "[runtime][number]") {
    SECTION("Integer toString") {
        TsString* s1 = (TsString*)ts_int_to_string(255, 16);
        REQUIRE(std::strcmp(s1->ToUtf8(), "ff") == 0);

        TsString* s2 = (TsString*)ts_int_to_string(255, 10);
        REQUIRE(std::strcmp(s2->ToUtf8(), "255") == 0);

        TsString* s3 = (TsString*)ts_int_to_string(255, 2);
        REQUIRE(std::strcmp(s3->ToUtf8(), "11111111") == 0);

        TsString* s4 = (TsString*)ts_int_to_string(-255, 16);
        REQUIRE(std::strcmp(s4->ToUtf8(), "-ff") == 0);
    }

    SECTION("Double toString") {
        TsString* s1 = (TsString*)ts_double_to_string(1.23, 10);
        REQUIRE(std::strcmp(s1->ToUtf8(), "1.23") == 0);

        TsString* s2 = (TsString*)ts_double_to_string(255.0, 16);
        REQUIRE(std::strcmp(s2->ToUtf8(), "ff") == 0);

        TsString* s3 = (TsString*)ts_double_to_string(NAN, 10);
        REQUIRE(std::strcmp(s3->ToUtf8(), "NaN") == 0);

        TsString* s4 = (TsString*)ts_double_to_string(INFINITY, 10);
        REQUIRE(std::strcmp(s4->ToUtf8(), "Infinity") == 0);
    }

    SECTION("Double toFixed") {
        TsString* s1 = (TsString*)ts_double_to_fixed(1.23456, 2);
        REQUIRE(std::strcmp(s1->ToUtf8(), "1.23") == 0);

        TsString* s2 = (TsString*)ts_double_to_fixed(1.23456, 0);
        REQUIRE(std::strcmp(s2->ToUtf8(), "1") == 0);

        TsString* s3 = (TsString*)ts_double_to_fixed(1.23456, 4);
        REQUIRE(std::strcmp(s3->ToUtf8(), "1.2346") == 0);
    }
}
