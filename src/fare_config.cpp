#include "fare_config.hpp"

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
constexpr std::size_t kFareOffset = 7;
constexpr std::size_t kEffectiveFromOffset = 15;
constexpr std::size_t kEffectiveFromSize = 10;
constexpr std::size_t kRevisionOffset = 25;
constexpr std::size_t kRevisionSize = 7;
constexpr std::uint64_t kMaximumFareRevision = (std::uint64_t{1} << 56U) - 1U;
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::array<std::string_view, 6> kSupportedClassCodes = {
    "SL", "3A", "2A", "1A", "CC", "2S"};

std::mutex& fare_config_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool is_supported_class_code(std::string_view class_code) {
    return std::find(kSupportedClassCodes.begin(), kSupportedClassCodes.end(), class_code) !=
           kSupportedClassCodes.end();
}

bool is_valid_utc_timestamp(std::string_view timestamp) {
    if (timestamp.size() != 20 || timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
        timestamp[19] != 'Z' || !is_valid_fare_date(timestamp.substr(0, 10))) {
        return false;
    }
    const auto is_digit = [&timestamp](std::size_t index) {
        return timestamp[index] >= '0' && timestamp[index] <= '9';
    };
    for (const std::size_t index : {11U, 12U, 14U, 15U, 17U, 18U}) {
        if (!is_digit(index)) {
            return false;
        }
    }
    const auto two_digits = [&timestamp](std::size_t offset) {
        return (timestamp[offset] - '0') * 10 + (timestamp[offset + 1] - '0');
    };
    return two_digits(11) <= 23 && two_digits(14) <= 59 && two_digits(17) <= 59;
}

std::string fare_key(std::uint32_t train_number, std::string_view class_code,
                     std::string_view effective_from) {
    return std::to_string(train_number) + ":" + std::string(class_code) + ":" +
           std::string(effective_from);
}

std::string record_id(const FareRecord& record) {
    return fare_key(record.train_number, record.class_code, record.effective_from);
}

std::uint64_t next_record_revision(const std::vector<FareRecord>& records) {
    std::uint64_t maximum_revision = 0;
    for (const auto& record : records) {
        maximum_revision = std::max(maximum_revision, record.record_revision);
    }
    if (maximum_revision == kMaximumFareRevision) {
        throw std::overflow_error("fare record revision limit reached");
    }
    return maximum_revision + 1U;
}

std::string encode_record(const FareRecord& record) {
    std::string bytes(kFareRecordSize, '\0');
    bytes[kVersionOffset] = static_cast<char>(kFormatVersion);
    for (unsigned int byte = 0; byte < 4; ++byte) {
        bytes[kTrainNumberOffset + byte] =
            static_cast<char>((record.train_number >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(kClassCodeOffset, 2, record.class_code);
    const auto encoded_fare = static_cast<std::uint64_t>(record.fare_paise);
    for (unsigned int byte = 0; byte < 8; ++byte) {
        bytes[kFareOffset + byte] =
            static_cast<char>((encoded_fare >> (byte * 8U)) & 0xffU);
    }
    bytes.replace(kEffectiveFromOffset, kEffectiveFromSize, record.effective_from);
    for (std::size_t byte = 0; byte < kRevisionSize; ++byte) {
        bytes[kRevisionOffset + byte] =
            static_cast<char>((record.record_revision >> (byte * 8U)) & 0xffU);
    }
    return bytes;
}

FareRecord decode_record(std::string_view bytes) {
    if (bytes.size() != kFareRecordSize) {
        throw std::runtime_error("corrupt fare record: invalid record size");
    }
    if (static_cast<std::uint8_t>(bytes[kVersionOffset]) != kFormatVersion) {
        throw std::runtime_error("unsupported fare record version");
    }
    FareRecord record;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        record.train_number |= static_cast<std::uint32_t>(
                                  static_cast<unsigned char>(bytes[kTrainNumberOffset + byte]))
                              << (byte * 8U);
    }
    record.class_code = std::string(bytes.substr(kClassCodeOffset, 2));
    std::uint64_t fare_bits = 0;
    for (unsigned int byte = 0; byte < 8; ++byte) {
        fare_bits |= static_cast<std::uint64_t>(
                         static_cast<unsigned char>(bytes[kFareOffset + byte]))
                     << (byte * 8U);
    }
    if (fare_bits > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("corrupt fare record: fare exceeds INT64_MAX");
    }
    record.fare_paise = static_cast<std::int64_t>(fare_bits);
    record.effective_from =
        std::string(bytes.substr(kEffectiveFromOffset, kEffectiveFromSize));
    for (std::size_t byte = 0; byte < kRevisionSize; ++byte) {
        record.record_revision |=
            static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[kRevisionOffset + byte]))
            << (byte * 8U);
    }

    if (record.train_number == 0) {
        throw std::runtime_error("corrupt fare record: train number must be nonzero");
    }
    if (!is_supported_class_code(record.class_code)) {
        throw std::runtime_error("corrupt fare record: invalid class code");
    }
    if (record.fare_paise <= 0) {
        throw std::runtime_error("corrupt fare record: fare must be positive");
    }
    if (!is_valid_fare_date(record.effective_from)) {
        throw std::runtime_error("corrupt fare record: invalid effective-from date");
    }
    if (record.record_revision == 0) {
        throw std::runtime_error("corrupt fare record: revision must be nonzero");
    }
    return record;
}

}  // namespace

