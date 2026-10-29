#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string_view>

#include "fixtures.hpp"

using rrs::AccountFixture;
using rrs::CalculationKnownAnswers;
using rrs::FareFixture;
using rrs::InventoryFixture;
using rrs::SeedFixtureData;
using rrs::TrainFixture;
using rrs::TravelClassFixture;

TEST_CASE("TC-SEARCH-01: seed fixture provides train 12345, route and classes for search enquiry",
          "[unit][M1]") {
    const auto& train = rrs::known_train_12345();
    REQUIRE(train.train_number == 12345);
    REQUIRE(train.source_station == "SBC");
    REQUIRE(train.destination_station == "NDLS");
    REQUIRE(train.classes.size() == 2);

    auto sl = rrs::find_class(train, "SL");
    auto three_a = rrs::find_class(train, "3A");
    REQUIRE(sl.has_value());
    REQUIRE(sl->capacity == 72);
    REQUIRE(three_a.has_value());
    REQUIRE(three_a->capacity == 64);
}

TEST_CASE("ENB-05-01: deterministic seed loads complete known-answer dataset",
          "[unit][M1]") {
    const auto seed1 = rrs::load_default_seed();
    const auto seed2 = rrs::load_seed(0);
    const auto seed3 = rrs::load_seed(42);

    REQUIRE(rrs::validate_seed(seed1));
    REQUIRE(rrs::validate_seed(seed2));
    REQUIRE(rrs::validate_seed(seed3));

    // Deterministic loading: multiple loads produce identical figures
    REQUIRE(seed1.train.train_number == seed2.train.train_number);
    REQUIRE(seed1.fare.fare_paise == seed2.fare.fare_paise);
    REQUIRE(seed1.inventory.initial_free_count == seed3.inventory.initial_free_count);
    REQUIRE(seed1.admin.username == seed2.admin.username);
    REQUIRE(seed1.passengers.size() == 4);
}

TEST_CASE("ENB-05-02: known-answer train 12345, travel classes and capacities match SRS",
          "[unit][M1]") {
    const auto& train = rrs::known_train_12345();
    CHECK(train.train_number == 12345);
    CHECK_FALSE(train.name.empty());
    CHECK_FALSE(train.departure_time.empty());
    CHECK_FALSE(train.arrival_time.empty());

    auto sl = rrs::find_class(train, "SL");
    auto three_a = rrs::find_class(train, "3A");
    REQUIRE(sl.has_value());
    CHECK(sl->capacity == 72);
    REQUIRE(three_a.has_value());
    CHECK(three_a->capacity == 64);

    // Initial seat inventory baseline (SRS s4.2, RRS-F-009, TC-SEAT-01)
    const auto& inv = rrs::known_inventory_12345_sl();
    CHECK(inv.train_number == 12345);
    CHECK(inv.class_code == "SL");
    CHECK(inv.journey_date == "2026-10-01");
    CHECK(inv.initial_free_count == 72);
    CHECK(inv.admin_block_count == 12);
    CHECK(inv.free_after_block == 60);
}

TEST_CASE("ENB-05-03: known-answer fare 45000 paise and derived figures match SRS figures",
          "[unit][M1]") {
    const auto& fare = rrs::known_fare_12345_sl();
    CHECK(fare.train_number == 12345);
    CHECK(fare.class_code == "SL");
    CHECK(fare.fare_paise == 45000);  // 45000 paise = ₹450.00
    CHECK(fare.effective_from == "2026-10-01");

    const auto& calcs = rrs::known_calculation_answers();
    CHECK(calcs.passenger_count == 4);
    CHECK(calcs.total_fare_paise == 180000);  // 4 * 45000
    CHECK(calcs.s2_refund_paise == 90000);    // 50% slab S2 refund
    CHECK(calcs.s2_released_seats == 4);
    CHECK(calcs.admin_block_seats == 12);
    CHECK(calcs.sl_free_seats_after_block == 60);

    // Derived computation matches SRS s4.4 RRS-F-014 and TC-FARE-02
    CHECK(rrs::compute_displayed_fare(fare.fare_paise, 1) == 45000);
    CHECK(rrs::compute_displayed_fare(fare.fare_paise, 4) == 180000);
    CHECK(rrs::compute_displayed_fare(fare.fare_paise, 6) == 270000);
}

