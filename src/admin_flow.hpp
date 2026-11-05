#pragma once

#include <iosfwd>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// M17 console flows for administrator master data (RRS-F-006 to RRS-F-008). An empty timestamp
// provider uses utc_timestamp_now(); tests can supply a deterministic provider.

Outcome manage_trains_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                   const UserStore& users, const AuditLog& audit_log,
                                   const Session& session,
                                   const AuditTimestampProvider& timestamp_provider,
                                   const SessionActivityCheck& session_activity);

Outcome manage_classes_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                    const UserStore& users, const AuditLog& audit_log,
                                    const Session& session,
                                    const AuditTimestampProvider& timestamp_provider,
                                    const SessionActivityCheck& session_activity);

Outcome manage_fares_from_console(std::istream& in, std::ostream& out, std::ostream& err,
                                  const UserStore& users, const AuditLog& audit_log,
                                  const Session& session,
                                  const AuditTimestampProvider& timestamp_provider,
                                  const SessionActivityCheck& session_activity);

// Registers Manage trains, Manage travel classes and Manage class fares.
void register_admin_flows(MenuRouter& router);

}  // namespace rrs
