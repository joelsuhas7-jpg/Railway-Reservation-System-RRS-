#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"
#include "session_context.hpp"

namespace rrs {

// Console flows that connect M17 prompts to M4 account operations, and the login session loop.

struct AccountForm {
    std::string user_id;
    std::string password;
    std::string contact;
};

// Prompts for user id, password and contact, one line each. nullopt at end of input.
std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out);

// Maps an M4 result to one of the four M17 outcomes; record_id is shown on success.
Outcome to_outcome(const AuthResult& result, const std::string& record_id);

// RRS-F-004: repeats the administrator prompts until the bootstrap succeeds. Returns false if
// input ends first. Returns true at once when an account already exists. Audited as
// ADMIN_BOOTSTRAPPED. I/O errors throw.
bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users, const AuditLog& audit_log,
                   const AuditTimestampProvider& timestamp_provider);

// RRS-F-001: the public menu's Register action; audited as PASSENGER_REGISTERED.
Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                              const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider);

struct ConsoleLogin {
    Outcome outcome;
    std::optional<Session> session;
};

// RRS-F-002: prompts for user id and password and authenticates; audited as LOGIN_SUCCESS or
// LOGIN_FAILED.
ConsoleLogin login_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                const AuditLog& audit_log,
                                const AuditTimestampProvider& timestamp_provider);

// The menu tree for a session's role (RRS-F-002).
MenuTree menu_for(const Session& session);

// ISO 8601 UTC timestamp for audit entries, e.g. "2026-10-30T18:30:00Z".
std::string format_utc_timestamp(std::int64_t epoch_seconds);
std::string utc_timestamp_now();

// RRS-F-005: Change password for the session's own account; audited as PASSWORD_CHANGE.
Outcome change_password_from_console(std::istream& in, std::ostream& out,
                                     const UserStore& users, const Session& session,
                                     const AuditLog& audit);

// RRS-F-005: administrator resets a passenger's password; audited as PASSWORD_RESET.
Outcome reset_password_from_console(std::istream& in, std::ostream& out,
                                    const UserStore& users, const Session& session,
                                    const AuditLog& audit);

// Registers Change password and Reset passenger password.
void register_account_flows(MenuRouter& router);

// RRS-F-002, RRS-F-003, RRS-SR-007: the public menu's Login action. On success reports the
// login, runs the role's menu until Logout, end of input or 15 minutes of inactivity, then
// discards the session; the caller's public menu resumes. Every action passes M5 authorisation
// before the router serves it.
Outcome login_and_run_session(const AppContext& context, const MenuRouter& router);

// Earlier entry points, kept so existing callers compile. Change password and Reset passenger
// password are served here; the handler serves the role menu's other actions.
Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const ActionHandler& role_handler);

using InteractiveSessionActionHandler =
    std::function<Outcome(MenuAction, const Session&, const SessionActivityCheck&)>;
Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const InteractiveSessionActionHandler& role_handler,
                              const SessionClockProvider& clock_provider = {});

}  // namespace rrs
