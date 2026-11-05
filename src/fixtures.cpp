#include "fixtures.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace rrs {
namespace {

const TrainFixture kDefaultTrain{
    12345,
    "Karnataka Express",
    "SBC",
    "NDLS",
    "06:00",
    "14:00",
    0b1111111,  // Runs daily
    {
        {"SL", 72},
        {"3A", 64}
    }
};

const FareFixture kDefaultFare{
    12345,
    "SL",
    45000,
    "2026-10-01"
};

const InventoryFixture kDefaultInventory{
    12345,
    "SL",
    "2026-10-01",
    72,
    12,
    60
};

const CalculationKnownAnswers kDefaultCalculations{
    4,
    180000,
    90000,
    4,
    12,
    60
};

const AccountFixture kDefaultAdmin{
    "admin",
    "ADMIN",
    "AdminPassword1",
    false
};

const std::array<AccountFixture, 4> kDefaultPassengers = {{
    {"passenger1", "PASSENGER", "Pass1Secure", false},
    {"passenger2", "PASSENGER", "Pass2Secure", false},
    {"passenger3", "PASSENGER", "Pass3Secure", false},
    {"passenger_lockout", "PASSENGER", "LockoutPass1", true}
}};

bool is_policy_compliant_password(std::string_view password) {
    if (password.size() < 8) {
        return false;
    }
    bool has_alpha = false;
    bool has_digit = false;
    for (char c : password) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            has_alpha = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            has_digit = true;
        }
    }
    return has_alpha && has_digit;
}

}  // namespace

const TrainFixture& known_train_12345() {
    return kDefaultTrain;
}

const FareFixture& known_fare_12345_sl() {
    return kDefaultFare;
}

const AccountFixture& known_admin_account() {
    return kDefaultAdmin;
}

const std::array<AccountFixture, 4>& known_passenger_accounts() {
    return kDefaultPassengers;
}

const AccountFixture& known_lockout_passenger_account() {
    for (const auto& account : kDefaultPassengers) {
        if (account.is_lockout_test_account) {
            return account;
        }
    }
    return kDefaultPassengers[3];
}

const InventoryFixture& known_inventory_12345_sl() {
    return kDefaultInventory;
}

const CalculationKnownAnswers& known_calculation_answers() {
    return kDefaultCalculations;
}

SeedFixtureData load_default_seed() {
    return load_seed(0);
}

SeedFixtureData load_seed(std::uint64_t seed_value) {
    // Deterministic seed loading: seed 0, 1, or any valid seed returns the baselined fixtures
    SeedFixtureData data;
    data.train = kDefaultTrain;
    data.fare = kDefaultFare;
    data.inventory = kDefaultInventory;
    data.calculations = kDefaultCalculations;
    data.admin = kDefaultAdmin;
    data.passengers = kDefaultPassengers;

    (void)seed_value;
    return data;
}

std::optional<TravelClassFixture> find_class(const TrainFixture& train,
                                             std::string_view class_code) {
    for (const auto& cls : train.classes) {
        if (cls.class_code == class_code) {
            return cls;
        }
    }
    return std::nullopt;
}

std::optional<AccountFixture> find_account(const SeedFixtureData& seed,
                                           std::string_view username) {
    if (seed.admin.username == username) {
        return seed.admin;
    }
    for (const auto& p : seed.passengers) {
        if (p.username == username) {
            return p;
        }
    }
    return std::nullopt;
}

std::int64_t compute_displayed_fare(std::int64_t fare_paise, std::size_t passenger_count) {
    if (fare_paise <= 0) {
        throw std::invalid_argument("fare_paise must be positive");
    }
    if (passenger_count < 1 || passenger_count > 6) {
        throw std::invalid_argument("passenger_count must be between 1 and 6");
    }
    return fare_paise * static_cast<std::int64_t>(passenger_count);
}

