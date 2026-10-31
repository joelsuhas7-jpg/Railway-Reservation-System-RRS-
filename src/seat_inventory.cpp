#include "seat_inventory.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "authorization.hpp"
#include "platform_shim.hpp"

namespace rrs {
namespace {

constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kTrainOffset = 1;
constexpr std::size_t kClassOffset = 5;
constexpr std::size_t kDateOffset = 7;
constexpr std::size_t kTotalOffset = 17;
constexpr std::size_t kBlockedOffset = 21;
constexpr std::size_t kReservedOffset = 25;
constexpr std::uint8_t kVersion = 1;

struct StoredInventory {
    std::uint32_t train_number{0};
    std::string class_code;
    std::string journey_date;
    std::uint32_t total_seats{0};
    std::uint32_t blocked_seats{0};
};

std::mutex& inventory_mutex() {
    static std::mutex mutex;
    return mutex;
}

bool valid_date(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) {
            return false;
        }
    }
    const auto two = [&date](std::size_t at) {
        return (date[at] - '0') * 10 + (date[at + 1] - '0');
    };
    const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
                     (date[2] - '0') * 10 + (date[3] - '0');
    const int month = two(5);
    const int day = two(8);
    if (year == 0 || month < 1 || month > 12) {
        return false;
    }
    constexpr std::array<int, 12> days{31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
    const bool leap = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
    const int maximum = days[static_cast<std::size_t>(month - 1)] +
                        ((month == 2 && leap) ? 1 : 0);
    return day >= 1 && day <= maximum;
}

bool valid_timestamp(std::string_view value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
        value[19] != 'Z') {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool separator = i == 4 || i == 7 || i == 10 ||
                               i == 13 || i == 16 || i == 19;
        if (!separator && (value[i] < '0' || value[i] > '9')) {
            return false;
        }
    }
    const auto two = [&value](std::size_t at) {
        return (value[at] - '0') * 10 + (value[at + 1] - '0');
    };
    const int year = (value[0] - '0') * 1000 + (value[1] - '0') * 100 +
                     (value[2] - '0') * 10 + (value[3] - '0');
    return year > 0 && valid_date(value.substr(0, 10)) &&
           two(11) <= 23 && two(13) <= 59 && two(17) <= 59;
}

std::string inventory_id(std::uint32_t train, std::string_view code,
                         std::string_view date) {
    return std::to_string(train) + ":" + std::string(code) + ":" + std::string(date);
}

std::string encode(const StoredInventory& value) {
    std::string bytes(kInventoryRecordSize, '\0');
    bytes[kVersionOffset] = static_cast<char>(kVersion);
    const auto put_u32 = [&bytes](std::size_t offset, std::uint32_t number) {
        for (unsigned int byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] = static_cast<char>((number >> (byte * 8U)) & 0xffU);
        }
    };
    put_u32(kTrainOffset, value.train_number);
    bytes.replace(kClassOffset, 2, value.class_code);
    bytes.replace(kDateOffset, 10, value.journey_date);
    put_u32(kTotalOffset, value.total_seats);
    put_u32(kBlockedOffset, value.blocked_seats);
    return bytes;
}

StoredInventory decode(std::string_view bytes) {
    if (bytes.size() != kInventoryRecordSize ||
        static_cast<std::uint8_t>(bytes[kVersionOffset]) != kVersion) {
        throw std::runtime_error("corrupt inventory record: unsupported size or version");
    }
    if (std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(kReservedOffset),
                    bytes.end(), [](char value) { return value != '\0'; })) {
        throw std::runtime_error("corrupt inventory record: reserved bytes are not zero");
    }
    const auto get_u32 = [&bytes](std::size_t offset) {
        std::uint32_t value = 0;
        for (unsigned int byte = 0; byte < 4; ++byte) {
            value |= static_cast<std::uint32_t>(
                         static_cast<unsigned char>(bytes[offset + byte])) << (byte * 8U);
        }
        return value;
    };
    StoredInventory value;
    value.train_number = get_u32(kTrainOffset);
    value.class_code = std::string(bytes.substr(kClassOffset, 2));
    value.journey_date = std::string(bytes.substr(kDateOffset, 10));
    value.total_seats = get_u32(kTotalOffset);
    value.blocked_seats = get_u32(kBlockedOffset);
    if (value.train_number == 0 || value.class_code.size() != 2 ||
        !valid_date(value.journey_date) || value.total_seats == 0 ||
        value.blocked_seats > value.total_seats) {
        throw std::runtime_error("corrupt inventory record: invalid field values");
    }
    return value;
}

std::vector<StoredInventory> read_all(const PersistenceStore& store) {
    const auto raw = store.read_records(kInventoryFile, kInventoryRecordSize);
    std::vector<StoredInventory> records;
    records.reserve(raw.size());
    for (const auto& bytes : raw) {
        auto record = decode(bytes);
        const auto duplicate = std::find_if(records.begin(), records.end(),
            [&record](const StoredInventory& existing) {
                return existing.train_number == record.train_number &&
                       existing.class_code == record.class_code &&
                       existing.journey_date == record.journey_date;
            });
        if (duplicate != records.end()) {
            throw std::runtime_error("corrupt inventory: duplicate train/class/date key");
        }
        records.push_back(std::move(record));
    }
    return records;
}

