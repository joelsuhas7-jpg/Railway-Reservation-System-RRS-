#include "class_fare_config.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "authorization.hpp"

namespace rrs {
namespace {

constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kTrainNumberOffset = 1;
constexpr std::size_t kClassCodeOffset = 5;
constexpr std::size_t kClassCodeSize = 2;
constexpr std::size_t kCapacityOffset = 7;
constexpr std::size_t kReservedOffset = 11;
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::array<std::string_view, 6> kClassCodes = {"SL", "3A", "2A", "1A", "CC", "2S"};

std::mutex& class_config_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

bool valid_class_code(std::string_view code) {
    return std::find(kClassCodes.begin(), kClassCodes.end(), code) != kClassCodes.end();
}

bool valid_utc_timestamp(std::string_view value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool separator = index == 4 || index == 7 || index == 10 || index == 13 ||
                               index == 16 || index == 19;
        if (!separator && (value[index] < '0' || value[index] > '9')) {
            return false;
        }
    }
    const auto two_digits = [&value](std::size_t offset) {
        return (value[offset] - '0') * 10 + (value[offset + 1] - '0');
    };
    const int year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 +
                     (value[2] - '0') * 10 + (value[3] - '0');
    const int month = two_digits(5);
    const int day = two_digits(8);
    const int hour = two_digits(11);
    const int minute = two_digits(14);
    const int second = two_digits(17);
    if (year == 0 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    constexpr std::array<int, 12> month_days = {31, 28, 31, 30, 31, 30,
                                                 31, 31, 30, 31, 30, 31};
    const bool leap_year = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
    const int max_day = month_days[static_cast<std::size_t>(month - 1)] +
                        ((month == 2 && leap_year) ? 1 : 0);
    return day >= 1 && day <= max_day;
}

std::string pair_id(std::uint32_t train_number, std::string_view class_code) {
    return std::to_string(train_number) + ":" + std::string(class_code);
}

std::string encode_record(const ClassRecord& record) {
    std::string bytes(kClassRecordSize, '\0');
    bytes[kVersionOffset] = static_cast<char>(kFormatVersion);
    for (unsigned int byte = 0; byte < 4; ++byte) {
        bytes[kTrainNumberOffset + byte] =
            static_cast<char>((record.train_number >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(kClassCodeOffset, kClassCodeSize, record.class_code);
    for (unsigned int byte = 0; byte < 4; ++byte) {
        bytes[kCapacityOffset + byte] =
            static_cast<char>((record.capacity >> (byte * 8U)) & 0xffU);
    }
    return bytes;
}

ClassRecord decode_record(std::string_view bytes) {
    if (bytes.size() != kClassRecordSize) {
        throw std::runtime_error("corrupt class record: invalid record size");
    }
    if (static_cast<std::uint8_t>(bytes[kVersionOffset]) != kFormatVersion) {
        throw std::runtime_error("unsupported class record version");
    }
    if (std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(kReservedOffset), bytes.end(),
                    [](char value) { return value != '\0'; })) {
        throw std::runtime_error("corrupt class record: reserved bytes must be zero");
    }
    ClassRecord record;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        record.train_number |= static_cast<std::uint32_t>(
                                   static_cast<unsigned char>(bytes[kTrainNumberOffset + byte]))
                               << (byte * 8U);
        record.capacity |= static_cast<std::uint32_t>(
                               static_cast<unsigned char>(bytes[kCapacityOffset + byte]))
                           << (byte * 8U);
    }
    record.class_code = std::string(bytes.substr(kClassCodeOffset, kClassCodeSize));
    if (record.train_number == 0) {
        throw std::runtime_error("corrupt class record: train number must be nonzero");
    }
    if (!valid_class_code(record.class_code)) {
        throw std::runtime_error("corrupt class record: invalid class code");
    }
    if (record.capacity == 0) {
        throw std::runtime_error("corrupt class record: capacity must be positive");
    }
    return record;
}

}  // namespace

ClassFareConfigModule::ClassFareConfigModule(PersistenceStore store,
                                             const TrainCatalogueModule& trains,
                                             const AuditLog& audit_log,
                                             ClassAuditTimestampProvider authorization_timestamp_provider)
    : store_(std::move(store)), trains_(trains), audit_log_(audit_log),
      authorization_timestamp_provider_(std::move(authorization_timestamp_provider)) {
    std::lock_guard<std::mutex> lock(class_config_mutex());
    journal_ = std::make_unique<AtomicJournal>(store_);
}

std::vector<ClassRecord> ClassFareConfigModule::read_all() const {
    const auto count = store_.record_count(kClassesFile, kClassRecordSize);
    std::vector<ClassRecord> records;
    records.reserve(count);
    std::unordered_set<std::string> pairs;
    for (std::size_t index = 0; index < count; ++index) {
        const auto bytes = store_.read_record(kClassesFile, kClassRecordSize, index);
        if (!bytes) {
            throw std::runtime_error("class catalogue changed while being read");
        }
        auto record = decode_record(*bytes);
        if (!pairs.insert(pair_id(record.train_number, record.class_code)).second) {
            throw std::runtime_error("corrupt class catalogue: duplicate train/class pair");
        }
        records.push_back(std::move(record));
    }
    return records;
}

std::optional<std::size_t> ClassFareConfigModule::find_index(
    const std::vector<ClassRecord>& records, std::uint32_t train_number,
    std::string_view class_code) const {
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (records[index].train_number == train_number && records[index].class_code == class_code) {
            return index;
        }
    }
    return std::nullopt;
}

Outcome ClassFareConfigModule::append_success_audit(const Session& session,
                                                    std::string_view timestamp,
                                                    std::string_view action,
                                                    std::uint32_t train_number,
                                                    std::string_view class_code) const {
    try {
        audit_log_.append({std::string(timestamp), session.user_id, role_name(session.role),
                           std::string(action), pair_id(train_number, class_code)});
    } catch (const std::exception& error) {
        return Outcome::system_failure("class mutation persisted but audit logging failed: " +
                                       std::string(error.what()));
    }
    return Outcome::success(action == "CLASS_ADDED" ? "class added" : "class updated",
                            pair_id(train_number, class_code));
}

Outcome ClassFareConfigModule::add_class(const Session& session, std::string_view timestamp,
                                         std::uint32_t train_number,
                                         std::string_view class_code,
                                         std::int64_t capacity) {
    std::lock_guard<std::mutex> lock(class_config_mutex());
    try {
        if (!authorization_timestamp_provider_) {
            return Outcome::system_failure("authorization timestamp provider is not configured");
        }
        const auto authorization_timestamp = authorization_timestamp_provider_();
        if (!valid_utc_timestamp(authorization_timestamp)) {
            return Outcome::system_failure("authorization timestamp provider returned an invalid UTC timestamp");
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "MANAGE_CLASSES", authorization_timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (!valid_utc_timestamp(timestamp)) {
            return Outcome::validation_failure("timestamp", "must be a valid ISO 8601 UTC timestamp");
        }
        if (!valid_class_code(class_code)) {
            return Outcome::validation_failure("class code", "must be SL, 3A, 2A, 1A, CC, or 2S");
        }
        if (train_number == 0) {
            return Outcome::validation_failure("train number", "must be nonzero");
        }
        if (capacity <= 0 ||
            static_cast<std::uint64_t>(capacity) > std::numeric_limits<std::uint32_t>::max()) {
            return Outcome::validation_failure("capacity", "must be between 1 and 4294967295");
        }
        if (!trains_.find_train(train_number)) {
            return Outcome::refusal("no active train with that number exists");
        }
        auto records = read_all();
        if (find_index(records, train_number, class_code)) {
            return Outcome::refusal("class is already configured for that train");
        }
        const auto class_count = static_cast<std::size_t>(std::count_if(
            records.begin(), records.end(), [train_number](const ClassRecord& record) {
                return record.train_number == train_number;
            }));
        if (class_count >= kMaximumClassesPerTrain) {
            return Outcome::refusal("train already has the maximum number of classes");
        }
        ClassRecord record{train_number, std::string(class_code),
                           static_cast<std::uint32_t>(capacity)};
        journal_->commit({{std::string(kClassesFile), kClassRecordSize, records.size(),
                           encode_record(record)}});
        return append_success_audit(session, timestamp, "CLASS_ADDED", train_number, class_code);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome ClassFareConfigModule::update_class(const Session& session, std::string_view timestamp,
                                            std::uint32_t train_number,
                                            std::string_view class_code,
                                            std::int64_t capacity) {
    std::lock_guard<std::mutex> lock(class_config_mutex());
    try {
        if (!authorization_timestamp_provider_) {
            return Outcome::system_failure("authorization timestamp provider is not configured");
        }
        const auto authorization_timestamp = authorization_timestamp_provider_();
        if (!valid_utc_timestamp(authorization_timestamp)) {
            return Outcome::system_failure("authorization timestamp provider returned an invalid UTC timestamp");
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "MANAGE_CLASSES", authorization_timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (!valid_utc_timestamp(timestamp)) {
            return Outcome::validation_failure("timestamp", "must be a valid ISO 8601 UTC timestamp");
        }
        if (!valid_class_code(class_code)) {
            return Outcome::validation_failure("class code", "must be SL, 3A, 2A, 1A, CC, or 2S");
        }
        if (train_number == 0) {
            return Outcome::validation_failure("train number", "must be nonzero");
        }
        if (capacity <= 0 ||
            static_cast<std::uint64_t>(capacity) > std::numeric_limits<std::uint32_t>::max()) {
            return Outcome::validation_failure("capacity", "must be between 1 and 4294967295");
        }
        if (!trains_.find_train(train_number)) {
            return Outcome::refusal("no active train with that number exists");
        }
        auto records = read_all();
        const auto index = find_index(records, train_number, class_code);
        if (!index) {
            return Outcome::refusal("no configured class with that code exists for the train");
        }
        if (capacity < records[*index].capacity) {
            return Outcome::refusal(
                "capacity reduction cannot be validated against allocated seats before M8 integration");
        }
        records[*index].capacity = static_cast<std::uint32_t>(capacity);
        journal_->commit({{std::string(kClassesFile), kClassRecordSize, *index,
                           encode_record(records[*index])}});
        return append_success_audit(session, timestamp, "CLASS_UPDATED", train_number, class_code);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

ClassLookupResult ClassFareConfigModule::find_class(std::uint32_t train_number,
                                                   std::string_view class_code) const {
    std::lock_guard<std::mutex> lock(class_config_mutex());
    if (!trains_.find_train(train_number)) {
        return {};
    }
    const auto records = read_all();
    const auto index = find_index(records, train_number, class_code);
    if (!index) {
        return {true, std::nullopt};
    }
    return {true, records[*index]};
}

ClassListResult ClassFareConfigModule::list_classes(std::uint32_t train_number) const {
    std::lock_guard<std::mutex> lock(class_config_mutex());
    if (!trains_.find_train(train_number)) {
        return {};
    }
    auto records = read_all();
    ClassListResult result;
    result.train_exists = true;
    for (auto& record : records) {
        if (record.train_number == train_number) {
            result.records.push_back(std::move(record));
        }
    }
    return result;
}

}  // namespace rrs
