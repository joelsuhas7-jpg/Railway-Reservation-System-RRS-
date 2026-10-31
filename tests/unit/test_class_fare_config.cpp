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
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "audit_log.hpp"
#include "class_fare_config.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";
constexpr std::array<std::string_view, 6> kCodes = {"SL", "3A", "2A", "1A", "CC", "2S"};

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-class-config-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create class test directory",
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

rrs::TrainRecord valid_train(std::uint32_t number = 12345) {
    return {number, "Karnataka Express", "SBC", "NDLS", "06:00", "14:00", 0x7f};
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

std::string raw_class_record(std::uint32_t train_number = 12345,
                             std::string_view code = "SL", std::uint32_t capacity = 72) {
    std::string bytes(rrs::kClassRecordSize, '\0');
    bytes[0] = 1;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        bytes[1 + byte] = static_cast<char>((train_number >> (byte * 8U)) & 0xffU);
        bytes[7 + byte] = static_cast<char>((capacity >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(5, 2, code);
    return bytes;
}

struct Modules {
    explicit Modules(const std::filesystem::path& path)
        : store(path), audit(path / "audit.log"), trains(store, audit),
          timestamp_provider([] { return std::string(kTimestamp); }),
          classes(store, trains, audit, timestamp_provider) {}
    rrs::PersistenceStore store;
    rrs::AuditLog audit;
    rrs::TrainCatalogueModule trains;
    rrs::ClassAuditTimestampProvider timestamp_provider;
    rrs::ClassFareConfigModule classes;
};

}  // namespace

TEST_CASE("TC-CLASS-01: all supported classes persist and known capacities report exactly",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "3A", 64).kind ==
            rrs::OutcomeKind::Success);
    const auto listed = modules.classes.list_classes(12345);
    REQUIRE(listed.train_exists);
    REQUIRE(listed.records.size() == 2);
    CHECK(listed.records[0].class_code == "SL");
    CHECK(listed.records[0].capacity == 72);
    CHECK(listed.records[1].class_code == "3A");
    CHECK(listed.records[1].capacity == 64);
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 2);

    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 3);
    CHECK(lines[1] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tCLASS_ADDED\t12345:SL");
    CHECK(lines[2] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tCLASS_ADDED\t12345:3A");
}

TEST_CASE("TC-CLASS-01: every supported class code is accepted once per train",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    for (const auto code : kCodes) {
        CHECK(modules.classes.add_class(admin, kTimestamp, 12345, code, 1).kind ==
              rrs::OutcomeKind::Success);
    }
    const auto listed = modules.classes.list_classes(12345);
    REQUIRE(listed.train_exists);
    CHECK(listed.records.size() == kCodes.size());
}

TEST_CASE("TC-CLASS-01: unsupported codes and non-positive capacities are rejected",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    for (const auto code : {"", "sl", "Sleeper", "3a", "S/"}) {
        CHECK(modules.classes.add_class(admin, kTimestamp, 12345, code, 1).kind ==
              rrs::OutcomeKind::ValidationFailure);
    }
    for (const std::int64_t capacity : {-1, 0}) {
        CHECK(modules.classes.add_class(admin, kTimestamp, 12345, "SL", capacity).kind ==
              rrs::OutcomeKind::ValidationFailure);
    }
    CHECK(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 4294967296LL).kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 1).kind ==
          rrs::OutcomeKind::Success);
    CHECK(modules.classes.add_class(admin, kTimestamp, 12345, "3A", 4294967295LL).kind ==
          rrs::OutcomeKind::Success);
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 2);
}

TEST_CASE("TC-CLASS-01: duplicate class add is refused and capacity can only increase",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);

    CHECK(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    const auto decrease = modules.classes.update_class(admin, kTimestamp, 12345, "SL", 71);
    CHECK(decrease.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(decrease.message.find("cannot be validated") != std::string::npos);
    REQUIRE(modules.classes.update_class(admin, kTimestamp, 12345, "SL", 80).kind ==
            rrs::OutcomeKind::Success);
    const auto found = modules.classes.find_class(12345, "SL");
    REQUIRE(found.train_exists);
    REQUIRE(found.record.has_value());
    CHECK(found.record->capacity == 80);
    CHECK(read_lines(temp.path() / "audit.log").size() == 3);
}

TEST_CASE("TC-CLASS-01: unknown and soft-removed trains cannot receive class changes",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.trains.remove_train(admin, kTimestamp, 12345).kind ==
            rrs::OutcomeKind::Success);

    CHECK_FALSE(modules.classes.list_classes(99999).train_exists);
    CHECK_FALSE(modules.classes.find_class(99999, "SL").train_exists);
    CHECK(modules.classes.add_class(admin, kTimestamp, 99999, "3A", 64).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.classes.update_class(admin, kTimestamp, 12345, "SL", 80).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK_FALSE(modules.classes.list_classes(12345).train_exists);
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 1);
}

