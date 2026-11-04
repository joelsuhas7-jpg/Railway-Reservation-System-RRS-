#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// UC-02 Cancel ticket (RRS-F-019 to RRS-F-021): shows the ticket, slab and refund, cancels on
// confirmation, then records the disbursement reference or leaves the refund PENDING.
Outcome cancel_ticket_from_console(const AppContext& context, const Session& session,
                                   const SessionActivityCheck& session_activity);

// Registers Cancel ticket.
void register_cancel_flows(MenuRouter& router);

}  // namespace rrs
