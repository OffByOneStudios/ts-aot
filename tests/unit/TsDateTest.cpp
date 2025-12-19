#include <catch2/catch_test_macros.hpp>
#include "TsDate.h"
#include "TsString.h"
#include <cstring>

TEST_CASE("TsDate Operations", "[runtime][date]") {
    SECTION("Creation and getTime") {
        TsDate* d = TsDate::Create(1609459200000); // 2021-01-01T00:00:00.000Z
        REQUIRE(d->GetTime() == 1609459200000);
    }

    SECTION("UTC Getters") {
        TsDate* d = TsDate::Create(1609459200000);
        REQUIRE(d->GetUTCFullYear() == 2021);
        REQUIRE(d->GetUTCMonth() == 0); // January
        REQUIRE(d->GetUTCDate() == 1);
        REQUIRE(d->GetUTCHours() == 0);
        REQUIRE(d->GetUTCMinutes() == 0);
        REQUIRE(d->GetUTCSeconds() == 0);
    }

    SECTION("UTC Setters") {
        TsDate* d = TsDate::Create(1609459200000);
        d->SetUTCFullYear(2022);
        REQUIRE(d->GetUTCFullYear() == 2022);
        REQUIRE(d->GetTime() == 1640995200000); // 2022-01-01T00:00:00.000Z
    }

    SECTION("ISO String") {
        TsDate* d = TsDate::Create(1609459200000);
        TsString* iso = d->ToISOString();
        REQUIRE(std::strcmp(iso->ToUtf8(), "2021-01-01T00:00:00.000Z") == 0);
    }

    SECTION("Parsing") {
        TsDate* d = TsDate::Create("2021-01-01T00:00:00.000Z");
        REQUIRE(d->GetTime() == 1609459200000);
    }
}
