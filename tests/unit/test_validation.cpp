#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

#include "validation.hpp"

TEST_CASE("RRS-SR-003-01: presence rejects empty and whitespace-only values", "[unit][M2]") {
    CHECK(rrs::validate_presence("name", "A"));
    CHECK(rrs::validate_presence("name", " A "));
    const auto empty = rrs::validate_presence("name", "");
    CHECK_FALSE(empty);
    CHECK(empty.field == "name");
    CHECK_FALSE(rrs::validate_presence("name", " \t\n"));
}

TEST_CASE("RRS-SR-003-02: length accepts the inclusive range only", "[unit][M2]") {
    // SRS name length rule: 1-60, boundary values 0, 1, 60, 61.
    CHECK_FALSE(rrs::validate_length("name", "", 1, 60));
    CHECK(rrs::validate_length("name", "a", 1, 60));
    CHECK(rrs::validate_length("name", std::string(60, 'a'), 1, 60));
    const auto too_long = rrs::validate_length("name", std::string(61, 'a'), 1, 60);
    CHECK_FALSE(too_long);
    CHECK(too_long.field == "name");
    CHECK_FALSE(rrs::validate_length("name", "a", 5, 1));
}

TEST_CASE("RRS-SR-003-03: integer type accepts whole base-10 numbers only", "[unit][M2]") {
    CHECK(rrs::validate_integer_type("age", "42"));
    CHECK(rrs::validate_integer_type("age", "-7"));
    CHECK_FALSE(rrs::validate_integer_type("age", ""));
    CHECK_FALSE(rrs::validate_integer_type("age", "4.2"));
    CHECK_FALSE(rrs::validate_integer_type("age", "42 "));
    CHECK_FALSE(rrs::validate_integer_type("age", "abc"));
    CHECK_FALSE(rrs::validate_integer_type("age", "99999999999999999999"));
}

TEST_CASE("RRS-SR-003-04: range accepts the inclusive bounds only", "[unit][M2]") {
    // SRS age rule: 1-120, boundary values 0, 1, 120, 121.
    CHECK_FALSE(rrs::validate_range("age", 0, 1, 120));
    CHECK(rrs::validate_range("age", 1, 1, 120));
    CHECK(rrs::validate_range("age", 120, 1, 120));
    CHECK_FALSE(rrs::validate_range("age", 121, 1, 120));
    CHECK_FALSE(rrs::validate_range("age", 5, 10, 1));
    CHECK(rrs::validate_range("x", std::numeric_limits<std::int64_t>::min(),
                              std::numeric_limits<std::int64_t>::min(), 0));
}

TEST_CASE("RRS-SR-003-05: integer combines presence, type and range", "[unit][M2]") {
    // SRS passenger count rule: 1-6, boundary values 0, 1, 6, 7.
    CHECK_FALSE(rrs::validate_integer("passengers", "0", 1, 6));
    CHECK(rrs::validate_integer("passengers", "1", 1, 6));
    CHECK(rrs::validate_integer("passengers", "6", 1, 6));
    CHECK_FALSE(rrs::validate_integer("passengers", "7", 1, 6));
    CHECK_FALSE(rrs::validate_integer("passengers", "  ", 1, 6));
    const auto not_number = rrs::validate_integer("passengers", "six", 1, 6);
    CHECK_FALSE(not_number);
    CHECK(not_number.field == "passengers");
}
