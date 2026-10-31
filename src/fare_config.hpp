#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "atomic_journal.hpp"
#include "audit_log.hpp"
#include "authentication.hpp"
#include "class_fare_config.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace rrs {

inline constexpr std::string_view kFaresFile = "fares.dat";
inline constexpr std::size_t kFareRecordSize = 32;
inline constexpr std::string_view kFareUnavailableMessage = "class not open for booking";

using FareTimestampProvider = std::function<std::string()>;

struct FareRecord {
    std::uint32_t train_number{0};
    std::string class_code;
    std::int64_t fare_paise{0};
    std::string effective_from;
    std::uint64_t record_revision{0};
};

enum class FareLookupState {
    Found,
    NoApplicableFare,
    UnknownTrain,
    ClassNotConfigured,
    InvalidJourneyDate,
};

struct FareLookupResult {
    FareLookupState state{FareLookupState::NoApplicableFare};
    std::optional<FareRecord> record;
    std::string message;
};

// Parses a positive decimal amount into integer paise without floating-point arithmetic.
std::optional<std::int64_t> parse_fare_paise(std::string_view amount);

// Validates a Gregorian ISO 8601 date in YYYY-MM-DD form.
bool is_valid_fare_date(std::string_view date) noexcept;

class FareConfigModule final {
public:
    FareConfigModule(PersistenceStore store, const TrainCatalogueModule& trains,
                     const ClassFareConfigModule& classes, const AuditLog& audit_log,
                     FareTimestampProvider timestamp_provider);

    FareConfigModule(const FareConfigModule&) = delete;
    FareConfigModule& operator=(const FareConfigModule&) = delete;
    FareConfigModule(FareConfigModule&&) = delete;
    FareConfigModule& operator=(FareConfigModule&&) = delete;

    Outcome configure_fare(const Session& session, std::uint32_t train_number,
                           std::string_view class_code, std::int64_t fare_paise,
                           std::string_view effective_from);
    Outcome replace_fare(const Session& session, std::uint32_t train_number,
                         std::string_view class_code,
                         std::string_view selected_effective_from,
                         std::uint64_t expected_record_revision,
                         std::int64_t expected_fare_paise,
                         std::int64_t fare_paise, std::string_view new_effective_from);

    std::vector<FareRecord> list_fares(std::uint32_t train_number,
                                       std::string_view class_code) const;
    FareLookupResult find_applicable_fare(std::uint32_t train_number,
                                          std::string_view class_code,
                                          std::string_view journey_date) const;

private:
    std::vector<FareRecord> read_all() const;
    std::optional<std::string> authorize_and_get_timestamp(const Session& session,
                                                           Outcome& failure) const;
    Outcome append_success_audit(const Session& session, std::string_view timestamp,
                                 std::string_view action, const FareRecord& record) const;
    bool valid_pair(std::uint32_t train_number, std::string_view class_code,
                    Outcome& failure) const;

    PersistenceStore store_;
    const TrainCatalogueModule& trains_;
    const ClassFareConfigModule& classes_;
    const AuditLog& audit_log_;
    FareTimestampProvider timestamp_provider_;
    std::unique_ptr<AtomicJournal> journal_;
};

}  // namespace rrs
