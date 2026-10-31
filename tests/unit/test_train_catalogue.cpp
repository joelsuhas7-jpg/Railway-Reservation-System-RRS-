#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include "audit_log.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace {

constexpr std::string_view kTimestamp = "2026-10-30T10:00:00Z";
constexpr std::string_view kTrainFile = "trains.dat";

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("rrs-train-catalogue-" + std::to_string(nonce) + "-" +
                     std::to_string(next_id.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error("cannot create train test directory",
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

std::string raw_train_record(std::uint32_t number) {
    std::string record(rrs::kTrainRecordSize, '\0');
    record[0] = 1;
    record[1] = 1;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        record[2 + byte] = static_cast<char>((number >> (byte * 8U)) & 0xffU);
    }
    record.replace(6, 40, 40, ' ');
    record.replace(6, 4, "Test");
    record.replace(46, 8, 8, ' ');
    record.replace(46, 3, "SBC");
    record.replace(54, 8, 8, ' ');
    record.replace(54, 4, "NDLS");
    record.replace(62, 5, "06:00");
    record.replace(67, 5, "14:00");
    record[72] = 0x7f;
    return record;
}

class StartGate {
public:
    explicit StartGate(std::size_t participants) : participants_(participants) {}

    void wait_until_released() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++ready_;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }

    void release_when_ready() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return ready_ == participants_; });
        released_ = true;
        condition_.notify_all();
    }

private:
    std::size_t participants_;
    std::size_t ready_{0};
    bool released_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
};

}  // namespace

TEST_CASE("TC-ADMIN-01: train records use the approved 128-byte format and round-trip",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};

    const auto result = catalogue.add_train(admin, kTimestamp, valid_train(0x12345678));
    REQUIRE(result.kind == rrs::OutcomeKind::Success);

    const auto record = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    REQUIRE(record.has_value());
    REQUIRE(record->size() == 128);
    CHECK(static_cast<unsigned char>((*record)[0]) == 1);
    CHECK(static_cast<unsigned char>((*record)[1]) == 1);
    CHECK(static_cast<unsigned char>((*record)[2]) == 0x78);
    CHECK(static_cast<unsigned char>((*record)[3]) == 0x56);
    CHECK(static_cast<unsigned char>((*record)[4]) == 0x34);
    CHECK(static_cast<unsigned char>((*record)[5]) == 0x12);
    CHECK(record->substr(6, 40) == "Karnataka Express" + std::string(23, ' '));
    CHECK(record->substr(46, 8) == "SBC     ");
    CHECK(record->substr(54, 8) == "NDLS    ");
    CHECK(record->substr(62, 5) == "06:00");
    CHECK(record->substr(67, 5) == "14:00");
    CHECK(static_cast<unsigned char>((*record)[72]) == 0x7f);
    CHECK(std::all_of(record->begin() + 73, record->end(),
                      [](char byte) { return byte == '\0'; }));

    const auto round_trip = catalogue.find_train(0x12345678);
    REQUIRE(round_trip.has_value());
    CHECK(round_trip->train_number == 0x12345678);
    CHECK(round_trip->name == "Karnataka Express");
    CHECK(round_trip->source_station == "SBC");
    CHECK(round_trip->destination_station == "NDLS");
    CHECK(round_trip->departure_time == "06:00");
    CHECK(round_trip->arrival_time == "14:00");
    CHECK(round_trip->running_days_mask == 0x7f);
    CHECK(read_lines(temp.path() / "audit.log") == std::vector<std::string>{
              "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t305419896"});
}

