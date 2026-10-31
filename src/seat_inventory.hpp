#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "atomic_journal.hpp"
#include "audit_log.hpp"
#include "authentication.hpp"
#include "class_fare_config.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"

namespace rrs {

inline constexpr std::string_view kInventoryFile = "inventory.dat";
inline constexpr std::size_t kInventoryRecordSize = 32;
using InventoryTimestampProvider = std::function<std::string()>;

struct SeatInventorySnapshot {
    std::uint32_t train_number{0};
    std::string class_code;
    std::string journey_date;
    std::uint32_t total_seats{0};
    std::uint32_t blocked_seats{0};
    std::uint32_t free_seats{0};
};

class SeatInventoryModule final {
public:
    SeatInventoryModule(PersistenceStore store, const ClassFareConfigModule& classes,
                        const AuditLog& audit_log,
                        InventoryTimestampProvider authorization_timestamp_provider);

    SeatInventoryModule(const SeatInventoryModule&) = delete;
    SeatInventoryModule& operator=(const SeatInventoryModule&) = delete;
    SeatInventoryModule(SeatInventoryModule&&) = delete;
    SeatInventoryModule& operator=(SeatInventoryModule&&) = delete;

    // First reference creates inventory from the configured class capacity.
    // Invalid dates and unknown trains/classes are refused; corrupt storage throws.
    SeatInventorySnapshot get_inventory(std::uint32_t train_number,
                                         std::string_view class_code,
                                         std::string_view journey_date);

    // An authenticated administrator may block additional seats. The count is cumulative.
    Outcome block_seats(const Session& session, std::string_view timestamp,
                        std::uint32_t train_number, std::string_view class_code,
                        std::string_view journey_date, std::int64_t seat_count);

private:
    PersistenceStore store_;
    const ClassFareConfigModule& classes_;
    const AuditLog& audit_log_;
    InventoryTimestampProvider authorization_timestamp_provider_;
    std::unique_ptr<AtomicJournal> journal_;
};

}  // namespace rrs
