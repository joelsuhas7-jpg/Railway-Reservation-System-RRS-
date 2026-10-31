#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "fare_config.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-09-30T10:00:00Z";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-fare-config-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create fare test directory",
                                                        path_, error);
            }
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

rrs::TrainRecord valid_train() {
    return {12345, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f};
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

std::string raw_fare_record(std::uint32_t train_number = 12345,
                            std::string_view code = "SL", std::uint64_t fare_paise = 45000,
                            std::string_view effective_from = "2026-10-01",
                            std::uint8_t version = 1, std::uint64_t revision = 1) {
    std::string bytes(rrs::kFareRecordSize, '\0');
    bytes[0] = static_cast<char>(version);
    for (unsigned int byte = 0; byte < 4; ++byte) {
        bytes[1 + byte] = static_cast<char>((train_number >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(5, 2, code);
    for (unsigned int byte = 0; byte < 8; ++byte) {
        bytes[7 + byte] = static_cast<char>((fare_paise >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(15, 10, effective_from);
    for (unsigned int byte = 0; byte < 7; ++byte) {
        bytes[25 + byte] = static_cast<char>((revision >> (byte * 8U)) & 0xffU);
    }
    return bytes;
}

struct Modules {
    explicit Modules(const std::filesystem::path& path)
        : store(path), audit(path / "audit.log"), trains(store, audit),
          class_timestamp_provider([] { return std::string(kTimestamp); }),
          classes(store, trains, audit, class_timestamp_provider),
          fare_timestamp_provider([] { return std::string(kTimestamp); }),
          fares(store, trains, classes, audit, fare_timestamp_provider) {}

    rrs::PersistenceStore store;
    rrs::AuditLog audit;
    rrs::TrainCatalogueModule trains;
    rrs::ClassAuditTimestampProvider class_timestamp_provider;
    rrs::ClassFareConfigModule classes;
    rrs::FareTimestampProvider fare_timestamp_provider;
    rrs::FareConfigModule fares;
};

void add_train_and_class(Modules& modules) {
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
}

}  // namespace

TEST_CASE("TC-FARE-01: decimal fare input converts exactly to integer paise",
          "[unit][M7]") {
    CHECK(rrs::parse_fare_paise("450.00") == 45000);
    CHECK(rrs::parse_fare_paise("450") == 45000);
    CHECK(rrs::parse_fare_paise("450.5") == 45050);
    CHECK(rrs::parse_fare_paise("0.01") == 1);
    CHECK(rrs::parse_fare_paise("92233720368547758.07") ==
          std::numeric_limits<std::int64_t>::max());
    for (const auto input : {"", "0", "0.00", "-1", "-1.00", ".50", "450.",
                             "450.123", "4.5.0", " 450.00", "450.00 ", "1e2",
                             "92233720368547759", "92233720368547758.08"}) {
        CHECK_FALSE(rrs::parse_fare_paise(input).has_value());
    }
}

TEST_CASE("TC-FARE-01: effective dates use strict Gregorian ISO validation",
          "[unit][M7]") {
    CHECK(rrs::is_valid_fare_date("2026-10-01"));
    CHECK(rrs::is_valid_fare_date("2000-02-29"));
    CHECK_FALSE(rrs::is_valid_fare_date("1900-02-29"));
    CHECK_FALSE(rrs::is_valid_fare_date("2026-02-29"));
    CHECK_FALSE(rrs::is_valid_fare_date("2026-13-01"));
    CHECK_FALSE(rrs::is_valid_fare_date("2026-04-31"));
    CHECK_FALSE(rrs::is_valid_fare_date("0000-01-01"));
    CHECK_FALSE(rrs::is_valid_fare_date("2026-1-01"));
    CHECK_FALSE(rrs::is_valid_fare_date("26-10-01"));
}

TEST_CASE("TC-FARE-01: 45000 paise and the effective date persist in the 32-byte format",
          "[unit][M1][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);

    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    const auto stored = modules.store.read_record(rrs::kFaresFile, rrs::kFareRecordSize, 0);
    REQUIRE(stored.has_value());
    std::string expected(rrs::kFareRecordSize, '\0');
    expected[0] = 1;
    expected[1] = static_cast<char>(0x39);
    expected[2] = static_cast<char>(0x30);
    expected.replace(5, 2, "SL");
    expected[7] = static_cast<char>(0xc8);
    expected[8] = static_cast<char>(0xaf);
    expected.replace(15, 10, "2026-10-01");
    expected[25] = 1;
    CHECK(*stored == expected);

    const auto lookup = modules.fares.find_applicable_fare(12345, "SL", "2026-10-01");
    CHECK(lookup.state == rrs::FareLookupState::Found);
    REQUIRE(lookup.record.has_value());
    CHECK(lookup.record->fare_paise == 45000);
    CHECK(lookup.record->effective_from == "2026-10-01");
    CHECK(lookup.record->record_revision == 1);
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 3);
    CHECK(lines.back() ==
          "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_CONFIGURED\t12345:SL:2026-10-01");
}

TEST_CASE("TC-FARE-01: fare serialization preserves all eight little-endian paise bytes",
          "[unit][M1][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    constexpr std::int64_t fare = 0x0102030405060708LL;

    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", fare, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    const auto stored = modules.store.read_record(rrs::kFaresFile, rrs::kFareRecordSize, 0);
    REQUIRE(stored.has_value());
    const std::array<unsigned char, 8> expected = {0x08, 0x07, 0x06, 0x05,
                                                   0x04, 0x03, 0x02, 0x01};
    for (std::size_t byte = 0; byte < expected.size(); ++byte) {
        CHECK(static_cast<unsigned char>((*stored)[7 + byte]) == expected[byte]);
    }
    const auto round_trip = modules.fares.find_applicable_fare(12345, "SL", "2026-10-01");
    REQUIRE(round_trip.record.has_value());
    CHECK(round_trip.record->fare_paise == fare);
}

TEST_CASE("TC-FARE-01: multiple effective dates coexist and select the latest applicable fare",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);

    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 50000, "2026-10-15").kind ==
            rrs::OutcomeKind::Success);
    CHECK(modules.fares.find_applicable_fare(12345, "SL", "2026-09-30").message ==
          "class not open for booking");
    const auto on_first = modules.fares.find_applicable_fare(12345, "SL", "2026-10-01");
    REQUIRE(on_first.record.has_value());
    CHECK(on_first.record->fare_paise == 45000);
    const auto between = modules.fares.find_applicable_fare(12345, "SL", "2026-10-14");
    REQUIRE(between.record.has_value());
    CHECK(between.record->fare_paise == 45000);
    const auto on_second = modules.fares.find_applicable_fare(12345, "SL", "2026-10-15");
    REQUIRE(on_second.record.has_value());
    CHECK(on_second.record->fare_paise == 50000);
    CHECK(modules.fares.list_fares(12345, "SL").size() == 2);
}

TEST_CASE("TC-FARE-01: duplicate date requires explicit selected-record replacement",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 50000, "2026-10-10").kind ==
            rrs::OutcomeKind::Success);

    const auto duplicate = modules.fares.configure_fare(admin, 12345, "SL", 52000,
                                                         "2026-10-10");
    CHECK(duplicate.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 2);
    const auto listed = modules.fares.list_fares(12345, "SL");
    const auto selected = std::find_if(listed.begin(), listed.end(), [](const auto& record) {
        return record.effective_from == "2026-10-10";
    });
    REQUIRE(selected != listed.end());
    const auto conflict = modules.fares.replace_fare(admin, 12345, "SL", "2026-10-10",
                                                      selected->record_revision, 50000, 52000,
                                                      "2026-10-01");
    CHECK(conflict.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 2);

    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", "2026-10-10",
                                       selected->record_revision, 50000, 55000,
                                       "2026-10-20").kind ==
            rrs::OutcomeKind::Success);
    const auto records = modules.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK(records[0].effective_from == "2026-10-01");
    CHECK(records[0].fare_paise == 45000);
    CHECK(records[1].effective_from == "2026-10-20");
    CHECK(records[1].fare_paise == 55000);
}

