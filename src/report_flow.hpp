#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// RRS-F-022: administrator Booking and cancellation report. Prompts for an optional train and
// journey-date range, prints the rows, totals and outstanding refunds, then settles PENDING
// refunds by PNR until a blank PNR (UC-02 M10, B8).
Outcome booking_report_from_console(const AppContext& context, const Session& session,
                                    const SessionActivityCheck& session_activity);

// Registers Booking and cancellation report.
void register_report_flows(MenuRouter& router);

}  // namespace rrs
