#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rrs {

// Travel class configuration for a train (SRS s4.2, RRS-F-007).
struct TravelClassFixture {
    std::string_view class_code;  // e.g. "SL", "3A"
    std::size_t capacity{0};       // e.g. 72, 64
};

// Flat per-passenger configured fare in paise (SRS s4.2, RRS-F-008).
struct FareFixture {
    std::uint32_t train_number{0};
    std::string_view class_code;
    std::int64_t fare_paise{0};  // all money is std::int64_t paise
    std::string_view effective_from;
};

// Train schedule record (SRS s4.2, RRS-F-006, s4.3.1).
struct TrainFixture {
    std::uint32_t train_number{0};
    std::string_view name;
    std::string_view source_station;
    std::string_view destination_station;
    std::string_view departure_time;
    std::string_view arrival_time;
    std::uint8_t running_days_mask{0};  // 7-bit mask
    std::vector<TravelClassFixture> classes;
};

// User account fixture (SRS s4.1, RRS-F-001, RRS-F-004, Test Plan s10).
struct AccountFixture {
    std::string_view username;
    std::string_view role;      // "ADMIN" or "PASSENGER"
    std::string_view password;  // policy-compliant (8+ chars, letter + digit)
    bool is_lockout_test_account{false};
};

// Seat inventory fixture for a (train, class, journey_date) triple (SRS s4.2, RRS-F-009).
struct InventoryFixture {
    std::uint32_t train_number{0};
    std::string_view class_code;
    std::string_view journey_date;
    std::size_t initial_free_count{0};
    std::size_t admin_block_count{0};
    std::size_t free_after_block{0};
};

// Exact literal figures specified in SRS and Test Plan for arithmetic assertions.
struct CalculationKnownAnswers {
    std::size_t passenger_count{0};            // 4
    std::int64_t total_fare_paise{0};          // 180000
    std::int64_t s2_refund_paise{0};           // 90000
    std::size_t s2_released_seats{0};          // 4
    std::size_t admin_block_seats{0};          // 12
    std::size_t sl_free_seats_after_block{0};  // 60
};

// Complete seeded fixture bundle for tests (Issue #5, ENB-05).
struct SeedFixtureData {
    TrainFixture train;
    FareFixture fare;
    InventoryFixture inventory;
    CalculationKnownAnswers calculations;
    AccountFixture admin;
    std::array<AccountFixture, 4> passengers;
};

// Returns reference to canonical known-answer train 12345 (SL 72, 3A 64).
const TrainFixture& known_train_12345();

// Returns reference to canonical known-answer fare for (12345, SL): 45000 paise.
const FareFixture& known_fare_12345_sl();

// Returns reference to bootstrapped administrator account fixture.
const AccountFixture& known_admin_account();

// Returns reference to the 4 passenger account fixtures.
const std::array<AccountFixture, 4>& known_passenger_accounts();

// Returns the dedicated lockout passenger account fixture (Test Plan s10).
const AccountFixture& known_lockout_passenger_account();

// Returns the baseline seat inventory fixture for (12345, SL, 2026-10-01).
const InventoryFixture& known_inventory_12345_sl();

// Returns the exact arithmetic known-answer calculations.
const CalculationKnownAnswers& known_calculation_answers();

// Loads the default deterministic seed dataset.
SeedFixtureData load_default_seed();

// Loads deterministic seed dataset by numeric seed (seed 0 or 1 loads default).
SeedFixtureData load_seed(std::uint64_t seed_value);

// Searches for a travel class on a train fixture by class code; returns nullopt if absent.
std::optional<TravelClassFixture> find_class(const TrainFixture& train,
                                             std::string_view class_code);

// Searches for an account in seed data by username; returns nullopt if absent.
std::optional<AccountFixture> find_account(const SeedFixtureData& seed,
                                           std::string_view username);

// Computes total fare: configured_fare * passenger_count (SRS s4.4, RRS-F-014).
// Throws std::invalid_argument if passenger_count is 0 or exceeds 6.
std::int64_t compute_displayed_fare(std::int64_t fare_paise, std::size_t passenger_count);

// Validates consistency and integrity of seed fixture data.
bool validate_seed(const SeedFixtureData& seed);

}  // namespace rrs
