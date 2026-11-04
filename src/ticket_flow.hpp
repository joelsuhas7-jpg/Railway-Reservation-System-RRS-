#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// RRS-F-018: View ticket by PNR. Prints the full ticket for its owner or an administrator;
// every other case is refused as "no such booking".
Outcome view_ticket_from_console(const AppContext& context, const Session& session,
                                 const SessionActivityCheck& session_activity);

// Registers View ticket by PNR.
void register_ticket_flows(MenuRouter& router);

}  // namespace rrs