std::size_t find_index(const std::vector<StoredInventory>& records,
                       std::uint32_t train, std::string_view code,
                       std::string_view date) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (records[i].train_number == train && records[i].class_code == code &&
            records[i].journey_date == date) {
            return i;
        }
    }
    return records.size();
}

SeatInventorySnapshot snapshot(const StoredInventory& value) {
    return {value.train_number, value.class_code, value.journey_date,
            value.total_seats, value.blocked_seats,
            value.total_seats - value.blocked_seats};
}

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

}  // namespace

SeatInventoryModule::SeatInventoryModule(
    PersistenceStore store, const ClassFareConfigModule& classes,
    const AuditLog& audit_log,
    InventoryTimestampProvider authorization_timestamp_provider)
    : store_(std::move(store)), classes_(classes), audit_log_(audit_log),
      authorization_timestamp_provider_(std::move(authorization_timestamp_provider)) {
    journal_ = std::make_unique<AtomicJournal>(store_);
}

SeatInventorySnapshot SeatInventoryModule::get_inventory(
    std::uint32_t train_number, std::string_view class_code,
    std::string_view journey_date) {
    if (train_number == 0 || class_code.size() != 2 || !valid_date(journey_date)) {
        throw std::invalid_argument("train, class code or journey date is invalid");
    }
    std::lock_guard<std::mutex> lock(inventory_mutex());
    FileLock process_lock(store_.data_directory() / ".rrs-seat-inventory.lock");
    auto records = read_all(store_);
    const auto index = find_index(records, train_number, class_code, journey_date);
    if (index != records.size()) {
        return snapshot(records[index]);
    }

    const auto configured = classes_.find_class(train_number, class_code);
    if (!configured.train_exists || !configured.record) {
        throw std::invalid_argument("train or travel class is not configured");
    }
    const StoredInventory created{train_number, std::string(class_code),
                                  std::string(journey_date),
                                  configured.record->capacity, 0};
    journal_->commit(std::vector<RecordWrite>{
        RecordWrite{std::string(kInventoryFile), kInventoryRecordSize,
                    records.size(), encode(created)}});
    return snapshot(created);
}

Outcome SeatInventoryModule::block_seats(
    const Session& session, std::string_view timestamp,
    std::uint32_t train_number, std::string_view class_code,
    std::string_view journey_date, std::int64_t seat_count) {
    std::lock_guard<std::mutex> lock(inventory_mutex());
    FileLock process_lock(store_.data_directory() / ".rrs-seat-inventory.lock");
    try {
        if (!authorization_timestamp_provider_) {
            return Outcome::system_failure("authorization timestamp provider is not configured");
        }
        const auto auth_timestamp = authorization_timestamp_provider_();
        if (!valid_timestamp(auth_timestamp)) {
            return Outcome::system_failure("authorization timestamp provider returned an invalid UTC timestamp");
        }
        const auto authorization = AuthorizationModule::require_administrator(
            session, "BLOCK_SEATS", auth_timestamp, audit_log_);
        if (!authorization) {
            return Outcome::refusal(authorization.message);
        }
        if (!valid_timestamp(timestamp)) {
            return Outcome::validation_failure("timestamp", "must be a valid ISO 8601 UTC timestamp");
        }
        if (train_number == 0 || class_code.size() != 2) {
            return Outcome::validation_failure("train/class", "must identify a valid train and class");
        }
        if (!valid_date(journey_date)) {
            return Outcome::validation_failure("journey date", "must be a valid YYYY-MM-DD date");
        }
        if (seat_count <= 0 ||
            static_cast<std::uint64_t>(seat_count) > std::numeric_limits<std::uint32_t>::max()) {
            return Outcome::validation_failure("seat count", "must be a positive 32-bit integer");
        }

        auto records = read_all(store_);
        auto index = find_index(records, train_number, class_code, journey_date);
        if (index == records.size()) {
            const auto configured = classes_.find_class(train_number, class_code);
            if (!configured.train_exists || !configured.record) {
                return Outcome::refusal("train or travel class is not configured");
            }
            records.push_back({train_number, std::string(class_code),
                               std::string(journey_date), configured.record->capacity, 0});
            index = records.size() - 1;
        }
        auto& record = records[index];
        const auto requested = static_cast<std::uint32_t>(seat_count);
        const auto free = record.total_seats - record.blocked_seats;
        if (requested > free) {
            return Outcome::refusal("cannot block more seats than are currently free");
        }
        record.blocked_seats += requested;
        RecordWrite write;
        write.file_name = std::string(kInventoryFile);
        write.record_size = kInventoryRecordSize;
        write.index = index;
        write.record = encode(record);
        std::vector<RecordWrite> writes;
        writes.push_back(std::move(write));
        journal_->commit(writes);
        const auto id = inventory_id(train_number, class_code, journey_date);
        try {
            audit_log_.append({std::string(timestamp), session.user_id, role_name(session.role),
                               "SEATS_BLOCKED", id});
        } catch (const std::exception& error) {
            return Outcome::system_failure(
                "seat block persisted but audit logging failed: " + std::string(error.what()));
        }
        return Outcome::success("seats blocked; " + std::to_string(record.total_seats -
                              record.blocked_seats) + " free seats remain", id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

}  // namespace rrs