TEST_CASE("TC-CLASS-01: concurrent class adds through separate module instances stay indexed",
          "[unit][M7]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(temp.path() / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    std::array<std::unique_ptr<rrs::ClassFareConfigModule>, 6> modules;
    for (auto& module : modules) {
        module = std::make_unique<rrs::ClassFareConfigModule>(
            store, trains, audit, [] { return std::string(kTimestamp); });
    }
    std::array<rrs::OutcomeKind, 6> results{};
    std::array<std::thread, 6> workers;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index] {
            results[index] = modules[index]->add_class(admin, kTimestamp, 12345,
                                                        kCodes[index], 72).kind;
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (const auto result : results) {
        CHECK(result == rrs::OutcomeKind::Success);
    }
    CHECK(modules[0]->list_classes(12345).records.size() == 6);
    CHECK(read_lines(temp.path() / "audit.log").size() == 7);
}

TEST_CASE("TC-CLASS-01: query distinguishes unknown trains from empty class lists",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    const auto empty = modules.classes.list_classes(12345);
    CHECK(empty.train_exists);
    CHECK(empty.records.empty());
    const auto missing = modules.classes.list_classes(54321);
    CHECK_FALSE(missing.train_exists);
    CHECK(missing.records.empty());
    const auto unknown_code = modules.classes.find_class(12345, "ZZ");
    CHECK(unknown_code.train_exists);
    CHECK_FALSE(unknown_code.record.has_value());
}

TEST_CASE("TC-CLASS-01: class records use the exact 16-byte little-endian layout",
          "[unit][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);
    const auto bytes = modules.store.read_record(rrs::kClassesFile, rrs::kClassRecordSize, 0);
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 16);
    CHECK(static_cast<unsigned char>((*bytes)[0]) == 1);
    CHECK(static_cast<unsigned char>((*bytes)[1]) == 0x39);
    CHECK(static_cast<unsigned char>((*bytes)[2]) == 0x30);
    CHECK(static_cast<unsigned char>((*bytes)[3]) == 0);
    CHECK(static_cast<unsigned char>((*bytes)[4]) == 0);
    CHECK(bytes->substr(5, 2) == "SL");
    CHECK(static_cast<unsigned char>((*bytes)[7]) == 72);
    CHECK(bytes->substr(11, 5) == std::string(5, '\0'));
}

TEST_CASE("TC-CLASS-01: malformed, partial, unsupported and duplicate records are rejected",
          "[unit][M7]") {
    SECTION("bad record fields and reserved bytes") {
        for (int defect = 0; defect < 5; ++defect) {
            TempDirectory temp;
            Modules modules(temp.path());
            REQUIRE(modules.trains.add_train(rrs::Session{"admin", rrs::Role::Administrator},
                                             kTimestamp, valid_train()).kind ==
                    rrs::OutcomeKind::Success);
            auto bytes = raw_class_record();
            if (defect == 0) bytes[0] = 2;
            if (defect == 1) std::fill(bytes.begin() + 1, bytes.begin() + 5, '\0');
            if (defect == 2) bytes.replace(5, 2, "XX");
            if (defect == 3) std::fill(bytes.begin() + 7, bytes.begin() + 11, '\0');
            if (defect == 4) bytes[11] = 1;
            modules.store.write_record(rrs::kClassesFile, rrs::kClassRecordSize, 0, bytes);
            CHECK_THROWS_AS(modules.classes.list_classes(12345), std::runtime_error);
        }
    }
    SECTION("partial file") {
        TempDirectory temp;
        Modules modules(temp.path());
        REQUIRE(modules.trains.add_train(rrs::Session{"admin", rrs::Role::Administrator},
                                         kTimestamp, valid_train()).kind ==
                rrs::OutcomeKind::Success);
        std::ofstream partial(temp.path() / rrs::kClassesFile, std::ios::binary);
        partial << "partial";
        partial.close();
        CHECK_THROWS_AS(modules.classes.list_classes(12345), std::runtime_error);
    }
    SECTION("duplicate pair") {
        TempDirectory temp;
        Modules modules(temp.path());
        REQUIRE(modules.trains.add_train(rrs::Session{"admin", rrs::Role::Administrator},
                                         kTimestamp, valid_train()).kind ==
                rrs::OutcomeKind::Success);
        modules.store.write_record(rrs::kClassesFile, rrs::kClassRecordSize, 0,
                                   raw_class_record());
        modules.store.write_record(rrs::kClassesFile, rrs::kClassRecordSize, 1,
                                   raw_class_record());
        CHECK_THROWS_AS(modules.classes.list_classes(12345), std::runtime_error);
    }
}

