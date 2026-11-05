#pragma once

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
    std::uint32_t allocated_seats{0};  // seats held by confirmed bookings
};

// Result of the RRS-F-016 authoritative availability check made inside the lock.
struct SeatAllocation {
    bool allocated{false};
    std::uint32_t free_seats{0};  // free seats re-read inside the lock, before this allocation
};

// Builds the records committed together with the inventory update, from the inventory re-read
// inside the lock. It runs inside the critical section and must not call back into this module.
using AllocationWrites = std::function<std::vector<RecordWrite>(const SeatInventorySnapshot&)>;

// Builds the records committed together with a seat release, from the inventory re-read inside
// the lock. nullopt abandons the release with nothing written. Must not call back into this module.
using ReleaseWrites =
    std::function<std::optional<std::vector<RecordWrite>>(const SeatInventorySnapshot&)>;

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

    // RRS-F-016 critical section (SRS 2.7): takes the inventory lock, re-reads the record,
    // checks that seat_count seats are still free, and commits the allocation together with
    // build_writes' records in one journal commit. Nothing is written when seats are short.
    // Invalid input or an unconfigured train/class throws std::invalid_argument.
    SeatAllocation allocate_seats(std::uint32_t train_number, std::string_view class_code,
                                  std::string_view journey_date, std::uint32_t seat_count,
                                  const AllocationWrites& build_writes);

    // RRS-F-020 critical section: takes the inventory lock, re-reads the record and returns
    // seat_count allocated seats to the free pool in one journal commit with build_writes'
    // records. False when build_writes abandons the release. Releasing more seats than are
    // allocated throws std::runtime_error; invalid input throws std::invalid_argument.
    bool release_seats(std::uint32_t train_number, std::string_view class_code,
                       std::string_view journey_date, std::uint32_t seat_count,
                       const ReleaseWrites& build_writes);

private:
    PersistenceStore store_;
    const ClassFareConfigModule& classes_;
    const AuditLog& audit_log_;
    InventoryTimestampProvider authorization_timestamp_provider_;
    std::unique_ptr<AtomicJournal> journal_;
};

}  // namespace rrs
