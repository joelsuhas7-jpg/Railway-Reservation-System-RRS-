#include "train_catalogue.hpp"

#include "authorization.hpp"
#include "validation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace rrs {
namespace {

constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kStatusOffset = 1;
constexpr std::size_t kTrainNumberOffset = 2;
constexpr std::size_t kNameOffset = 6;
constexpr std::size_t kNameSize = 40;
constexpr std::size_t kSourceOffset = 46;
constexpr std::size_t kStationSize = 8;
constexpr std::size_t kDestinationOffset = 54;
constexpr std::size_t kDepartureOffset = 62;
constexpr std::size_t kTimeSize = 5;
constexpr std::size_t kArrivalOffset = 67;
constexpr std::size_t kRunningDaysOffset = 72;
constexpr std::size_t kReservedOffset = 73;
constexpr std::size_t kReservedSize = 55;
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::uint8_t kActiveStatus = 1;
constexpr std::uint8_t kRemovedStatus = 0;

ValidationResult invalid(std::string_view field, std::string message);

std::mutex& catalogue_mutex() {
    static std::mutex mutex;
    return mutex;
}

ValidationResult validate_timestamp(std::string_view value) {
    constexpr std::string_view field = "timestamp";
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
        return invalid(field, "must use YYYY-MM-DDTHH:MM:SSZ format");
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool separator = index == 4 || index == 7 || index == 10 || index == 13 ||
                               index == 16 || index == 19;
        if (!separator && (value[index] < '0' || value[index] > '9')) {
            return invalid(field, "must use YYYY-MM-DDTHH:MM:SSZ format");
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
        return invalid(field, "contains an out-of-range date or time component");
    }
    constexpr std::array<int, 12> month_days = {31, 28, 31, 30, 31, 30,
                                                 31, 31, 30, 31, 30, 31};
    const bool leap_year = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
    const int maximum_day = month_days[static_cast<std::size_t>(month - 1)] +
                            ((month == 2 && leap_year) ? 1 : 0);
    if (day < 1 || day > maximum_day) {
        return invalid(field, "contains an invalid day for its month");
    }
    return {};
}

ValidationResult invalid(std::string_view field, std::string message) {
    return {false, std::string(field), std::move(message)};
}

ValidationResult validate_ascii_text(std::string_view field, std::string_view value,
                                     std::size_t maximum) {
    if (const auto presence = validate_presence(field, value); !presence) {
        return presence;
    }
    if (const auto length = validate_length(field, value, 1, maximum); !length) {
        return length;
    }
    if (field == "name" && (value.front() == ' ' || value.back() == ' ')) {
        return invalid(field, "must not start or end with a space");
    }
    for (const unsigned char character : value) {
        if (character < 0x20 || character > 0x7e) {
            return invalid(field, "must contain printable ASCII characters only");
        }
    }
    return {};
}

ValidationResult validate_station(std::string_view field, std::string_view value) {
    if (const auto presence = validate_presence(field, value); !presence) {
        return presence;
    }
    if (const auto length = validate_length(field, value, 1, kStationSize); !length) {
        return length;
    }
    for (const unsigned char character : value) {
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9'))) {
            return invalid(field, "must contain uppercase letters and digits only");
        }
    }
    return {};
}

ValidationResult validate_time(std::string_view field, std::string_view value) {
    if (value.size() != kTimeSize) {
        return invalid(field, "must use HH:MM format");
    }
    if (value[2] != ':' || value[0] < '0' || value[0] > '9' || value[1] < '0' ||
        value[1] > '9' || value[3] < '0' || value[3] > '9' || value[4] < '0' ||
        value[4] > '9') {
        return invalid(field, "must use HH:MM format");
    }
    const int hour = (value[0] - '0') * 10 + (value[1] - '0');
    const int minute = (value[3] - '0') * 10 + (value[4] - '0');
    if (hour > 23 || minute > 59) {
        return invalid(field, "must be a valid 24-hour time");
    }
    return {};
}

ValidationResult validate_train(const TrainRecord& train) {
    if (train.train_number == 0) {
        return invalid("train number", "must be nonzero");
    }
    if (const auto name = validate_ascii_text("name", train.name, kNameSize); !name) {
        return name;
    }
    if (const auto source = validate_station("source station", train.source_station); !source) {
        return source;
    }
    if (const auto destination =
            validate_station("destination station", train.destination_station);
        !destination) {
        return destination;
    }
    if (train.source_station == train.destination_station) {
        return invalid("destination station", "must differ from the source station");
    }
    if (const auto departure = validate_time("departure time", train.departure_time);
        !departure) {
        return departure;
    }
    if (const auto arrival = validate_time("arrival time", train.arrival_time); !arrival) {
        return arrival;
    }
    if (train.running_days_mask == 0) {
        return invalid("running days mask", "must include at least one day");
    }
    if ((train.running_days_mask & 0x80U) != 0) {
        return invalid("running days mask", "reserved bit 7 must be clear");
    }
    return {};
}

