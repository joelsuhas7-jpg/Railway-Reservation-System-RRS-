#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

namespace {
constexpr std::string_view kTime = "2026-10-31T10:00:00Z";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-seat-unit-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                     std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error) throw std::filesystem::filesystem_error(
                "cannot create seat inventory test directory", path_, error);
        }
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

struct Fixture {
    explicit Fixture(const std::filesystem::path& path)
        : store(path), audit(path / "audit.log"), trains(store, audit),
          classes(store, trains, audit, [] { return std::string(kTime); }),
          inventory(store, classes, audit, [] { return std::string(kTime); }) {
        const rrs::Session admin{"admin", rrs::Role::Administrator};
        REQUIRE(trains.add_train(admin, kTime,
            {12345, "Test Express", "SBC", "NDLS", "06:00", "14:00", 0x7f}).kind ==
            rrs::OutcomeKind::Success);
        REQUIRE(classes.add_class(admin, kTime, 12345, "SL", 72).kind ==
                rrs::OutcomeKind::Success);
    }
    rrs::PersistenceStore store;
    rrs::AuditLog audit;
    rrs::TrainCatalogueModule trains;
    rrs::ClassFareConfigModule classes;
    rrs::SeatInventoryModule inventory;
};
}

TEST_CASE("TC-SEAT-01: first reference initializes configured class capacity", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const auto inventory = fixture.inventory.get_inventory(12345, "SL", "2026-10-01");
    CHECK(inventory.train_number == 12345);
    CHECK(inventory.class_code == "SL");
    CHECK(inventory.journey_date == "2026-10-01");
    CHECK(inventory.total_seats == 72);
    CHECK(inventory.blocked_seats == 0);
    CHECK(inventory.free_seats == 72);
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 1);
}

TEST_CASE("TC-SEAT-01: repeated reference returns the same persisted inventory", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    (void)fixture.inventory.get_inventory(12345, "SL", "2026-10-01");
    (void)fixture.inventory.get_inventory(12345, "SL", "2026-10-01");
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 1);
    CHECK(fixture.inventory.get_inventory(12345, "SL", "2026-10-01").free_seats == 72);
}

TEST_CASE("TC-SEAT-01: blocking twelve seats leaves sixty free and is audited", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const auto result = fixture.inventory.block_seats(
        admin, kTime, 12345, "SL", "2026-10-01", 12);
    REQUIRE(result.kind == rrs::OutcomeKind::Success);
    const auto current = fixture.inventory.get_inventory(12345, "SL", "2026-10-01");
    CHECK(current.total_seats == 72);
    CHECK(current.blocked_seats == 12);
    CHECK(current.free_seats == 60);

    std::ifstream input(temp.path() / "audit.log");
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) lines.push_back(line);
    REQUIRE(lines.size() == 3);
    CHECK(lines.back() ==
          "2026-10-31T10:00:00Z\tadmin\tADMIN\tSEATS_BLOCKED\t12345:SL:2026-10-01");
}

TEST_CASE("TC-SEAT-01: blocks are cumulative and cannot exceed free inventory", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
            "2026-10-01", 12).kind == rrs::OutcomeKind::Success);
    REQUIRE(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
            "2026-10-01", 10).kind == rrs::OutcomeKind::Success);
    CHECK(fixture.inventory.get_inventory(12345, "SL", "2026-10-01").free_seats == 50);
    CHECK(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
            "2026-10-01", 51).kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(fixture.inventory.get_inventory(12345, "SL", "2026-10-01").free_seats == 50);
}

TEST_CASE("TC-SEAT-01: invalid date and unknown train or class are rejected", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    CHECK_THROWS_AS(fixture.inventory.get_inventory(12345, "SL", "2026-02-30"),
                    std::invalid_argument);
    CHECK_THROWS_AS(fixture.inventory.get_inventory(99999, "SL", "2026-10-01"),
                    std::invalid_argument);
    CHECK_THROWS_AS(fixture.inventory.get_inventory(12345, "3A", "2026-10-01"),
                    std::invalid_argument);
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 0);
}

TEST_CASE("TC-SEAT-01: only administrators can block seats", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const rrs::Session passenger{"passenger", rrs::Role::Passenger};
    const auto result = fixture.inventory.block_seats(
        passenger, kTime, 12345, "SL", "2026-10-01", 1);
    CHECK(result.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 0);
}

TEST_CASE("TC-SEAT-01: invalid block counts and invalid dates do not mutate inventory", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    CHECK(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
          "2026-10-01", 0).kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
          "2026-02-30", 1).kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 0);
}

TEST_CASE("TC-SEAT-01: inventory is isolated by journey date", "[unit][M8]") {
    TempDirectory temp;
    Fixture fixture(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(fixture.inventory.block_seats(admin, kTime, 12345, "SL",
            "2026-10-01", 12).kind == rrs::OutcomeKind::Success);
    CHECK(fixture.inventory.get_inventory(12345, "SL", "2026-10-02").free_seats == 72);
    CHECK(fixture.store.record_count(rrs::kInventoryFile, rrs::kInventoryRecordSize) == 2);
}