std::optional<std::int64_t> parse_fare_paise(std::string_view amount) {
    if (amount.empty()) {
        return std::nullopt;
    }
    const auto decimal = amount.find('.');
    if (decimal != std::string_view::npos && amount.find('.', decimal + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    const auto whole = amount.substr(0, decimal);
    if (whole.empty()) {
        return std::nullopt;
    }
    const auto fractional = decimal == std::string_view::npos
                                ? std::string_view{}
                                : amount.substr(decimal + 1);
    if (fractional.size() > 2 || (decimal != std::string_view::npos && fractional.empty())) {
        return std::nullopt;
    }
    for (const char character : whole) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }
    for (const char character : fractional) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }

    std::uint64_t rupees = 0;
    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), rupees);
    if (parsed.ec != std::errc{} || parsed.ptr != whole.data() + whole.size()) {
        return std::nullopt;
    }
    std::uint64_t paise = 0;
    if (rupees > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 100U) {
        return std::nullopt;
    }
    paise = rupees * 100U;
    std::uint64_t fraction = 0;
    if (!fractional.empty()) {
        fraction = static_cast<std::uint64_t>(fractional[0] - '0') * 10U;
        if (fractional.size() == 2) {
            fraction += static_cast<std::uint64_t>(fractional[1] - '0');
        }
    }
    if (paise > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - fraction) {
        return std::nullopt;
    }
    paise += fraction;
    if (paise == 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(paise);
}

bool is_valid_fare_date(std::string_view date) noexcept {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (std::size_t index = 0; index < date.size(); ++index) {
        if (index != 4 && index != 7 && (date[index] < '0' || date[index] > '9')) {
            return false;
        }
    }
    const auto two_digits = [&date](std::size_t offset) {
        return (date[offset] - '0') * 10 + (date[offset + 1] - '0');
    };
    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + (date[3] - '0');
    const int month = two_digits(5);
    const int day = two_digits(8);
    if (year == 0 || month < 1 || month > 12) {
        return false;
    }
    constexpr std::array<int, 12> month_days = {31, 28, 31, 30, 31, 30,
                                                 31, 31, 30, 31, 30, 31};
    const bool leap_year = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
    const int max_day = month_days[static_cast<std::size_t>(month - 1)] +
                        ((month == 2 && leap_year) ? 1 : 0);
    return day >= 1 && day <= max_day;
}

FareConfigModule::FareConfigModule(PersistenceStore store, const TrainCatalogueModule& trains,
                                   const ClassFareConfigModule& classes,
                                   const AuditLog& audit_log,
                                   FareTimestampProvider timestamp_provider)
    : store_(std::move(store)), trains_(trains), classes_(classes), audit_log_(audit_log),
      timestamp_provider_(std::move(timestamp_provider)) {
    std::lock_guard<std::mutex> lock(fare_config_mutex());
    auto transaction_lock = store_.lock_file(kFaresFile);
    journal_ = std::make_unique<AtomicJournal>(store_);
}

std::vector<FareRecord> FareConfigModule::read_all() const {
    const auto encoded_records = store_.read_records(kFaresFile, kFareRecordSize);
    std::vector<FareRecord> records;
    records.reserve(encoded_records.size());
    std::unordered_set<std::string> keys;
    std::unordered_set<std::uint64_t> revisions;
    for (const auto& bytes : encoded_records) {
        auto record = decode_record(bytes);
        if (!keys.insert(record_id(record)).second) {
            throw std::runtime_error("corrupt fare catalogue: duplicate fare composite key");
        }
        if (!revisions.insert(record.record_revision).second) {
            throw std::runtime_error("corrupt fare catalogue: duplicate record revision");
        }
        records.push_back(std::move(record));
    }
    return records;
}

