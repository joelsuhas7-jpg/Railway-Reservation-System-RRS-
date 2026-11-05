#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

#include "audit_log.hpp"
#include "fixture_generator.hpp"
#include "persistence.hpp"

namespace {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto prefix = "rrs-enb06-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    (prefix + "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::filesystem::filesystem_error(
                    "cannot create ENB-06 temporary directory", path_, error);
            }
        }
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void refresh_manifest_counts(rrs::GeneratedFixturePlan& plan) {
    plan.manifest.train_count = plan.trains.size();
    plan.manifest.class_count = 0;
    plan.manifest.fare_count = 0;
    for (const auto& generated : plan.trains) {
        plan.manifest.class_count += generated.classes.size();
        for (const auto& class_fare : generated.classes) {
            (void)class_fare;
            ++plan.manifest.fare_count;
        }
    }
    plan.manifest.fingerprint = rrs::fixture_fingerprint(plan);
}

void copy_fixture_catalogues(const std::filesystem::path& source,
                             const std::filesystem::path& destination) {
    std::filesystem::create_directory(destination);
    for (const auto file : {rrs::kTrainsFile, rrs::kClassesFile, rrs::kFaresFile}) {
        const std::string file_name(file);
        std::filesystem::copy_file(source / file_name, destination / file_name);
    }
}

}  // namespace

