#pragma once

#include <optional>
#include <string_view>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "booking.hpp"
#include "console_ui.hpp"
#include "persistence.hpp"

namespace rrs {

// One message for an unknown, malformed or unowned PNR, so ownership cannot be probed.
inline constexpr std::string_view kNoSuchBookingMessage = "no such booking";

struct TicketLookup {
    Outcome outcome;
    std::optional<BookingRecord> booking;  // set only on success
};

// M14 TicketRetrievalModule: retrieves a ticket using its PNR (RRS-F-018).
class TicketRetrievalModule final {
public:
    TicketRetrievalModule(PersistenceStore store, const AuditLog& audit_log);

    // The owner or an administrator receives the booking. Anyone else receives
    // kNoSuchBookingMessage, and a refused access to an existing booking is audited
    // (RRS-SR-005).
    TicketLookup retrieve(const Session& session, std::string_view timestamp,
                          std::string_view pnr) const;

private:
    PersistenceStore store_;
    const AuditLog& audit_log_;
};

}  // namespace rrs
