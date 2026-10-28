#include <catch2/catch_test_macros.hpp>

#include "app_info.hpp"

TEST_CASE("SMOKE-01: application name matches the SRS title", "[unit][core]") {
    REQUIRE(rrs::app_name() == "Railway Reservation System");
}

TEST_CASE("SMOKE-02: application version is non-empty and dotted", "[unit][core]") {
    const auto version = rrs::app_version();
    REQUIRE_FALSE(version.empty());
    REQUIRE(version.find('.') != std::string_view::npos);
}