TEST_CASE("ENB-06-05: failed materialization never returns a verified handle",
          "[integration][ENB-06]") {
    auto plan = rrs::generate_fixture_plan(1);
    plan.trains.resize(1);
    plan.trains.front().classes.resize(1);
    refresh_manifest_counts(plan);
    TempDirectory temp;
    const auto occupied = temp.path() / "occupied";
    std::filesystem::create_directory(occupied);
    std::ofstream marker(occupied / "existing.dat");
    marker << "preserve";
    marker.close();
    const auto result = rrs::materialize_fixture(plan, occupied);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.verified.has_value());
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("ENB-06-06: small multi-class fixtures materialize and checker detects corruption",
          "[integration][ENB-06][M6][M7]") {
    const auto generated = rrs::generate_fixture_plan(UINT64_C(20261105));
    rrs::GeneratedFixturePlan plan;
    plan.manifest = generated.manifest;
    std::set<std::uint32_t> selected_train_numbers;
    std::array<rrs::GeneratedTrain, 3> class_sources;
    const std::array<std::size_t, 3> desired_class_counts = {1, 3, 6};
    for (std::size_t selected_index = 0; selected_index < desired_class_counts.size();
         ++selected_index) {
        const auto desired_class_count = desired_class_counts[selected_index];
        const auto minimum_source_count = desired_class_count == 1 ? 2 : desired_class_count;
        bool found_train = false;
        for (const auto& candidate : generated.trains) {
            if (candidate.classes.size() < minimum_source_count ||
                selected_train_numbers.find(candidate.train.train_number) !=
                    selected_train_numbers.end()) {
                continue;
            }
            selected_train_numbers.insert(candidate.train.train_number);
            class_sources[selected_index] = candidate;
            auto selected = candidate;
            selected.classes.resize(desired_class_count);
            plan.trains.push_back(std::move(selected));
            found_train = true;
            break;
        }
        REQUIRE(found_train);
    }
    refresh_manifest_counts(plan);

    TempDirectory temp;
    const auto result = rrs::materialize_fixture(plan, temp.path() / "multi-class");
    INFO(result.error);
    for (const auto& violation : result.report.violations) {
        INFO(violation);
    }
    REQUIRE(result.ok());
    REQUIRE(result.verified.has_value());
    CHECK(result.verified->manifest().train_count == 3);
    CHECK(result.verified->manifest().class_count == 10);
    CHECK(result.verified->manifest().fare_count == 10);

    const auto manifest_path = result.verified->data_directory() / rrs::kFixtureManifestFile;
    std::ifstream manifest_file(manifest_path, std::ios::binary);
    REQUIRE(manifest_file.good());
    const std::string persisted_manifest(
        (std::istreambuf_iterator<char>(manifest_file)), std::istreambuf_iterator<char>());
    CHECK(persisted_manifest == rrs::format_fixture_manifest(plan.manifest));

    rrs::PersistenceStore store(result.verified->data_directory());
    rrs::AuditLog audit(result.verified->data_directory() / "audit.log");
    rrs::TrainCatalogueModule trains(store, audit);
    const rrs::Session admin{"fixture-admin", rrs::Role::Administrator};
    const std::string timestamp = plan.manifest.timestamp;
    rrs::ClassFareConfigModule classes(store, trains, audit,
                                       [timestamp] { return timestamp; });
    rrs::FareConfigModule fares(store, trains, classes, audit,
                                [timestamp] { return timestamp; });
    CHECK(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());

    for (const auto& expected_train : plan.trains) {
        const auto actual_classes = classes.list_classes(expected_train.train.train_number);
        REQUIRE(actual_classes.train_exists);
        REQUIRE(actual_classes.records.size() == expected_train.classes.size());
        for (std::size_t index = 0; index < expected_train.classes.size(); ++index) {
            const auto& expected = expected_train.classes[index];
            CHECK(actual_classes.records[index].class_code == expected.class_code);
            CHECK(actual_classes.records[index].capacity == expected.capacity);

            const auto actual_fares =
                fares.list_fares(expected_train.train.train_number, expected.class_code);
            REQUIRE(actual_fares.size() == 1);
            CHECK(actual_fares.front().fare_paise == expected.fare_paise);
            CHECK(actual_fares.front().effective_from == expected.effective_from);

            const auto applicable = fares.find_applicable_fare(
                expected_train.train.train_number, expected.class_code, expected.effective_from);
            CHECK(applicable.state == rrs::FareLookupState::Found);
            REQUIRE(applicable.record.has_value());
            CHECK(applicable.record->fare_paise == expected.fare_paise);
        }
    }
    CHECK(store.record_count(rrs::kTrainsFile, rrs::kTrainRecordSize) == 3);
    CHECK(store.record_count(rrs::kClassesFile, rrs::kClassRecordSize) == 10);
    CHECK(store.record_count(rrs::kFaresFile, rrs::kFareRecordSize) == 10);

    const auto check_copy = [&](const auto& corrupt) {
        static std::size_t copy_sequence = 0;
        const auto path = temp.path() / ("checker-copy-" + std::to_string(copy_sequence++));
        copy_fixture_catalogues(result.verified->data_directory(), path);
        corrupt(path);
        rrs::PersistenceStore copy_store(path);
        rrs::AuditLog copy_audit(path / "audit.log");
        rrs::TrainCatalogueModule copy_trains(copy_store, copy_audit);
        rrs::ClassFareConfigModule copy_classes(copy_store, copy_trains, copy_audit,
                                                [timestamp] { return timestamp; });
        rrs::FareConfigModule copy_fares(copy_store, copy_trains, copy_classes, copy_audit,
                                         [timestamp] { return timestamp; });
        return rrs::check_generated_fixture(plan, copy_store, copy_trains,
                                            copy_classes, copy_fares);
    };

    auto missing_train = plan;
    auto absent_train = plan.trains.front();
    absent_train.train.train_number = 900001;
    absent_train.train.name = "Expected absent train";
    missing_train.trains.push_back(absent_train);
    refresh_manifest_counts(missing_train);
    CHECK_FALSE(rrs::check_generated_fixture(missing_train, store, trains, classes, fares).ok());

    auto wrong_train_fields = plan;
    wrong_train_fields.trains.front().train.name += " altered";
    refresh_manifest_counts(wrong_train_fields);
    CHECK_FALSE(rrs::check_generated_fixture(wrong_train_fields, store, trains,
                                              classes, fares).ok());

    auto missing_class = plan;
    missing_class.trains.front().classes.push_back(class_sources.front().classes[1]);
    refresh_manifest_counts(missing_class);
    CHECK_FALSE(rrs::check_generated_fixture(missing_class, store, trains,
                                              classes, fares).ok());

    auto unexpected_class = plan;
    unexpected_class.trains[1].classes.pop_back();
    refresh_manifest_counts(unexpected_class);
    CHECK_FALSE(rrs::check_generated_fixture(unexpected_class, store, trains,
                                              classes, fares).ok());

    auto bad_fingerprint = plan;
    bad_fingerprint.manifest.fingerprint ^= UINT64_C(1);
    CHECK_FALSE(rrs::check_generated_fixture(bad_fingerprint, store, trains,
                                              classes, fares).ok());

    CHECK_FALSE(check_copy([&](const auto& path) {
        const auto fare_path = path / rrs::kFaresFile;
        std::filesystem::resize_file(
            fare_path, std::filesystem::file_size(fare_path) - rrs::kFareRecordSize);
    }).ok());

    CHECK_FALSE(check_copy([&](const auto& path) {
        std::ifstream input(path / rrs::kClassesFile, std::ios::binary);
        REQUIRE(input.good());
        std::string first_class(rrs::kClassRecordSize, '\0');
        input.read(first_class.data(), static_cast<std::streamsize>(first_class.size()));
        REQUIRE(input.good());
        std::ofstream output(path / rrs::kClassesFile,
                             std::ios::binary | std::ios::app);
        REQUIRE(output.good());
        output.write(first_class.data(), static_cast<std::streamsize>(first_class.size()));
        REQUIRE(output.good());
    }).ok());

    CHECK_FALSE(check_copy([&](const auto& path) {
        std::fstream fare_file(path / rrs::kFaresFile,
                               std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(fare_file.good());
        const auto changed_fare = static_cast<std::uint64_t>(
            plan.trains.front().classes.front().fare_paise + 1);
        fare_file.seekp(7);
        for (unsigned int byte = 0; byte < 8; ++byte) {
            fare_file.put(static_cast<char>((changed_fare >> (byte * 8U)) & 0xffU));
        }
        REQUIRE(fare_file.good());
    }).ok());

    CHECK_FALSE(check_copy([&](const auto& path) {
        std::fstream fare_file(path / rrs::kFaresFile,
                               std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(fare_file.good());
        fare_file.seekp(static_cast<std::streamoff>(rrs::kFareRecordSize + 25));
        for (unsigned int byte = 0; byte < 7; ++byte) {
            fare_file.put(static_cast<char>((UINT64_C(1) >> (byte * 8U)) & 0xffU));
        }
        REQUIRE(fare_file.good());
    }).ok());

    CHECK_FALSE(check_copy([&](const auto& path) {
        std::ofstream partial(path / rrs::kTrainsFile,
                             std::ios::binary | std::ios::app);
        REQUIRE(partial.good());
        partial.put('x');
        REQUIRE(partial.good());
    }).ok());

    const auto first_train = plan.trains.front().train;
    auto changed_train = first_train;
    changed_train.name += " updated";
    REQUIRE(trains.update_train(admin, timestamp, first_train.train_number,
                                changed_train).kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());
    REQUIRE(trains.update_train(admin, timestamp, first_train.train_number,
                                first_train).kind == rrs::OutcomeKind::Success);

    const auto& first_class = plan.trains.front().classes.front();
    const auto changed_capacity = classes.update_class(
        admin, timestamp, first_train.train_number, first_class.class_code,
        static_cast<std::int64_t>(first_class.capacity) + 1);
    REQUIRE(changed_capacity.kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());
    REQUIRE(classes.update_class(admin, timestamp, first_train.train_number,
                                 first_class.class_code, first_class.capacity).kind ==
            rrs::OutcomeKind::Success);

    const auto original_fare = fares.list_fares(first_train.train_number, first_class.class_code);
    REQUIRE(original_fare.size() == 1);
    const auto changed_fare = fares.replace_fare(
        admin, first_train.train_number, first_class.class_code,
        original_fare.front().effective_from, original_fare.front().record_revision,
        original_fare.front().fare_paise, original_fare.front().fare_paise + 1,
        original_fare.front().effective_from);
    REQUIRE(changed_fare.kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());

    const auto extra_fare = fares.configure_fare(
        admin, first_train.train_number, first_class.class_code,
        first_class.fare_paise + 100, "2026-11-01");
    REQUIRE(extra_fare.kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());

    constexpr std::array<std::string_view, 6> codes = {"SL", "3A", "2A", "1A", "CC", "2S"};
    std::string unused_code;
    for (const auto code : codes) {
        const auto existing = std::find_if(
            plan.trains.front().classes.begin(), plan.trains.front().classes.end(),
            [code](const auto& item) { return item.class_code == code; });
        if (existing == plan.trains.front().classes.end()) {
            unused_code = code;
            break;
        }
    }
    REQUIRE_FALSE(unused_code.empty());
    REQUIRE(classes.add_class(admin, timestamp, first_train.train_number,
                              unused_code, 30).kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());

    const rrs::TrainRecord extra_train{
        900002, "Unexpected fixture train", "MAS", "HWH", "10:00", "14:00", 0x7f};
    REQUIRE(trains.add_train(admin, timestamp, extra_train).kind == rrs::OutcomeKind::Success);
    CHECK_FALSE(rrs::check_generated_fixture(plan, store, trains, classes, fares).ok());
    REQUIRE(trains.remove_train(admin, timestamp, extra_train.train_number).kind ==
            rrs::OutcomeKind::Success);

    REQUIRE(trains.remove_train(admin, timestamp, first_train.train_number).kind ==
            rrs::OutcomeKind::Success);
    const auto orphaned_relationships =
        rrs::check_generated_fixture(plan, store, trains, classes, fares);
    CHECK_FALSE(orphaned_relationships.ok());
    CHECK(std::any_of(orphaned_relationships.violations.begin(),
                      orphaned_relationships.violations.end(), [](const auto& issue) {
                          return issue.find("class record refers to an unexpected train") !=
                                 std::string::npos;
                      }));
    CHECK(std::any_of(orphaned_relationships.violations.begin(),
                      orphaned_relationships.violations.end(), [](const auto& issue) {
                          return issue.find("fare record refers to an unexpected train") !=
                                 std::string::npos;
                      }));
}
