#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "audit_log.hpp"
#include "persistence.hpp"
#include "search.hpp"
#include "train_catalogue.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> id{0};
        const auto prefix = "rrs-search-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    (prefix + "-" + std::to_string(id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create search test directory",
                                                        path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

constexpr std::string_view kAuditTime = "2026-10-30T10:00:00Z";
const rrs::Session kAdmin{"admin", rrs::Role::Administrator};

rrs::TrainRecord train(std::uint32_t number, std::string source = "SBC",
                       std::string destination = "NDLS", std::string departure = "10:00",
                       std::uint8_t days = 0x7f) {
    return {number, "Train " + std::to_string(number), std::move(source),
            std::move(destination), std::move(departure), "14:00", days};
}

rrs::SearchQuery query(std::string date = "2026-10-30", std::string time = "09:00") {
    return {"SBC", "NDLS", std::move(date), "2026-10-30", std::move(time)};
}

}  // namespace

TEST_CASE("TC-SEARCH-01: route matching sorts departure times and preserves past-only state",
          "[unit][M6][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    const auto add_result = catalogue.add_train(kAdmin, kAuditTime, train(10, "SBC", "NDLS", "12:00"));
    INFO(add_result.message);
    REQUIRE(add_result.kind == rrs::OutcomeKind::Success);
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime, train(11, "SBC", "NDLS", "10:00")).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime, train(9, "SBC", "NDLS", "10:00")).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime, train(12, "BLR", "NDLS", "08:00")).kind ==
            rrs::OutcomeKind::Success);
    rrs::SearchModule search(catalogue);
    const auto found = search.search(query());
    REQUIRE(found.status == rrs::SearchStatus::ResultsFound);
    REQUIRE(found.trains.size() == 3);
    CHECK(found.trains[0].train_number == 9);
    CHECK(found.trains[1].train_number == 11);
    CHECK(found.trains[2].train_number == 10);
    CHECK(search.search({"SBC", "MYS", "2026-10-30", "2026-10-30", "09:00"})
              .status == rrs::SearchStatus::NoTrainServesRoute);

    const auto boundary = search.search(query("2026-10-30", "10:00"));
    REQUIRE(boundary.trains.size() == 3);
    CHECK(boundary.trains.front().departure_time == "10:00");
    const auto passed = search.search(query("2026-10-30", "12:01"));
    CHECK(passed.trains.empty());
    CHECK(passed.status == rrs::SearchStatus::AllDeparturesHavePassed);
    CHECK(search.search(query("2026-10-31", "23:59")).trains.size() == 3);
}

TEST_CASE("TC-SEARCH-01: query station codes use M6 validation and exact matching", "[unit][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    rrs::SearchModule search(catalogue);
    CHECK(search.search(query()).status == rrs::SearchStatus::NoTrainServesRoute);
    CHECK(search.search({"A", "B", "2026-10-30", "2026-10-30", "09:00"}).status ==
          rrs::SearchStatus::NoTrainServesRoute);
    CHECK(search.search({"ABCDEFGH", "12345678", "2026-10-30", "2026-10-30", "09:00"})
              .status == rrs::SearchStatus::NoTrainServesRoute);
    CHECK_THROWS_AS(search.search({"", "NDLS", "2026-10-30", "2026-10-30", "09:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"sbc", "NDLS", "2026-10-30", "2026-10-30", "09:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SB!", "NDLS", "2026-10-30", "2026-10-30", "09:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"ABCDEFGHI", "NDLS", "2026-10-30", "2026-10-30", "09:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SBC", "SBC", "2026-10-30", "2026-10-30", "09:00"}),
                    std::invalid_argument);
}

