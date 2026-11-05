#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "fixture_generator.hpp"

TEST_CASE("ENB-06-01: SplitMix64 has stable known answers and unbiased range bounds",
          "[unit][ENB-06]") {
    rrs::SplitMix64 random(0);
    CHECK(random.next() == UINT64_C(0xe220a8397b1dcdaf));
    CHECK(random.next() == UINT64_C(0x6e789e6aa1b965f4));

    rrs::SplitMix64 bounded(42);
    CHECK(bounded.bounded(1) == 0);
    for (int index = 0; index < 1000; ++index) {
        CHECK(bounded.bounded(7) < 7);
    }
    CHECK_THROWS_AS(bounded.bounded(0), std::invalid_argument);
}

TEST_CASE("ENB-06-02: fixture plans are reproducible and contain valid bounded records",
          "[unit][ENB-06]") {
    const auto first = rrs::generate_fixture_plan(UINT64_C(0x123456789abcdef0));
    const auto same = rrs::generate_fixture_plan(UINT64_C(0x123456789abcdef0));
    const auto other = rrs::generate_fixture_plan(UINT64_C(0x123456789abcdef1));
    REQUIRE(first.trains.size() == rrs::kGeneratedFixtureTrainCount);
    REQUIRE(first.manifest.class_count >= first.trains.size());
    REQUIRE(first.manifest.class_count <= first.trains.size() * 6);
    CHECK(first.manifest.train_count == rrs::kGeneratedFixtureTrainCount);
    CHECK(first.manifest.fare_count == first.manifest.class_count);
    CHECK(first.manifest.fingerprint == same.manifest.fingerprint);
    CHECK(first.manifest.fingerprint != other.manifest.fingerprint);
    CHECK(rrs::fixture_fingerprint(first) == first.manifest.fingerprint);
    CHECK(rrs::validate_fixture_plan(first).ok());
    const auto manifest = rrs::format_fixture_manifest(first.manifest);
    CHECK(manifest.find("format=RRS-FIXTURE-MANIFEST-V1\n") == 0);
    CHECK(manifest.find("algorithm=splitmix64-v1\n") != std::string::npos);
    CHECK(manifest.find("fingerprint_fnv1a64=") != std::string::npos);

    constexpr std::array<std::string_view, 6> codes = {"SL", "3A", "2A", "1A", "CC", "2S"};
    std::set<std::uint32_t> train_numbers;
    std::size_t minimum_classes = std::numeric_limits<std::size_t>::max();
    std::size_t maximum_classes = 0;
    std::array<bool, 6> class_counts_seen{};
    for (const auto& generated : first.trains) {
        const auto& train = generated.train;
        CHECK(train_numbers.insert(train.train_number).second);
        CHECK(train.train_number != 0);
        CHECK_FALSE(train.name.empty());
        CHECK(train.name.size() <= 40);
        CHECK(train.source_station.size() >= 1);
        CHECK(train.source_station.size() <= 8);
        CHECK(train.destination_station.size() >= 1);
        CHECK(train.destination_station.size() <= 8);
        CHECK(train.source_station != train.destination_station);
        CHECK(train.departure_time.size() == 5);
        CHECK(train.arrival_time.size() == 5);
        CHECK(train.running_days_mask > 0);
        CHECK((train.running_days_mask & 0x80U) == 0);
        CHECK(generated.classes.size() >= 1);
        CHECK(generated.classes.size() <= 6);
        minimum_classes = std::min(minimum_classes, generated.classes.size());
        maximum_classes = std::max(maximum_classes, generated.classes.size());
        if (generated.classes.size() >= 1 && generated.classes.size() <= class_counts_seen.size()) {
            class_counts_seen[generated.classes.size() - 1] = true;
        }
        std::set<std::string> per_train_codes;
        for (const auto& item : generated.classes) {
            CHECK(std::find(codes.begin(), codes.end(), item.class_code) != codes.end());
            CHECK(per_train_codes.insert(item.class_code).second);
            CHECK(item.capacity > 0);
            CHECK(item.fare_paise > 0);
            CHECK(item.effective_from == rrs::kFixtureDate);
        }
    }
    CHECK(train_numbers.size() == rrs::kGeneratedFixtureTrainCount);
    CHECK(minimum_classes == 1);
    CHECK(maximum_classes == 6);
    CHECK(std::all_of(class_counts_seen.begin(), class_counts_seen.end(),
                      [](bool seen) { return seen; }));

    for (std::size_t index = 0; index < first.trains.size(); ++index) {
        const auto& left = first.trains[index];
        const auto& right = same.trains[index];
        CHECK(left.train.train_number == right.train.train_number);
        CHECK(left.train.name == right.train.name);
        CHECK(left.train.source_station == right.train.source_station);
        CHECK(left.train.destination_station == right.train.destination_station);
        CHECK(left.train.departure_time == right.train.departure_time);
        CHECK(left.train.arrival_time == right.train.arrival_time);
        CHECK(left.train.running_days_mask == right.train.running_days_mask);
        REQUIRE(left.classes.size() == right.classes.size());
        for (std::size_t class_index = 0; class_index < left.classes.size(); ++class_index) {
            CHECK(left.classes[class_index].class_code == right.classes[class_index].class_code);
            CHECK(left.classes[class_index].capacity == right.classes[class_index].capacity);
            CHECK(left.classes[class_index].fare_paise == right.classes[class_index].fare_paise);
            CHECK(left.classes[class_index].effective_from == right.classes[class_index].effective_from);
        }
    }
}

TEST_CASE("ENB-06-03: manifest fingerprint covers logical fixture fields",
          "[unit][ENB-06]") {
    auto plan = rrs::generate_fixture_plan(7);
    const auto original = rrs::fixture_fingerprint(plan);
    plan.trains[0].classes[0].fare_paise += 1;
    CHECK(rrs::fixture_fingerprint(plan) != original);
    CHECK(plan.manifest.fingerprint == original);
}