TEST_CASE("TC-CLASS-01: passenger mutation is denied and audited exactly once",
          "[unit][M5][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::Session passenger{"rider", rrs::Role::Passenger};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    const auto result = modules.classes.add_class(passenger, kTimestamp, 12345, "SL", 72);
    CHECK(result.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(result.message == "administrator role required");
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 0);
    const auto lines = read_lines(temp.path() / "audit.log");
    REQUIRE(lines.size() == 2);
    CHECK(lines.back() == "2026-10-30T10:00:00Z\trider\tPASSENGER\tACCESS_DENIED_MANAGE_CLASSES\t-");
}

TEST_CASE("TC-CLASS-01: authorization precedes invalid operation timestamps",
          "[unit][M5][M7]") {
    TempDirectory temp;
    Modules modules(temp.path());
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const rrs::Session passenger{"rider", rrs::Role::Passenger};
    REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
            rrs::OutcomeKind::Success);

    const auto denied_add = modules.classes.add_class(passenger, "bad timestamp", 12345, "3A", 64);
    const auto denied_update =
        modules.classes.update_class(passenger, "bad timestamp", 12345, "SL", 80);
    CHECK(denied_add.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(denied_update.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 1);
    const auto existing = modules.classes.find_class(12345, "SL");
    REQUIRE(existing.record.has_value());
    CHECK(existing.record->capacity == 72);

    const auto denied_lines = read_lines(temp.path() / "audit.log");
    REQUIRE(denied_lines.size() == 4);
    CHECK(denied_lines[2] ==
          "2026-10-30T10:00:00Z\trider\tPASSENGER\tACCESS_DENIED_MANAGE_CLASSES\t-");
    CHECK(denied_lines[3] == denied_lines[2]);

    const auto rejected_add = modules.classes.add_class(admin, "bad timestamp", 12345, "3A", 64);
    const auto rejected_update =
        modules.classes.update_class(admin, "bad timestamp", 12345, "SL", 80);
    CHECK(rejected_add.kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(rejected_update.kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(modules.store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 1);
    const auto unchanged = modules.classes.find_class(12345, "SL");
    REQUIRE(unchanged.record.has_value());
    CHECK(unchanged.record->capacity == 72);
    const auto final_lines = read_lines(temp.path() / "audit.log");
    CHECK(final_lines == denied_lines);
    CHECK(std::count_if(final_lines.begin(), final_lines.end(), [](const std::string& line) {
              return line.find("\tCLASS_") != std::string::npos;
          }) == 1);
}

TEST_CASE("TC-CLASS-01: persisted class survives module recreation",
          "[unit][M1][M7]") {
    TempDirectory temp;
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    {
        Modules modules(temp.path());
        REQUIRE(modules.trains.add_train(admin, kTimestamp, valid_train()).kind ==
                rrs::OutcomeKind::Success);
        REQUIRE(modules.classes.add_class(admin, kTimestamp, 12345, "SL", 72).kind ==
                rrs::OutcomeKind::Success);
    }
    Modules reopened(temp.path());
    const auto result = reopened.classes.find_class(12345, "SL");
    REQUIRE(result.train_exists);
    REQUIRE(result.record.has_value());
    CHECK(result.record->capacity == 72);
}

TEST_CASE("TC-CLASS-01: audit append failure reports the persisted class mutation",
          "[unit][M3][M7]") {
    TempDirectory temp;
    const auto audit_path = temp.path() / "audit.log";
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit(audit_path);
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(trains.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    std::filesystem::remove(audit_path);
    std::filesystem::create_directory(audit_path);
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [] { return std::string(kTimestamp); });

    const auto result = classes.add_class(admin, kTimestamp, 12345, "SL", 72);
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("class mutation persisted but audit logging failed") !=
          std::string::npos);
    const auto record = store.read_record(rrs::kClassesFile, rrs::kClassRecordSize, 0);
    CHECK(record == raw_class_record());
}

TEST_CASE("TC-CLASS-01: update audit append failure reports the persisted capacity",
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

    const auto result = classes.update_class(admin, kTimestamp, 12345, "SL", 80);
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("class mutation persisted but audit logging failed") !=
          std::string::npos);
    const auto persisted = classes.find_class(12345, "SL");
    REQUIRE(persisted.record.has_value());
    CHECK(persisted.record->capacity == 80);
}
