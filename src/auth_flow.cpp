#include "auth_flow.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <istream>
#include <ostream>
#include <utility>

#include "audit_log.hpp"
#include "authorization.hpp"

namespace rrs {
namespace {

constexpr auto kSessionIdleTimeout = std::chrono::minutes(15);

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
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
                   const UserStore& users) {
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
            return true;
        }
    }
}

Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users) {
    const auto form = read_account_form(in, out);
    if (!form) {
        return Outcome::refusal("registration cancelled");
    }
    try {
        return to_outcome(register_passenger(users, form->user_id, form->password, form->contact),
                          form->user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

ConsoleLogin login_from_console(std::istream& in, std::ostream& out, const UserStore& users) {
    std::string user_id;
    std::string password;
    if (!read_field(in, out, "User id", user_id) || !read_field(in, out, "Password", password)) {
        return {Outcome::refusal("login cancelled"), std::nullopt};
    }
    try {
        auto login = authenticate(users, user_id, password);
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

std::string format_utc_timestamp(std::int64_t epoch_seconds) {
    // Civil date from days since 1970-01-01 (proleptic Gregorian), so no platform gmtime.
    std::int64_t days = epoch_seconds / 86400;
    std::int64_t seconds_of_day = epoch_seconds % 86400;
    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        --days;
    }
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t day_of_era = days - era * 146097;
    const std::int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_index = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
    const std::int64_t month = month_index < 10 ? month_index + 3 : month_index - 9;
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ",
                  static_cast<long long>(year), static_cast<long long>(month),
                  static_cast<long long>(day), static_cast<long long>(seconds_of_day / 3600),
                  static_cast<long long>(seconds_of_day / 60 % 60),
                  static_cast<long long>(seconds_of_day % 60));
    return buffer;
}

std::string utc_timestamp_now() {
    return format_utc_timestamp(std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
}

Outcome change_password_from_console(std::istream& in, std::ostream& out,
                                     const UserStore& users, const Session& session,
                                     const AuditLog& audit) {
    std::string current;
    std::string replacement;
    if (!read_field(in, out, "Current password", current) ||
        !read_field(in, out, "New password", replacement)) {
        return Outcome::refusal("password change cancelled");
    }
    try {
        const auto result = change_password(users, session, current, replacement);
        if (result) {
            audit.append({utc_timestamp_now(), session.user_id, role_name(session.role),
                          "PASSWORD_CHANGE", session.user_id});
        }
        return to_outcome(result, session.user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome reset_password_from_console(std::istream& in, std::ostream& out, const UserStore& users,
                                    const Session& session, const AuditLog& audit) {
    std::string passenger_id;
    std::string replacement;
    if (!read_field(in, out, "Passenger user id", passenger_id) ||
        !read_field(in, out, "New password", replacement)) {
        return Outcome::refusal("password reset cancelled");
    }
    try {
        const auto result = reset_password(users, session, passenger_id, replacement);
        if (result) {
            audit.append({utc_timestamp_now(), session.user_id, role_name(session.role),
                          "PASSWORD_RESET", passenger_id});
        }
        return to_outcome(result, passenger_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const ActionHandler& role_handler) {
    auto login = login_from_console(in, out, users);
    if (!login.session) {
        return login.outcome;
    }
    report_outcome(out, err, login.outcome);
    const Session session = std::move(*login.session);
    const AuditLog audit_log{users.data_directory() / "audit.log"};
    auto last_activity = std::chrono::steady_clock::now();
    bool timed_out = false;
    run_menu(in, out, err, menu_for(session), [&](MenuAction action) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_activity >= kSessionIdleTimeout) {
            audit_log.append({utc_timestamp_now(), session.user_id, role_name(session.role),
                              "SESSION_IDLE_TIMEOUT", "-"});
            timed_out = true;
            return Outcome::refusal("session expired after 15 minutes of inactivity; please log in again");
        }
        last_activity = now;
        return AuthorizationModule::invoke(&session, action, [&](MenuAction authorized_action) {
            switch (authorized_action) {
                case MenuAction::Logout:
                    return Outcome::success("logged out", session.user_id);
                case MenuAction::ChangePassword:
                    return change_password_from_console(in, out, users, session, audit_log);
                case MenuAction::ResetPassword:
                    return reset_password_from_console(in, out, users, session, audit_log);
                default:
                    return role_handler(authorized_action);
            }
        });
    }, [&](MenuAction) { return timed_out; });
    // The session ends here (RRS-F-003); the caller's public menu is shown next.
    return Outcome::success("returned to the main menu");
}

}  // namespace rrs
