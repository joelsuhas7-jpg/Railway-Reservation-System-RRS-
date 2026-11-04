#include "ticket_retrieval.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace rrs {

TicketRetrievalModule::TicketRetrievalModule(PersistenceStore store, const AuditLog& audit_log)
    : store_(std::move(store)), audit_log_(audit_log) {}

TicketLookup TicketRetrievalModule::retrieve(const Session& session, std::string_view timestamp,
                                             std::string_view pnr) const {
    const auto no_such_booking = [] {
        return TicketLookup{Outcome::refusal(std::string(kNoSuchBookingMessage)), std::nullopt};
    };
    try {
        if (session.user_id.empty() || !is_valid_pnr(pnr)) {
            return no_such_booking();
        }
        auto booking = find_booking(store_, pnr);
        if (!booking) {
            return no_such_booking();
        }
        if (session.role != Role::Administrator && booking->user_id != session.user_id) {
            audit_log_.append({std::string(timestamp), session.user_id, "PASSENGER",
                               "TICKET_ACCESS_REFUSED", std::string(pnr)});
            return no_such_booking();
        }
        return TicketLookup{Outcome::success("ticket shown", booking->pnr), std::move(booking)};
    } catch (const std::exception& error) {
        return TicketLookup{Outcome::system_failure(error.what()), std::nullopt};
    }
}

}  // namespace rrs