TEST_CASE("ENB-05-04: admin and 4 passenger fixtures satisfy account and security policy",
          "[unit][M1]") {
    const auto& admin = rrs::known_admin_account();
    CHECK(admin.username == "admin");
    CHECK(admin.role == "ADMIN");
    CHECK(admin.password.size() >= 8);
    CHECK_FALSE(admin.is_lockout_test_account);

    const auto& passengers = rrs::known_passenger_accounts();
    REQUIRE(passengers.size() == 4);

    std::size_t lockout_count = 0;
    for (const auto& p : passengers) {
        CHECK(p.role == "PASSENGER");
        CHECK(p.password.size() >= 8);
        CHECK_FALSE(p.username.empty());
        if (p.is_lockout_test_account) {
            lockout_count++;
        }
    }
    // Test Plan s10: exactly one passenger dedicated for lockout testing
    CHECK(lockout_count == 1);

    const auto& lockout_acct = rrs::known_lockout_passenger_account();
    CHECK(lockout_acct.is_lockout_test_account);
    CHECK(lockout_acct.role == "PASSENGER");
}

TEST_CASE("ENB-05-05: corrupted seed data fails validation",
          "[unit][M1]") {
    auto seed = rrs::load_default_seed();
    REQUIRE(rrs::validate_seed(seed));

    SECTION("bad train number is rejected") {
        seed.train.train_number = 99999;
        CHECK_FALSE(rrs::validate_seed(seed));
    }

    SECTION("bad class capacity is rejected") {
        seed.train.classes[0].capacity = 100;
        CHECK_FALSE(rrs::validate_seed(seed));
    }

    SECTION("bad fare amount is rejected") {
        seed.fare.fare_paise = 50000;
        CHECK_FALSE(rrs::validate_seed(seed));
    }

    SECTION("inconsistent inventory math is rejected") {
        seed.inventory.free_after_block = 50;  // 72 - 12 != 50
        CHECK_FALSE(rrs::validate_seed(seed));
    }

    SECTION("non-admin role for admin is rejected") {
        seed.admin.role = "PASSENGER";
        CHECK_FALSE(rrs::validate_seed(seed));
    }

    SECTION("missing lockout account is rejected") {
        for (auto& p : seed.passengers) {
            p.is_lockout_test_account = false;
        }
        CHECK_FALSE(rrs::validate_seed(seed));
    }
}

TEST_CASE("ENB-05-06: missing train class, unknown account, and invalid passenger counts are refused",
          "[unit][M1]") {
    const auto seed = rrs::load_default_seed();

    // Querying non-configured classes on train 12345
    CHECK_FALSE(rrs::find_class(seed.train, "1A").has_value());
    CHECK_FALSE(rrs::find_class(seed.train, "2A").has_value());
    CHECK_FALSE(rrs::find_class(seed.train, "CC").has_value());
    CHECK_FALSE(rrs::find_class(seed.train, "NONEXISTENT").has_value());

    // Querying unknown user
    CHECK_FALSE(rrs::find_account(seed, "unknown_user").has_value());

    // Boundary validation for compute_displayed_fare (SRS RRS-F-013: 1 to 6 passengers)
    CHECK_THROWS_AS(rrs::compute_displayed_fare(45000, 0), std::invalid_argument);
    CHECK_THROWS_AS(rrs::compute_displayed_fare(45000, 7), std::invalid_argument);
    CHECK_THROWS_AS(rrs::compute_displayed_fare(0, 4), std::invalid_argument);
    CHECK_THROWS_AS(rrs::compute_displayed_fare(-100, 4), std::invalid_argument);
}