TEST_CASE("TC-FARE-01: replacement refuses a fare changed since it was selected",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                         "2026-10-01").kind == rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 50000,
                                         "2026-10-10").kind == rrs::OutcomeKind::Success);
    const auto original = modules.fares.list_fares(12345, "SL").front();
    const auto before = read_lines(temp.path() / "audit.log").size();

    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", "2026-10-01",
                                       original.record_revision, 45000, 46000,
                                       "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    const auto stale = modules.fares.replace_fare(admin, 12345, "SL", "2026-10-01",
                                                  original.record_revision, 45000, 47000,
                                                  "2026-10-01");
    CHECK(stale.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(stale.message.find("changed") != std::string::npos);
    const auto records = modules.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK(records[0].fare_paise == 46000);
    CHECK(records[1].fare_paise == 50000);
    const auto after = read_lines(temp.path() / "audit.log");
    CHECK(after.size() == before + 1);
    CHECK(after.back() ==
          "2026-09-30T10:00:00Z\tadmin\tADMIN\tFARE_UPDATED\t12345:SL:2026-10-01");
}

TEST_CASE("TC-FARE-01: stale selection is refused after fare changes away and back",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                         "2026-10-01").kind == rrs::OutcomeKind::Success);
    const auto original = modules.fares.list_fares(12345, "SL").front();
    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                       original.record_revision, original.fare_paise, 50000,
                                       original.effective_from).kind == rrs::OutcomeKind::Success);
    const auto intermediate = modules.fares.list_fares(12345, "SL").front();
    REQUIRE(intermediate.record_revision > original.record_revision);
    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", intermediate.effective_from,
                                       intermediate.record_revision, intermediate.fare_paise,
                                       45000, intermediate.effective_from).kind ==
            rrs::OutcomeKind::Success);
    const auto latest = modules.fares.list_fares(12345, "SL").front();
    REQUIRE(latest.fare_paise == original.fare_paise);
    REQUIRE(latest.record_revision > intermediate.record_revision);
    const auto audit_before_stale_attempt = read_lines(temp.path() / "audit.log");

    const auto stale = modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                                   original.record_revision,
                                                   original.fare_paise, 47000,
                                                   original.effective_from);
    CHECK(stale.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(stale.message.find("changed") != std::string::npos);
    const auto after = modules.fares.list_fares(12345, "SL");
    REQUIRE(after.size() == 1);
    CHECK(after.front().fare_paise == 45000);
    CHECK(after.front().record_revision == latest.record_revision);
    CHECK(read_lines(temp.path() / "audit.log") == audit_before_stale_attempt);
}

