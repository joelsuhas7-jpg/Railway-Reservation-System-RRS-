#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "passenger.hpp"

namespace {

rrs::Passenger valid_passenger(
    std::string name = "Joel",
    int age = 21,
    rrs::Gender gender = rrs::Gender::Male) {
    return {std::move(name), age, gender};
}

}  // namespace

TEST_CASE("TC-BOOK-01-01: one valid passenger is accepted", "[unit][M10]") {
    const std::vector<rrs::Passenger> passengers = {valid_passenger()};

    const auto result = rrs::validate_passengers(passengers);

    CHECK(result.valid);
}

TEST_CASE("TC-BOOK-01-02: six valid passengers are accepted", "[unit][M10]") {
    std::vector<rrs::Passenger> passengers(6, valid_passenger());

    const auto result = rrs::validate_passengers(passengers);

    CHECK(result.valid);
}

TEST_CASE("TC-BOOK-01-03: seven passengers are rejected", "[unit][M10]") {
    std::vector<rrs::Passenger> passengers(7, valid_passenger());

    const auto result = rrs::validate_passengers(passengers);

    CHECK_FALSE(result.valid);
    CHECK(result.field == "passengers");
}

TEST_CASE("TC-BOOK-01-04: empty passenger list is rejected", "[unit][M10]") {
    const std::vector<rrs::Passenger> passengers;

    const auto result = rrs::validate_passengers(passengers);

    CHECK_FALSE(result.valid);
    CHECK(result.field == "passengers");
}

TEST_CASE("TC-BOOK-01-05: passenger name must contain 1 to 60 characters",
          "[unit][M10]") {
    CHECK(rrs::validate_passenger(valid_passenger("A")).valid);
    CHECK(rrs::validate_passenger(valid_passenger(std::string(60, 'A'))).valid);

    const auto empty = rrs::validate_passenger(valid_passenger(""));
    CHECK_FALSE(empty.valid);
    CHECK(empty.field == "name");

    const auto too_long =
        rrs::validate_passenger(valid_passenger(std::string(61, 'A')));
    CHECK_FALSE(too_long.valid);
    CHECK(too_long.field == "name");
}

TEST_CASE("TC-BOOK-01-06: passenger age must be between 1 and 120",
          "[unit][M10]") {
    CHECK(rrs::validate_passenger(valid_passenger("Joel", 1)).valid);
    CHECK(rrs::validate_passenger(valid_passenger("Joel", 120)).valid);

    const auto zero = rrs::validate_passenger(valid_passenger("Joel", 0));
    CHECK_FALSE(zero.valid);
    CHECK(zero.field == "age");

    const auto above_limit =
        rrs::validate_passenger(valid_passenger("Joel", 121));
    CHECK_FALSE(above_limit.valid);
    CHECK(above_limit.field == "age");
}

TEST_CASE("TC-BOOK-01-07: supported gender values are accepted",
          "[unit][M10]") {
    CHECK(rrs::validate_passenger(
              valid_passenger("A", 20, rrs::Gender::Male)).valid);
    CHECK(rrs::validate_passenger(
              valid_passenger("B", 20, rrs::Gender::Female)).valid);
    CHECK(rrs::validate_passenger(
              valid_passenger("C", 20, rrs::Gender::Other)).valid);
}

TEST_CASE("TC-BOOK-01-08: invalid gender value is rejected",
          "[unit][M10]") {
    const auto invalid = static_cast<rrs::Gender>(99);
    const auto result =
        rrs::validate_passenger(valid_passenger("Joel", 21, invalid));

    CHECK_FALSE(result.valid);
    CHECK(result.field == "gender");
}

TEST_CASE("TC-BOOK-01-09: invalid passenger identifies its position",
          "[unit][M10]") {
    std::vector<rrs::Passenger> passengers = {
        valid_passenger("First"),
        valid_passenger("", 20)
    };

    const auto result = rrs::validate_passengers(passengers);

    CHECK_FALSE(result.valid);
    CHECK(result.field == "name (passenger 2)");
}
