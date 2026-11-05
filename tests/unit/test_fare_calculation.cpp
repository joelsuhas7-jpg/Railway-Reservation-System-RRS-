#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "fare_calculation.hpp"

TEST_CASE("TC-FARE-02: the total is the configured fare times the passenger count",
          "[unit][M11]") {
    CHECK(rrs::total_fare_paise(45000, 4) == std::int64_t{180000});
    CHECK(rrs::total_fare_paise(45000, 1) == std::int64_t{45000});
    CHECK(rrs::total_fare_paise(1, 6) == std::int64_t{6});
}

TEST_CASE("TC-FARE-02: no total exists without a positive fare or for an overflowing product",
          "[unit][M11]") {
    CHECK_FALSE(rrs::total_fare_paise(0, 4));
    CHECK_FALSE(rrs::total_fare_paise(-45000, 4));
    CHECK_FALSE(rrs::total_fare_paise(45000, 0));
    CHECK_FALSE(rrs::total_fare_paise(std::numeric_limits<std::int64_t>::max() / 2 + 1, 2));
}
