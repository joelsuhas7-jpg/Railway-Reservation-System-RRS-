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
                    ("rrs-seat-flow-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                     std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error) throw std::filesystem::filesystem_error(
                "cannot create seat flow directory", path_, error);
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
}

TEST_CASE("TC-SEAT-01: configured train class initializes, blocks and survives module reopen",
          "[integration][M1][M3][M5][M6][M7][M8]") {
    TempDirectory temp;
    const rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    rrs::TrainCatalogueModule trains(store, audit);
    REQUIRE(trains.add_train(admin, kTime,
        {12345, "Test Express", "SBC", "NDLS", "06:00", "14:00", 0x7f}).kind ==
        rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTime); });
    REQUIRE(classes.add_class(admin, kTime, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);

    {
        rrs::SeatInventoryModule inventory(store, classes, audit,
                                           [] { return std::string(kTime); });
        CHECK(inventory.get_inventory(12345, "SL", "2026-10-01").free_seats == 72);
        REQUIRE(inventory.block_seats(admin, kTime, 12345, "SL",
                "2026-10-01", 12).kind == rrs::OutcomeKind::Success);
    }

    rrs::SeatInventoryModule reopened(store, classes, audit,
                                      [] { return std::string(kTime); });
    const auto result = reopened.get_inventory(12345, "SL", "2026-10-01");
    CHECK(result.total_seats == 72);
    CHECK(result.blocked_seats == 12);
    CHECK(result.free_seats == 60);

    std::ifstream input(temp.path() / "audit.log");
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) lines.push_back(line);
    REQUIRE(lines.size() == 3);
    CHECK(lines.back().find("\tSEATS_BLOCKED\t12345:SL:2026-10-01") !=
          std::string::npos);
}