TEST_CASE("TC-FARE-01: same-value replacement advances revision and invalidates old selection",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                         "2026-10-01").kind == rrs::OutcomeKind::Success);
    const auto original = modules.fares.list_fares(12345, "SL").front();
    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                       original.record_revision, original.fare_paise,
                                       original.fare_paise, original.effective_from).kind ==
            rrs::OutcomeKind::Success);
    const auto replaced = modules.fares.list_fares(12345, "SL").front();
    CHECK(replaced.record_revision > original.record_revision);
    const auto audit_before_stale_attempt = read_lines(temp.path() / "audit.log");
    const auto stale = modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                                   original.record_revision,
                                                   original.fare_paise, 46000,
                                                   original.effective_from);
    CHECK(stale.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.fares.list_fares(12345, "SL").front().record_revision ==
          replaced.record_revision);
    CHECK(read_lines(temp.path() / "audit.log") == audit_before_stale_attempt);
}

TEST_CASE("TC-FARE-01: moved fare date cannot stale-replace a recreated original key",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                         "2026-10-01").kind == rrs::OutcomeKind::Success);
    const auto original = modules.fares.list_fares(12345, "SL").front();
    REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                       original.record_revision, original.fare_paise, 51000,
                                       "2026-10-20").kind == rrs::OutcomeKind::Success);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 52000,
                                         "2026-10-01").kind == rrs::OutcomeKind::Success);
    const auto after_recreate = modules.fares.list_fares(12345, "SL");
    const auto recreated = std::find_if(after_recreate.begin(), after_recreate.end(),
                                        [](const auto& record) {
                                            return record.effective_from == "2026-10-01";
                                        });
    REQUIRE(recreated != after_recreate.end());
    REQUIRE(recreated->effective_from == "2026-10-01");
    const auto audit_before_stale_attempt = read_lines(temp.path() / "audit.log");
    const auto stale = modules.fares.replace_fare(admin, 12345, "SL", original.effective_from,
                                                   original.record_revision,
                                                   original.fare_paise, 53000,
                                                   "2026-10-01");
    CHECK(stale.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    const auto records = modules.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    const auto current_original_date = std::find_if(records.begin(), records.end(),
                                                    [](const auto& record) {
                                                        return record.effective_from ==
                                                               "2026-10-01";
                                                    });
    const auto moved = std::find_if(records.begin(), records.end(), [](const auto& record) {
        return record.effective_from == "2026-10-20";
    });
    REQUIRE(current_original_date != records.end());
    REQUIRE(moved != records.end());
    CHECK(current_original_date->fare_paise == 52000);
    CHECK(current_original_date->record_revision == recreated->record_revision);
    CHECK(moved->fare_paise == 51000);
    CHECK(read_lines(temp.path() / "audit.log") == audit_before_stale_attempt);
}