std::optional<std::string> FareConfigModule::authorize_and_get_timestamp(
    const Session& session, Outcome& failure) const {
    if (!timestamp_provider_) {
        failure = Outcome::system_failure("fare timestamp provider is not configured");
        return std::nullopt;
    }
    const auto timestamp = timestamp_provider_();
    if (!is_valid_utc_timestamp(timestamp)) {
        failure = Outcome::system_failure(
            "fare timestamp provider returned an invalid UTC timestamp");
        return std::nullopt;
    }
    const auto authorization = AuthorizationModule::require_administrator(
        session, "MANAGE_FARES", timestamp, audit_log_);
    if (!authorization) {
        failure = Outcome::refusal(authorization.message);
        return std::nullopt;
    }
    return timestamp;
}

bool FareConfigModule::valid_pair(std::uint32_t train_number, std::string_view class_code,
                                  Outcome& failure) const {
    if (train_number == 0) {
        failure = Outcome::validation_failure("train number", "must be nonzero");
        return false;
    }
    if (!is_supported_class_code(class_code)) {
        failure = Outcome::validation_failure(
            "class code", "must be SL, 3A, 2A, 1A, CC, or 2S");
        return false;
    }
    if (!trains_.find_train(train_number)) {
        failure = Outcome::refusal("no active train with that number exists");
        return false;
    }
    const auto configured_class = classes_.find_class(train_number, class_code);
    if (!configured_class.record) {
        failure = Outcome::refusal("no configured class with that code exists for the train");
        return false;
    }
    return true;
}

Outcome FareConfigModule::append_success_audit(const Session& session,
                                              std::string_view timestamp,
                                              std::string_view action,
                                              const FareRecord& record) const {
    try {
        audit_log_.append({std::string(timestamp), session.user_id,
                           session.role == Role::Administrator ? "ADMIN" : "PASSENGER",
                           std::string(action), record_id(record)});
    } catch (const std::exception& error) {
        return Outcome::system_failure("fare was persisted but audit logging failed: " +
                                       std::string(error.what()));
    }
    return Outcome::success(action == "FARE_CONFIGURED" ? "fare configured" : "fare updated",
                            record_id(record));
}

