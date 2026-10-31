#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// RRS-F-010 to RRS-F-012: passenger Search trains. Prompts for source, destination and journey
// date, then shows each matching train with the fare and free seats of every configured class.
// The decision date and time come from the context's UTC timestamp.
Outcome search_trains_from_console(const AppContext& context, const Session& session,
                                   const SessionActivityCheck& session_activity);

// RRS-F-011, RRS-F-012: passenger View train detail and class availability for one train and
// journey date.
Outcome view_train_detail_from_console(const AppContext& context, const Session& session,
                                       const SessionActivityCheck& session_activity);

// Registers Search trains and View train detail and class availability.
void register_search_flows(MenuRouter& router);

}  // namespace rrs