TEST_CASE("TC-FARE-01: revisions increase uniquely and persist across reopen",
          "[unit][M1][M7]") {
    TempDirectory temp;
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    std::uint64_t first_revision = 0;
    std::uint64_t second_revision = 0;
    std::uint64_t replacement_revision = 0;
    {
        Modules modules(temp.path());
        add_train_and_class(modules);
        REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                             "2026-10-01").kind == rrs::OutcomeKind::Success);
        REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 50000,
                                             "2026-10-10").kind == rrs::OutcomeKind::Success);
        auto records = modules.fares.list_fares(12345, "SL");
        REQUIRE(records.size() == 2);
        first_revision = records[0].record_revision;
        second_revision = records[1].record_revision;
        REQUIRE(first_revision > 0);
        REQUIRE(second_revision > first_revision);
        REQUIRE(modules.fares.replace_fare(admin, 12345, "SL", records[0].effective_from,
                                           records[0].record_revision, records[0].fare_paise,
                                           46000, records[0].effective_from).kind ==
                rrs::OutcomeKind::Success);
        records = modules.fares.list_fares(12345, "SL");
        replacement_revision = records[0].record_revision;
        CHECK(replacement_revision > second_revision);
    }
    Modules reopened(temp.path());
    const auto records = reopened.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 2);
    CHECK(records[0].record_revision == replacement_revision);
    CHECK(records[1].record_revision == second_revision);
    CHECK(records[0].record_revision != records[1].record_revision);
}

TEST_CASE("TC-FARE-01: revision uses all seven little-endian bytes",
          "[unit][M1][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    constexpr std::uint64_t seeded_revision = 0x01020304050607ULL;
    modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0,
                               raw_fare_record(12345, "SL", 45000, "2026-10-01", 1,
                                               seeded_revision));
    const auto decoded = modules.fares.list_fares(12345, "SL").front();
    CHECK(decoded.record_revision == seeded_revision);
    REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 50000,
                                         "2026-10-10").kind == rrs::OutcomeKind::Success);
    const auto added = modules.fares.list_fares(12345, "SL").back();
    CHECK(added.record_revision == seeded_revision + 1U);
    const auto bytes = modules.store.read_record(rrs::kFaresFile, rrs::kFareRecordSize, 1);
    REQUIRE(bytes.has_value());
    const std::array<unsigned char, 7> expected = {0x08, 0x06, 0x05, 0x04,
                                                  0x03, 0x02, 0x01};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK(static_cast<unsigned char>((*bytes)[25 + index]) == expected[index]);
    }
}

