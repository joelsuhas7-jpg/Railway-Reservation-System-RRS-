#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// RRS-F-009: administrator Adjust seat availability. Blocks seats for a train, class and journey
// date; the outcome reports the remaining free count.
Outcome adjust_seats_from_console(const AppContext& context, const Session& session,
                                  const SessionActivityCheck& session_activity);

// Registers Adjust seat availability.
void register_inventory_flows(MenuRouter& router);

}  // namespace rrs
