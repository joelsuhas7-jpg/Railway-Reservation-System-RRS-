#include "fixture_generator.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "audit_log.hpp"
#include "persistence.hpp"

namespace rrs {
namespace {

constexpr std::array<std::string_view, 6> kClassCodes = {"SL", "3A", "2A", "1A", "CC", "2S"};
constexpr std::array<std::string_view, 16> kStations = {
    "SBC", "NDLS", "MAS", "HWH", "BCT", "PUNE", "JP", "LKO",
    "ADI", "BPL", "CNB", "HYB", "ERS", "GHY", "VSKP", "BBS"};

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= UINT64_C(1099511628211);
}

void hash_integer(std::uint64_t& hash, std::uint64_t value, unsigned int byte_count) noexcept {
    for (unsigned int i = 0; i < byte_count; ++i) {
        hash_byte(hash, static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
}

void hash_text(std::uint64_t& hash, std::string_view value) noexcept {
    hash_integer(hash, value.size(), 8);
    for (const unsigned char character : value) {
        hash_byte(hash, character);
    }
}

bool same_train(const TrainRecord& left, const TrainRecord& right) {
    return left.train_number == right.train_number && left.name == right.name &&
           left.source_station == right.source_station &&
           left.destination_station == right.destination_station &&
           left.departure_time == right.departure_time &&
           left.arrival_time == right.arrival_time &&
           left.running_days_mask == right.running_days_mask;
}

std::string train_context(std::uint32_t number, std::string_view operation) {
    return std::string(operation) + " train " + std::to_string(number);
}

bool valid_station(std::string_view value) {
    return !value.empty() && value.size() <= 8 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9');
           });
}

bool valid_time(std::string_view value) {
    if (value.size() != 5 || value[2] != ':' || value[0] < '0' || value[0] > '9' ||
        value[1] < '0' || value[1] > '9' || value[3] < '0' || value[3] > '9' ||
        value[4] < '0' || value[4] > '9') {
        return false;
    }
    const auto hour = (value[0] - '0') * 10 + value[1] - '0';
    const auto minute = (value[3] - '0') * 10 + value[4] - '0';
    return hour <= 23 && minute <= 59;
}

bool valid_timestamp(std::string_view value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z' ||
        !is_valid_fare_date(value.substr(0, 10))) {
        return false;
    }
    for (std::size_t index = 11; index < 19; ++index) {
        if (index != 13 && index != 16 && (value[index] < '0' || value[index] > '9')) {
            return false;
        }
    }
    const auto two_digits = [&value](std::size_t index) {
        return (value[index] - '0') * 10 + value[index + 1] - '0';
    };
    return two_digits(11) <= 23 && two_digits(14) <= 59 && two_digits(17) <= 59;
}

}  // namespace