bool validate_seed(const SeedFixtureData& seed) {
    if (seed.train.train_number != 12345) {
        return false;
    }
    auto sl = find_class(seed.train, "SL");
    auto three_a = find_class(seed.train, "3A");
    if (!sl.has_value() || sl->capacity != 72) {
        return false;
    }
    if (!three_a.has_value() || three_a->capacity != 64) {
        return false;
    }

    if (seed.fare.train_number != 12345 || seed.fare.class_code != "SL" ||
        seed.fare.fare_paise != 45000 || seed.fare.effective_from != "2026-10-01") {
        return false;
    }

    if (seed.inventory.train_number != 12345 || seed.inventory.class_code != "SL" ||
        seed.inventory.journey_date != "2026-10-01" ||
        seed.inventory.initial_free_count != 72 ||
        seed.inventory.admin_block_count != 12 ||
        seed.inventory.free_after_block != 60) {
        return false;
    }

    if (seed.calculations.passenger_count != 4 ||
        seed.calculations.total_fare_paise != 180000 ||
        seed.calculations.s2_refund_paise != 90000 ||
        seed.calculations.s2_released_seats != 4 ||
        seed.calculations.admin_block_seats != 12 ||
        seed.calculations.sl_free_seats_after_block != 60) {
        return false;
    }

    if (seed.admin.role != "ADMIN" || !is_policy_compliant_password(seed.admin.password)) {
        return false;
    }

    std::size_t lockout_count = 0;
    for (const auto& p : seed.passengers) {
        if (p.role != "PASSENGER" || !is_policy_compliant_password(p.password)) {
            return false;
        }
        if (p.is_lockout_test_account) {
            lockout_count++;
        }
    }
    if (lockout_count != 1) {
        return false;
    }

    return true;
}

std::vector<GeneratedTrainFixture> generate_search_fixtures(std::uint64_t seed_value) {
    // SplitMix64 has a fixed, portable sequence and avoids implementation-defined
    // distribution behavior from standard library random adapters.
    auto next = [&seed_value]() {
        std::uint64_t value = (seed_value += 0x9e3779b97f4a7c15ULL);
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    };
    constexpr const char* stations[] = {"SBC", "NDLS", "MAS", "HWH", "BCT",
                                        "PUNE", "HYB", "JP", "LKO", "ADI"};
    constexpr const char* class_codes[] = {"SL", "3A", "2A", "1A", "CC",
                                           "EC", "2S", "EA", "FC", "GN"};
    std::vector<GeneratedTrainFixture> trains;
    trains.reserve(2000);
    for (std::uint32_t i = 1; i <= 2000; ++i) {
        const auto source_index = static_cast<std::size_t>(next() % 10U);
        auto destination_index = static_cast<std::size_t>(next() % 9U);
        if (destination_index >= source_index) ++destination_index;
        const auto departure_minutes = static_cast<unsigned int>(next() % 1440U);
        const auto duration = static_cast<unsigned int>(60U + next() % 900U);
        const auto arrival_minutes = (departure_minutes + duration) % 1440U;
        const auto class_count = static_cast<std::size_t>(1U + next() % 10U);
        GeneratedTrainFixture train;
        train.train_number = 10000U + i;
        train.name = "Fixture Train " + std::to_string(i);
        train.source_station = stations[source_index];
        train.destination_station = stations[destination_index];
        const auto format_time = [](unsigned int minutes) {
            std::string result(5, '0');
            result[0] = static_cast<char>('0' + minutes / 60U / 10U);
            result[1] = static_cast<char>('0' + minutes / 60U % 10U);
            result[2] = ':';
            result[3] = static_cast<char>('0' + minutes % 60U / 10U);
            result[4] = static_cast<char>('0' + minutes % 60U % 10U);
            return result;
        };
        train.departure_time = format_time(departure_minutes);
        train.arrival_time = format_time(arrival_minutes);
        train.running_days_mask = static_cast<std::uint8_t>(1U + next() % 127U);
        train.classes.reserve(class_count);
        for (std::size_t cls = 0; cls < class_count; ++cls) {
            train.classes.emplace_back(class_codes[cls], 40U + next() % 81U);
        }
        trains.push_back(std::move(train));
    }
    return trains;
}

bool validate_search_fixtures(const std::vector<GeneratedTrainFixture>& trains) {
    if (trains.size() != 2000) return false;
    std::set<std::uint32_t> numbers;
    for (const auto& train : trains) {
        if (train.train_number == 0 || !numbers.insert(train.train_number).second ||
            train.name.empty() || train.source_station.empty() || train.destination_station.empty() ||
            train.source_station == train.destination_station || train.departure_time.size() != 5 ||
            train.arrival_time.size() != 5 || train.running_days_mask == 0 ||
            (train.running_days_mask & 0x80U) != 0 || train.classes.empty() ||
            train.classes.size() > 10) return false;
        for (const auto& cls : train.classes) {
            if (cls.first.empty() || cls.second == 0) return false;
        }
    }
    return true;
}

}  // namespace rrs