TEST_CASE("TC-ADMIN-01: add rejects duplicate and invalid train data without writing",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(catalogue.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);

    CHECK(catalogue.add_train(admin, kTimestamp, valid_train()).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);

    auto invalid = valid_train(12346);
    invalid.name.clear();
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.name = std::string(41, 'N');
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.name = " Leading";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.name = "Trailing ";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.name = "Name\twith tab";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.name = "Route \xc3\xa9";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.source_station = "sbc";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.destination_station = "SBC";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.departure_time = "24:00";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.arrival_time = "12:60";
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.running_days_mask = 0;
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(12346);
    invalid.running_days_mask = 0x80;
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    invalid = valid_train(0);
    CHECK(catalogue.add_train(admin, kTimestamp, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);

    CHECK(store.record_count(kTrainFile, rrs::kTrainRecordSize) == 1);
    CHECK(catalogue.list_active_trains().size() == 1);
    const auto audit = read_lines(temp.path() / "audit.log");
    REQUIRE(audit.size() == 1);
    CHECK(audit[0].find("TRAIN_ADDED") != std::string::npos);
}

TEST_CASE("TC-ADMIN-01: removed train IDs stay reserved and removed records survive reopen",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    {
        rrs::TrainCatalogueModule catalogue(store, audit_log);
        REQUIRE(catalogue.add_train(admin, kTimestamp, valid_train()).kind ==
                rrs::OutcomeKind::Success);
        CHECK(catalogue.remove_train(admin, kTimestamp, 12345).kind ==
              rrs::OutcomeKind::Success);
        CHECK_FALSE(catalogue.find_train(12345).has_value());
        CHECK(catalogue.list_active_trains().empty());
        CHECK(catalogue.add_train(admin, kTimestamp, valid_train()).kind ==
              rrs::OutcomeKind::BusinessRuleRefusal);
        CHECK(catalogue.remove_train(admin, kTimestamp, 12345).kind ==
              rrs::OutcomeKind::BusinessRuleRefusal);
        CHECK(read_lines(temp.path() / "audit.log").size() == 2);
    }
    rrs::TrainCatalogueModule reopened(store, audit_log);
    CHECK_FALSE(reopened.find_train(12345).has_value());
    CHECK(store.record_count(kTrainFile, rrs::kTrainRecordSize) == 1);
    const auto record = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    REQUIRE(record.has_value());
    CHECK(static_cast<unsigned char>((*record)[1]) == 0);
    const auto audit = read_lines(temp.path() / "audit.log");
    REQUIRE(audit.size() == 2);
    CHECK(audit[0].find("TRAIN_ADDED") != std::string::npos);
    CHECK(audit[1].find("TRAIN_REMOVED") != std::string::npos);
}

TEST_CASE("TC-ADMIN-01: accepted train names round-trip without trimming",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    auto train = valid_train(12346);
    train.name = "North Central Express";

    REQUIRE(catalogue.add_train(rrs::Session{"admin", rrs::Role::Administrator}, kTimestamp,
                                train)
                .kind == rrs::OutcomeKind::Success);
    const auto loaded = catalogue.find_train(12346);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == train.name);
}

TEST_CASE("TC-ADMIN-01: active trains remain available after catalogue reopen",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    {
        rrs::TrainCatalogueModule catalogue(store, audit_log);
        REQUIRE(catalogue.add_train(rrs::Session{"admin", rrs::Role::Administrator}, kTimestamp,
                                    valid_train())
                    .kind == rrs::OutcomeKind::Success);
    }
    rrs::TrainCatalogueModule reopened(store, audit_log);
    const auto loaded = reopened.find_train(12345);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "Karnataka Express");
    REQUIRE(reopened.list_active_trains().size() == 1);
    CHECK(reopened.list_active_trains()[0].train_number == 12345);
}

TEST_CASE("TC-ADMIN-01: timestamps require a real UTC date and time before mutation",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    const std::vector<std::string> invalid_timestamps = {
        "2026-02-29T10:00:00Z", "2024-02-30T10:00:00Z", "2026-00-01T10:00:00Z",
        "2026-13-01T10:00:00Z", "2026-10-30T24:00:00Z", "2026-10-30T10:60:00Z",
        "2026-10-30T10:00:60Z", "2026/10/30T10:00:00Z", "2026-10-30T10:00Z"};
    for (std::size_t index = 0; index < invalid_timestamps.size(); ++index) {
        auto train = valid_train(static_cast<std::uint32_t>(20000 + index));
        CHECK(catalogue.add_train(admin, invalid_timestamps[index], train).kind ==
              rrs::OutcomeKind::ValidationFailure);
    }
    CHECK(store.record_count(kTrainFile, rrs::kTrainRecordSize) == 0);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "audit.log"));

    REQUIRE(catalogue.add_train(admin, "2024-02-29T23:59:59Z", valid_train()).kind ==
            rrs::OutcomeKind::Success);
    const auto before = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    REQUIRE(before.has_value());
    CHECK(catalogue.update_train(admin, "2024-02-30T10:00:00Z", 12345,
                                 valid_train())
              .kind == rrs::OutcomeKind::ValidationFailure);
    CHECK(catalogue.remove_train(admin, "not-a-timestamp", 12345).kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(store.read_record(kTrainFile, rrs::kTrainRecordSize, 0) == before);
    CHECK(catalogue.find_train(12345).has_value());
    CHECK(read_lines(temp.path() / "audit.log").size() == 1);
}