TEST_CASE("TC-FARE-01: invalid fare, date, train and class inputs do not persist or audit success",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    const auto initial_audit_count = read_lines(temp.path() / "audit.log").size();

    CHECK(modules.fares.configure_fare(admin, 12345, "SL", 0, "2026-10-01").kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-09-29").kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.fares.configure_fare(admin, 12345, "SL", 45000, "2026-02-29").kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.fares.configure_fare(admin, 99999, "SL", 45000, "2026-10-01").kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.fares.configure_fare(admin, 12345, "3A", 45000, "2026-10-01").kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.fares.configure_fare(admin, 12345, "XX", 45000, "2026-10-01").kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 0);
    CHECK(read_lines(temp.path() / "audit.log").size() == initial_audit_count);
}

TEST_CASE("TC-FARE-01: today is accepted and an inactive train cannot receive a fare",
          "[unit][M6][M7]") {
    SECTION("today is an inclusive effective date") {
        TempDirectory temp;
        Modules modules(temp.path());
        const rrs::Session admin{"admin", rrs::Role::Administrator};
        add_train_and_class(modules);
        CHECK(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                           "2026-09-30").kind == rrs::OutcomeKind::Success);
        CHECK(modules.fares.configure_fare(admin, 12345, "SL", 50000,
                                           "2026-10-01").kind == rrs::OutcomeKind::Success);
    }
    SECTION("removed train is no longer eligible") {
        TempDirectory temp;
        Modules modules(temp.path());
        const rrs::Session admin{"admin", rrs::Role::Administrator};
        add_train_and_class(modules);
        REQUIRE(modules.trains.remove_train(admin, kTimestamp, 12345).kind ==
                rrs::OutcomeKind::Success);
        CHECK(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                           "2026-10-01").kind ==
              rrs::OutcomeKind::BusinessRuleRefusal);
        CHECK(modules.store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 0);
    }
}

TEST_CASE("TC-FARE-01: absent fare is explicit and lookup rejects invalid references",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    add_train_and_class(modules);

    const auto absent = modules.fares.find_applicable_fare(12345, "SL", "2026-10-01");
    CHECK(absent.state == rrs::FareLookupState::NoApplicableFare);
    CHECK(absent.message == "class not open for booking");
    CHECK_FALSE(absent.record.has_value());
    CHECK(modules.fares.find_applicable_fare(12345, "SL", "2026-02-30").state ==
          rrs::FareLookupState::InvalidJourneyDate);
    CHECK(modules.fares.find_applicable_fare(99999, "SL", "2026-10-01").state ==
          rrs::FareLookupState::UnknownTrain);
    CHECK(modules.fares.find_applicable_fare(12345, "3A", "2026-10-01").state ==
          rrs::FareLookupState::ClassNotConfigured);
    CHECK(modules.fares.find_applicable_fare(12345, "XX", "2026-10-01").state ==
          rrs::FareLookupState::ClassNotConfigured);
}