TEST_CASE("TC-SEARCH-01: all weekday bits map Monday through Sunday and non-running days refuse",
          "[unit][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    rrs::SearchModule search(catalogue);
    const std::vector<std::string> dates = {"2026-10-05", "2026-10-06", "2026-10-07",
                                            "2026-10-08", "2026-10-09", "2026-10-10",
                                            "2026-10-11"};
    for (unsigned int day = 0; day < dates.size(); ++day) {
        REQUIRE(catalogue.add_train(kAdmin, kAuditTime,
                                    train(day + 1, "SBC", "NDLS", "10:00",
                                          static_cast<std::uint8_t>(1U << day))).kind ==
                rrs::OutcomeKind::Success);
        auto day_query = query(dates[day], "08:00");
        day_query.current_date = dates[day];
        const auto result = search.search(day_query);
        REQUIRE(result.trains.size() == 1);
        CHECK(result.trains[0].train_number == day + 1);
    }
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime,
                                train(99, "MYS", "MAS", "10:00", 0x01)).kind ==
            rrs::OutcomeKind::Success);
    const rrs::SearchQuery no_weekday_query{"MYS", "MAS", "2026-10-06", "2026-10-06",
                                             "08:00"};
    CHECK(search.search(no_weekday_query).status ==
          rrs::SearchStatus::NoTrainRunsOnDate);
}