TEST_CASE("TC-ADMIN-01: update audit failure reports the persisted updated train",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog working_audit(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    {
        rrs::TrainCatalogueModule seed(store, working_audit);
        REQUIRE(seed.add_train(admin, kTimestamp, valid_train()).kind == rrs::OutcomeKind::Success);
    }
    const rrs::AuditLog failing_audit(temp.path() / "missing-directory" / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, failing_audit);
    auto updated = valid_train();
    updated.name = "Persisted Update";
    const auto result = catalogue.update_train(admin, kTimestamp, 12345, updated);
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("train mutation persisted but audit logging failed") !=
          std::string::npos);
    REQUIRE(catalogue.find_train(12345).has_value());
    CHECK(catalogue.find_train(12345)->name == "Persisted Update");
    CHECK(read_lines(temp.path() / "audit.log").size() == 1);
}

TEST_CASE("TC-ADMIN-01: remove audit failure reports the persisted removal",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog working_audit(temp.path() / "audit.log");
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    {
        rrs::TrainCatalogueModule seed(store, working_audit);
        REQUIRE(seed.add_train(admin, kTimestamp, valid_train()).kind == rrs::OutcomeKind::Success);
    }
    const rrs::AuditLog failing_audit(temp.path() / "missing-directory" / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, failing_audit);
    const auto result = catalogue.remove_train(admin, kTimestamp, 12345);
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("train mutation persisted but audit logging failed") !=
          std::string::npos);
    CHECK_FALSE(catalogue.find_train(12345).has_value());
    const auto record = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    REQUIRE(record.has_value());
    CHECK(static_cast<unsigned char>((*record)[1]) == 0);
    CHECK(read_lines(temp.path() / "audit.log").size() == 1);
}

TEST_CASE("TC-ADMIN-01: catalogue rejects a 2,001st stored record",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    const std::string record = raw_train_record(1);
    for (std::size_t index = 0; index < rrs::kMaximumTrainRecords; ++index) {
        auto unique_record = record;
        const std::uint32_t number = static_cast<std::uint32_t>(index + 1);
        for (unsigned int byte = 0; byte < 4; ++byte) {
            unique_record[2 + byte] = static_cast<char>((number >> (byte * 8U)) & 0xffU);
        }
        store.write_record(kTrainFile, rrs::kTrainRecordSize, index, unique_record);
    }
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const auto result = catalogue.add_train(
        rrs::Session{"admin", rrs::Role::Administrator}, kTimestamp, valid_train(3000));
    CHECK(result.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(store.record_count(kTrainFile, rrs::kTrainRecordSize) == rrs::kMaximumTrainRecords);
    CHECK(catalogue.list_active_trains().size() == rrs::kMaximumTrainRecords);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "audit.log"));

    store.write_record(kTrainFile, rrs::kTrainRecordSize, rrs::kMaximumTrainRecords,
                       raw_train_record(2001));
    CHECK_THROWS_AS(catalogue.list_active_trains(), std::runtime_error);
}

TEST_CASE("TC-ADMIN-01: multiple trains remain independently readable",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    auto first = valid_train(10001);
    auto second = valid_train(10002);
    second.name = "Second Express";
    REQUIRE(catalogue.add_train(admin, kTimestamp, first).kind == rrs::OutcomeKind::Success);
    REQUIRE(catalogue.add_train(admin, kTimestamp, second).kind == rrs::OutcomeKind::Success);
    CHECK(catalogue.find_train(10001)->name == "Karnataka Express");
    CHECK(catalogue.find_train(10002)->name == "Second Express");
    CHECK(catalogue.list_active_trains().size() == 2);
}

TEST_CASE("TC-ADMIN-01: audit append failure reports persisted mutation as system failure",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "missing-directory" / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const auto result = catalogue.add_train(
        rrs::Session{"admin", rrs::Role::Administrator}, kTimestamp, valid_train());
    CHECK(result.kind == rrs::OutcomeKind::SystemFailure);
    CHECK(result.message.find("train mutation persisted but audit logging failed") !=
          std::string::npos);
    CHECK(catalogue.find_train(12345).has_value());
}

