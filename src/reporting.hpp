#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "booking.hpp"
#include "cancellation_refund.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"

namespace rrs {

// Journey dates are inclusive; an absent bound leaves that side open.
struct ReportFilter {
    std::optional<std::uint32_t> train_number;
    std::optional<std::string> from_date;
    std::optional<std::string> to_date;
};

struct ReportRow {
    BookingRecord booking;
    std::optional<CancellationRecord> cancellation;
};

struct ReservationReport {
    std::vector<ReportRow> rows;                    // file order of bookings.dat
    std::size_t booking_count{0};
    std::size_t cancellation_count{0};
    std::int64_t fare_collected_paise{0};           // total fare of every booking in the filter
    std::int64_t refund_payable_paise{0};           // refund of every cancellation in the filter
    std::int64_t refund_outstanding_paise{0};       // refunds still PENDING
    std::vector<CancellationRecord> outstanding;    // PENDING refunds (UC-02 B8)
};

struct ReportResult {
    Outcome outcome;
    ReservationReport report;  // filled only on success
};

// M16 ReportingModule: booking and cancellation report for administrators (RRS-F-022).
class ReportingModule final {
public:
    ReportingModule(PersistenceStore store, const AuditLog& audit_log);

    // A non-administrator is refused and the attempt audited (RRS-SR-005). A malformed date
    // or a from date after the to date is a validation failure. Success audits REPORT_VIEWED.
    ReportResult build(const Session& session, std::string_view timestamp,
                       const ReportFilter& filter) const;

private:
    PersistenceStore store_;
    const AuditLog& audit_log_;
};

}  // namespace rrs