Outcome FareConfigModule::configure_fare(const Session& session, std::uint32_t train_number,
                                         std::string_view class_code,
                                         std::int64_t fare_paise,
                                         std::string_view effective_from) {
    std::lock_guard<std::mutex> lock(fare_config_mutex());
    try {
        Outcome failure;
        const auto timestamp = authorize_and_get_timestamp(session, failure);
        if (!timestamp) {
            return failure;
        }
        auto transaction_lock = store_.lock_file(kFaresFile);
        const auto today = std::string_view(*timestamp).substr(0, 10);
        if (fare_paise <= 0) {
            return Outcome::validation_failure("fare", "must be a positive amount in paise");
        }
        if (!is_valid_fare_date(effective_from)) {
            return Outcome::validation_failure("effective-from date",
                                               "must be a valid ISO 8601 date (YYYY-MM-DD)");
        }
        if (effective_from < today) {
            return Outcome::validation_failure("effective-from date", "must not be in the past");
        }
        if (!valid_pair(train_number, class_code, failure)) {
            return failure;
        }

        auto records = read_all();
        const auto duplicate = std::find_if(records.begin(), records.end(),
                                            [&](const FareRecord& record) {
                                                return record.train_number == train_number &&
                                                       record.class_code == class_code &&
                                                       record.effective_from == effective_from;
                                            });
        if (duplicate != records.end()) {
            return Outcome::refusal("fare already exists for that effective-from date; explicit replacement is required");
        }
        FareRecord record{train_number, std::string(class_code), fare_paise,
                          std::string(effective_from), next_record_revision(records)};
        journal_->commit({{std::string(kFaresFile), kFareRecordSize, records.size(),
                           encode_record(record)}});
        return append_success_audit(session, *timestamp, "FARE_CONFIGURED", record);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome FareConfigModule::replace_fare(const Session& session, std::uint32_t train_number,
                                       std::string_view class_code,
                                       std::string_view selected_effective_from,
                                       std::uint64_t expected_record_revision,
                                       std::int64_t expected_fare_paise,
                                       std::int64_t fare_paise,
                                       std::string_view new_effective_from) {
    std::lock_guard<std::mutex> lock(fare_config_mutex());
    try {
        Outcome failure;
        const auto timestamp = authorize_and_get_timestamp(session, failure);
        if (!timestamp) {
            return failure;
        }
        auto transaction_lock = store_.lock_file(kFaresFile);
        const auto today = std::string_view(*timestamp).substr(0, 10);
        if (fare_paise <= 0) {
            return Outcome::validation_failure("fare", "must be a positive amount in paise");
        }
        if (!is_valid_fare_date(selected_effective_from)) {
            return Outcome::validation_failure("selected effective-from date",
                                               "must be a valid ISO 8601 date (YYYY-MM-DD)");
        }
        if (!is_valid_fare_date(new_effective_from)) {
            return Outcome::validation_failure("effective-from date",
                                               "must be a valid ISO 8601 date (YYYY-MM-DD)");
        }
        if (new_effective_from < today) {
            return Outcome::validation_failure("effective-from date", "must not be in the past");
        }
        if (selected_effective_from < today) {
            return Outcome::refusal("only a current or future-effective fare can be replaced");
        }
        if (!valid_pair(train_number, class_code, failure)) {
            return failure;
        }

        auto records = read_all();
        const auto selected = std::find_if(records.begin(), records.end(),
                                           [&](const FareRecord& record) {
                                               return record.train_number == train_number &&
                                                      record.class_code == class_code &&
                                                      record.effective_from == selected_effective_from;
                                           });
        if (selected == records.end()) {
            return Outcome::refusal("selected fare record does not exist");
        }
        if (selected->record_revision != expected_record_revision ||
            selected->fare_paise != expected_fare_paise) {
            return Outcome::refusal("selected fare record changed; review the current fare before replacing it");
        }
        const auto selected_index = static_cast<std::size_t>(selected - records.begin());
        const auto key_conflict = std::find_if(records.begin(), records.end(),
                                               [&](const FareRecord& record) {
                                                   return record.train_number == train_number &&
                                                          record.class_code == class_code &&
                                                          record.effective_from == new_effective_from;
                                               });
        if (key_conflict != records.end() &&
            static_cast<std::size_t>(key_conflict - records.begin()) != selected_index) {
            return Outcome::refusal("another fare record already uses the requested effective-from date");
        }
        FareRecord replacement{train_number, std::string(class_code), fare_paise,
                               std::string(new_effective_from), next_record_revision(records)};
        journal_->commit({{std::string(kFaresFile), kFareRecordSize, selected_index,
                           encode_record(replacement)}});
        return append_success_audit(session, *timestamp, "FARE_UPDATED", replacement);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

std::vector<FareRecord> FareConfigModule::list_fares(std::uint32_t train_number,
                                                    std::string_view class_code) const {
    std::lock_guard<std::mutex> lock(fare_config_mutex());
    auto transaction_lock = store_.lock_file(kFaresFile);
    auto records = read_all();
    std::vector<FareRecord> result;
    for (auto& record : records) {
        if (record.train_number == train_number && record.class_code == class_code) {
            result.push_back(std::move(record));
        }
    }
    return result;
}

FareLookupResult FareConfigModule::find_applicable_fare(std::uint32_t train_number,
                                                        std::string_view class_code,
                                                        std::string_view journey_date) const {
    std::lock_guard<std::mutex> lock(fare_config_mutex());
    if (!trains_.find_train(train_number)) {
        return {FareLookupState::UnknownTrain, std::nullopt,
                "no active train with that number exists"};
    }
    if (!is_supported_class_code(class_code) ||
        !classes_.find_class(train_number, class_code).record) {
        return {FareLookupState::ClassNotConfigured, std::nullopt,
                "no configured class with that code exists for the train"};
    }
    if (!is_valid_fare_date(journey_date)) {
        return {FareLookupState::InvalidJourneyDate, std::nullopt,
                "journey date must be a valid ISO 8601 date (YYYY-MM-DD)"};
    }
    auto transaction_lock = store_.lock_file(kFaresFile);
    const auto records = read_all();
    const FareRecord* applicable = nullptr;
    for (const auto& record : records) {
        if (record.train_number == train_number && record.class_code == class_code &&
            record.effective_from <= journey_date &&
            (applicable == nullptr || record.effective_from > applicable->effective_from)) {
            applicable = &record;
        }
    }
    if (applicable == nullptr) {
        return {FareLookupState::NoApplicableFare, std::nullopt,
                std::string(kFareUnavailableMessage)};
    }
    return {FareLookupState::Found, *applicable, {}};
}

}  // namespace rrs