void put_text(std::string& record, std::size_t offset, std::size_t size,
              std::string_view value) {
    record.replace(offset, size, size, ' ');
    record.replace(offset, value.size(), value);
}

std::string get_text(std::string_view record, std::size_t offset, std::size_t size) {
    std::string value(record.substr(offset, size));
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

std::string encode_record(const TrainRecord& train, bool active) {
    if (const auto result = validate_train(train); !result) {
        throw std::invalid_argument(result.field + " " + result.message);
    }

    std::string record(kTrainRecordSize, '\0');
    record[kVersionOffset] = static_cast<char>(kFormatVersion);
    record[kStatusOffset] = static_cast<char>(active ? kActiveStatus : kRemovedStatus);
    for (unsigned int byte = 0; byte < 4; ++byte) {
        record[kTrainNumberOffset + byte] =
            static_cast<char>((train.train_number >> (byte * 8U)) & 0xffU);
    }
    put_text(record, kNameOffset, kNameSize, train.name);
    put_text(record, kSourceOffset, kStationSize, train.source_station);
    put_text(record, kDestinationOffset, kStationSize, train.destination_station);
    put_text(record, kDepartureOffset, kTimeSize, train.departure_time);
    put_text(record, kArrivalOffset, kTimeSize, train.arrival_time);
    record[kRunningDaysOffset] = static_cast<char>(train.running_days_mask);
    std::fill(record.begin() + static_cast<std::ptrdiff_t>(kReservedOffset), record.end(), '\0');
    return record;
}

std::pair<TrainRecord, bool> decode_record(std::string_view record) {
    if (record.size() != kTrainRecordSize) {
        throw std::runtime_error("corrupt train record: invalid record size");
    }
    if (static_cast<std::uint8_t>(record[kVersionOffset]) != kFormatVersion) {
        throw std::runtime_error("unsupported train record version");
    }
    const auto status = static_cast<std::uint8_t>(record[kStatusOffset]);
    if (status != kActiveStatus && status != kRemovedStatus) {
        throw std::runtime_error("corrupt train record: invalid status");
    }

    TrainRecord train;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        train.train_number |= static_cast<std::uint32_t>(
                                  static_cast<unsigned char>(record[kTrainNumberOffset + byte]))
                              << (byte * 8U);
    }
    train.name = get_text(record, kNameOffset, kNameSize);
    train.source_station = get_text(record, kSourceOffset, kStationSize);
    train.destination_station = get_text(record, kDestinationOffset, kStationSize);
    train.departure_time = get_text(record, kDepartureOffset, kTimeSize);
    train.arrival_time = get_text(record, kArrivalOffset, kTimeSize);
    train.running_days_mask = static_cast<std::uint8_t>(record[kRunningDaysOffset]);

    if (std::any_of(record.begin() + static_cast<std::ptrdiff_t>(kReservedOffset), record.end(),
                    [](char byte) { return byte != '\0'; })) {
        throw std::runtime_error("corrupt train record: reserved bytes must be zero");
    }
    if (const auto result = validate_train(train); !result) {
        throw std::runtime_error("corrupt train record: " + result.field + " " + result.message);
    }
    return {std::move(train), status == kActiveStatus};
}

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

}  // namespace

TrainCatalogueModule::TrainCatalogueModule(PersistenceStore store, const AuditLog& audit_log)
    : store_(std::move(store)), audit_log_(audit_log) {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    journal_ = std::make_unique<AtomicJournal>(store_);
}

std::vector<TrainCatalogueModule::StoredTrain> TrainCatalogueModule::read_all() const {
    const auto count = store_.record_count(kTrainsFile, kTrainRecordSize);
    if (count > kMaximumTrainRecords) {
        throw std::runtime_error("train catalogue exceeds the 2,000-record limit");
    }
    const auto records = store_.read_records(kTrainsFile, kTrainRecordSize,
                                             kMaximumTrainRecords);
    if (records.size() != count) {
        throw std::runtime_error("train catalogue changed while being read");
    }
    std::vector<StoredTrain> trains;
    trains.reserve(count);
    std::unordered_set<std::uint32_t> train_numbers;
    for (std::size_t index = 0; index < count; ++index) {
        auto decoded = decode_record(records[index]);
        if (!train_numbers.insert(decoded.first.train_number).second) {
            throw std::runtime_error("corrupt train catalogue: duplicate train number");
        }
        trains.push_back({std::move(decoded.first), decoded.second});
    }
    return trains;
}