TEST_CASE("TC-ADMIN-01: update is immutable, validates before writing and refuses inactive IDs",
          "[unit][M6]") {
    TempDirectory temp;
    rrs::PersistenceStore store(temp.path());
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    rrs::TrainCatalogueModule catalogue(store, audit_log);
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    REQUIRE(catalogue.add_train(admin, kTimestamp, valid_train()).kind ==
            rrs::OutcomeKind::Success);
    const auto original = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    REQUIRE(original.has_value());

    auto changed = valid_train();
    changed.name = "Updated Express";
    changed.departure_time = "23:59";
    changed.arrival_time = "00:10";
    changed.running_days_mask = 0x60;
    CHECK(catalogue.update_train(admin, kTimestamp, 12345, changed).kind ==
          rrs::OutcomeKind::Success);
    auto found = catalogue.find_train(12345);
    REQUIRE(found.has_value());
    CHECK(found->name == "Updated Express");
    CHECK(found->departure_time == "23:59");
    CHECK(found->arrival_time == "00:10");
    CHECK(found->running_days_mask == 0x60);

    auto number_change = valid_train(54321);
    CHECK(catalogue.update_train(admin, kTimestamp, 12345, number_change).kind ==
          rrs::OutcomeKind::ValidationFailure);
    auto invalid = valid_train();
    invalid.source_station = "bad code";
    const auto before_invalid = store.read_record(kTrainFile, rrs::kTrainRecordSize, 0);
    CHECK(catalogue.update_train(admin, kTimestamp, 12345, invalid).kind ==
          rrs::OutcomeKind::ValidationFailure);
    CHECK(store.read_record(kTrainFile, rrs::kTrainRecordSize, 0) == before_invalid);
    CHECK(catalogue.update_train(admin, kTimestamp, 54321, valid_train(54321)).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(catalogue.update_train(admin, kTimestamp, 0, valid_train(0)).kind ==
          rrs::OutcomeKind::ValidationFailure);

    REQUIRE(catalogue.remove_train(admin, kTimestamp, 12345).kind == rrs::OutcomeKind::Success);
    CHECK(catalogue.update_train(admin, kTimestamp, 12345, changed).kind ==
          rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(read_lines(temp.path() / "audit.log").size() == 3);
}

TEST_CASE("TC-ADMIN-01: malformed and duplicate records fail catalogue reads",
          "[unit][M6]") {
    SECTION("unsupported version, status, reserved bytes, and duplicate identity") {
        const auto assert_corrupt = [](const std::string& malformed) {
            TempDirectory temp;
            rrs::PersistenceStore store(temp.path());
            store.write_record(kTrainFile, rrs::kTrainRecordSize, 0, raw_train_record(1));
            store.write_record(kTrainFile, rrs::kTrainRecordSize, 0, malformed);
            const rrs::AuditLog audit_log(temp.path() / "audit.log");
            rrs::TrainCatalogueModule catalogue(store, audit_log);
            CHECK_THROWS_AS(catalogue.list_active_trains(), std::runtime_error);
        };

        auto unsupported_version = raw_train_record(1);
        unsupported_version[0] = 2;
        assert_corrupt(unsupported_version);
        auto invalid_status = raw_train_record(1);
        invalid_status[1] = 2;
        assert_corrupt(invalid_status);
        assert_corrupt(raw_train_record(0));
        auto invalid_text_padding = raw_train_record(1);
        invalid_text_padding[10] = '\0';
        assert_corrupt(invalid_text_padding);
        auto invalid_time = raw_train_record(1);
        invalid_time.replace(62, 5, "25:00");
        assert_corrupt(invalid_time);
        auto invalid_mask = raw_train_record(1);
        invalid_mask[72] = static_cast<char>(0x80);
        assert_corrupt(invalid_mask);
        auto reserved_byte = raw_train_record(1);
        reserved_byte[127] = 1;
        assert_corrupt(reserved_byte);

        TempDirectory temp;
        rrs::PersistenceStore store(temp.path());
        const auto duplicate = raw_train_record(1);
        store.write_record(kTrainFile, rrs::kTrainRecordSize, 0, duplicate);
        store.write_record(kTrainFile, rrs::kTrainRecordSize, 1, duplicate);
        rrs::TrainCatalogueModule catalogue(store, rrs::AuditLog(temp.path() / "audit.log"));
        CHECK_THROWS_AS(catalogue.list_active_trains(), std::runtime_error);
    }

    SECTION("partial file") {
        TempDirectory temp;
        {
            std::ofstream malformed(temp.path() / kTrainFile, std::ios::binary);
            malformed.put('x');
        }
        rrs::PersistenceStore store(temp.path());
        const rrs::AuditLog audit_log(temp.path() / "audit.log");
        rrs::TrainCatalogueModule catalogue(store, audit_log);
        CHECK_THROWS_AS(catalogue.list_active_trains(), std::runtime_error);
    }
}

TEST_CASE("TC-ADMIN-01: concurrent distinct additions preserve every train and audit",
          "[unit][M6]") {
    constexpr std::size_t kThreadCount = 12;
    TempDirectory temp;
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    std::vector<std::unique_ptr<rrs::TrainCatalogueModule>> catalogues;
    catalogues.reserve(kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        catalogues.push_back(std::make_unique<rrs::TrainCatalogueModule>(
            rrs::PersistenceStore(temp.path()), audit_log));
    }
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    StartGate gate(kThreadCount);
    std::vector<rrs::OutcomeKind> outcomes(kThreadCount, rrs::OutcomeKind::SystemFailure);
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        workers.emplace_back([&, index] {
            gate.wait_until_released();
            outcomes[index] = catalogues[index]->add_train(
                admin, kTimestamp,
                valid_train(static_cast<std::uint32_t>(30000 + index))).kind;
        });
    }
    gate.release_when_ready();
    for (auto& worker : workers) {
        worker.join();
    }

    for (const auto outcome : outcomes) {
        CHECK(outcome == rrs::OutcomeKind::Success);
    }
    const auto trains = catalogues.front()->list_active_trains();
    REQUIRE(trains.size() == kThreadCount);
    std::unordered_set<std::uint32_t> train_numbers;
    for (const auto& train : trains) {
        train_numbers.insert(train.train_number);
    }
    CHECK(train_numbers.size() == kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        CHECK(train_numbers.count(static_cast<std::uint32_t>(30000 + index)) == 1);
    }

    const auto audit = read_lines(temp.path() / "audit.log");
    REQUIRE(audit.size() == kThreadCount);
    std::unordered_set<std::string> audit_records;
    for (const auto& line : audit) {
        const auto separator = line.rfind('\t');
        REQUIRE(separator != std::string::npos);
        audit_records.insert(line.substr(separator + 1));
        CHECK(line.find("\tadmin\tADMIN\tTRAIN_ADDED\t") != std::string::npos);
    }
    CHECK(audit_records.size() == kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        CHECK(audit_records.count(std::to_string(30000 + index)) == 1);
    }
}

TEST_CASE("TC-ADMIN-01: concurrent duplicate additions yield one record and one audit",
          "[unit][M6]") {
    constexpr std::size_t kThreadCount = 12;
    TempDirectory temp;
    const rrs::AuditLog audit_log(temp.path() / "audit.log");
    std::vector<std::unique_ptr<rrs::TrainCatalogueModule>> catalogues;
    catalogues.reserve(kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        catalogues.push_back(std::make_unique<rrs::TrainCatalogueModule>(
            rrs::PersistenceStore(temp.path()), audit_log));
    }
    const rrs::Session admin{"admin", rrs::Role::Administrator};
    StartGate gate(kThreadCount);
    std::vector<rrs::OutcomeKind> outcomes(kThreadCount, rrs::OutcomeKind::SystemFailure);
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);
    for (std::size_t index = 0; index < kThreadCount; ++index) {
        workers.emplace_back([&, index] {
            gate.wait_until_released();
            outcomes[index] = catalogues[index]->add_train(admin, kTimestamp, valid_train()).kind;
        });
    }
    gate.release_when_ready();
    for (auto& worker : workers) {
        worker.join();
    }

    std::size_t successes = 0;
    std::size_t refusals = 0;
    for (const auto outcome : outcomes) {
        successes += outcome == rrs::OutcomeKind::Success ? 1U : 0U;
        refusals += outcome == rrs::OutcomeKind::BusinessRuleRefusal ? 1U : 0U;
    }
    CHECK(successes == 1);
    CHECK(refusals == kThreadCount - 1);
    const auto trains = catalogues.front()->list_active_trains();
    REQUIRE(trains.size() == 1);
    CHECK(trains[0].train_number == 12345);
    const auto audit = read_lines(temp.path() / "audit.log");
    REQUIRE(audit.size() == 1);
    CHECK(audit[0] == "2026-10-30T10:00:00Z\tadmin\tADMIN\tTRAIN_ADDED\t12345");
}