TEST_CASE("TC-SEARCH-01: date validation handles leap years and rejects malformed dates",
          "[unit][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    rrs::SearchModule search(catalogue);
    CHECK_NOTHROW(search.search(query("2024-02-29", "00:00")));
    CHECK_NOTHROW(search.search(query("2000-02-29", "00:00")));
    CHECK_THROWS_AS(search.search(query("1900-02-29")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("0000-01-01")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("2026-02-29")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("2026-13-01")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("2026/10/30")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("2026-1-01")), std::invalid_argument);
    CHECK_THROWS_AS(search.search(query("2026-10-3")), std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SBC", "NDLS", "2026-10-30", "2026-02-30", "10:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SBC", "NDLS", "2026-10-30", "2026-10-30", "24:00"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SBC", "NDLS", "2026-10-30", "2026-10-30", "12:60"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(search.search({"SBC", "NDLS", "2026-10-30", "2026-10-30", "1:00"}),
                    std::invalid_argument);
}

TEST_CASE("TC-SEARCH-01: availability distinguishes no class, available and unknown",
          "[unit][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime, train(1)).kind == rrs::OutcomeKind::Success);
    rrs::SearchModule search(catalogue);
    const auto no_class = search.search(query(), [](const auto&, auto) {
        return rrs::AvailabilityStatus::NoBookableClass;
    });
    CHECK(no_class.status == rrs::SearchStatus::NoClassHasFreeSeats);
    CHECK(no_class.trains.size() == 1);
    const auto unknown = search.search(query(), [](const auto&, auto) {
        return rrs::AvailabilityStatus::Unknown;
    });
    CHECK(unknown.status == rrs::SearchStatus::ResultsFound);
    CHECK(unknown.trains.size() == 1);
    const auto available = search.search(query(), [](const auto&, auto) {
        return rrs::AvailabilityStatus::HasBookableClass;
    });
    CHECK(available.status == rrs::SearchStatus::ResultsFound);
    REQUIRE(catalogue.add_train(kAdmin, kAuditTime, train(2)).kind == rrs::OutcomeKind::Success);
    const auto mixed = search.search(query(), [](const auto& item, auto) {
        return item.train_number == 1 ? rrs::AvailabilityStatus::NoBookableClass
                                      : rrs::AvailabilityStatus::Unknown;
    });
    CHECK(mixed.status == rrs::SearchStatus::ResultsFound);
    const auto mixed_with_available = search.search(query(), [](const auto& item, auto) {
        return item.train_number == 1 ? rrs::AvailabilityStatus::NoBookableClass
                                      : rrs::AvailabilityStatus::HasBookableClass;
    });
    CHECK(mixed_with_available.status == rrs::SearchStatus::ResultsFound);
}

TEST_CASE("TC-SEARCH-01: deterministic 2000-train catalogue returns exact matching order",
          "[unit][M6][M9]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    std::ofstream records(temp.path() / rrs::kTrainsFile, std::ios::binary);
    REQUIRE(records.good());
    for (std::uint32_t i = 1; i <= 2000; ++i) {
        const bool match = i == 2000 || i == 17 || i == 991;
        const std::string departure = i == 2000 ? "06:30" : i == 17 ? "05:30" : "06:00";
        std::string record(rrs::kTrainRecordSize, '\0');
        record[0] = 1;
        record[1] = 1;
        for (unsigned int byte = 0; byte < 4; ++byte) {
            record[2 + byte] = static_cast<char>((i >> (byte * 8U)) & 0xffU);
        }
        const auto name = "Train " + std::to_string(i);
        record.replace(6, 40, std::string(40, ' '));
        record.replace(6, name.size(), name);
        record.replace(46, 8, match ? "SBC     " : "MYS     ");
        record.replace(54, 8, match ? "NDLS    " : "MAS     ");
        record.replace(62, 5, departure);
        record.replace(67, 5, "14:00");
        record[72] = 0x7f;
        records.write(record.data(), static_cast<std::streamsize>(record.size()));
    }
    records.close();
    REQUIRE(records.good());
    rrs::SearchModule search(catalogue);
    auto large_query = query("2026-10-30", "00:00");
    large_query.current_date = large_query.journey_date;
    const auto result = search.search(large_query);
    REQUIRE(result.trains.size() == 3);
    CHECK(result.trains[0].train_number == 17);
    CHECK(result.trains[1].train_number == 991);
    CHECK(result.trains[2].train_number == 2000);
}

TEST_CASE("TC-SEARCH-01: 100 searches over 2000 M6 records meet the latency target",
          "[unit][M6][M9][performance]") {
    TempDirectory temp;
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue{rrs::PersistenceStore{temp.path()}, audit};
    const auto construction_start = std::chrono::steady_clock::now();
    std::ofstream records(temp.path() / rrs::kTrainsFile, std::ios::binary);
    REQUIRE(records.good());
    for (std::uint32_t i = 1; i <= 2000; ++i) {
        const bool match = i == 17 || i == 991 || i == 2000;
        const std::string departure = i == 17 ? "05:30" : i == 991 ? "06:00" : "06:30";
        std::string record(rrs::kTrainRecordSize, '\0');
        record[0] = 1;
        record[1] = 1;
        for (unsigned int byte = 0; byte < 4; ++byte) {
            record[2 + byte] = static_cast<char>((i >> (byte * 8U)) & 0xffU);
        }
        const auto name = "Train " + std::to_string(i);
        record.replace(6, 40, std::string(40, ' '));
        record.replace(6, name.size(), name);
        record.replace(46, 8, match ? "SBC     " : "MYS     ");
        record.replace(54, 8, match ? "NDLS    " : "MAS     ");
        record.replace(62, 5, departure);
        record.replace(67, 5, "14:00");
        record[72] = 0x7f;
        records.write(record.data(), static_cast<std::streamsize>(record.size()));
    }
    records.close();
    REQUIRE(records.good());
    const auto construction_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - construction_start).count();

    rrs::SearchModule search(catalogue);
    auto fixed_query = query("2026-10-30", "00:00");
    fixed_query.current_date = fixed_query.journey_date;
    std::vector<std::int64_t> latencies_us;
    latencies_us.reserve(100);
    bool exact_results = true;
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = search.search(fixed_query);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        exact_results = exact_results && result.trains.size() == 3 &&
                        result.trains[0].train_number == 17 &&
                        result.trains[1].train_number == 991 &&
                        result.trains[2].train_number == 2000;
        latencies_us.push_back(elapsed);
    }
    std::sort(latencies_us.begin(), latencies_us.end());
    const auto p95_us = latencies_us[94];
    const auto max_us = latencies_us.back();
    std::cout << "M9 benchmark: dataset construction=" << construction_us
              << " us, 100 searches p95=" << p95_us << " us, max=" << max_us << " us\n";
    CHECK(exact_results);
}
