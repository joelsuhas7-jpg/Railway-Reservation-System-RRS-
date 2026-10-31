
#pragma once

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

}  // namespace rrs
