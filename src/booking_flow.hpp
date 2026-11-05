#pragma once

#include <iosfwd>

#include "authentication.hpp"
#include "booking.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// Prints the PNR with the full ticket detail (UC-01 M10).
void render_ticket(std::ostream& out, const BookingRecord& booking);

// UC-01 Book ticket (RRS-F-012 to RRS-F-017): shows the train's classes, collects the class,
// passengers, fare confirmation and payment outcome, then books and prints the ticket.
Outcome book_ticket_from_console(const AppContext& context, const Session& session,
                                 const SessionActivityCheck& session_activity);

// Registers Book ticket.
void register_booking_flows(MenuRouter& router);

}  // namespace rrs
