#include "auth_flow.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <utility>

#include "audit_log.hpp"
#include "authorization.hpp"
#include "console_input.hpp"

namespace rrs {
namespace {

constexpr auto kSessionIdleTimeout = std::chrono::minutes(15);

std::string role_name(Role role) {
    return role == Role::Administrator ? "ADMIN" : "PASSENGER";
}

// Runs one login session. dispatch serves every authorised role-menu action except Logout.
Outcome run_login_session(const AppContext& context,
                          const InteractiveSessionActionHandler& dispatch) {
    auto login = login_from_console(context.in, context.out, context.users, context.audit_log,
                                    [&context] { return context.now_timestamp(); });
    if (!login.session) {
        return login.outcome;
    }
    report_outcome(context.out, context.err, login.outcome);
    const Session session = std::move(*login.session);
    const auto now = [&context] {
        return context.clock ? context.clock() : std::chrono::steady_clock::now();
    };
    auto last_activity = now();
    bool timed_out = false;
    const SessionActivityCheck session_activity = [&](bool activity) {
        const auto current = now();
        if (timed_out || current - last_activity >= kSessionIdleTimeout) {
            if (!timed_out) {
                context.audit_log.append({context.now_timestamp(), session.user_id,
                                          role_name(session.role), "SESSION_IDLE_TIMEOUT", "-"});
                timed_out = true;
            }
            return false;
        }
        if (activity) {
            last_activity = current;
        }
        return true;
    };
    run_menu(context.in, context.out, context.err, menu_for(session), [&](MenuAction action) {
        if (!session_activity(true)) {
            return Outcome::refusal(std::string(kSessionExpiredMessage));
        }
        return AuthorizationModule::invoke(&session, action, [&](MenuAction authorized_action) {
            if (authorized_action == MenuAction::Logout) {
                return Outcome::success("logged out", session.user_id);
            }
            return dispatch(authorized_action, session, session_activity);
        });
    }, [&](MenuAction) { return timed_out; });
    // The session ends here (RRS-F-003); the caller's public menu is shown next.
    return Outcome::success("returned to the main menu");
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

    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-'
         << std::setw(2) << day << 'T' << std::setw(2) << seconds_of_day / 3600 << ':'
         << std::setw(2) << seconds_of_day / 60 % 60 << ':' << std::setw(2)
         << seconds_of_day % 60 << 'Z';
    return text.str();
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

void register_account_flows(MenuRouter& router) {
    router.add(MenuAction::ChangePassword, [](const AppContext& context, const Session& session,
                                              const SessionActivityCheck&) {
        return change_password_from_console(context.in, context.out, context.users, session,
                                            context.audit_log);
    });
    router.add(MenuAction::ResetPassword, [](const AppContext& context, const Session& session,
                                             const SessionActivityCheck&) {
        return reset_password_from_console(context.in, context.out, context.users, session,
                                           context.audit_log);
    });
}

Outcome login_and_run_session(const AppContext& context, const MenuRouter& router) {
    return run_login_session(context, [&](MenuAction action, const Session& session,
                                          const SessionActivityCheck& activity) {
        return router.dispatch(action, context, session, activity);
    });
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const ActionHandler& role_handler) {
    const InteractiveSessionActionHandler adapter =
        [&role_handler](MenuAction action, const Session&,
                        const SessionActivityCheck&) { return role_handler(action); };
    return login_and_run_session(in, out, err, users, audit_log, timestamp_provider, adapter);
}

Outcome login_and_run_session(std::istream& in, std::ostream& out, std::ostream& err,
                              const UserStore& users, const AuditLog& audit_log,
                              const AuditTimestampProvider& timestamp_provider,
                              const InteractiveSessionActionHandler& role_handler,
                              const SessionClockProvider& clock_provider) {
    const AppContext context{in, out, err, users, audit_log, timestamp_provider, clock_provider};
    MenuRouter accounts;
    register_account_flows(accounts);
    return run_login_session(context, [&](MenuAction action, const Session& session,
                                          const SessionActivityCheck& activity) {
        return accounts.handles(action) ? accounts.dispatch(action, context, session, activity)
                                        : role_handler(action, session, activity);
    });
}

}  // namespace rrs
