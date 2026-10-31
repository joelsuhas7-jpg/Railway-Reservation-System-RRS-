
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"

namespace rrs {

using AuditTimestampProvider = std::function<std::string()>;

struct AccountForm {
    std::string user_id;
    std::string password;
    std::string contact;
};

std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out);

Outcome to_outcome(const AuthResult& result, const std::string& record_id);

bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users, const AuditLog& audit_log,
                   const AuditTimestampProvider& timestamp_provider);

Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                              const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider);

struct ConsoleLogin {
    Outcome outcome;
    std::optional<Session> session;
};

ConsoleLogin login_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                const AuditLog& audit_log,
                                const AuditTimestampProvider& timestamp_provider);

MenuTree menu_for(const Session& session);

std::string format_utc_timestamp(std::int64_t epoch_seconds);
std::string utc_timestamp_now();

using AuditTimestampProvider = std::function<std::string()>;
// Monotonic clock seam for deterministic idle-timeout tests.
using SessionClockProvider =
    std::function<std::chrono::steady_clock::time_point()>;
// Checks expiry; pass true after a user input line is accepted to mark activity.
using SessionActivityCheck = std::function<bool(bool)>;

// M17 console flow for the administrator train catalogue menu. An empty timestamp provider
// uses utc_timestamp_now(); tests can supply a deterministic provider.
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

// RRS-F-005: Change password for the session's own account; audited as PASSWORD_CHANGE.
Outcome change_password_from_console(std::istream& in, std::ostream& out,
                                     const UserStore& users, const Session& session,
                                     const AuditLog& audit);

Outcome reset_password_from_console(std::istream& in, std::ostream& out,
                                    const UserStore& users, const Session& session,
                                    const AuditLog& audit);

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const ActionHandler& role_handler);

// Interaction-aware overload for handlers that collect multiple inputs within one action.
using InteractiveSessionActionHandler =
    std::function<Outcome(MenuAction, const Session&, const SessionActivityCheck&)>;
Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const InteractiveSessionActionHandler& role_handler,
                              const SessionClockProvider& clock_provider = {});

}  // namespace rrs