std::uint64_t SplitMix64::next() noexcept {
    std::uint64_t value = (state_ += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

std::uint64_t SplitMix64::bounded(std::uint64_t exclusive_upper_bound) {
    if (exclusive_upper_bound == 0) {
        throw std::invalid_argument("random range upper bound must be positive");
    }
    const std::uint64_t threshold = (std::uint64_t{0} - exclusive_upper_bound) %
                                    exclusive_upper_bound;
    for (;;) {
        const auto value = next();
        if (value >= threshold) {
            return value % exclusive_upper_bound;
        }
    }
}

GeneratedFixturePlan generate_fixture_plan(std::uint64_t seed) {
    SplitMix64 random(seed);
    GeneratedFixturePlan plan;
    plan.manifest.seed = seed;
    plan.trains.reserve(kGeneratedFixtureTrainCount);
    for (std::size_t index = 0; index < kGeneratedFixtureTrainCount; ++index) {
        const auto number = static_cast<std::uint32_t>(300000U + index);
        const auto source_index = static_cast<std::size_t>(random.bounded(kStations.size()));
        auto destination_index = static_cast<std::size_t>(random.bounded(kStations.size() - 1));
        if (destination_index >= source_index) {
            ++destination_index;
        }
        std::string source(kStations[source_index]);
        std::string destination(kStations[destination_index]);
        // These fixed records make one route query useful for every seed.
        if (index < 3) {
            source = "SBC";
            destination = "NDLS";
        }
        const auto departure_minute = index < 3 ? 5U * 60U + 30U * static_cast<unsigned int>(index)
                                                 : static_cast<unsigned int>(random.bounded(1440));
        const auto arrival_minute = static_cast<unsigned int>(random.bounded(1440));
        const auto format_time = [](unsigned int minute) {
            const auto hour = minute / 60U;
            const auto remainder = minute % 60U;
            std::string result(5, '0');
            result[0] = static_cast<char>('0' + hour / 10U);
            result[1] = static_cast<char>('0' + hour % 10U);
            result[2] = ':';
            result[3] = static_cast<char>('0' + remainder / 10U);
            result[4] = static_cast<char>('0' + remainder % 10U);
            return result;
        };
        std::uint8_t days = static_cast<std::uint8_t>(1U + random.bounded(0x7fU));
        if (index < 3) {
            days = 0x7fU;
        }
        GeneratedTrain generated;
        generated.train = {number, "Fixture Train " + std::to_string(number), source,
                           destination, format_time(departure_minute),
                           format_time(arrival_minute), days};

        std::array<std::string_view, 6> shuffled = kClassCodes;
        for (std::size_t i = shuffled.size(); i > 1; --i) {
            const auto selected = static_cast<std::size_t>(random.bounded(i));
            std::swap(shuffled[i - 1], shuffled[selected]);
        }
        const auto class_count = static_cast<std::size_t>(1U + random.bounded(6));
        generated.classes.reserve(class_count);
        for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
            const auto capacity = static_cast<std::uint32_t>(20U + random.bounded(481));
            const auto fare = static_cast<std::int64_t>(1000U + random.bounded(499001));
            generated.classes.push_back(
                {std::string(shuffled[class_index]), capacity, fare, kFixtureDate});
        }
        plan.trains.push_back(std::move(generated));
    }
    plan.manifest.train_count = plan.trains.size();
    for (const auto& generated : plan.trains) {
        plan.manifest.class_count += generated.classes.size();
        for (const auto& class_fare : generated.classes) {
            (void)class_fare;
            ++plan.manifest.fare_count;
        }
    }
    plan.manifest.fingerprint = fixture_fingerprint(plan);
    return plan;
}

std::uint64_t fixture_fingerprint(const GeneratedFixturePlan& plan) noexcept {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    hash_text(hash, "RRS-FIXTURE-PLAN");
    hash_integer(hash, plan.manifest.generator_version, 4);
    hash_integer(hash, plan.manifest.seed, 8);
    hash_text(hash, plan.manifest.timestamp);
    hash_text(hash, plan.manifest.effective_from);
    hash_integer(hash, plan.trains.size(), 8);
    for (const auto& generated : plan.trains) {
        const auto& train = generated.train;
        hash_integer(hash, train.train_number, 4);
        hash_text(hash, train.name);
        hash_text(hash, train.source_station);
        hash_text(hash, train.destination_station);
        hash_text(hash, train.departure_time);
        hash_text(hash, train.arrival_time);
        hash_integer(hash, train.running_days_mask, 1);
        hash_integer(hash, generated.classes.size(), 8);
        for (const auto& item : generated.classes) {
            hash_text(hash, item.class_code);
            hash_integer(hash, item.capacity, 4);
            hash_integer(hash, static_cast<std::uint64_t>(item.fare_paise), 8);
            hash_text(hash, item.effective_from);
        }
    }
    return hash;
}

FixtureCheckReport validate_fixture_plan(const GeneratedFixturePlan& plan) {
    FixtureCheckReport report;
    const auto add = [&report](std::string issue) { report.violations.push_back(std::move(issue)); };
    if (plan.manifest.generator_version != kFixtureGeneratorVersion) {
        add("unsupported fixture generator version");
    }
    if (!valid_timestamp(plan.manifest.timestamp) || !is_valid_fare_date(plan.manifest.effective_from)) {
        add("manifest timestamp or effective-from date is invalid");
    }
    if (plan.trains.empty()) {
        add("fixture must contain at least one train");
    }
    if (plan.manifest.train_count != plan.trains.size()) {
        add("manifest train count does not match plan");
    }
    std::unordered_set<std::uint32_t> numbers;
    std::size_t class_count = 0;
    std::size_t fare_count = 0;
    for (const auto& generated : plan.trains) {
        const auto& train = generated.train;
        if (!numbers.insert(train.train_number).second || train.train_number == 0) {
            add("train number is zero or duplicated: " + std::to_string(train.train_number));
        }
        const bool printable_name = !train.name.empty() && train.name.size() <= 40 &&
            train.name.front() != ' ' && train.name.back() != ' ' &&
            std::all_of(train.name.begin(), train.name.end(), [](unsigned char c) {
                return c >= 0x20 && c <= 0x7e;
            });
        if (!printable_name) {
            add("invalid train name for " + std::to_string(train.train_number));
        }
        if (!valid_station(train.source_station) || !valid_station(train.destination_station) ||
            train.source_station == train.destination_station) {
            add("invalid route for train " + std::to_string(train.train_number));
        }
        if (!valid_time(train.departure_time) || !valid_time(train.arrival_time)) {
            add("invalid schedule time for train " + std::to_string(train.train_number));
        }
        if (train.running_days_mask == 0 || (train.running_days_mask & 0x80U) != 0) {
            add("invalid running-days mask for train " + std::to_string(train.train_number));
        }
        if (generated.classes.empty() || generated.classes.size() > kClassCodes.size()) {
            add("class count must be between one and six for train " +
                std::to_string(train.train_number));
        }
        std::set<std::string> codes;
        for (const auto& item : generated.classes) {
            ++class_count;
            ++fare_count;
            if (std::find(kClassCodes.begin(), kClassCodes.end(), item.class_code) ==
                    kClassCodes.end() || !codes.insert(item.class_code).second) {
                add("unsupported or duplicate class code on train " +
                    std::to_string(train.train_number));
            }
            if (item.capacity == 0 || item.fare_paise <= 0 ||
                !is_valid_fare_date(item.effective_from) ||
                item.effective_from != plan.manifest.effective_from) {
                add("invalid capacity or fare details on train " +
                    std::to_string(train.train_number) + " class " + item.class_code);
            }
        }
    }
    if (plan.manifest.class_count != class_count || plan.manifest.fare_count != fare_count) {
        add("manifest class/fare counts do not match plan");
    }
    if (plan.manifest.fingerprint != fixture_fingerprint(plan)) {
        add("manifest fingerprint does not match fixture plan");
    }
    return report;
}

std::string format_fixture_manifest(const FixtureManifest& manifest) {
    std::ostringstream output;
    output << "format=RRS-FIXTURE-MANIFEST-V1\n"
           << "algorithm=splitmix64-v1\n"
           << "seed=" << manifest.seed << '\n'
           << "timestamp=" << manifest.timestamp << '\n'
           << "effective_from=" << manifest.effective_from << '\n'
           << "train_count=" << manifest.train_count << '\n'
           << "class_count=" << manifest.class_count << '\n'
           << "fare_count=" << manifest.fare_count << '\n'
           << "fingerprint_fnv1a64=" << std::hex << std::setfill('0') << std::setw(16)
           << manifest.fingerprint << '\n';
    return output.str();
}

FixtureCheckReport check_generated_fixture(const GeneratedFixturePlan& plan,
                                           const PersistenceStore& store,
                                           const TrainCatalogueModule& trains,
                                           const ClassFareConfigModule& classes,
                                           const FareConfigModule& fares) {
    FixtureCheckReport report;
    const auto add = [&report](std::string issue) { report.violations.push_back(std::move(issue)); };
    const auto plan_report = validate_fixture_plan(plan);
    for (const auto& issue : plan_report.violations) {
        add("invalid expected plan: " + issue);
    }
    std::vector<TrainRecord> active_trains;
    std::vector<ClassRecord> actual_classes;
    std::vector<FareRecord> actual_fares;
    try {
        active_trains = trains.list_active_trains();
    } catch (const std::exception& error) {
        add("cannot read active train catalogue: " + std::string(error.what()));
    }
    try {
        actual_classes = classes.list_all_classes();
    } catch (const std::exception& error) {
        add("cannot read class catalogue: " + std::string(error.what()));
    }
    try {
        actual_fares = fares.list_all_fares();
    } catch (const std::exception& error) {
        add("cannot read fare catalogue: " + std::string(error.what()));
    }
    std::unordered_set<std::uint32_t> expected_train_ids;
    std::unordered_set<std::string> expected_pairs;
    std::map<std::uint32_t, TrainRecord> active_by_id;
    for (const auto& train : active_trains) {
        if (!active_by_id.emplace(train.train_number, train).second) {
            add("active train catalogue contains duplicate train number " +
                std::to_string(train.train_number));
        }
    }
    using Pair = std::pair<std::uint32_t, std::string>;
    std::map<Pair, ClassRecord> classes_by_pair;
    for (const auto& item : actual_classes) {
        if (!classes_by_pair.emplace(Pair{item.train_number, item.class_code}, item).second) {
            add("class catalogue contains duplicate train/class pair");
        }
    }
    std::map<Pair, FareRecord> fares_by_pair;
    std::unordered_set<std::uint64_t> fare_revisions;
    for (const auto& item : actual_fares) {
        if (item.fare_paise <= 0 || !is_valid_fare_date(item.effective_from)) {
            add("fare catalogue contains invalid amount or effective date");
        }
        if (!fare_revisions.insert(item.record_revision).second) {
            add("fare catalogue contains duplicate revision identifier");
        }
        if (!fares_by_pair.emplace(Pair{item.train_number, item.class_code}, item).second) {
            add("fare catalogue contains multiple records for a generated class");
        }
    }
    std::size_t expected_classes = 0;
    std::size_t expected_fares = 0;
    std::uint64_t expected_revision = 0;
    for (const auto& generated : plan.trains) {
        if (!expected_train_ids.insert(generated.train.train_number).second) {
            add("plan contains duplicate train number " + std::to_string(generated.train.train_number));
        }
        const auto actual = active_by_id.find(generated.train.train_number);
        if (actual == active_by_id.end() || !same_train(actual->second, generated.train)) {
            add("missing or mismatched train " + std::to_string(generated.train.train_number));
        }
        for (const auto& expected : generated.classes) {
            ++expected_classes;
            ++expected_fares;
            const auto key = std::to_string(generated.train.train_number) + ":" + expected.class_code;
            if (!expected_pairs.insert(key).second) {
                add("plan contains duplicate train/class pair " + key);
            }
            const Pair pair{generated.train.train_number, expected.class_code};
            const auto found_class = classes_by_pair.find(pair);
            if (found_class == classes_by_pair.end() ||
                found_class->second.capacity != expected.capacity) {
                add("missing or mismatched class " + key);
            }
            ++expected_revision;
            const auto found_fare = fares_by_pair.find(pair);
            if (found_fare == fares_by_pair.end() ||
                found_fare->second.fare_paise != expected.fare_paise ||
                found_fare->second.effective_from != expected.effective_from) {
                add("missing or mismatched fare " + key);
            } else {
                if (found_fare->second.record_revision != expected_revision) {
                    add("unexpected fare revision for " + key);
                }
            }
        }
    }
    if (active_trains.size() != plan.trains.size()) {
        add("active train count differs from generated plan");
    }
    for (const auto& item : active_trains) {
        if (expected_train_ids.find(item.train_number) == expected_train_ids.end()) {
            add("unexpected active train " + std::to_string(item.train_number));
        }
    }
    if (classes_by_pair.size() != expected_classes) {
        add("class records contain missing or unexpected pairs");
    }
    for (const auto& item : classes_by_pair) {
        if (expected_pairs.find(std::to_string(item.first.first) + ":" + item.first.second) ==
            expected_pairs.end()) {
            add("orphan or unexpected class record");
        }
        if (expected_train_ids.find(item.first.first) == expected_train_ids.end() ||
            active_by_id.find(item.first.first) == active_by_id.end()) {
            add("class record refers to an unexpected train");
        }
    }
    if (fares_by_pair.size() != expected_fares) {
        add("fare records contain missing or unexpected pairs");
    }
    for (const auto& item : fares_by_pair) {
        const auto pair_key = std::to_string(item.first.first) + ":" + item.first.second;
        if (expected_pairs.find(pair_key) == expected_pairs.end()) {
            add("orphan or unexpected fare record");
        }
        if (classes_by_pair.find(item.first) == classes_by_pair.end()) {
            add("fare record refers to an unconfigured class");
        }
        if (expected_train_ids.find(item.first.first) == expected_train_ids.end() ||
            active_by_id.find(item.first.first) == active_by_id.end()) {
            add("fare record refers to an unexpected train");
        }
    }
    try {
        if (store.record_count(kTrainsFile, kTrainRecordSize) != plan.trains.size()) {
            add("persisted train record count differs from generated plan");
        }
        if (store.record_count(kClassesFile, kClassRecordSize) != expected_classes) {
            add("persisted class record count differs from generated plan");
        }
        if (store.record_count(kFaresFile, kFareRecordSize) != expected_fares) {
            add("persisted fare record count differs from generated plan");
        }
    } catch (const std::exception& error) {
        add("cannot verify persisted record counts: " + std::string(error.what()));
    }
    if (expected_classes != plan.manifest.class_count ||
        expected_fares != plan.manifest.fare_count) {
        add("manifest record counts differ from generated plan");
    }
    if (plan.manifest.fingerprint != fixture_fingerprint(plan)) {
        add("manifest fingerprint does not match generated plan");
    }
    return report;
}

FixtureMaterializationResult materialize_fixture(const GeneratedFixturePlan& plan,
                                                 const std::filesystem::path& data_directory) {
    FixtureMaterializationResult result;
    try {
        result.report = validate_fixture_plan(plan);
        if (!result.report.ok()) {
            throw std::runtime_error("fixture plan failed pre-materialization validation");
        }
        if (std::filesystem::exists(data_directory) &&
            std::filesystem::directory_iterator(data_directory) !=
                std::filesystem::directory_iterator{}) {
            throw std::runtime_error("fixture data directory must be fresh and empty");
        }
        PersistenceStore store(data_directory);
        AuditLog audit(data_directory / "audit.log");
        TrainCatalogueModule trains(store, audit);
        const Session administrator{"fixture-admin", Role::Administrator};
        const std::string timestamp = plan.manifest.timestamp;
        ClassFareConfigModule classes(store, trains, audit, [timestamp] { return timestamp; });
        FareConfigModule fares(store, trains, classes, audit, [timestamp] { return timestamp; });
        for (const auto& generated : plan.trains) {
            const auto outcome = trains.add_train(administrator, timestamp, generated.train);
            if (outcome.kind != OutcomeKind::Success) {
                throw std::runtime_error(train_context(generated.train.train_number, "add failed:") +
                                         " " + outcome.message);
            }
        }
        for (const auto& generated : plan.trains) {
            for (const auto& item : generated.classes) {
                const auto outcome = classes.add_class(administrator, timestamp,
                    generated.train.train_number, item.class_code, item.capacity);
                if (outcome.kind != OutcomeKind::Success) {
                    throw std::runtime_error(train_context(generated.train.train_number,
                        "add class " + item.class_code + " failed:") + " " + outcome.message);
                }
            }
        }
        for (const auto& generated : plan.trains) {
            for (const auto& item : generated.classes) {
                const auto outcome = fares.configure_fare(administrator,
                    generated.train.train_number, item.class_code, item.fare_paise,
                    item.effective_from);
                if (outcome.kind != OutcomeKind::Success) {
                    throw std::runtime_error(train_context(generated.train.train_number,
                        "configure fare " + item.class_code + " failed:") + " " + outcome.message);
                }
            }
        }
        result.report = check_generated_fixture(plan, store, trains, classes, fares);
        if (!result.report.ok()) {
            result.error = "generated fixture failed consistency verification";
            return result;
        }
        const auto manifest = format_fixture_manifest(plan.manifest);
        std::ofstream manifest_file(data_directory / kFixtureManifestFile,
                                    std::ios::binary | std::ios::trunc);
        if (!manifest_file) {
            throw std::runtime_error("cannot create fixture manifest");
        }
        manifest_file.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        manifest_file.flush();
        if (!manifest_file) {
            throw std::runtime_error("cannot write complete fixture manifest");
        }
        manifest_file.close();
        if (!manifest_file) {
            throw std::runtime_error("cannot close fixture manifest");
        }
        result.verified = VerifiedFixture(data_directory, plan.manifest);
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    return result;
}

}  // namespace rrs