TEST_CASE("TC-FARE-01: passenger denial is audited once before invalid fare validation",
          "[unit][M5][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session passenger{"rider", rrs::Role::Passenger};
    add_train_and_class(modules);
    REQUIRE(modules.fares.configure_fare(rrs::Session{"admin", rrs::Role::Administrator},
                                         12345, "SL", 45000, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    const auto before = read_lines(temp.path() / "audit.log");

    const auto result = modules.fares.configure_fare(passenger, 12345, "SL", 0,
                                                      "not-a-date");
    const auto replace_result = modules.fares.replace_fare(
        passenger, 12345, "SL", "not-a-date", 1, 45000, 0, "not-a-date");
    CHECK(result.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(result.message == "administrator role required");
    CHECK(replace_result.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(replace_result.message == "administrator role required");
    CHECK(modules.store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 1);
    const auto after = read_lines(temp.path() / "audit.log");
    REQUIRE(after.size() == before.size() + 2);
    CHECK(after.back() ==
          "2026-09-30T10:00:00Z\trider\tPASSENGER\tACCESS_DENIED_MANAGE_FARES\t-");
    CHECK(after[after.size() - 2] == after.back());
    CHECK(std::count_if(after.begin(), after.end(), [](const std::string& line) {
              return line.find("\tFARE_") != std::string::npos;
          }) == 1);
}

TEST_CASE("TC-FARE-01: fare records survive module recreation",
          "[unit][M1][M7]") {
    TempDirectory temp;
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    {
        Modules modules(temp.path());
        add_train_and_class(modules);
        REQUIRE(modules.fares.configure_fare(admin, 12345, "SL", 45000,
                                             "2026-10-01").kind == rrs::OutcomeKind::Success);
    }
    Modules reopened(temp.path());
    const auto records = reopened.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 1);
    CHECK(records[0].fare_paise == 45000);
    CHECK(records[0].effective_from == "2026-10-01");
}

TEST_CASE("TC-FARE-01: AtomicJournal recovery restores a durable fare write",
          "[unit][M1][M7]") {
    TempDirectory temp;
    {
        Modules modules(temp.path());
        add_train_and_class(modules);
        rrs::AtomicJournal interrupted(modules.store);
        CHECK_THROWS_AS(interrupted.commit(
                            {{std::string(rrs::kFaresFile), rrs::kFareRecordSize, 0,
                              raw_fare_record()}},
                            [](rrs::JournalStage stage, std::size_t) {
                                if (stage == rrs::JournalStage::journal_durable) {
                                    throw std::runtime_error("simulated interruption");
                                }
                            }),
                        std::runtime_error);
        CHECK(std::filesystem::exists(temp.path() / ".rrs-atomic-journal"));
        CHECK_FALSE(std::filesystem::exists(temp.path() / rrs::kFaresFile));
    }
    Modules reopened(temp.path());
    const auto records = reopened.fares.list_fares(12345, "SL");
    REQUIRE(records.size() == 1);
    CHECK(records[0].fare_paise == 45000);
    CHECK(records[0].effective_from == "2026-10-01");
    CHECK_FALSE(std::filesystem::exists(temp.path() / ".rrs-atomic-journal"));
    const auto lines = read_lines(temp.path() / "audit.log");
    CHECK(std::none_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("\tFARE_") != std::string::npos;
    }));
}

TEST_CASE("TC-FARE-01: malformed fare records are rejected rather than skipped",
          "[unit][M1][M7]") {
    SECTION("invalid version, fields and reserved bytes") {
        for (int defect = 0; defect < 6; ++defect) {
            TempDirectory temp;
            Modules modules(temp.path());
            add_train_and_class(modules);
            auto bytes = raw_fare_record();
            if (defect == 0) bytes[0] = 2;
            if (defect == 1) std::fill(bytes.begin() + 1, bytes.begin() + 5, '\0');
            if (defect == 2) bytes.replace(5, 2, "XX");
            if (defect == 3) std::fill(bytes.begin() + 7, bytes.begin() + 15, '\0');
            if (defect == 4) bytes.replace(15, 10, "2026-02-30");
            if (defect == 5) std::fill(bytes.begin() + 25, bytes.end(), '\0');
            modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0, bytes);
            CHECK_THROWS_AS(modules.fares.list_fares(12345, "SL"), std::runtime_error);
        }
    }
    SECTION("fare greater than INT64_MAX") {
        TempDirectory temp;
        Modules modules(temp.path());
        add_train_and_class(modules);
        modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0,
                                   raw_fare_record(12345, "SL", 0x8000000000000000ULL));
        CHECK_THROWS_AS(modules.fares.list_fares(12345, "SL"), std::runtime_error);
    }
    SECTION("partial record") {
        TempDirectory temp;
        Modules modules(temp.path());
        add_train_and_class(modules);
        std::ofstream partial(temp.path() / rrs::kFaresFile, std::ios::binary);
        partial << "partial";
        partial.close();
        CHECK_THROWS_AS(modules.fares.list_fares(12345, "SL"), std::runtime_error);
    }
    SECTION("duplicate composite key") {
        TempDirectory temp;
        Modules modules(temp.path());
        add_train_and_class(modules);
        modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0,
                                   raw_fare_record());
        modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 1,
                                   raw_fare_record());
        CHECK_THROWS_AS(modules.fares.list_fares(12345, "SL"), std::runtime_error);
    }
    SECTION("duplicate revisions across different fare keys") {
        TempDirectory temp;
        Modules modules(temp.path());
        add_train_and_class(modules);
        modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0,
                                   raw_fare_record(12345, "SL", 45000, "2026-10-01", 1, 7));
        modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 1,
                                   raw_fare_record(12345, "SL", 50000, "2026-10-10", 1, 7));
        CHECK_THROWS_AS(modules.fares.list_fares(12345, "SL"), std::runtime_error);
    }
}

