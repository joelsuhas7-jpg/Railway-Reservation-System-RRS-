#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "atomic_journal.hpp"
#include "audit_log.hpp"
#include "authentication.hpp"
#include "booking.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"
#include "seat_inventory.hpp"
#include "train_catalogue.hpp"

namespace rrs {

inline constexpr std::string_view kCancellationsFile = "cancellations.dat";
inline constexpr std::size_t kCancellationRecordSize = 128;
inline constexpr std::size_t kDisbursementReferenceMaxLength = 32;

// SRS Table 4.5-1 refund slabs.
enum class RefundSlab { S1, S2, S3, S4 };

const char* refund_slab_name(RefundSlab slab) noexcept;  // "S1" to "S4"
int refund_percentage(RefundSlab slab) noexcept;         // 90, 50, 25, 0

// S1 at 48 h or more, S2 at 12 h or more, S3 at 4 h or more, otherwise S4 (including after
// departure, where the interval is negative).
RefundSlab select_refund_slab(std::int64_t seconds_before_departure) noexcept;

// (total fare x slab percentage) / 100 in integer paise, truncating toward zero.
std::int64_t refund_paise(std::int64_t total_fare_paise, RefundSlab slab) noexcept;

// Seconds from a YYYY-MM-DDTHH:MM:SSZ instant to the scheduled departure (journey date and
// HH:MM), both read on the same clock (A-06). nullopt when either value is malformed.
std::optional<std::int64_t> seconds_until_departure(std::string_view now_timestamp,
                                                    std::string_view journey_date,
                                                    std::string_view departure_time);

enum class RefundStatus { Pending, Settled };

// One cancellations.dat record (SRS 3.4). No disbursement reference means PENDING.
struct CancellationRecord {
    std::string pnr;
    std::string cancelled_at;
    RefundSlab slab{RefundSlab::S4};
    std::int64_t refund_paise{0};
    std::string disbursement_reference;

    RefundStatus status() const noexcept {
        return disbursement_reference.empty() ? RefundStatus::Pending : RefundStatus::Settled;
    }
};

const char* refund_status_name(RefundStatus status) noexcept;  // "PENDING" or "SETTLED"

std::string encode_cancellation(const CancellationRecord& record);
CancellationRecord decode_cancellation(std::string_view bytes);
std::vector<CancellationRecord> read_cancellations(const PersistenceStore& store);

// What UC-02 M3 to M5 show before the passenger confirms.
struct CancellationQuote {
    Outcome outcome;                               // success when cancellation may proceed
    std::optional<BookingRecord> booking;          // set once ownership is verified
    std::optional<CancellationRecord> existing;    // set for an already cancelled booking (B3)
    std::string departure;                         // "YYYY-MM-DD HH:MM" compared against
    RefundSlab slab{RefundSlab::S4};
    std::int64_t refund_paise{0};
};

// M15 CancellationRefundModule: cancellation eligibility, refund and settlement (UC-02).
class CancellationRefundModule final {
public:
    CancellationRefundModule(PersistenceStore store, const TrainCatalogueModule& trains,
                             SeatInventoryModule& inventory, const AuditLog& audit_log);

    CancellationRefundModule(const CancellationRefundModule&) = delete;
    CancellationRefundModule& operator=(const CancellationRefundModule&) = delete;
    CancellationRefundModule(CancellationRefundModule&&) = delete;
    CancellationRefundModule& operator=(CancellationRefundModule&&) = delete;

    // M1 to M5: ownership through M14, status (B3, B4) and slab (B5). Writes nothing.
    CancellationQuote quote(const Session& session, std::string_view now,
                            std::string_view pnr) const;

    // M7 to M9 (RRS-F-019, RRS-F-020): re-checks status and slab inside the inventory lock,
    // then commits the CANCELLED booking, the PENDING cancellation record and the seat
    // release together. Success carries the PNR as its record identifier.
    Outcome cancel(const Session& session, std::string_view now, std::string_view pnr);

    // M10 (RRS-F-021): records the disbursement reference, moving PENDING to SETTLED.
    Outcome settle_refund(const Session& session, std::string_view timestamp,
                          std::string_view pnr, std::string_view disbursement_reference);

    std::optional<CancellationRecord> find_cancellation(std::string_view pnr) const;

private:
    PersistenceStore store_;
    const TrainCatalogueModule& trains_;
    SeatInventoryModule& inventory_;
    const AuditLog& audit_log_;
    std::unique_ptr<AtomicJournal> journal_;
};

}  // namespace rrs
