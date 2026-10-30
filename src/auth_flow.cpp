#include "auth_flow.hpp"

#include <exception>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <utility>

#include "audit_log.hpp"
#include "authorization.hpp"

namespace rrs {
namespace {

constexpr auto kSessionIdleTimeout = std::chrono::minutes(15);

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream formatted;
    formatted << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return formatted.str();
}

bool read_field(std::istream& in, std::ostream& out, const char* label, std::string& value) {
    out << label << ": " << std::flush;
    if (!std::getline(in, value)) {
        out << '\n';
        return false;
    }
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return true;
}

}  // namespace

std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out) {
    AccountForm form;
    if (!read_field(in, out, "User id", form.user_id) ||
        !read_field(in, out, "Password", form.password) ||
        !read_field(in, out, "Contact", form.contact)) {
        return std::nullopt;
    }
    return form;
}

Outcome to_outcome(const AuthResult& result, const std::string& record_id) {
    switch (result.status) {
        case AuthStatus::Ok:
            return Outcome::success(result.message, record_id);
        case AuthStatus::InvalidInput:
            return Outcome::validation_failure(result.field, result.message);
        case AuthStatus::Refused:
            break;
    }
    return Outcome::refusal(result.message);
}

bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users, const AuditLog& audit_log,
                   const AuditTimestampProvider& timestamp_provider) {
    if (!bootstrap_required(users)) {
        return true;
    }
    out << "First run: create the administrator account.\n";
    while (true) {
        const auto form = read_account_form(in, out);
        if (!form) {
            return false;
        }
        const auto result = bootstrap_admin(users, form->user_id, form->password, form->contact);
        report_outcome(out, err, to_outcome(result, form->user_id));
        if (result) {
            audit_log.append({timestamp_provider(), form->user_id, "ADMIN",
                              "ADMIN_BOOTSTRAPPED", form->user_id});
            return true;
        }
    }
}

Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                              const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider) {
    const auto form = read_account_form(in, out);
    if (!form) {
        return Outcome::refusal("registration cancelled");
    }
    try {
        const auto result = register_passenger(users, form->user_id, form->password, form->contact);
        if (result) {
            audit_log.append({timestamp_provider(), form->user_id, "PASSENGER",
                              "PASSENGER_REGISTERED", form->user_id});
        }
        return to_outcome(result, form->user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

ConsoleLogin login_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                const AuditLog& audit_log,
                                const AuditTimestampProvider& timestamp_provider) {
    std::string user_id;
    std::string password;
    if (!read_field(in, out, "User id", user_id) || !read_field(in, out, "Password", password)) {
        return {Outcome::refusal("login cancelled"), std::nullopt};
    }
    try {
        auto login = authenticate(users, user_id, password);
        const auto account = users.find(user_id);
        const auto actor = login.session
                               ? std::string_view(login.session->user_id)
                               : (validate_user_id(user_id) ? user_id : std::string_view("UNKNOWN"));
        const auto role = account ? (account->role == Role::Administrator ? "ADMIN" : "PASSENGER")
                                  : "UNKNOWN";
        audit_log.append({timestamp_provider(), std::string(actor), role,
                          login.session ? "LOGIN_SUCCESS" : "LOGIN_FAILED",
                          account ? account->user_id : "-"});
        if (!login.session) {
            return {to_outcome(login.result, {}), std::nullopt};
        }
        return {Outcome::success(login.result.message, login.session->user_id),
                std::move(login.session)};
    } catch (const std::exception& error) {
        return {Outcome::system_failure(error.what()), std::nullopt};
    }
}

MenuTree menu_for(const Session& session) {
    return session.role == Role::Administrator ? MenuTree::Administrator : MenuTree::Passenger;
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const ActionHandler& role_handler) {
    auto login = login_from_console(in, out, users, audit_log, timestamp_provider);
    if (!login.session) {
        return login.outcome;
    }
    report_outcome(out, err, login.outcome);
    const Session session = std::move(*login.session);
    auto last_activity = std::chrono::steady_clock::now();
    bool timed_out = false;
    run_menu(in, out, err, menu_for(session), [&](MenuAction action) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_activity >= kSessionIdleTimeout) {
            audit_log.append({utc_timestamp(), session.user_id,
                              session.role == Role::Administrator ? "ADMINISTRATOR" : "PASSENGER",
                              "SESSION_IDLE_TIMEOUT", "-"});
            timed_out = true;
            return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
        }
        last_activity = now;
        return AuthorizationModule::invoke(&session, action, [&](MenuAction authorized_action) {
            if (authorized_action == MenuAction::Logout) {
                return Outcome::success("logged out", session.user_id);
            }
            return role_handler(authorized_action);
        });
    }, [&](MenuAction) { return timed_out; });
    // The session ends here (RRS-F-003); the caller's public menu is shown next.
    return Outcome::success("returned to the main menu");
}

}  // namespace rrs
