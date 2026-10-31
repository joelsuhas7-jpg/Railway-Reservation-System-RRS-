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
#include "console_ui.hpp"
#include "persistence.hpp"
#include "train_catalogue.hpp"

namespace rrs {

inline constexpr std::string_view kClassesFile = "classes.dat";
inline constexpr std::size_t kClassRecordSize = 16;
inline constexpr std::size_t kMaximumClassesPerTrain = 10;
// Trusted ISO 8601 UTC timestamp source for M5 denial auditing.
using ClassAuditTimestampProvider = std::function<std::string()>;

struct ClassRecord {
    std::uint32_t train_number{0};
    std::string class_code;
    std::uint32_t capacity{0};
};

struct ClassLookupResult {
    bool train_exists{false};
    std::optional<ClassRecord> record;
};

struct ClassListResult {
    bool train_exists{false};
    std::vector<ClassRecord> records;
};

class ClassFareConfigModule final {
public:
    ClassFareConfigModule(PersistenceStore store, const TrainCatalogueModule& trains,
                          const AuditLog& audit_log,
                          ClassAuditTimestampProvider authorization_timestamp_provider);

    ClassFareConfigModule(const ClassFareConfigModule&) = delete;
    ClassFareConfigModule& operator=(const ClassFareConfigModule&) = delete;
    ClassFareConfigModule(ClassFareConfigModule&&) = delete;
    ClassFareConfigModule& operator=(ClassFareConfigModule&&) = delete;

    Outcome add_class(const Session& session, std::string_view timestamp,
                      std::uint32_t train_number, std::string_view class_code,
                      std::int64_t capacity);
    Outcome update_class(const Session& session, std::string_view timestamp,
                         std::uint32_t train_number, std::string_view class_code,
                         std::int64_t capacity);

    ClassLookupResult find_class(std::uint32_t train_number,
                                 std::string_view class_code) const;
    ClassListResult list_classes(std::uint32_t train_number) const;

private:
    std::vector<ClassRecord> read_all() const;
    std::optional<std::size_t> find_index(const std::vector<ClassRecord>& records,
                                          std::uint32_t train_number,
                                          std::string_view class_code) const;
    Outcome append_success_audit(const Session& session, std::string_view timestamp,
                                 std::string_view action, std::uint32_t train_number,
                                 std::string_view class_code) const;

    PersistenceStore store_;
    const TrainCatalogueModule& trains_;
    std::unique_ptr<AtomicJournal> journal_;
    const AuditLog& audit_log_;
    ClassAuditTimestampProvider authorization_timestamp_provider_;
};

}  // namespace rrs