std::optional<std::size_t> TrainCatalogueModule::find_index(
    const std::vector<StoredTrain>& trains, std::uint32_t train_number) const {
    for (std::size_t index = 0; index < trains.size(); ++index) {
        if (trains[index].train.train_number == train_number) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::string> TrainCatalogueModule::append_success_audit(
    const Session& session, std::string_view timestamp, std::string_view action,
    std::uint32_t train_number) const {
    try {
        audit_log_.append({std::string(timestamp), session.user_id, role_name(session.role),
                           std::string(action), std::to_string(train_number)});
    } catch (const std::exception& error) {
        return "train mutation persisted but audit logging failed: " + std::string(error.what());
    }
    return std::nullopt;
}

Outcome TrainCatalogueModule::add_train(const Session& session, std::string_view timestamp,
                                        const TrainRecord& train) {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    try {
        if (const auto result = validate_timestamp(timestamp); !result) {
            return Outcome::validation_failure(result.field, result.message);
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "ADD_TRAIN", timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (const auto result = validate_train(train); !result) {
            return Outcome::validation_failure(result.field, result.message);
        }
        auto trains = read_all();
        if (find_index(trains, train.train_number)) {
            return Outcome::refusal("train number is already registered");
        }
        if (trains.size() >= kMaximumTrainRecords) {
            return Outcome::refusal("train catalogue is full");
        }
        journal_->commit({{std::string(kTrainsFile), kTrainRecordSize, trains.size(),
                           encode_record(train, true)}});
        if (const auto error = append_success_audit(session, timestamp, "TRAIN_ADDED",
                                                    train.train_number)) {
            return Outcome::system_failure(*error);
        }
        return Outcome::success("train added", std::to_string(train.train_number));
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome TrainCatalogueModule::update_train(const Session& session, std::string_view timestamp,
                                          std::uint32_t train_number,
                                          const TrainRecord& train) {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    try {
        if (const auto result = validate_timestamp(timestamp); !result) {
            return Outcome::validation_failure(result.field, result.message);
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "UPDATE_TRAIN", timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (train_number == 0) {
            return Outcome::validation_failure("train number", "must be nonzero");
        }
        if (train.train_number != train_number) {
            return Outcome::validation_failure("train number", "cannot be changed on update");
        }
        if (const auto result = validate_train(train); !result) {
            return Outcome::validation_failure(result.field, result.message);
        }
        const auto trains = read_all();
        const auto index = find_index(trains, train_number);
        if (!index || !trains[*index].active) {
            return Outcome::refusal("no active train with that number exists");
        }
        journal_->commit({{std::string(kTrainsFile), kTrainRecordSize, *index,
                           encode_record(train, true)}});
        if (const auto error = append_success_audit(session, timestamp, "TRAIN_UPDATED",
                                                    train_number)) {
            return Outcome::system_failure(*error);
        }
        return Outcome::success("train updated", std::to_string(train_number));
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome TrainCatalogueModule::remove_train(const Session& session, std::string_view timestamp,
                                          std::uint32_t train_number) {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    try {
        if (const auto result = validate_timestamp(timestamp); !result) {
            return Outcome::validation_failure(result.field, result.message);
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "REMOVE_TRAIN", timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (train_number == 0) {
            return Outcome::validation_failure("train number", "must be nonzero");
        }
        const auto trains = read_all();
        const auto index = find_index(trains, train_number);
        if (!index || !trains[*index].active) {
            return Outcome::refusal("no active train with that number exists");
        }
        journal_->commit({{std::string(kTrainsFile), kTrainRecordSize, *index,
                           encode_record(trains[*index].train, false)}});
        if (const auto error = append_success_audit(session, timestamp, "TRAIN_REMOVED",
                                                    train_number)) {
            return Outcome::system_failure(*error);
        }
        return Outcome::success("train removed", std::to_string(train_number));
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

std::optional<TrainRecord> TrainCatalogueModule::find_train(std::uint32_t train_number) const {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    const auto trains = read_all();
    const auto index = find_index(trains, train_number);
    if (!index || !trains[*index].active) {
        return std::nullopt;
    }
    return trains[*index].train;
}

std::vector<TrainRecord> TrainCatalogueModule::list_active_trains() const {
    std::lock_guard<std::mutex> lock(catalogue_mutex());
    const auto trains = read_all();
    std::vector<TrainRecord> active;
    active.reserve(trains.size());
    for (const auto& stored : trains) {
        if (stored.active) {
            active.push_back(stored.train);
        }
    }
    return active;
}

}  // namespace rrs