TEST_CASE("TC-FARE-01: revision overflow refuses mutation without persistence or success audit",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    add_train_and_class(modules);
    constexpr std::uint64_t max_revision = (std::uint64_t{1} << 56U) - 1U;
    modules.store.write_record(rrs::kFaresFile, rrs::kFareRecordSize, 0,
                               raw_fare_record(12345, "SL", 45000, "2026-10-01", 1,
                                               max_revision));
    const auto audit_before = read_lines(temp.path() / "audit.log");
    const auto result = modules.fares.configure_fare(admin, 12345, "SL", 50000,
                                                     "2026-10-10");
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("revision limit") != std::string::npos);
    const auto replacement = modules.fares.replace_fare(admin, 12345, "SL", "2026-10-01",
                                                         max_revision, 45000, 46000,
                                                         "2026-10-01");
    CHECK(replacement.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(replacement.message.find("revision limit") != std::string::npos);
    const auto persisted = modules.fares.list_fares(12345, "SL");
    REQUIRE(persisted.size() == 1);
    CHECK(persisted.front().fare_paise == 45000);
    CHECK(persisted.front().record_revision == max_revision);
    CHECK(read_lines(temp.path() / "audit.log") == audit_before);
}

TEST_CASE("TC-FARE-01: audit append failure reports that fare persistence already succeeded",
          "[unit][M3][M7]") {
    TempDirectory temp;
    const auto audit_path = temp.path() / "audit.log";
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(audit_path);
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    std::filesystem::remove(audit_path);
    std::filesystem::create_directory(audit_path);
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });

    const auto result = fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01");
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("fare was persisted but audit logging failed") !=
          std::string::npos);
    const auto persisted = store.read_record(rrs::kFaresFile, rrs::kFareRecordSize, 0);
    REQUIRE(persisted.has_value());
    CHECK(*persisted == raw_fare_record());
}

TEST_CASE("TC-FARE-01: replacement audit append failure reports the updated persisted fare",
          "[unit][M3][M7]") {
    TempDirectory temp;
    const auto audit_path = temp.path() / "audit.log";
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(audit_path);
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });
    REQUIRE(classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [] { return std::string(kTimestamp); });
    REQUIRE(fares.configure_fare(admin, 12345, "SL", 45000, "2026-10-01").kind ==
            rrs::OutcomeKind::Success);
    std::filesystem::remove(audit_path);
    std::filesystem::create_directory(audit_path);

    const auto selected = fares.list_fares(12345, "SL").front();
    const auto result = fares.replace_fare(admin, 12345, "SL", "2026-10-01",
                                           selected.record_revision, 45000, 55000,
                                           "2026-10-15");
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("fare was persisted but audit logging failed") !=
          std::string::npos);
    const auto persisted = fares.list_fares(12345, "SL");
    REQUIRE(persisted.size() == 1);
    CHECK(persisted[0].fare_paise == 55000);
    CHECK(persisted[0].effective_from == "2026-10-15");
}
